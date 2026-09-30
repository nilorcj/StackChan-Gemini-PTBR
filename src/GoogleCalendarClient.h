#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include <functional>
#include <vector>
#include <algorithm>
#include "ConfigManager.h"

class GoogleCalendarClient {
 public:
  struct CalendarEvent {
    String title;
    String dateStr;     // "DD/MM/YYYY"
    String startTimeStr;
    String endTimeStr;
    String location;
    bool isAllDay = false;
    int startMinutes = 0; // minutes from midnight for sorting
    int dayOffset = 0;    // 0 = hoje, 1 = amanha, 2 = depois de amanha
  };

  explicit GoogleCalendarClient(ConfigManager& config) : config_(config) {
    calMutex_ = xSemaphoreCreateMutex();
  }

  ~GoogleCalendarClient() {
    if (calMutex_) {
      vSemaphoreDelete(calMutex_);
      calMutex_ = nullptr;
    }
  }

  // Must be called on Core 1 (setup or config reload) to safely cache URL and timezone in RAM
  void refreshCachedUrl() {
    String url = config_.readCalendarUrl();
    url.trim();
    int tz = config_.config().timezoneOffset;
    if (calMutex_ && xSemaphoreTake(calMutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
      cachedUrl_ = url;
      cachedTimezoneOffset_ = tz;
      xSemaphoreGive(calMutex_);
    }
  }

  bool hasCache() const { return hasCache_.load(std::memory_order_acquire); }
  bool isInFlight() const { return inFlight_.load(std::memory_order_acquire); }
  bool isRefreshRequested() const { return refreshRequested_.load(std::memory_order_acquire); }
  void requestRefresh() { refreshRequested_.store(true, std::memory_order_release); }

  bool isCacheValidForToday() const {
    if (!hasCache_.load(std::memory_order_acquire)) return false;
    time_t nowSec = time(nullptr);
    struct tm nowTm {};
    localtime_r(&nowSec, &nowTm);
    char todayStr[16];
    snprintf(todayStr, sizeof(todayStr), "%04d%02d%02d",
             nowTm.tm_year + 1900, nowTm.tm_mon + 1, nowTm.tm_mday);
    bool valid = false;
    if (calMutex_ && xSemaphoreTake(calMutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
      valid = (lastPrefetchDateStr_ == todayStr);
      xSemaphoreGive(calMutex_);
    }
    return valid;
  }

  size_t cachedEventCount() const {
    size_t count = 0;
    if (calMutex_ && xSemaphoreTake(calMutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
      count = cachedEvents_.size();
      xSemaphoreGive(calMutex_);
    }
    return count;
  }

  // Non-blocking prefetch on Core 1: requests background refresh on Core 0
  bool prefetch(bool force = false) {
    (void)force;
    requestRefresh();
    return hasCache_.load(std::memory_order_acquire);
  }

  // Core 0 background fetch execution (zero SD access, zero blocking of Core 1)
  bool executeFetch(const std::function<bool()>& preflightGate = nullptr) {
    if (inFlight_.load(std::memory_order_acquire)) {
      Serial.println("[GoogleCalendar] fetch aborted: already in flight");
      return false;
    }

    struct InFlightGuard {
      std::atomic<bool>& flag;
      explicit InFlightGuard(std::atomic<bool>& f) : flag(f) {
        flag.store(true, std::memory_order_release);
      }
      ~InFlightGuard() {
        flag.store(false, std::memory_order_release);
      }
    } guard(inFlight_);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[GoogleCalendar] fetch aborted: Wi-Fi not connected");
      return false;
    }

    String url;
    int tzOffset = -3;
    if (calMutex_ && xSemaphoreTake(calMutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
      url = cachedUrl_;
      tzOffset = cachedTimezoneOffset_;
      xSemaphoreGive(calMutex_);
    } else {
      Serial.println("[GoogleCalendar] fetch aborted: could not acquire calMutex_");
      return false;
    }

    if (!url.length()) {
      Serial.println("[GoogleCalendar] fetch aborted: calendar URL missing");
      return false;
    }

    time_t nowSec = time(nullptr);
    struct tm nowTm {};
    localtime_r(&nowSec, &nowTm);
    int curYear = nowTm.tm_year + 1900;
    if (curYear < 2024) {
      Serial.printf("[GoogleCalendar] fetch postponed: clock year (%d) < 2024\n", curYear);
      return false;
    }

    // Preflight gate check
    if (preflightGate && !preflightGate()) {
      Serial.println("[GoogleCalendar] fetch aborted: preflight gate closed");
      return false;
    }

    // Strict heap check before TLS allocation
    size_t freeHeap = ESP.getFreeHeap();
    size_t maxAlloc = ESP.getMaxAllocHeap();
    if (freeHeap < 50000 || maxAlloc < 20000) {
      Serial.printf("[GoogleCalendar] fetch deferred: freeHeap=%u maxAlloc=%u (< 50000/20000)\n",
                    static_cast<unsigned>(freeHeap), static_cast<unsigned>(maxAlloc));
      return false;
    }

    int curMonth = nowTm.tm_mon + 1;
    int curDay = nowTm.tm_mday;
    char todayStr[16];
    snprintf(todayStr, sizeof(todayStr), "%04d%02d%02d", curYear, curMonth, curDay);

    char readableDate[32];
    snprintf(readableDate, sizeof(readableDate), "%02d/%02d/%04d", curDay, curMonth, curYear);

    time_t tomorrowSec = nowSec + 86400;
    struct tm tomorrowTm {};
    localtime_r(&tomorrowSec, &tomorrowTm);
    int tomYear = tomorrowTm.tm_year + 1900;
    int tomMonth = tomorrowTm.tm_mon + 1;
    int tomDay = tomorrowTm.tm_mday;
    char tomorrowStr[16];
    snprintf(tomorrowStr, sizeof(tomorrowStr), "%04d%02d%02d", tomYear, tomMonth, tomDay);
    char readableTomorrow[32];
    snprintf(readableTomorrow, sizeof(readableTomorrow), "%02d/%02d/%04d", tomDay, tomMonth, tomYear);

    time_t dayAfterSec = nowSec + 172800;
    struct tm dayAfterTm {};
    localtime_r(&dayAfterSec, &dayAfterTm);
    int dayAfterYear = dayAfterTm.tm_year + 1900;
    int dayAfterMonth = dayAfterTm.tm_mon + 1;
    int dayAfterDay = dayAfterTm.tm_mday;
    char dayAfterStr[16];
    snprintf(dayAfterStr, sizeof(dayAfterStr), "%04d%02d%02d", dayAfterYear, dayAfterMonth, dayAfterDay);
    char readableDayAfter[32];
    snprintf(readableDayAfter, sizeof(readableDayAfter), "%02d/%02d/%04d", dayAfterDay, dayAfterMonth, dayAfterYear);

    static const char* const kDayCodes[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    const char* todayCode = kDayCodes[nowTm.tm_wday >= 0 && nowTm.tm_wday < 7 ? nowTm.tm_wday : 0];
    const char* tomorrowCode = kDayCodes[tomorrowTm.tm_wday >= 0 && tomorrowTm.tm_wday < 7 ? tomorrowTm.tm_wday : 0];
    const char* dayAfterCode = kDayCodes[dayAfterTm.tm_wday >= 0 && dayAfterTm.tm_wday < 7 ? dayAfterTm.tm_wday : 0];

    Serial.printf("[GoogleCalendar] background fetch on Core %d: freeHeap=%u maxAlloc=%u\n",
                  xPortGetCoreID(),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);

    // Recheck gate immediately before http.begin()
    if (preflightGate && !preflightGate()) {
      Serial.println("[GoogleCalendar] fetch aborted right before http.begin");
      return false;
    }

    Serial.printf("[GoogleCalendar] http.begin with url_len=%u (prefix: %.35s)\n",
                  static_cast<unsigned>(url.length()), url.c_str());

    if (!http.begin(client, url)) {
      Serial.println("[GoogleCalendar] HTTPS begin failed");
      return false;
    }

    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) StackChan-Pix/1.0");
    http.addHeader("Accept", "text/calendar, text/plain, */*");
    http.addHeader("Accept-Encoding", "identity");

    // Recheck gate immediately before http.GET()
    if (preflightGate && !preflightGate()) {
      Serial.println("[GoogleCalendar] fetch aborted right before http.GET");
      http.end();
      return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      char errBuf[128] = {0};
      client.lastError(errBuf, sizeof(errBuf));
      Serial.printf("[GoogleCalendar] HTTP GET failed code=%d (%s) clientErr='%s'\n",
                    httpCode, http.errorToString(httpCode).c_str(), errBuf);
      http.end();
      return false;
    }

    std::vector<CalendarEvent> parsedEvents;
    parsedEvents.reserve(16);
    WiFiClient* stream = http.getStreamPtr();

    enum class PropType { None, Summary, Location, DtStart, DtEnd, Rrule, Ignored };
    PropType curProp = PropType::None;

    bool inEvent = false;
    char curSummary[256]; curSummary[0] = '\0';
    char curLoc[128]; curLoc[0] = '\0';
    char curStartRaw[32]; curStartRaw[0] = '\0';
    char curEndRaw[32]; curEndRaw[0] = '\0';
    char curRrule[256]; curRrule[0] = '\0';
    bool isAllDay = false;
    bool sawEndVCalendar = false;

    uint32_t fetchStartMs = millis();
    uint32_t lastDataMs = fetchStartMs;
    size_t lineCount = 0;
    size_t totalBytes = 0;
    size_t veventCount = 0;
    bool abortRequested = false;

    struct ParsedRrule {
      bool hasRrule = false;
      enum Freq { None, Daily, Weekly, Monthly, Yearly } freq = None;
      char untilDateStr[16] = {0};
      bool hasUntil = false;
      char maxDateStr[16] = {0};
      bool hasCountLimit = false;
      char byday[32] = {0};
      bool hasByday = false;
    };

    auto parseRruleOnce = [&](const char* rruleStr, int evYear, int evMonth, int evDay) -> ParsedRrule {
      ParsedRrule pr;
      if (!rruleStr || rruleStr[0] == '\0') return pr;
      pr.hasRrule = true;

      if (strstr(rruleStr, "FREQ=DAILY")) pr.freq = ParsedRrule::Daily;
      else if (strstr(rruleStr, "FREQ=WEEKLY")) pr.freq = ParsedRrule::Weekly;
      else if (strstr(rruleStr, "FREQ=MONTHLY")) pr.freq = ParsedRrule::Monthly;
      else if (strstr(rruleStr, "FREQ=YEARLY")) pr.freq = ParsedRrule::Yearly;

      const char* uPtr = strstr(rruleStr, "UNTIL=");
      if (uPtr) {
        char uVal[32] = {0};
        const char* valStart = uPtr + 6;
        size_t vi = 0;
        while (valStart[vi] != '\0' && valStart[vi] != ';' && vi + 1 < sizeof(uVal)) {
          uVal[vi] = valStart[vi];
          vi++;
        }
        uVal[vi] = '\0';
        int uY = 0, uM = 0, uD = 0, uH = 0, uMin = 0;
        bool uUtc = false, uAll = false;
        parseIcalDateTime(uVal, uY, uM, uD, uH, uMin, uUtc, uAll);
        snprintf(pr.untilDateStr, sizeof(pr.untilDateStr), "%04d%02d%02d", uY, uM, uD);
        pr.hasUntil = true;
      }

      const char* cPtr = strstr(rruleStr, "COUNT=");
      if (cPtr) {
        int countN = atoi(cPtr + 6);
        if (countN > 0) {
          int maxDays = countN;
          if (pr.freq == ParsedRrule::Weekly) maxDays = countN * 7;
          else if (pr.freq == ParsedRrule::Monthly) maxDays = countN * 31;
          else if (pr.freq == ParsedRrule::Yearly) maxDays = countN * 366;

          const char* intPtr = strstr(rruleStr, "INTERVAL=");
          if (intPtr) {
            int intervalN = atoi(intPtr + 9);
            if (intervalN > 1) maxDays *= intervalN;
          }

          struct tm et = {};
          et.tm_year = evYear - 1900;
          et.tm_mon = evMonth - 1;
          et.tm_mday = evDay + maxDays;
          et.tm_hour = 12;
          et.tm_isdst = -1;
          mktime(&et);
          snprintf(pr.maxDateStr, sizeof(pr.maxDateStr), "%04d%02d%02d",
                   et.tm_year + 1900, et.tm_mon + 1, et.tm_mday);
          pr.hasCountLimit = true;
        }
      }

      const char* bPtr = strstr(rruleStr, "BYDAY=");
      if (bPtr) {
        const char* valStart = bPtr + 6;
        size_t vi = 0;
        while (valStart[vi] != '\0' && valStart[vi] != ';' && vi + 1 < sizeof(pr.byday)) {
          pr.byday[vi] = valStart[vi];
          vi++;
        }
        pr.byday[vi] = '\0';
        pr.hasByday = true;
      }

      return pr;
    };

    auto evaluateEvent = [&]() {
      if (!inEvent || curSummary[0] == '\0' || curStartRaw[0] == '\0') return;
      veventCount++;

      int evYear = 0, evMonth = 0, evDay = 0, evHour = 0, evMin = 0;
      bool isUtc = false;
      parseIcalDateTime(curStartRaw, evYear, evMonth, evDay, evHour, evMin, isUtc, isAllDay);

      if (isUtc && !isAllDay) {
        struct tm t = {};
        t.tm_year = evYear - 1900;
        t.tm_mon = evMonth - 1;
        t.tm_mday = evDay;
        t.tm_hour = evHour + tzOffset;
        t.tm_min = evMin;
        t.tm_sec = 0;
        t.tm_isdst = -1;
        mktime(&t);
        evYear = t.tm_year + 1900;
        evMonth = t.tm_mon + 1;
        evDay = t.tm_mday;
        evHour = t.tm_hour;
        evMin = t.tm_min;
      }

      char evDateStr[16];
      snprintf(evDateStr, sizeof(evDateStr), "%04d%02d%02d", evYear, evMonth, evDay);

      struct tm st = {};
      st.tm_year = evYear - 1900;
      st.tm_mon = evMonth - 1;
      st.tm_mday = evDay;
      st.tm_hour = 12;
      st.tm_isdst = -1;
      mktime(&st);
      int evOrigWday = st.tm_wday;

      char evEndDateStr[16] = {0};
      char endTimeFormatted[16] = {0};
      if (curEndRaw[0] != '\0') {
        int endY = 0, endM = 0, endD = 0, endH = 0, endMin = 0;
        bool endUtc = false, endAll = false;
        parseIcalDateTime(curEndRaw, endY, endM, endD, endH, endMin, endUtc, endAll);
        if (endUtc && !endAll) {
          struct tm et = {};
          et.tm_year = endY - 1900;
          et.tm_mon = endM - 1;
          et.tm_mday = endD;
          et.tm_hour = endH + tzOffset;
          et.tm_min = endMin;
          et.tm_sec = 0;
          et.tm_isdst = -1;
          mktime(&et);
          endY = et.tm_year + 1900;
          endM = et.tm_mon + 1;
          endD = et.tm_mday;
          endH = et.tm_hour;
          endMin = et.tm_min;
        }
        snprintf(evEndDateStr, sizeof(evEndDateStr), "%04d%02d%02d", endY, endM, endD);
        snprintf(endTimeFormatted, sizeof(endTimeFormatted), "%02d:%02d", endH, endMin);
      }

      // Analisar RRULE uma única vez por VEVENT
      ParsedRrule pr = parseRruleOnce(curRrule, evYear, evMonth, evDay);

      auto matchesTargetDate = [&](int targetOffset, const char* targetDateStr, int targetWday, const char* targetDayCode) -> bool {
        if (strcmp(evDateStr, targetDateStr) == 0) return true;

        if (evEndDateStr[0] != 0 && isAllDay) {
          if (strcmp(targetDateStr, evDateStr) >= 0 && strcmp(targetDateStr, evEndDateStr) < 0) {
            return true;
          }
        }

        if (pr.hasRrule && strcmp(targetDateStr, evDateStr) >= 0) {
          if (pr.hasUntil && strcmp(targetDateStr, pr.untilDateStr) > 0) return false;
          if (pr.hasCountLimit && strcmp(targetDateStr, pr.maxDateStr) > 0) return false;

          if (pr.freq == ParsedRrule::Daily) {
            return true;
          }
          if (pr.freq == ParsedRrule::Weekly) {
            if (pr.hasByday) {
              if (strstr(pr.byday, targetDayCode) != nullptr) return true;
            } else {
              if (evOrigWday == targetWday) return true;
            }
          }
          if (pr.freq == ParsedRrule::Yearly) {
            int tMonth = (targetOffset == 0 ? curMonth : (targetOffset == 1 ? tomMonth : dayAfterMonth));
            int tDay = (targetOffset == 0 ? curDay : (targetOffset == 1 ? tomDay : dayAfterDay));
            return (tMonth == evMonth && tDay == evDay);
          }
        }
        return false;
      };

      bool matchesToday = matchesTargetDate(0, todayStr, nowTm.tm_wday, todayCode);
      bool matchesTom = matchesTargetDate(1, tomorrowStr, tomorrowTm.tm_wday, tomorrowCode);
      bool matchesDayAfter = matchesTargetDate(2, dayAfterStr, dayAfterTm.tm_wday, dayAfterCode);

      if (matchesToday || matchesTom || matchesDayAfter) {
        Serial.printf("[GoogleCalendar:EVENT] '%s' raw=%s local=%04d%02d%02d %02d:%02d rrule=%s d0=%d d1=%d d2=%d\n",
                      curSummary, curStartRaw, evYear, evMonth, evDay, evHour, evMin,
                      curRrule, matchesToday ? 1 : 0, matchesTom ? 1 : 0, matchesDayAfter ? 1 : 0);
      }

      auto addEventForDay = [&](int offset, const char* dStr) {
        if (parsedEvents.size() >= 32) return; // Hard cap on vector size
        CalendarEvent ev;
        char cleaned[128];
        cleanIcalText(curSummary, cleaned, sizeof(cleaned));
        ev.title = cleaned;
        cleanIcalText(curLoc, cleaned, sizeof(cleaned));
        ev.location = cleaned;
        ev.isAllDay = isAllDay;
        ev.dayOffset = offset;
        ev.dateStr = dStr;
        if (isAllDay) {
          ev.startTimeStr = "Dia todo";
          ev.endTimeStr = "";
          ev.startMinutes = 0;
        } else {
          char sBuf[16];
          snprintf(sBuf, sizeof(sBuf), "%02d:%02d", evHour, evMin);
          ev.startTimeStr = sBuf;
          ev.startMinutes = evHour * 60 + evMin;
          ev.endTimeStr = endTimeFormatted;
        }
        parsedEvents.push_back(std::move(ev));
      };

      if (matchesToday) {
        addEventForDay(0, readableDate);
      }
      if (matchesTom) {
        addEventForDay(1, readableTomorrow);
      }
      if (matchesDayAfter) {
        addEventForDay(2, readableDayAfter);
      }
    };

    auto handleCompletedLine = [&](char* line, size_t len) {
      if (len == 0) return;

      // RFC 5545 unfolding: if starts with space or tab
      if (line[0] == ' ' || line[0] == '\t') {
        const char* cont = line + 1;
        size_t contLen = len - 1;
        if (curProp == PropType::Summary) {
          size_t curLen = strlen(curSummary);
          if (curLen + contLen < sizeof(curSummary) - 1) {
            memcpy(curSummary + curLen, cont, contLen);
            curSummary[curLen + contLen] = '\0';
          }
        } else if (curProp == PropType::Location) {
          size_t curLen = strlen(curLoc);
          if (curLen + contLen < sizeof(curLoc) - 1) {
            memcpy(curLoc + curLen, cont, contLen);
            curLoc[curLen + contLen] = '\0';
          }
        } else if (curProp == PropType::Rrule) {
          size_t curLen = strlen(curRrule);
          if (curLen + contLen < sizeof(curRrule) - 1) {
            memcpy(curRrule + curLen, cont, contLen);
            curRrule[curLen + contLen] = '\0';
          }
        }
        // Ignored properties drop continuation lines with ZERO heap allocations!
        return;
      }

      // Non-folded line: identify property
      if (strncmp(line, "BEGIN:VEVENT", 12) == 0) {
        inEvent = true;
        curProp = PropType::None;
        curSummary[0] = '\0';
        curLoc[0] = '\0';
        curStartRaw[0] = '\0';
        curEndRaw[0] = '\0';
        curRrule[0] = '\0';
        isAllDay = false;
        return;
      }

      if (strncmp(line, "END:VEVENT", 10) == 0) {
        evaluateEvent();
        inEvent = false;
        curProp = PropType::None;
        return;
      }

      if (strncmp(line, "END:VCALENDAR", 13) == 0) {
        sawEndVCalendar = true;
        curProp = PropType::None;
        return;
      }

      if (!inEvent) {
        curProp = PropType::Ignored;
        return;
      }

      if (strncmp(line, "SUMMARY:", 8) == 0) {
        curProp = PropType::Summary;
        const char* val = line + 8;
        size_t vLen = len - 8;
        if (vLen >= sizeof(curSummary)) vLen = sizeof(curSummary) - 1;
        memcpy(curSummary, val, vLen);
        curSummary[vLen] = '\0';
      } else if (strncmp(line, "LOCATION:", 9) == 0) {
        curProp = PropType::Location;
        const char* val = line + 9;
        size_t vLen = len - 9;
        if (vLen >= sizeof(curLoc)) vLen = sizeof(curLoc) - 1;
        memcpy(curLoc, val, vLen);
        curLoc[vLen] = '\0';
      } else if (strncmp(line, "DTSTART", 7) == 0) {
        curProp = PropType::DtStart;
        const char* colon = strchr(line, ':');
        if (colon) {
          const char* val = colon + 1;
          size_t vLen = strlen(val);
          if (vLen >= sizeof(curStartRaw)) vLen = sizeof(curStartRaw) - 1;
          memcpy(curStartRaw, val, vLen);
          curStartRaw[vLen] = '\0';
        }
      } else if (strncmp(line, "DTEND", 5) == 0) {
        curProp = PropType::DtEnd;
        const char* colon = strchr(line, ':');
        if (colon) {
          const char* val = colon + 1;
          size_t vLen = strlen(val);
          if (vLen >= sizeof(curEndRaw)) vLen = sizeof(curEndRaw) - 1;
          memcpy(curEndRaw, val, vLen);
          curEndRaw[vLen] = '\0';
        }
      } else if (strncmp(line, "RRULE:", 6) == 0) {
        curProp = PropType::Rrule;
        const char* val = line + 6;
        size_t vLen = len - 6;
        if (vLen >= sizeof(curRrule)) vLen = sizeof(curRrule) - 1;
        memcpy(curRrule, val, vLen);
        curRrule[vLen] = '\0';
      } else {
        curProp = PropType::Ignored;
      }
    };

    // Leitura em blocos de 512B com buffer de linha fixo de 512B (ZERO heap allocations no hot path)
    char readBuf[512];
    char lineBuf[512];
    size_t lineLen = 0;

    while (http.connected() || stream->available() > 0) {
      int avail = stream->available();
      if (avail > 0) {
        int toRead = avail > (int)sizeof(readBuf) ? (int)sizeof(readBuf) : avail;
        int bytesRead = stream->readBytes(readBuf, toRead);
        if (bytesRead <= 0) break;

        totalBytes += bytesRead;
        lastDataMs = millis();

        for (int i = 0; i < bytesRead; ++i) {
          char c = readBuf[i];
          if (c == '\r') continue;

          if (c == '\n') {
            lineBuf[lineLen] = '\0';
            handleCompletedLine(lineBuf, lineLen);
            lineLen = 0;
            lineCount++;

            // Periodic gate check (every 64 lines or ~250ms)
            if ((lineCount & 0x3F) == 0) {
              uint32_t nowCheck = millis();
              if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[GoogleCalendar] fetch aborted: Wi-Fi dropped mid-stream");
                abortRequested = true;
                break;
              }
              if (preflightGate && !preflightGate()) {
                Serial.println("[GoogleCalendar] fetch aborted: gate closed mid-stream (Gemini active or system not idle)");
                abortRequested = true;
                break;
              }
              size_t fHeap = ESP.getFreeHeap();
              size_t mAlloc = ESP.getMaxAllocHeap();
              if (fHeap < 12000 || mAlloc < 5000) {
                Serial.printf("[GoogleCalendar] fetch aborted: low heap mid-stream (freeHeap=%u maxAlloc=%u)\n",
                              static_cast<unsigned>(fHeap), static_cast<unsigned>(mAlloc));
                abortRequested = true;
                break;
              }
              if (veventCount > 6000 || totalBytes > 4 * 1024 * 1024 || (nowCheck - fetchStartMs > 60000)) {
                Serial.printf("[GoogleCalendar] fetch aborted: defensive hard limit hit (vevents=%u bytes=%u elapsed=%lu)\n",
                              static_cast<unsigned>(veventCount), static_cast<unsigned>(totalBytes),
                              static_cast<unsigned long>(nowCheck - fetchStartMs));
                abortRequested = true;
                break;
              }
            }
          } else {
            if (lineLen < sizeof(lineBuf) - 1) {
              lineBuf[lineLen++] = c;
            }
          }
        }
        if (abortRequested) break;
      } else {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (millis() - lastDataMs > 5000) {
          Serial.println("[GoogleCalendar] Stream read timeout (5s idle)");
          break;
        }
      }
    }

    if (!abortRequested && lineLen > 0) {
      lineBuf[lineLen] = '\0';
      handleCompletedLine(lineBuf, lineLen);
      lineCount++;
    }

    http.end();

    if (abortRequested) {
      Serial.println("[GoogleCalendar] Fetch aborted mid-stream. PRESERVING OLD CACHE.");
      return false;
    }

    // Guard: Never publish a truncated or incomplete stream!
    if (!sawEndVCalendar) {
      Serial.printf("[GoogleCalendar] Incomplete stream (missing END:VCALENDAR, %u bytes, %u lines). PRESERVING OLD CACHE.\n",
                    static_cast<unsigned>(totalBytes), static_cast<unsigned>(lineCount));
      return false;
    }

    Serial.printf("[GoogleCalendar] stream finished: %u bytes, %u lines, %u vevents, %u parsed events\n",
                  static_cast<unsigned>(totalBytes),
                  static_cast<unsigned>(lineCount),
                  static_cast<unsigned>(veventCount),
                  static_cast<unsigned>(parsedEvents.size()));

    std::sort(parsedEvents.begin(), parsedEvents.end(), [](const CalendarEvent& a, const CalendarEvent& b) {
      if (a.dayOffset != b.dayOffset) return a.dayOffset < b.dayOffset;
      if (a.isAllDay != b.isAllDay) return a.isAllDay;
      return a.startMinutes < b.startMinutes;
    });

    // Destroy old vector OUTSIDE the lock to minimize hold time
    std::vector<CalendarEvent> oldEventsToDestroy;
    if (calMutex_ && xSemaphoreTake(calMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
      cachedEvents_.swap(parsedEvents);
      oldEventsToDestroy.swap(parsedEvents);
      lastPrefetchDateStr_ = String(todayStr);
      hasCache_.store(true, std::memory_order_release);
      refreshRequested_.store(false, std::memory_order_release);
      lastFetchMs_ = millis();
      xSemaphoreGive(calMutex_);
    } else {
      Serial.println("[GoogleCalendar] ERROR: Could not acquire calMutex_ to commit cache");
      return false;
    }

    Serial.printf("[GoogleCalendar] fetch complete on Core %d: %u events in 3-day cache\n",
                  xPortGetCoreID(),
                  static_cast<unsigned>(cachedEvents_.size()));
    return true;
  }

  String fetchDailyAgendaJson() {
    Serial.printf("[DIAG:calendar:entry] task='%s' core=%d stackHWM=%u freeHeap=%u minFreeHeap=%u maxAllocHeap=%u hasCache=%d\n",
                  pcTaskGetName(nullptr),
                  xPortGetCoreID(),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()),
                  hasCache_.load(std::memory_order_acquire) ? 1 : 0);

    String url;
    if (calMutex_ && xSemaphoreTake(calMutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
      url = cachedUrl_;
      xSemaphoreGive(calMutex_);
    } else {
      return makeWarmingUpJson();
    }

    if (!url.length()) {
      JsonDocument doc;
      doc["ok"] = false;
      doc["error"] = "calendar_url_missing";
      doc["message"] = "A URL da agenda do Google Calendar ainda nao foi configurada no cartao SD (/app/StackChan/secrets/calendar_url.txt).";
      doc["instruction"] = "Avise com simpatia que a URL da agenda ainda nao foi configurada no cartao SD. NUNCA diga que o dia esta livre.";
      String out;
      serializeJson(doc, out);
      return out;
    }

    if (!hasCache_.load(std::memory_order_acquire)) {
      return makeWarmingUpJson();
    }

    // Copy cached events locally under short mutex lock
    std::vector<CalendarEvent> localEvents;
    if (calMutex_ && xSemaphoreTake(calMutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
      localEvents = cachedEvents_;
      xSemaphoreGive(calMutex_);
    } else {
      Serial.println("[GoogleCalendar] calMutex_ timeout in fetchDailyAgendaJson -> returning warming up");
      return makeWarmingUpJson();
    }

    // Now format JSON from localEvents (100% lock-free)
    time_t nowSec = time(nullptr);
    struct tm nowTm {};
    localtime_r(&nowSec, &nowTm);

    int curYear = nowTm.tm_year + 1900;
    int curMonth = nowTm.tm_mon + 1;
    int curDay = nowTm.tm_mday;
    int curHour = nowTm.tm_hour;
    int curMin = nowTm.tm_min;

    char readableDate[32];
    snprintf(readableDate, sizeof(readableDate), "%02d/%02d/%04d", curDay, curMonth, curYear);

    time_t tomorrowSec = nowSec + 86400;
    struct tm tomorrowTm {};
    localtime_r(&tomorrowSec, &tomorrowTm);
    char readableTomorrow[32];
    snprintf(readableTomorrow, sizeof(readableTomorrow), "%02d/%02d/%04d",
             tomorrowTm.tm_mday, tomorrowTm.tm_mon + 1, tomorrowTm.tm_year + 1900);

    char readableTime[16];
    snprintf(readableTime, sizeof(readableTime), "%02d:%02d", curHour, curMin);

    size_t countToday = 0;
    size_t countTomorrow = 0;
    int nowMinutes = curHour * 60 + curMin;
    bool nextFound = false;

    JsonDocument outDoc;
    auto evArr = outDoc["events"].to<JsonArray>();

    for (const auto& ev : localEvents) {
      String dayLabel = "";
      bool isToday = (ev.dateStr == readableDate);
      bool isTomorrow = (ev.dateStr == readableTomorrow);

      if (isToday) {
        dayLabel = "hoje";
        countToday++;
      } else if (isTomorrow) {
        dayLabel = "amanha";
        countTomorrow++;
      } else {
        continue;
      }

      auto obj = evArr.add<JsonObject>();
      obj["day"] = dayLabel;
      obj["date"] = ev.dateStr;
      obj["title"] = ev.title;
      obj["time"] = ev.isAllDay ? "Dia todo" : (ev.startTimeStr + (ev.endTimeStr.length() ? " as " + ev.endTimeStr : ""));
      if (ev.location.length()) obj["location"] = ev.location;

      if (isToday && !ev.isAllDay && !nextFound && ev.startMinutes >= nowMinutes) {
        obj["is_next"] = true;
        int deltaMin = ev.startMinutes - nowMinutes;
        obj["starts_in_minutes"] = deltaMin;
        nextFound = true;
      }
    }

    if (!nextFound) {
      for (auto obj : evArr) {
        if (obj["day"] == "amanha") {
          obj["is_next"] = true;
          nextFound = true;
          break;
        }
      }
    }

    outDoc["ok"] = true;
    outDoc["today_date"] = readableDate;
    outDoc["tomorrow_date"] = readableTomorrow;
    outDoc["current_time"] = readableTime;
    outDoc["total_events_today"] = countToday;
    outDoc["total_events_tomorrow"] = countTomorrow;
    outDoc["total_events"] = countToday + countTomorrow;

    if (countToday == 0) {
      outDoc["busy_level_today"] = "dia_livre";
    } else if (countToday >= 3) {
      outDoc["busy_level_today"] = "dia_cheio";
    } else {
      outDoc["busy_level_today"] = "dia_tranquilo";
    }

    if (countTomorrow == 0) {
      outDoc["busy_level_tomorrow"] = "dia_livre";
    } else if (countTomorrow >= 3) {
      outDoc["busy_level_tomorrow"] = "dia_cheio";
    } else {
      outDoc["busy_level_tomorrow"] = "dia_tranquilo";
    }

    outDoc["instruction"] =
        "Responda com naturalidade, carinho e objetividade a pergunta do usuario: "
        "- Se perguntou da agenda de hoje: resuma os eventos de hoje e o ritmo do dia ('dia_livre', 'dia_tranquilo' ou 'dia_cheio'). Se livre, comente com bom humor que a agenda esta limpinha. "
        "- Se perguntou de amanha: resuma os compromissos de amanha (ou avise se amanha estiver livre). "
        "- Se perguntou do proximo compromisso: informe qual o proximo evento marcado (identificado por 'is_next': true), o horario e quando vai comecar. "
        "Fale sempre em portugues brasileiro acolhedor e fluido.";

    String jsonResult;
    serializeJson(outDoc, jsonResult);
    Serial.printf("[GoogleCalendar] returned %u events (%u hoje, %u amanha). JSON len=%u\n",
                  static_cast<unsigned>(countToday + countTomorrow),
                  static_cast<unsigned>(countToday),
                  static_cast<unsigned>(countTomorrow),
                  static_cast<unsigned>(jsonResult.length()));
    return jsonResult;
  }

 private:
  ConfigManager& config_;
  SemaphoreHandle_t calMutex_ = nullptr;
  String cachedUrl_;
  int cachedTimezoneOffset_ = -3;
  std::vector<CalendarEvent> cachedEvents_;
  String lastPrefetchDateStr_;
  std::atomic<bool> hasCache_{false};
  std::atomic<bool> refreshRequested_{false};
  std::atomic<bool> inFlight_{false};
  uint32_t lastFetchMs_ = 0;

  static String makeWarmingUpJson() {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = "calendar_warming_up";
    doc["message"] = "A sincronizacao da agenda ainda esta em andamento ou temporariamente indisponivel.";
    doc["instruction"] = "Avise com simpatia que a agenda ainda esta sincronizando ou temporariamente indisponivel e peca para tentar novamente em alguns instantes. NUNCA diga que o dia esta livre nem invente compromissos.";
    String out;
    serializeJson(doc, out);
    return out;
  }

  static void parseIcalDateTime(const char* raw, int& year, int& month, int& day,
                                int& hour, int& min, bool& isUtc, bool& isAllDay) {
    year = 0; month = 0; day = 0; hour = 0; min = 0;
    if (!raw) return;
    size_t len = strlen(raw);
    isUtc = (len > 0 && raw[len - 1] == 'Z');
    isAllDay = (len == 8 && strchr(raw, 'T') == nullptr);

    if (len >= 8) {
      char tmp[5] = {0};
      tmp[0] = raw[0]; tmp[1] = raw[1]; tmp[2] = raw[2]; tmp[3] = raw[3];
      year = atoi(tmp);
      tmp[2] = '\0';
      tmp[0] = raw[4]; tmp[1] = raw[5];
      month = atoi(tmp);
      tmp[0] = raw[6]; tmp[1] = raw[7];
      day = atoi(tmp);
    }
    const char* tPos = strchr(raw, 'T');
    if (tPos && strlen(tPos) >= 5) {
      char tmp[3] = {0};
      tmp[0] = tPos[1]; tmp[1] = tPos[2];
      hour = atoi(tmp);
      tmp[0] = tPos[3]; tmp[1] = tPos[4];
      min = atoi(tmp);
    }
  }

  static void cleanIcalText(const char* src, char* out, size_t maxLen) {
    if (!src || !out || maxLen == 0) return;
    size_t si = 0, oi = 0;
    while (src[si] != '\0' && oi + 1 < maxLen) {
      if (src[si] == '\\') {
        si++;
        if (src[si] == '\0') break;
        if (src[si] == 'n' || src[si] == 'N') out[oi++] = ' ';
        else if (src[si] == ',' || src[si] == ';' || src[si] == '\\') out[oi++] = src[si];
        else out[oi++] = src[si];
        si++;
      } else {
        out[oi++] = src[si++];
      }
    }
    out[oi] = '\0';
    while (oi > 0 && (out[oi - 1] == ' ' || out[oi - 1] == '\r' || out[oi - 1] == '\n' || out[oi - 1] == '\t')) {
      out[--oi] = '\0';
    }
  }
};

