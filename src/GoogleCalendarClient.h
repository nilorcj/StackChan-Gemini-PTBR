#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include "ConfigManager.h"

class GoogleCalendarClient {
 public:
  struct CalendarEvent {
    String title;
    String dateStr;     // "DD/MM/YYYY"
    String dateLabel;   // "hoje" ou "amanha"
    String startTimeStr;
    String endTimeStr;
    String location;
    bool isAllDay = false;
    int startMinutes = 0; // minutes from midnight for sorting
    int dayOffset = 0;    // 0 = hoje, 1 = amanha
  };

  explicit GoogleCalendarClient(ConfigManager& config) : config_(config) {}

  bool prefetch(bool force = false) {
    uint32_t now = millis();
    if (!force && hasCache_ && (now - lastFetchMs_ < 1800000)) {
      return true; // Cache valid for 30 minutes
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

    if (ESP.getMaxAllocHeap() < 36000) {
      Serial.printf("[GoogleCalendar] prefetch deferred: maxAllocHeap=%u < 36KB\n",
                    static_cast<unsigned>(ESP.getMaxAllocHeap()));
      return false;
    }

    // Determine current local date/time in Brasilia (GMT-3)
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

    // Tomorrow calculation
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

    Serial.printf("[GoogleCalendar] fetching agenda for %s (hoje) and %s (amanha)\n", readableDate, readableTomorrow);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(7500);

    if (!http.begin(client, url)) {
      Serial.println("[GoogleCalendar] HTTPS begin failed");
      return false;
    }

    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) StackChan-Pix/1.0");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      Serial.printf("[GoogleCalendar] HTTP GET failed code=%d\n", httpCode);
      http.end();
      return false;
    }

    std::vector<CalendarEvent> parsedEvents;
    WiFiClient* stream = http.getStreamPtr();

    bool inEvent = false;
    String curSummary = "";
    String curStartRaw = "";
    String curEndRaw = "";
    String curLoc = "";
    bool isAllDay = false;

    while (http.connected() && (stream->available() > 0 || stream->peek() != -1)) {
      String line = stream->readStringUntil('\n');
      line.trim();
      if (!line.length()) continue;

      if (line.startsWith("BEGIN:VEVENT")) {
        inEvent = true;
        curSummary = "";
        curStartRaw = "";
        curEndRaw = "";
        curLoc = "";
        isAllDay = false;
        continue;
      }

      if (line.startsWith("END:VEVENT")) {
        if (inEvent && curSummary.length() > 0 && curStartRaw.length() > 0) {
          int evYear = 0, evMonth = 0, evDay = 0, evHour = 0, evMin = 0;
          bool isUtc = false;

          parseIcalDateTime(curStartRaw, evYear, evMonth, evDay, evHour, evMin, isUtc, isAllDay);

          // If time was UTC (ending with Z), convert to Brasilia (GMT-3) using tm normalization
          if (isUtc && !isAllDay) {
            struct tm t = {};
            t.tm_year = evYear - 1900;
            t.tm_mon = evMonth - 1;
            t.tm_mday = evDay;
            t.tm_hour = evHour - 3;
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

          int dayOffset = -1;
          String dateLabel = "";
          String dateDisplay = "";

          if (strcmp(evDateStr, todayStr) == 0) {
            dayOffset = 0;
            dateLabel = "hoje";
            dateDisplay = String(readableDate);
          } else if (strcmp(evDateStr, tomorrowStr) == 0) {
            dayOffset = 1;
            dateLabel = "amanha";
            dateDisplay = String(readableTomorrow);
          }

          if (dayOffset >= 0) {
            CalendarEvent ev;
            ev.title = cleanIcalText(curSummary);
            ev.location = cleanIcalText(curLoc);
            ev.isAllDay = isAllDay;
            ev.dayOffset = dayOffset;
            ev.dateLabel = dateLabel;
            ev.dateStr = dateDisplay;

            if (isAllDay) {
              ev.startTimeStr = "Dia todo";
              ev.endTimeStr = "";
              ev.startMinutes = 0;
            } else {
              char sBuf[16];
              snprintf(sBuf, sizeof(sBuf), "%02d:%02d", evHour, evMin);
              ev.startTimeStr = String(sBuf);
              ev.startMinutes = evHour * 60 + evMin;

              if (curEndRaw.length()) {
                int endY = 0, endM = 0, endD = 0, endH = 0, endMin = 0;
                bool endUtc = false, endAll = false;
                parseIcalDateTime(curEndRaw, endY, endM, endD, endH, endMin, endUtc, endAll);
                if (endUtc && !endAll) {
                  struct tm et = {};
                  et.tm_year = endY - 1900;
                  et.tm_mon = endM - 1;
                  et.tm_mday = endD;
                  et.tm_hour = endH - 3;
                  et.tm_min = endMin;
                  et.tm_sec = 0;
                  et.tm_isdst = -1;
                  mktime(&et);
                  endH = et.tm_hour;
                  endMin = et.tm_min;
                }
                char eBuf[16];
                snprintf(eBuf, sizeof(eBuf), "%02d:%02d", endH, endMin);
                ev.endTimeStr = String(eBuf);
              }
            }

            parsedEvents.push_back(ev);
          }
        }
        inEvent = false;
        continue;
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
        }
      }
    }

