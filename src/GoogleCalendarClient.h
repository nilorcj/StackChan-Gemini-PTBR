#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_sntp.h>
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

  explicit GoogleCalendarClient(ConfigManager& config) : config_(config) {}

  bool isCacheValidForToday() const {
    if (!hasCache_) return false;
    time_t nowSec = time(nullptr);
    struct tm nowTm {};
    localtime_r(&nowSec, &nowTm);
    char todayStr[16];
    snprintf(todayStr, sizeof(todayStr), "%04d%02d%02d",
             nowTm.tm_year + 1900, nowTm.tm_mon + 1, nowTm.tm_mday);
    return (lastPrefetchDateStr_ == todayStr);
  }

  bool prefetch(bool force = false) {
    uint32_t now = millis();
    if (hasCache_ && !isCacheValidForToday()) {
      Serial.printf("[GoogleCalendar] cache date (%s) != today -> forcing refresh\n",
                    lastPrefetchDateStr_.c_str());
      force = true;
    }
    if (!force && hasCache_ && (now - lastFetchMs_ < 1800000)) {
      return true; // Cache valid for 30 minutes on same date
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[GoogleCalendar] prefetch aborted: Wi-Fi not connected");
      return false;
    }

    String url = config_.readCalendarUrl();
    url.trim();
    if (!url.length()) {
      Serial.println("[GoogleCalendar] prefetch aborted: calendar URL missing");
      return false;
    }

    if (ESP.getFreeHeap() < 24000 || ESP.getMaxAllocHeap() < 12000) {
      Serial.printf("[GoogleCalendar] prefetch deferred: freeHeap=%u maxAllocHeap=%u\n",
                    static_cast<unsigned>(ESP.getFreeHeap()),
                    static_cast<unsigned>(ESP.getMaxAllocHeap()));
      return false;
    }

    // Ensure NTP has synchronized so we do not evaluate dates with an un-synced clock
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
      for (int i = 0; i < 20 && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; ++i) {
        delay(100);
      }
    }

    // Determine current local date/time
    time_t nowSec = time(nullptr);
    struct tm nowTm {};
    localtime_r(&nowSec, &nowTm);

    int curYear = nowTm.tm_year + 1900;
    if (curYear < 2024) {
      for (int i = 0; i < 10 && curYear < 2024; ++i) {
        delay(150);
        nowSec = time(nullptr);
        localtime_r(&nowSec, &nowTm);
        curYear = nowTm.tm_year + 1900;
      }
    }
    int curMonth = nowTm.tm_mon + 1;
    int curDay = nowTm.tm_mday;

    char todayStr[16];
    snprintf(todayStr, sizeof(todayStr), "%04d%02d%02d", curYear, curMonth, curDay);

    char readableDate[32];
    snprintf(readableDate, sizeof(readableDate), "%02d/%02d/%04d", curDay, curMonth, curYear);

    // Tomorrow calculation (+1 day)
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

    // Day after tomorrow calculation (+2 days, buffer for midnight rollover)
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

    Serial.printf("[GoogleCalendar] fetching agenda for %s (hoje), %s (amanha), %s (+2d)\n",
                  readableDate, readableTomorrow, readableDayAfter);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(12000);

    if (!http.begin(client, url)) {
      Serial.println("[GoogleCalendar] HTTPS begin failed");
      return false;
    }

    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) StackChan-Pix/1.0");
    http.addHeader("Accept", "text/calendar, text/plain, */*");
    http.addHeader("Accept-Encoding", "identity");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      Serial.printf("[GoogleCalendar] HTTP GET failed code=%d\n", httpCode);
      http.end();
      return false;
    }

    String transfer = http.header("Transfer-Encoding");
    String encoding = http.header("Content-Encoding");
    Serial.printf("[GoogleCalendar] HTTP 200 OK transfer=%s encoding=%s len=%d\n",
                  transfer.c_str(), encoding.c_str(), http.getSize());

    std::vector<CalendarEvent> parsedEvents;
    WiFiClient* stream = http.getStreamPtr();

    bool inEvent = false;
    String curSummary = "";
    String curStartRaw = "";
    String curEndRaw = "";
    String curLoc = "";
    String curRrule = "";
    bool isAllDay = false;

    uint32_t lastDataMs = millis();
    size_t lineCount = 0;
    size_t totalBytes = 0;
    size_t veventCount = 0;
    String prevLine = "";

    auto evaluateEvent = [&]() {
      if (!inEvent || !curSummary.length() || !curStartRaw.length()) return;
      veventCount++;

      int evYear = 0, evMonth = 0, evDay = 0, evHour = 0, evMin = 0;
      bool isUtc = false;
      parseIcalDateTime(curStartRaw, evYear, evMonth, evDay, evHour, evMin, isUtc, isAllDay);

      // If time was UTC (ending with Z), convert to local time using tm normalization
      if (isUtc && !isAllDay) {
        struct tm t = {};
        t.tm_year = evYear - 1900;
        t.tm_mon = evMonth - 1;
        t.tm_mday = evDay;
        t.tm_hour = evHour + config_.config().timezoneOffset;
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

      char evEndDateStr[16] = {0};
      int endH = 0, endMin = 0;
      String endTimeFormatted = "";
      if (curEndRaw.length()) {
        int endY = 0, endM = 0, endD = 0;
        bool endUtc = false, endAll = false;
        parseIcalDateTime(curEndRaw, endY, endM, endD, endH, endMin, endUtc, endAll);
        if (endUtc && !endAll) {
          struct tm et = {};
          et.tm_year = endY - 1900;
          et.tm_mon = endM - 1;
          et.tm_mday = endD;
          et.tm_hour = endH + config_.config().timezoneOffset;
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
        char eBuf[16];
        snprintf(eBuf, sizeof(eBuf), "%02d:%02d", endH, endMin);
        endTimeFormatted = String(eBuf);
      }

      // Check match for a given target date
      auto matchesTargetDate = [&](int targetOffset, const char* targetDateStr, int targetWday, const char* targetDayCode) -> bool {
        // Direct exact date match
        if (strcmp(evDateStr, targetDateStr) == 0) return true;

        // Multi-day event spanning target date
        if (evEndDateStr[0] != 0 && isAllDay) {
          if (strcmp(targetDateStr, evDateStr) >= 0 && strcmp(targetDateStr, evEndDateStr) < 0) {
            return true;
          }
        }

        // Recurrence rule (RRULE)
        if (curRrule.length() > 0 && strcmp(targetDateStr, evDateStr) >= 0) {
          // Check UNTIL=
          int untilIdx = curRrule.indexOf("UNTIL=");
          if (untilIdx >= 0) {
            String uVal = curRrule.substring(untilIdx + 6);
            int semi = uVal.indexOf(';');
            if (semi >= 0) uVal = uVal.substring(0, semi);
            int uY = 0, uM = 0, uD = 0, uH = 0, uMin = 0;
            bool uUtc = false, uAll = false;
            parseIcalDateTime(uVal, uY, uM, uD, uH, uMin, uUtc, uAll);
            char uDateStr[16];
            snprintf(uDateStr, sizeof(uDateStr), "%04d%02d%02d", uY, uM, uD);
            if (strcmp(targetDateStr, uDateStr) > 0) return false; // Expired
          }

          // Check COUNT=
          int countIdx = curRrule.indexOf("COUNT=");
          if (countIdx >= 0) {
            String countVal = curRrule.substring(countIdx + 6);
            int semi = countVal.indexOf(';');
            if (semi >= 0) countVal = countVal.substring(0, semi);
            int countN = countVal.toInt();
            if (countN > 0) {
              int maxDays = countN;
              if (curRrule.indexOf("FREQ=WEEKLY") >= 0) maxDays = countN * 7;
              else if (curRrule.indexOf("FREQ=MONTHLY") >= 0) maxDays = countN * 31;
              else if (curRrule.indexOf("FREQ=YEARLY") >= 0) maxDays = countN * 366;
              int intIdx = curRrule.indexOf("INTERVAL=");
              if (intIdx >= 0) {
                String intVal = curRrule.substring(intIdx + 9);
                int semiI = intVal.indexOf(';');
                if (semiI >= 0) intVal = intVal.substring(0, semiI);
                int intervalN = intVal.toInt();
                if (intervalN > 1) maxDays *= intervalN;
              }
              struct tm et = {};
              et.tm_year = evYear - 1900;
              et.tm_mon = evMonth - 1;
              et.tm_mday = evDay + maxDays;
              et.tm_hour = 12;
              et.tm_isdst = -1;
              mktime(&et);
              char maxDateStr[16];
              snprintf(maxDateStr, sizeof(maxDateStr), "%04d%02d%02d", et.tm_year + 1900, et.tm_mon + 1, et.tm_mday);
              if (strcmp(targetDateStr, maxDateStr) > 0) return false; // Expired
            }
          }

          if (curRrule.indexOf("FREQ=DAILY") >= 0) {
            return true;
          }
          if (curRrule.indexOf("FREQ=WEEKLY") >= 0) {
            int bydayIdx = curRrule.indexOf("BYDAY=");
            if (bydayIdx >= 0) {
              String bydayStr = curRrule.substring(bydayIdx + 6);
              int semi = bydayStr.indexOf(';');
              if (semi >= 0) bydayStr = bydayStr.substring(0, semi);
              if (bydayStr.indexOf(targetDayCode) >= 0) return true;
            } else {
              struct tm st = {};
              st.tm_year = evYear - 1900;
              st.tm_mon = evMonth - 1;
              st.tm_mday = evDay;
              st.tm_hour = 12;
              st.tm_isdst = -1;
              mktime(&st);
              if (st.tm_wday == targetWday) return true;
            }
          }
          if (curRrule.indexOf("FREQ=YEARLY") >= 0) {
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
                      curSummary.c_str(), curStartRaw.c_str(), evYear, evMonth, evDay, evHour, evMin,
                      curRrule.c_str(), matchesToday ? 1 : 0, matchesTom ? 1 : 0, matchesDayAfter ? 1 : 0);
      }

      auto addEventForDay = [&](int offset, const String& dStr) {
        CalendarEvent ev;
        ev.title = cleanIcalText(curSummary);
        ev.location = cleanIcalText(curLoc);
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
          ev.startTimeStr = String(sBuf);
          ev.startMinutes = evHour * 60 + evMin;
          ev.endTimeStr = endTimeFormatted;
        }
        parsedEvents.push_back(ev);
      };

      if (matchesToday) {
        addEventForDay(0, String(readableDate));
      }
      if (matchesTom) {
        addEventForDay(1, String(readableTomorrow));
      }
      if (matchesDayAfter) {
        addEventForDay(2, String(readableDayAfter));
      }
    };

    auto handleIcalLine = [&](String line) {
      line.trim();
      if (!line.length()) return;

      if (line.startsWith("BEGIN:VEVENT")) {
        inEvent = true;
        curSummary = "";
        curStartRaw = "";
        curEndRaw = "";
        curLoc = "";
        curRrule = "";
        isAllDay = false;
        return;
      }

      if (line.startsWith("END:VEVENT")) {
        evaluateEvent();
        inEvent = false;
        return;
      }

      if (inEvent) {
        if (line.startsWith("SUMMARY:")) {
          curSummary = line.substring(8);
        } else if (line.startsWith("LOCATION:")) {
          curLoc = line.substring(9);
        } else if (line.startsWith("DTSTART")) {
          int colon = line.indexOf(':');
          if (colon >= 0) curStartRaw = line.substring(colon + 1);
        } else if (line.startsWith("DTEND")) {
          int colon = line.indexOf(':');
          if (colon >= 0) curEndRaw = line.substring(colon + 1);
        } else if (line.startsWith("RRULE:")) {
          curRrule = line.substring(6);
        }
      }
    };

    while (http.connected() || stream->available() > 0) {
      if (stream->available() > 0) {
        String line = stream->readStringUntil('\n');
        totalBytes += line.length() + 1;
        lastDataMs = millis();

        if (line.endsWith("\r")) line.remove(line.length() - 1);

        // RFC 5545 line folding: if starts with space or tab, append to previous line
        if (line.startsWith(" ") || line.startsWith("\t")) {
          prevLine += line.substring(1);
          continue;
        }

        if (prevLine.length() > 0) {
          handleIcalLine(prevLine);
        }
        prevLine = line;
        lineCount++;
      } else {
        delay(10);
        if (millis() - lastDataMs > 5000) {
          Serial.println("[GoogleCalendar] Stream read timeout (5s idle)");
          break;
        }
      }
    }
    if (prevLine.length() > 0) {
      handleIcalLine(prevLine);
    }

    http.end();

    Serial.printf("[GoogleCalendar] stream finished: %u bytes, %u lines, %u vevents, %u parsed events\n",
                  static_cast<unsigned>(totalBytes),
                  static_cast<unsigned>(lineCount),
                  static_cast<unsigned>(veventCount),
                  static_cast<unsigned>(parsedEvents.size()));

    // Sort: earlier days first, then all-day, then by start time
    std::sort(parsedEvents.begin(), parsedEvents.end(), [](const CalendarEvent& a, const CalendarEvent& b) {
      if (a.dayOffset != b.dayOffset) return a.dayOffset < b.dayOffset;
      if (a.isAllDay != b.isAllDay) return a.isAllDay;
      return a.startMinutes < b.startMinutes;
    });

    cachedEvents_ = std::move(parsedEvents);
    lastPrefetchDateStr_ = String(todayStr);
    hasCache_ = true;
    lastFetchMs_ = millis();

    Serial.printf("[GoogleCalendar] prefetch complete: %u events in 3-day cache\n",
                  static_cast<unsigned>(cachedEvents_.size()));
    return true;
  }

  String fetchDailyAgendaJson() {
    Serial.printf("[DIAG:calendar:entry] task='%s' stackHWM=%u freeHeap=%u minFreeHeap=%u maxAllocHeap=%u hasCache=%d\n",
                  pcTaskGetName(nullptr),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()),
                  hasCache_ ? 1 : 0);

    String url = config_.readCalendarUrl();
    url.trim();
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

    if (!hasCache_) {
      JsonDocument doc;
      doc["ok"] = false;
      doc["error"] = "calendar_warming_up";
      doc["message"] = "A sincronizacao da agenda ainda esta em andamento ou temporariamente indisponivel.";
      doc["instruction"] = "Avise com simpatia que a agenda ainda esta sincronizando ou temporariamente indisponivel e peca para tentar novamente em alguns instantes. NUNCA diga que o dia esta livre nem invente compromissos.";
      String out;
      serializeJson(doc, out);
      return out;
    }

    // Determine current local date/time
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

    // Build the result JSON for Gemini Live - dynamic date matching against current clock
    JsonDocument outDoc;
    auto evArr = outDoc["events"].to<JsonArray>();

    for (const auto& ev : cachedEvents_) {
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
        continue; // Event is outside today/tomorrow window
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

  bool hasCache() const { return hasCache_; }
  size_t cachedEventCount() const { return cachedEvents_.size(); }

 private:
  ConfigManager& config_;
  std::vector<CalendarEvent> cachedEvents_;
  String lastPrefetchDateStr_;
  bool hasCache_ = false;
  uint32_t lastFetchMs_ = 0;

  static void parseIcalDateTime(const String& raw, int& year, int& month, int& day,
                                int& hour, int& min, bool& isUtc, bool& isAllDay) {
    year = 0; month = 0; day = 0; hour = 0; min = 0;
    isUtc = raw.endsWith("Z");
    isAllDay = (raw.length() == 8 && raw.indexOf('T') < 0);

    if (raw.length() >= 8) {
      year = raw.substring(0, 4).toInt();
      month = raw.substring(4, 6).toInt();
      day = raw.substring(6, 8).toInt();
    }
    int tPos = raw.indexOf('T');
    if (tPos >= 0 && raw.length() >= tPos + 5) {
      hour = raw.substring(tPos + 1, tPos + 3).toInt();
      min = raw.substring(tPos + 3, tPos + 5).toInt();
    }
  }

  static String cleanIcalText(String s) {
    s.replace("\\,", ",");
    s.replace("\\;", ";");
    s.replace("\\n", " ");
    s.replace("\\N", " ");
    s.replace("\\\\", "\\");
    s.trim();
    return s;
  }
};
