#include "GeminiToolBridge.h"
#include "GeminiLiveProbe.h"
#include "MemoryStore.h"
#include "UnitPaHubManager.h"
#include "LiveFeedsManager.h"
#include "GoogleCalendarClient.h"
#include "ConfigManager.h"
#include <M5Unified.h>
#include <M5StackChan.h>
#include <WiFi.h>


String GeminiToolBridge::functionDeclarationsJson() {
  // Keep tool declarations deterministic: do not block Gemini setup on a LAN
  // /tools fetch. Local robot tools are handled here; gateway tools still go
  // through gateway_.callTool().
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  auto noArgTool = [&](const char* name, const char* description) {
    auto tool = arr.add<JsonObject>();
    tool["name"] = name;
    tool["description"] = description;
    auto params = tool["parameters"].to<JsonObject>();
    params["type"] = "object";
    params["properties"].to<JsonObject>();
  };

  noArgTool("get_robot_status", "Get StackChan robot status: battery, network, state, and local memory health.");
  noArgTool("get_sensor_readings", "Read the real sensors connected to StackChan: distance, temperature, humidity, and atmospheric pressure.");
  noArgTool("get_current_datetime", "Consulta a data e o horário atual no relógio local oficial da Pix. Retorna a hora_local, data_local, dia_da_semana e fuso_horario. IMPORTANTE: os valores retornados já são o horário e data locais finais prontos para falar ao usuário; não faça nenhum cálculo ou conversão de fuso horário adicional. Use sempre que o usuário perguntar que horas são, que dia é hoje, dia da semana ou qualquer pergunta sobre data e hora atual.");

  auto volControl = arr.add<JsonObject>();
  volControl["name"] = "set_speaker_volume";
  volControl["description"] = "Ajusta fisicamente o volume dos alto-falantes do robô (0 a 100%). Use sempre que o usuário pedir para aumentar, abaixar ou mudar o volume.";
  auto vcParams = volControl["parameters"].to<JsonObject>();
  vcParams["type"] = "object";
  auto vcProps = vcParams["properties"].to<JsonObject>();
  vcProps["volume_pct"]["type"] = "integer";
  vcProps["volume_pct"]["description"] = "Nível de volume desejado de 0 a 100. Exemplo: 80 para alto, 40 para médio, 20 para baixo.";
  vcProps["volume_pct"]["minimum"] = 0;
  vcProps["volume_pct"]["maximum"] = 100;
  vcParams["required"].add("volume_pct");
  auto news = arr.add<JsonObject>();
  news["name"] = "get_news";
  news["description"] = "Read recent headlines from public RSS feeds. Use for current news; do not invent headlines. Categories: geral, brasil, politica, economia, tecnologia, mundo.";
  auto newsParams = news["parameters"].to<JsonObject>();
  newsParams["type"] = "object";
  auto newsProps = newsParams["properties"].to<JsonObject>();
  newsProps["category"]["type"] = "string";
  newsProps["category"]["description"] = "News category: geral, brasil, politica, economia, tecnologia, or mundo.";
  newsProps["limit"]["type"] = "integer";
  newsProps["limit"]["minimum"] = 1;
  newsProps["limit"]["maximum"] = 5;
  noArgTool("refresh_news_feeds", "Forca a atualizacao imediata dos feeds RSS de noticias do G1 e clima pela internet. Use quando o usuario pedir para atualizar ou buscar noticias novas.");
  noArgTool("get_daily_agenda", "Consulta a agenda do Google Calendar do usuario para hoje e amanha. Use sempre que o usuario perguntar sobre agenda, compromissos de hoje, de amanha ou proximo compromisso, reunioes, afazeres ou horarios. Retorna os compromissos para que voce responda de forma calorosa, organica e humana.");
  noArgTool("ping_gateway", "Check that the home-server StackChan Tool Gateway is reachable.");
  noArgTool("get_server_time", "Get current Unix time from the home server running the gateway.");
  noArgTool("ha_get_light_states", "Get Home Assistant states for StackChan-allowlisted lights.");

  auto haControl = arr.add<JsonObject>();
  haControl["name"] = "ha_control_light";
  haControl["description"] = "Control one allowlisted Home Assistant light. Use only when the user explicitly asks to change a light.";
  auto hcParams = haControl["parameters"].to<JsonObject>();
  hcParams["type"] = "object";
  auto hcProps = hcParams["properties"].to<JsonObject>();
  hcProps["entity_id"]["type"] = "string";
  hcProps["entity_id"]["description"] = "Allowlisted HA light entity_id.";
  hcProps["action"]["type"] = "string";
  hcProps["action"]["description"] = "on, off, or toggle.";
  hcProps["brightness_pct"]["type"] = "integer";
  hcProps["brightness_pct"]["minimum"] = 1;
  hcProps["brightness_pct"]["maximum"] = 100;
  hcParams["required"].add("entity_id");
  hcParams["required"].add("action");

  auto searchMemory = arr.add<JsonObject>();
  searchMemory["name"] = "search_memory";
  searchMemory["description"] = "Time-aware local memory search over active context, safe facts, and semantic summaries. Use for ordinary recall questions. It groups by observed time/session, prefers recent results by default, never exposes raw archived dialogues, and never exposes private secrets/PINs/passwords.";
  auto smParams = searchMemory["parameters"].to<JsonObject>();
  smParams["type"] = "object";
  auto smProps = smParams["properties"].to<JsonObject>();
  smProps["query"]["type"] = "string";
  smProps["query"]["description"] = "Memory search query.";
  smParams["required"].add("query");

  auto rememberFact = arr.add<JsonObject>();
  rememberFact["name"] = "remember_fact";
  rememberFact["description"] = "Grava na memória persistente de longo prazo do robô um fato geral durável sobre o usuário, seu criador, ambiente ou preferências (ex: nome do usuário, quem te criou, estilo de resposta). NUNCA use para senhas, PINs ou segredos (para segredos, use remember_private_memory).";
  auto rfParams = rememberFact["parameters"].to<JsonObject>();
  rfParams["type"] = "object";
  auto rfProps = rfParams["properties"].to<JsonObject>();
  rfProps["fact"]["type"] = "string";
  rfProps["fact"]["description"] = "O fato em linguagem natural a ser lembrado (ex: 'O nome do usuário é Nilo Júnior', 'Nilo Júnior é o criador da Pix', 'O usuário prefere respostas curtas').";
  rfProps["key"]["type"] = "string";
  rfProps["key"]["description"] = "Identificador ou tópico curto para permitir atualização futura (ex: 'user_name', 'creator', 'user_preference').";
  rfProps["category"]["type"] = "string";
  rfProps["category"]["description"] = "Categoria: identity, profile, work, relationship, preference, travel, project, plan, temporary, other ou general.";
  rfProps["importance"]["type"] = "integer";
  rfProps["importance"]["description"] = "Nivel de importancia: 1=baixa (temporario/detalhe menor), 2=normal (padrao), 3=alta (essencial/identidade/trabalho/perfil).";
  rfProps["importance"]["minimum"] = 1;
  rfProps["importance"]["maximum"] = 3;
  rfParams["required"].add("fact");

  auto rememberPrivate = arr.add<JsonObject>();
  rememberPrivate["name"] = "remember_private_memory";
  rememberPrivate["description"] = "Store a private local memory such as a PIN, code, password, token, address, or other user-explicit secret. Use only when the user explicitly asks you to remember it. This stays on the robot SD card and must never be sent to Hermes/gateway or ordinary search_memory.";
  auto rpmParams = rememberPrivate["parameters"].to<JsonObject>();
  rpmParams["type"] = "object";
  auto rpmProps = rpmParams["properties"].to<JsonObject>();
  rpmProps["kind"]["type"] = "string";
  rpmProps["kind"]["description"] = "Type, e.g. private_code, private_pin, private_password, private_address, private_note.";
  rpmProps["label"]["type"] = "string";
  rpmProps["label"]["description"] = "Short non-secret label describing what this value is for.";
  rpmProps["value"]["type"] = "string";
  rpmProps["value"]["description"] = "The exact private value the user explicitly asked to remember.";
  rpmParams["required"].add("value");

  auto recallPrivate = arr.add<JsonObject>();
  recallPrivate["name"] = "recall_private_memory";
  recallPrivate["description"] = "Recall a private local memory only when the user explicitly asks for that saved private value. Never call this for ordinary conversation, never forward its result to external tools, and do not expose it unless the user clearly requested recall.";
  auto rclParams = recallPrivate["parameters"].to<JsonObject>();
  rclParams["type"] = "object";
  auto rclProps = rclParams["properties"].to<JsonObject>();
  rclProps["query"]["type"] = "string";
  rclProps["query"]["description"] = "Short label/query, e.g. 'PIN', 'door code', 'wifi password'. Empty means latest private memory.";

  if (gateway_.isEnabled()) {
    auto askHermes = arr.add<JsonObject>();
    askHermes["name"] = "ask_hermes";
    askHermes["description"] = "Send a command or automation task exclusively to the external Hermes server (e.g. smart home actions, local scripts, LAN devices). NEVER use for general knowledge, tourist advice, chat, opinions, or memory/secrets; answer conversation and general questions directly with your own knowledge.";
    auto ahParams = askHermes["parameters"].to<JsonObject>();
    ahParams["type"] = "object";
    auto ahProps = ahParams["properties"].to<JsonObject>();
    ahProps["question"]["type"] = "string";
    ahProps["question"]["description"] = "Specific server task or home automation command for Hermes.";
    ahProps["urgency"]["type"] = "string";
    ahProps["urgency"]["description"] = "low, normal, or high.";
    ahParams["required"].add("question");
  }

  auto setEmotion = arr.add<JsonObject>();
  setEmotion["name"] = "set_emotion";
  setEmotion["description"] = "Set StackChan face/emotion state.";
  auto seParams = setEmotion["parameters"].to<JsonObject>();
  seParams["type"] = "object";
  auto seProps = seParams["properties"].to<JsonObject>();
  seProps["emotion"]["type"] = "string";
  seProps["emotion"]["description"] = "One of neutral,happy,thinking,looking,speaking,found,error,sleep.";
  seParams["required"].add("emotion");

  auto servo = arr.add<JsonObject>();
  servo["name"] = "servo_gesture";
  servo["description"] =
      "Executa um gesto de cabeca da StackChan. Regra obrigatoria: se o usuario pedir explicitamente para olhar ou virar para a esquerda ou direita ('olhe para a esquerda', 'olhe para a direita', 'vire para o lado'), use obrigatoriamente look_left ou look_right; nunca substitua por nod_yes, shake_no, curious_tilt ou outro gesto. Para expressao espontanea durante conversa, use apenas gestos expressivos sutis (nod_yes, shake_no, curious_tilt).";
  auto sgParams = servo["parameters"].to<JsonObject>();
  sgParams["type"] = "object";
  auto sgProps = sgParams["properties"].to<JsonObject>();
  sgProps["gesture"]["type"] = "string";
  sgProps["gesture"]["description"] =
      "Nome do gesto. Direcionais: look_left, look_right, center_head, look_at_user, look_up, look_down, look_top_left, look_top_right, search_left, search_right, search_center. Expressivos: nod_yes, shake_no, curious_tilt, speaking_micro_motion, stop_motion.";
  sgProps["hold"]["type"] = "string";
  sgProps["hold"]["description"] =
      "Sustentacao do olhar para look_left/look_right: 'normal' (padrao: vira, mantem ~6s e auto-centraliza ao final; ex: 'olhe para a direita'); 'glance' (olhar breve de ~2s e retorna; ex: 'de uma olhada'); 'sustain' (vira e permanece no angulo sem auto-center ate center_head; ex: 'olhe e fique', 'continue olhando').";
  auto holdEnum = sgProps["hold"]["enum"].to<JsonArray>();
  holdEnum.add("glance");
  holdEnum.add("normal");
  holdEnum.add("sustain");
  sgParams["required"].add("gesture");

  auto motion = arr.add<JsonObject>();
  motion["name"] = "head_motion";
  motion["description"] = "Create a safe smooth StackChan head motion on the fly. Use this when the user asks for a custom amount such as 'a little left', 'slightly up', or to keep looking toward someone after a camera search. Coordinates are deltas from the robot's current look anchor: x_deg negative means robot's left, positive means robot's right; y_deg positive means up, negative means down. Firmware clamps to safe ranges, executes asynchronously, and the final keyframe becomes the new look anchor.";
  auto hmParams = motion["parameters"].to<JsonObject>();
  hmParams["type"] = "object";
  auto hmProps = hmParams["properties"].to<JsonObject>();
  hmProps["motion_name"]["type"] = "string";
  hmProps["motion_name"]["description"] = "Short descriptive name, e.g. slight_left, curious_scan.";
  auto steps = hmProps["steps"].to<JsonObject>();
  steps["type"] = "array";
  steps["description"] = "1 to 6 keyframes, each relative to the current look anchor. For a small left turn use one step like {x_deg:-12,y_deg:0,speed:500,hold_ms:500}. For a smooth expressive sequence, include multiple keyframes and usually end at {x_deg:0,y_deg:0} to preserve the current anchor.";
  steps["minItems"] = 1;
  steps["maxItems"] = 6;
  auto item = steps["items"].to<JsonObject>();
  item["type"] = "object";
  auto ip = item["properties"].to<JsonObject>();
  ip["x_deg"]["type"] = "number";
  ip["x_deg"]["description"] = "Yaw delta from the current look anchor. Safe useful range -65..65. Negative=left, positive=right.";
  ip["y_deg"]["type"] = "number";
  ip["y_deg"]["description"] = "Pitch delta from the current look anchor. Safe useful range -6..40. Positive=up, negative=down.";
  ip["speed"]["type"] = "integer";
  ip["speed"]["description"] = "Servo speed 100..700 for normal motions; lower is softer.";
  ip["hold_ms"]["type"] = "integer";
  ip["hold_ms"]["description"] = "How long to wait before next keyframe, 100..1500 ms.";
  hmParams["required"].add("steps");

  auto lookCamera = arr.add<JsonObject>();
  lookCamera["name"] = "look_with_camera";
  lookCamera["description"] = "Take exactly one still snapshot with StackChan's camera ONLY when the user explicitly asks you to visually inspect, look at, or describe what is shown in front of the camera (e.g. 'o que você está vendo na câmera?', 'olha isso aqui'). NEVER call this tool unsolicited, during ordinary conversation, or when the user simply asks you to turn your head (for head turns use servo_gesture).";
  auto lcParams = lookCamera["parameters"].to<JsonObject>();
  lcParams["type"] = "object";
  auto lcProps = lcParams["properties"].to<JsonObject>();
  lcProps["question"]["type"] = "string";
  lcProps["question"]["description"] = "Short visual question, e.g. 'what is in front of me?' or 'what is on the table?'";

  auto endSession = arr.add<JsonObject>();
  endSession["name"] = "end_session";
  endSession["description"] = "End the continuous StackChan conversation and go to sleep ONLY when the user explicitly asks to sleep or rest (e.g. 'vai dormir', 'pode dormir', 'hora de dormir', 'modo soneca', 'não me perturbe', or stop listening). NEVER call this on greetings like 'boa noite' or 'olá'! The robot speaks a brief goodbye and goes to sleep, closing the microphone.";
  auto esParams = endSession["parameters"].to<JsonObject>();
  esParams["type"] = "object";
  auto esProps = esParams["properties"].to<JsonObject>();
  esProps["reason"]["type"] = "string";
  esProps["reason"]["description"] = "Reason, e.g. 'vai_dormir', 'modo_soneca', 'do_not_disturb'.";
  esProps["dnd_minutes"]["type"] = "integer";
  esProps["dnd_minutes"]["description"] = "Minutes for 'não me perturbe' (do not disturb). 0 = normal sleep.";

  String out;
  serializeJson(arr, out);
  return out;
}