    http.end();

    // Sort: today first (offset 0), then tomorrow (offset 1); within each day: all-day first, then by time
    std::sort(parsedEvents.begin(), parsedEvents.end(), [](const CalendarEvent& a, const CalendarEvent& b) {
      if (a.dayOffset != b.dayOffset) return a.dayOffset < b.dayOffset;
      if (a.isAllDay != b.isAllDay) return a.isAllDay;
      return a.startMinutes < b.startMinutes;
    });

    cachedEvents_ = std::move(parsedEvents);
    lastTodayStr_ = String(todayStr);
    lastReadableDate_ = String(readableDate);
    lastReadableTomorrow_ = String(readableTomorrow);
    hasCache_ = true;
    lastFetchMs_ = millis();

    size_t countToday = 0;
    size_t countTomorrow = 0;
    for (const auto& ev : cachedEvents_) {
      if (ev.dayOffset == 0) countToday++;
      else if (ev.dayOffset == 1) countTomorrow++;
    }

    Serial.printf("[GoogleCalendar] prefetch complete: %u events (%u hoje, %u amanha)\n",
                  static_cast<unsigned>(cachedEvents_.size()),
                  static_cast<unsigned>(countToday),
                  static_cast<unsigned>(countTomorrow));
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

    // Determine current local date/time in Brasilia (GMT-3)
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
    for (const auto& ev : cachedEvents_) {
      if (ev.dayOffset == 0) countToday++;
      else if (ev.dayOffset == 1) countTomorrow++;
    }

    // Build the result JSON for Gemini Live
    JsonDocument outDoc;
    outDoc["ok"] = true;
    outDoc["today_date"] = readableDate;
    outDoc["tomorrow_date"] = readableTomorrow;
    outDoc["current_time"] = readableTime;
    outDoc["total_events_today"] = countToday;
    outDoc["total_events_tomorrow"] = countTomorrow;
    outDoc["total_events"] = cachedEvents_.size();

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

    auto evArr = outDoc["events"].to<JsonArray>();
    int nowMinutes = curHour * 60 + curMin;
    bool nextFound = false;

    for (const auto& ev : cachedEvents_) {
      auto obj = evArr.add<JsonObject>();
      obj["day"] = ev.dateLabel; // "hoje" ou "amanha"
      obj["date"] = ev.dateStr;  // "25/09/2026"
      obj["title"] = ev.title;
      obj["time"] = ev.isAllDay ? "Dia todo" : (ev.startTimeStr + (ev.endTimeStr.length() ? " as " + ev.endTimeStr : ""));
      if (ev.location.length()) obj["location"] = ev.location;

      if (ev.dayOffset == 0 && !ev.isAllDay && !nextFound && ev.startMinutes >= nowMinutes) {
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

    outDoc["instruction"] =
        "Responda com naturalidade, carinho e objetividade a pergunta do usuario: "
        "- Se perguntou da agenda de hoje: resuma os eventos de hoje e o ritmo do dia ('dia_livre', 'dia_tranquilo' ou 'dia_cheio'). Se livre, comente com bom humor que a agenda esta limpinha. "
        "- Se perguntou de amanha: resuma os compromissos de amanha (ou avise se amanha estiver livre). "
        "- Se perguntou do proximo compromisso: informe qual o proximo evento marcado (identificado por 'is_next': true), o horario e quando vai comecar. "
        "Fale sempre em portugues brasileiro acolhedor e fluido.";

    String jsonResult;
    serializeJson(outDoc, jsonResult);
    Serial.printf("[GoogleCalendar] returned %u events (%u hoje, %u amanha). JSON len=%u\n",
                  static_cast<unsigned>(cachedEvents_.size()),
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
  String lastTodayStr_;
  String lastReadableDate_;
  String lastReadableTomorrow_;
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
