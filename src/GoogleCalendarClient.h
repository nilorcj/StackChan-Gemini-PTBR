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
    String startTimeStr;
    String endTimeStr;
    String location;
    bool isAllDay = false;
    int startMinutes = 0; // minutes from midnight for sorting
  };

  explicit GoogleCalendarClient(ConfigManager& config) : config_(config) {}

  String fetchDailyAgendaJson() {
    Serial.printf("[DIAG:calendar:entry] task='%s' stackHWM=%u freeHeap=%u minFreeHeap=%u maxAllocHeap=%u\n",
                  pcTaskGetName(nullptr),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));

    if (WiFi.status() != WL_CONNECTED) {
      JsonDocument doc;
      doc["ok"] = false;
      doc["error"] = "wifi_disconnected";
      doc["message"] = "Sem conexao Wi-Fi para consultar a agenda.";
      String out;
      serializeJson(doc, out);
      return out;
    }

    String url = config_.readCalendarUrl();
    url.trim();
    if (!url.length()) {
      JsonDocument doc;
      doc["ok"] = false;
      doc["error"] = "calendar_url_missing";
      doc["message"] = "A URL da agenda do Google Calendar ainda nao foi configurada no cartao SD (/app/StackChan/secrets/calendar_url.txt).";
      String out;
      serializeJson(doc, out);
      return out;
    }

    // Determine current local date/time in Brasilia (GMT-3)
    time_t nowSec = time(nullptr);
    struct tm nowTm {};
    localtime_r(&nowSec, &nowTm);

    int curYear = nowTm.tm_year + 1900;
    if (curYear < 2024) {
      // NTP may take a moment after boot; give it a brief wait if just connected
      for (int i = 0; i < 10 && curYear < 2024; ++i) {
        delay(150);
        nowSec = time(nullptr);
        localtime_r(&nowSec, &nowTm);
        curYear = nowTm.tm_year + 1900;
      }
    }
    int curMonth = nowTm.tm_mon + 1;
    int curDay = nowTm.tm_mday;
    int curHour = nowTm.tm_hour;
    int curMin = nowTm.tm_min;

    char todayStr[16];
    snprintf(todayStr, sizeof(todayStr), "%04d%02d%02d", curYear, curMonth, curDay);

    char readableDate[32];
    snprintf(readableDate, sizeof(readableDate), "%02d/%02d/%04d", curDay, curMonth, curYear);

    char readableTime[16];
    snprintf(readableTime, sizeof(readableTime), "%02d:%02d", curHour, curMin);

    Serial.printf("GoogleCalendar: fetching agenda for %s (%s) from URL\n", todayStr, readableDate);

    Serial.printf("[DIAG:calendar:before_client_secure] task='%s' stackHWM=%u freeHeap=%u minFreeHeap=%u maxAllocHeap=%u\n",
                  pcTaskGetName(nullptr),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(7500);

    Serial.printf("[DIAG:calendar:before_http_begin] task='%s' stackHWM=%u freeHeap=%u minFreeHeap=%u maxAllocHeap=%u\n",
                  pcTaskGetName(nullptr),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));

    if (!http.begin(client, url)) {
      JsonDocument doc;
      doc["ok"] = false;
      doc["error"] = "connection_failed";
      doc["message"] = "Nao foi possivel iniciar conexao HTTPS com a agenda.";
      String out;
      serializeJson(doc, out);
      return out;
    }

    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) StackChan-Pix/1.0");

    Serial.printf("[DIAG:calendar:before_http_get] task='%s' stackHWM=%u freeHeap=%u minFreeHeap=%u maxAllocHeap=%u\n",
                  pcTaskGetName(nullptr),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));

    int httpCode = http.GET();

    Serial.printf("[DIAG:calendar:after_http_get] code=%d task='%s' stackHWM=%u freeHeap=%u minFreeHeap=%u maxAllocHeap=%u\n",
                  httpCode,
                  pcTaskGetName(nullptr),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));

    if (httpCode != HTTP_CODE_OK) {
      Serial.printf("GoogleCalendar: HTTP GET failed code=%d\n", httpCode);
      http.end();
      JsonDocument doc;
      doc["ok"] = false;
      doc["http_code"] = httpCode;
      doc["error"] = "http_error";
      doc["message"] = "Erro ao baixar arquivo da agenda Google.";
      String out;
      serializeJson(doc, out);
      return out;
    }

    std::vector<CalendarEvent> todayEvents;
    WiFiClient* stream = http.getStreamPtr();

    bool inEvent = false;
    String curSummary = "";
    String curStartRaw = "";
    String curEndRaw = "";
    String curLoc = "";
    bool isAllDay = false;

    // Stream-parse line by line to minimize RAM usage
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

          // If time was UTC (ending with Z), convert to Brasilia (GMT-3)
          if (isUtc && !isAllDay) {
            evHour -= 3;
            if (evHour < 0) {
              evHour += 24;
              evDay -= 1; // approximate for day boundary
            }
          }

          char evDateStr[16];
          snprintf(evDateStr, sizeof(evDateStr), "%04d%02d%02d", evYear, evMonth, evDay);

          // Check if event is today
          if (strcmp(evDateStr, todayStr) == 0) {
            CalendarEvent ev;
            ev.title = cleanIcalText(curSummary);
            ev.location = cleanIcalText(curLoc);
            ev.isAllDay = isAllDay;

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
                  endH -= 3;
                  if (endH < 0) endH += 24;
                }
                char eBuf[16];
                snprintf(eBuf, sizeof(eBuf), "%02d:%02d", endH, endMin);
                ev.endTimeStr = String(eBuf);
              }
            }

            todayEvents.push_back(ev);
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

    // Sort events chronologically (all-day first, then by hour/minute)
    std::sort(todayEvents.begin(), todayEvents.end(), [](const CalendarEvent& a, const CalendarEvent& b) {
      if (a.isAllDay != b.isAllDay) return a.isAllDay;
      return a.startMinutes < b.startMinutes;
    });

    // Build the result JSON for Gemini Live
    JsonDocument outDoc;
    outDoc["ok"] = true;
    outDoc["today_date"] = readableDate;
    outDoc["current_time"] = readableTime;
    outDoc["total_events"] = todayEvents.size();

    if (todayEvents.empty()) {
      outDoc["busy_level"] = "dia_livre";
      outDoc["events"].to<JsonArray>();
      outDoc["instruction"] =
          "Nenhum compromisso marcado para hoje. Diga isso com muita simpatia, leveza e bom humor (ex: 'Agenda limpinha hoje! Nada marcado, dia perfeito pra focar ou descansar um pouco').";
    } else {
      if (todayEvents.size() >= 3) {
        outDoc["busy_level"] = "dia_cheio";
      } else {
        outDoc["busy_level"] = "dia_tranquilo";
      }

      auto evArr = outDoc["events"].to<JsonArray>();
      int nowMinutes = curHour * 60 + curMin;
      bool nextFound = false;

      for (const auto& ev : todayEvents) {
        auto obj = evArr.add<JsonObject>();
        obj["title"] = ev.title;
        obj["time"] = ev.isAllDay ? "Dia todo" : (ev.startTimeStr + (ev.endTimeStr.length() ? " as " + ev.endTimeStr : ""));
        if (ev.location.length()) obj["location"] = ev.location;

        if (!ev.isAllDay && !nextFound && ev.startMinutes >= nowMinutes) {
          obj["is_next"] = true;
          int deltaMin = ev.startMinutes - nowMinutes;
          obj["starts_in_minutes"] = deltaMin;
          nextFound = true;
        }
      }

      outDoc["instruction"] =
          "Faca um resumo ORGANICO, vivo e acolhedor da agenda de hoje. Comente sobre o ritmo do dia de forma humana (ex: 'Nossa, hoje seu dia ta bem corrido hein!', 'Hoje ta super tranquilo, so tem uma reuniao a tarde...'). Apresente os eventos em ordem com horarios, avise se algum ja esta perto de comecar, e seja prestativa e calorosa.";
    }

    String jsonResult;
    serializeJson(outDoc, jsonResult);
    Serial.printf("GoogleCalendar: returned %u events for today. JSON len=%u\n",
                  static_cast<unsigned>(todayEvents.size()), static_cast<unsigned>(jsonResult.length()));
    return jsonResult;
  }

 private:
  ConfigManager& config_;

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