bool GeminiToolBridge::executeFastPathGesture(const String& gesture, uint32_t turnId, const String& hold) {
  bool queued = servoGestures_.queueGesture(gesture, hold);
  if (queued) {
    pendingLocalAction_ = gesture;
    pendingLocalActionTurnId_ = turnId;
    pendingLocalActionMs_ = millis();
    pendingLocalActionValid_ = true;
    Serial.printf("[LOCAL_CMD] %s (hold=%s) executed (turnId=%u)\n", gesture.c_str(), hold.c_str(), static_cast<unsigned>(turnId));
  } else {
    invalidatePendingLocalAction();
    Serial.printf("[LOCAL_CMD] %s queue rejected\n", gesture.c_str());
  }
  return queued;
}

void GeminiToolBridge::invalidatePendingLocalAction() {
  pendingLocalActionValid_ = false;
  pendingLocalAction_ = "";
  pendingLocalActionTurnId_ = 0;
  pendingLocalActionMs_ = 0;
}

String GeminiToolBridge::handleFunctionCall(const String& name, const JsonVariantConst& args,
                                            const String& sessionId) {
  JsonDocument localResponse;
  LocalStatus local = handleLocal(name, args, localResponse);
  if (local == LocalStatus::Handled || local == LocalStatus::Error) {
    String out;
    serializeJson(localResponse, out);
    return out;
  }

  if (name == "ask_hermes") {
    String question = String((const char*)(args["question"] | ""));
    String q = question;
    q.toLowerCase();
    if (q.indexOf("pin") >= 0 || q.indexOf("\xD0\xBF\xD0\xB8\xD0\xBD") >= 0 ||
        q.indexOf("password") >= 0 || q.indexOf("\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB") >= 0 ||
        q.indexOf("token") >= 0 || q.indexOf("secret") >= 0 || q.indexOf("\xD1\x81\xD0\xB5\xD0\xBA\xD1\x80\xD0\xB5\xD1\x82") >= 0 ||
        q.indexOf("code") >= 0 || q.indexOf("\xD0\xBA\xD0\xBE\xD0\xB4") >= 0 ||
        q.indexOf("remember") >= 0 || q.indexOf("\xD0\xB7\xD0\xB0\xD0\xBF\xD0\xBE\xD0\xBC") >= 0) {
      JsonDocument blocked;
      blocked["ok"] = false;
      blocked["blocked"] = true;
      blocked["error"] = "private_memory_request_not_forwarded";
      blocked["message"] = "Private remember/secrets/PIN content is not forwarded to Hermes. Acknowledge locally; local transcript memory records the conversation automatically.";
      String out;
      serializeJson(blocked, out);
      return out;
    }
  }

  auto gatewayResult = gateway_.callTool(name, args, sessionId);
  if (gatewayResult.ok) return gatewayResult.body;
  return errorJson(gatewayResult.error.length() ? gatewayResult.error : "gateway_call_failed",
                   gatewayResult.body.length() ? gatewayResult.body : "Tool gateway call failed");
}

GeminiToolBridge::LocalStatus GeminiToolBridge::handleLocal(
    const String& name, const JsonVariantConst& args, JsonDocument& response) {
  if (name == "get_robot_status") {
    response["ok"] = true;
    response["battery_mv"] = M5StackChan.getBatteryVoltage();
    response["battery_ma"] = M5StackChan.getBatteryCurrent();
    response["free_heap"] = ESP.getFreeHeap();
    response["free_psram"] = ESP.getFreePsram();
    response["gateway_enabled"] = gateway_.isEnabled();
    return LocalStatus::Handled;
  }

  if (name == "get_sensor_readings") {
    if (!unitPaHub_ || !unitPaHub_->isFound()) {
      response["ok"] = false;
      response["error"] = "sensor_bus_unavailable";
      return LocalStatus::Error;
    }
    uint16_t distanceMm = 0;
    UnitPaHubManager::EnvData env;
    bool ok = unitPaHub_->readSensors(distanceMm, env);
    response["ok"] = ok;
    response["distance_mm"] = distanceMm;
    response["temperature_c"] = env.temperature;
    response["humidity_pct"] = env.humidity;
    response["pressure_hpa"] = env.pressure;
    response["environment_valid"] = env.valid;
    return ok ? LocalStatus::Handled : LocalStatus::Error;
  }

  if (name == "get_current_datetime") {
    struct tm timeInfo;
    bool hasTime = getLocalTime(&timeInfo, 10);
    // If system time is not yet synced, try restoring from RTC if available
    if ((!hasTime || timeInfo.tm_year < (2024 - 1900)) && M5.Rtc.isEnabled()) {
      M5.Rtc.setSystemTimeFromRtc();
      hasTime = getLocalTime(&timeInfo, 10);
    }

    if (!hasTime || timeInfo.tm_year < (2024 - 1900)) {
      response["ok"] = false;
      response["error"] = "time_not_synchronized";
      response["message"] = "O relogio local ainda nao sincronizou via NTP/SNTP.";
      return LocalStatus::Handled;
    }

    // Sync RTC with valid UTC time for offline resilience
    if (M5.Rtc.isEnabled()) {
      time_t nowSec = time(nullptr);
      struct tm utcTm;
      if (gmtime_r(&nowSec, &utcTm)) {
        M5.Rtc.setDateTime(&utcTm);
      }
    }

    static const char* const weekdaysPt[] = {
      "domingo", "segunda-feira", "terça-feira", "quarta-feira",
      "quinta-feira", "sexta-feira", "sábado"
    };

    char dateBuf[16];
    char timeBuf[16];
    char timeExtensoBuf[32];
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday);
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
    snprintf(timeExtensoBuf, sizeof(timeExtensoBuf), "%02dh%02d", timeInfo.tm_hour, timeInfo.tm_min);

    int wday = timeInfo.tm_wday;
    const char* weekday = (wday >= 0 && wday <= 6) ? weekdaysPt[wday] : "desconhecido";

    int8_t tzOffset = configManager_ ? configManager_->config().timezoneOffset : -3;
    String tzName = configManager_ ? configManager_->config().timezoneName : "UTC-3";
    char offsetBuf[10];
    snprintf(offsetBuf, sizeof(offsetBuf), "%c%02d:00", (tzOffset >= 0 ? '+' : '-'), abs(tzOffset));

    response["ok"] = true;
    response["hora_local"] = timeBuf;
    response["hora_formatada"] = timeExtensoBuf;
    response["data_local"] = dateBuf;
    response["dia_da_semana"] = weekday;
    response["fuso_horario"] = tzName;
    // Legacy keys for compatibility
    response["date"] = dateBuf;
    response["time"] = timeBuf;
    response["weekday"] = weekday;
    response["timezone"] = tzName;
    response["utc_offset"] = offsetBuf;
    response["instrucao"] = "Estes valores JÁ SÃO a hora e data locais exatas da Pix. Diga este horário diretamente ao usuário sem converter ou recalcular fuso.";
    return LocalStatus::Handled;
  }

  if (name == "get_news") {
    Serial.printf("[DIAG:STACK] HWM_BEFORE_GET_NEWS=%u\n",
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    String category = String((const char*)(args["category"] | "geral"));
    category.toLowerCase();
    uint8_t limit = args["limit"] | 3;
    if (limit < 1) limit = 1;
    if (limit > 5) limit = 5;
    JsonArray headlines = response["headlines"].to<JsonArray>();

    bool found = false;
    bool stale = false;
    if (liveFeeds_) {
      LiveFeedsManager::LiveFeedsSnapshot snap = liveFeeds_->getSnapshot();
      if (snap.cachedNewsCount > 0) {
        for (uint8_t i = 0; i < snap.cachedNewsCount && headlines.size() < limit; ++i) {
          const auto& item = snap.cachedNews[i];
          if (category != "geral" && strcmp(item.category, category.c_str()) != 0) continue;
          auto entry = headlines.add<JsonObject>();
          entry["category"] = item.category;
          entry["title"] = item.title;
          if (item.summary[0] != '\0') entry["summary"] = item.summary;
          entry["link"] = item.link;
          found = true;
        }

        uint32_t ageMs = (snap.lastNewsUpdatedMs > 0) ? (millis() - snap.lastNewsUpdatedMs) : 0;
        if (ageMs > 25 * 60 * 1000UL) {
          stale = true;
        }
      }
    }

    if (found && headlines.size() > 0) {
      response["ok"] = true;
      response["category"] = category;
      if (stale) {
        response["source"] = "stale_cache";
        response["warning"] = "Noticias do cache anterior (atualizacao recente pendente ou em andamento).";
      } else {
        response["source"] = "cached_feed";
      }
    } else {
      response["ok"] = false;
      response["category"] = category;
      response["source"] = "cache_empty";
      response["error"] = "news_cache_empty_or_updating";
      response["message"] = "Noticias locais ainda nao carregadas em cache pelo background task.";
    }
    return LocalStatus::Handled;
  }

  if (name == "refresh_news_feeds") {
    if (liveFeeds_) {
      liveFeeds_->requestRefresh();
    }
    LiveFeedsManager::LiveFeedsSnapshot snap = liveFeeds_ ? liveFeeds_->getSnapshot() : LiveFeedsManager::LiveFeedsSnapshot{};
    bool cacheAvailable = (snap.cachedNewsCount > 0);
    uint32_t cacheAgeSec = (snap.lastNewsUpdatedMs > 0) ? ((millis() - snap.lastNewsUpdatedMs) / 1000UL) : 0;

    response["ok"] = true;
    response["message"] = "Atualizacao dos feeds em background enfileirada.";
    response["latest_headline"] = liveFeeds_ ? liveFeeds_->latestNews() : "";
    response["refresh_requested"] = true;
    response["cache_available"] = cacheAvailable;
    response["cache_age_s"] = cacheAgeSec;
    response["cached_items_count"] = snap.cachedNewsCount;
    response["message"] = "Uma atualizacao em segundo plano foi solicitada. O cache atual continua valido e disponivel; use get_news para consultar as noticias ja armazenadas.";
    response["instruction"] = "Se o usuario pedir uma noticia especifica que nao conste no cache retornado por get_news, informe apenas que ela nao consta no snapshot atual, sem afirmar que o sistema esta travado ou indisponivel.";
    return LocalStatus::Handled;
  }

  if (name == "get_daily_agenda") {
    Serial.printf("[DIAG:get_daily_agenda:pre] task='%s' stackHWM=%u freeHeap=%u minFreeHeap=%u maxAllocHeap=%u\n",
                  pcTaskGetName(nullptr),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));
    if (googleCalendar_) {
      String agendaJson = googleCalendar_->fetchDailyAgendaJson();
      deserializeJson(response, agendaJson);
    } else {
      response["ok"] = false;
      response["error"] = "calendar_client_not_attached";
      response["message"] = "Modulo de agenda nao conectado.";
    }
    return LocalStatus::Handled;
  }

  if (name == "set_speaker_volume") {
    int pct = args["volume_pct"] | 50;
    pct = constrain(pct, 0, 100);
    uint8_t rawVol = static_cast<uint8_t>((pct * 255) / 100);
    M5.Speaker.setVolume(rawVol);
    M5.Speaker.setAllChannelVolume(rawVol);
    if (gemini_) gemini_->setSpeakerVolume(rawVol);
    if (unitPaHub_) unitPaHub_->click();
    Serial.printf("Audio: speaker volume set to %d%% (raw=%u)\n", pct, rawVol);
    response["ok"] = true;
    response["volume_pct"] = pct;
    response["raw_volume"] = rawVol;
    response["message"] = "Speaker volume adjusted physically.";
    return LocalStatus::Handled;
  }

  if (name == "set_emotion") {
    const char* emotion = args["emotion"] | "neutral";
    emotion_.setEmotion(emotion);
    response["ok"] = true;
    response["emotion"] = emotion_.currentEmotion();
    return LocalStatus::Handled;
  }

  if (name == "search_memory") {
    const char* queryRaw = args["query"] | "";
    String query = String(queryRaw);
    String q = query;
    q.toLowerCase();
    response["ok"] = true;
    response["source"] = "local_active_dialogue_context_only";
    response["query"] = query;

    if (q.indexOf("pin") >= 0 || q.indexOf("\xD0\xBF\xD0\xB8\xD0\xBD") >= 0 ||
        q.indexOf("password") >= 0 || q.indexOf("\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB") >= 0 ||
        q.indexOf("token") >= 0 || q.indexOf("secret") >= 0 || q.indexOf("\xD1\x81\xD0\xB5\xD0\xBA\xD1\x80\xD0\xB5\xD1\x82") >= 0 ||
        q.indexOf("code") >= 0 || q.indexOf("\xD0\xBA\xD0\xBE\xD0\xB4") >= 0) {
      response["private_query"] = true;
      response["active_context"] = "";
      response["instruction"] = "search_memory never exposes private values. If and only if the user explicitly asks to recall a saved private code/PIN/password, call recall_private_memory locally instead.";
      return LocalStatus::Handled;
    }

    if (memory_) {
      String hits = memory_->searchMemory(query, 8, 9000);
      response["retrieval_json"] = hits;
      response["raw_dialogues_exposed"] = false;
      response["retrieval_layers"] = "active_context,facts,semantic_summaries";
      response["time_aware"] = true;
    } else {
      response["retrieval_json"] = "";
      response["warning"] = "memory_store_not_attached";
    }
    response["instruction"] = "Use the time-aware local retrieval_json. Prefer active/fresh results; if several dates/sessions are present, answer as separate episodes and do not merge them. Do not request raw archived dialogues or expose private values.";
    return LocalStatus::Handled;
  }

  if (name == "remember_fact") {
    if (!memory_) {
      response["ok"] = false;
      response["error"] = "memory_store_not_attached";
      return LocalStatus::Error;
    }
    String fact = String((const char*)(args["fact"] | ""));
    String key = String((const char*)(args["key"] | ""));
    String category = String((const char*)(args["category"] | "general"));
    uint8_t importance = (uint8_t)(args["importance"] | 2);

    // Check sensitive terms
    String fLower = fact;
    fLower.toLowerCase();
    if (fLower.indexOf("pin") >= 0 || fLower.indexOf("password") >= 0 ||
        fLower.indexOf("senha") >= 0 || fLower.indexOf("token") >= 0 ||
        fLower.indexOf("segredo") >= 0 || fLower.indexOf("secret") >= 0) {
      response["ok"] = false;
      response["error"] = "sensitive_content_rejected";
      response["instruction"] = "NUNCA salve senhas, PINs ou segredos com remember_fact. Se o usuario pediu explicitamente para guardar um segredo, use remember_private_memory.";
      return LocalStatus::Handled;
    }

    auto res = memory_->saveFact(fact, key, category, importance);
    response["ok"] = res.ok;
    response["stored"] = res.ok;
    response["already_known"] = res.alreadyKnown;
    response["updated"] = res.updated;
    response["importance"] = importance;
    if (res.key.length()) response["key"] = res.key;
    response["fact"] = res.fact;
    response["message"] = res.message;
    return res.ok ? LocalStatus::Handled : LocalStatus::Error;
  }

  if (name == "remember_private_memory") {
    if (!memory_) {
      response["ok"] = false;
      response["error"] = "memory_store_not_attached";
      return LocalStatus::Error;
    }
    String kind = String((const char*)(args["kind"] | "private_note"));
    String label = String((const char*)(args["label"] | "private memory"));
    String value = String((const char*)(args["value"] | ""));
    bool ok = memory_->rememberPrivateMemory(kind, label, value);
    response["ok"] = ok;
    response["stored"] = ok;
    response["kind"] = kind;
    response["label"] = label;
    response["recall_policy"] = "explicit_user_request_only_local";
    response["value_echoed"] = false;
    response["message"] = ok ? "Private memory stored locally on SD. Do not repeat the value unless the user explicitly asks to recall it." : "Private memory was not stored.";
    return ok ? LocalStatus::Handled : LocalStatus::Error;
  }

  if (name == "recall_private_memory") {
    if (!memory_) {
      response["ok"] = false;
      response["error"] = "memory_store_not_attached";
      return LocalStatus::Error;
    }
    String query = String((const char*)(args["query"] | ""));
    String kind, label, value, ts;
    bool found = memory_->recallPrivateMemory(query, kind, label, value, ts);
    response["ok"] = found;
    response["private_recall"] = true;
    response["query"] = query;
    if (found) {
      response["kind"] = kind;
      response["label"] = label;
      response["value"] = value;
      response["ts"] = ts;
      response["recall_policy"] = "explicit_user_request_only_local";
      response["external_forwarding_allowed"] = false;
    } else {
      response["error"] = "private_memory_not_found";
      response["message"] = "No matching local private memory was found.";
    }
    return found ? LocalStatus::Handled : LocalStatus::Error;
  }

  if (name == "servo_gesture") {
    const char* gesture = args["gesture"] | "";
    uint32_t now = millis();

    // Turn-based deduplication: suppress remote toolCall if identical local action was already executed in this turn
    uint32_t currentTurn = gemini_ ? gemini_->currentUserTurnId() : 0;
    if (pendingLocalActionValid_ &&
        String(gesture) == pendingLocalAction_ &&
        pendingLocalActionTurnId_ == currentTurn &&
        (now - pendingLocalActionMs_ <= 8000)) {
      Serial.println("[LOCAL_CMD] duplicate remote toolCall suppressed");
      pendingLocalActionValid_ = false;
      pendingLocalAction_ = "";
      pendingLocalActionTurnId_ = 0;
      pendingLocalActionMs_ = 0;

      response["ok"] = true;
      response["suppressed"] = true;
      response["action"] = "Comando local ja executado previamente.";
      if (strcmp(gesture, "look_left") == 0 || strcmp(gesture, "look_left_small") == 0) {
        response["suggested_reply"] = "Opa, olhei!; Pronto, olhei!; Prontinho, to olhando pra ca!; To aqui!";
      } else if (strcmp(gesture, "look_right") == 0 || strcmp(gesture, "look_right_small") == 0) {
        response["suggested_reply"] = "Pronto, olhei!; Olhando pra direita!; Prontinho!; Virei!";
      } else if (strcmp(gesture, "center_head") == 0 || strcmp(gesture, "look_at_user") == 0) {
        response["suggested_reply"] = "Prontinho, to aqui olhando pra voce!; Centralizada!";
      }
      return LocalStatus::Handled;
    }

    const char* hold = args["hold"] | "normal";
    bool queued = servoGestures_.queueGesture(String(gesture), String(hold));
    response["ok"] = queued;
    if (queued) {
      response["queued"] = gesture;
      response["hold"] = hold;
      response["active"] = servoGestures_.currentGesture();
      response["async"] = true;
      response["anchor_x"] = servoGestures_.anchorX();
      response["anchor_y"] = servoGestures_.anchorY();

      // Guide Gemini to vocalize a prompt natural response in Brazilian Portuguese
      if (strcmp(gesture, "look_left") == 0 || strcmp(gesture, "look_left_small") == 0) {
        response["action"] = (strcmp(hold, "sustain") == 0) ? "Cabeca virou e permanece olhando para a esquerda." :
                             (strcmp(hold, "glance") == 0)  ? "Cabeca deu uma olhada breve para a esquerda e retornou." :
                                                              "Cabeca virou para a esquerda, mantem observacao por ~6s e auto-centraliza.";
        response["suggested_reply"] = "Opa, olhei!; Pronto, olhei!; Prontinho, to olhando pra ca!; To aqui!";
      } else if (strcmp(gesture, "look_right") == 0 || strcmp(gesture, "look_right_small") == 0) {
        response["action"] = (strcmp(hold, "sustain") == 0) ? "Cabeca virou e permanece olhando para a direita." :
                             (strcmp(hold, "glance") == 0)  ? "Cabeca deu uma olhada breve para a direita e retornou." :
                                                              "Cabeca virou para a direita, mantem observacao por ~6s e auto-centraliza.";
        response["suggested_reply"] = "Pronto, olhei!; Olhando pra direita!; Prontinho!; Virei!";
      } else if (strcmp(gesture, "look_up") == 0) {
        response["action"] = "Cabeca olhou fisicamente para cima.";
        response["suggested_reply"] = "Olhando pra cima!; Pronto, olhei pro teto!; Prontinho!";
      } else if (strcmp(gesture, "look_down") == 0) {
        response["action"] = "Cabeca olhou fisicamente para baixo.";
        response["suggested_reply"] = "Olhando pra baixo!; De olho na mesa!; Prontinho!";
      } else if (strcmp(gesture, "center_head") == 0 || strcmp(gesture, "look_at_user") == 0) {
        response["action"] = "Cabeca centralizada olhando para o usuario.";
        response["suggested_reply"] = "Prontinho, to aqui olhando pra voce!; Centralizada!";
      } else if (strcmp(gesture, "nod_yes") == 0) {
        response["action"] = "Cabeca assentiu dizendo sim.";
        response["suggested_reply"] = "Sim!; Com certeza!; Concordo plenamente!";
      } else if (strcmp(gesture, "shake_no") == 0) {
        response["action"] = "Cabeca balancou dizendo nao.";
        response["suggested_reply"] = "Nao, nao!; De jeito nenhum!; Nem pensar!";
      }
    } else {
      response["error"] = "unknown_gesture";
      response["allowed"] = "center_head,look_at_user,look_left_small,look_right_small,look_left,look_right,look_up,look_down,look_top_left,look_top_right,search_left_wide,search_left,search_center,search_right,search_right_wide,nod_yes,shake_no,curious_tilt,speaking_micro_motion,stop_motion";
      return LocalStatus::Error;
    }
    return LocalStatus::Handled;
  }

  if (name == "head_motion") {
    auto clampInt = [](int v, int lo, int hi) -> int {
      if (v < lo) return lo;
      if (v > hi) return hi;
      return v;
    };
    ServoGestureController::Step steps[6]{};
    uint8_t count = 0;
    JsonArrayConst arr = args["steps"].as<JsonArrayConst>();
    for (JsonObjectConst s : arr) {
      if (count >= 6) break;
      float xDeg = s["x_deg"] | 0.0f;
      float yDeg = s["y_deg"] | 0.0f;
      int speed = s["speed"] | 500;
      int holdMs = s["hold_ms"] | 500;
      xDeg = constrain(xDeg, -65.0f, 65.0f);
      yDeg = constrain(yDeg, -6.0f, 40.0f);
      steps[count].x = clampInt(static_cast<int>(xDeg * 10.0f), -650, 650);
      steps[count].y = clampInt(static_cast<int>(yDeg * 10.0f), -60, 400);
      steps[count].speed = clampInt(speed, 100, 700);
      steps[count].holdMs = static_cast<uint16_t>(clampInt(holdMs, 100, 1500));
      steps[count].relative = true;
      count++;
    }
    if (count == 0) {
      response["ok"] = false;
      response["error"] = "missing_steps";
      response["message"] = "head_motion requires 1..6 steps with x_deg/y_deg/speed/hold_ms";
      return LocalStatus::Error;
    }
    String motionName = String((const char*)(args["motion_name"] | "custom_motion"));
    bool queued = servoGestures_.queueSteps(motionName, steps, count);
    response["ok"] = queued;
    response["queued"] = motionName;
    response["async"] = true;
    response["step_count"] = count;
    response["clamped"] = true;
    response["anchor_x"] = servoGestures_.anchorX();
    response["anchor_y"] = servoGestures_.anchorY();
    response["coordinate_note"] = "x_deg negative=robot left, positive=robot right; y_deg positive=up, negative=down; values are deltas from the current look anchor; final keyframe becomes the new anchor";
    response["action"] = "Movimento de cabeca executado com sucesso.";
    response["suggested_reply"] = "Pronto, olhei!; Prontinho!; Opa, olhei!; To aqui!";
    return queued ? LocalStatus::Handled : LocalStatus::Error;
  }

  if (name == "end_session") {
    const char* reason = args["reason"] | "user_requested";
    int dndMin = args["dnd_minutes"] | 0;
    if (dndMin > 0 && gemini_) {
      gemini_->setDoNotDisturb(static_cast<uint16_t>(dndMin));
    }
    // Sleep/rest semantics: explicitly return the physical head home and reset
    // the persistent look anchor only when the session is ending.
    servoGestures_.queueGesture("center_head");
    if (gemini_) {
      gemini_->requestSessionEnd(reason);
    }
    response["ok"] = true;
    response["ending"] = true;
    response["reason"] = reason;
    response["dnd_minutes"] = dndMin;
    response["message"] = "Conversation will end after the final response. Going to sleep.";
    return LocalStatus::Handled;
  }

  if (name == "look_with_camera") {
    const char* question = args["question"] | "What is visible in this snapshot?";
    emotion_.setEmotion("looking");
    if (!gemini_ || !gemini_->isReady()) {
      response["ok"] = false;
      response["error"] = "gemini_not_ready";
      response["message"] = "Cannot send camera image because Gemini Live is not ready.";
      return LocalStatus::Error;
    }
    String imageBase64;
    if (!camera_.captureJpegBase64(imageBase64)) {
      response["ok"] = false;
      response["error"] = camera_.lastError();
      response["message"] = "Camera capture failed.";
      response["camera_enabled"] = camera_.enabled();
      return LocalStatus::Error;
    }
    String prompt = "This is one frame from my StackChan camera. Answer concisely and practically in the user's language. User question: ";
    prompt += question;
    bool sent = gemini_->sendImageTurn(imageBase64, prompt);
    // The bright white looking LEDs are useful only while the camera is actively
    // capturing/sending. Once the still image is handed to Gemini Live, switch
    // to thinking so the white assist light cannot stick through the later
    // audio response or overload the small robot power/speaker path.
    emotion_.setEmotion(sent ? "thinking" : "error");
    response["ok"] = sent;
    response["image_sent_to_current_gemini_session"] = sent;
    response["jpeg_bytes"] = camera_.lastJpegBytes();
    response["base64_bytes"] = camera_.lastBase64Bytes();
    response["message"] = sent ? "Snapshot was sent to the current Gemini Live conversation for visual analysis." : "Failed to send snapshot into Gemini Live.";
    return sent ? LocalStatus::Handled : LocalStatus::Error;
  }

  return LocalStatus::NotHandled;
}

String GeminiToolBridge::errorJson(const String& code, const String& message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = code;
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  return out;
}
