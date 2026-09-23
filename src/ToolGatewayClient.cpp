#include "ToolGatewayClient.h"

bool ToolGatewayClient::begin(const Config& config) {
  config_ = config;
  if (config_.baseUrl.endsWith("/")) {
    config_.baseUrl.remove(config_.baseUrl.length() - 1);
  }
  Serial.printf("ToolGateway: %s base=%s\n", config_.enabled ? "enabled" : "disabled", config_.baseUrl.c_str());
  return true;
}

ToolGatewayClient::CallResult ToolGatewayClient::fetchTools(String* toolsJsonOut) {
  if (!config_.enabled) {
    CallResult r;
    r.ok = true;
    r.httpCode = 0;
    r.body = defaultToolDeclarationsJson();
    if (toolsJsonOut) *toolsJsonOut = r.body;
    return r;
  }

  CallResult r = getJson(joinUrl("/tools"));
  if (r.ok && toolsJsonOut) *toolsJsonOut = r.body;
  return r;
}

ToolGatewayClient::CallResult ToolGatewayClient::callTool(
    const String& toolName, const JsonVariantConst& arguments, const String& sessionId) {
  JsonDocument doc;
  doc["robot_id"] = config_.robotId;
  if (sessionId.length() > 0) doc["session_id"] = sessionId;
  doc["tool"] = toolName;
  doc["arguments"].set(arguments);

  String payload;
  serializeJson(doc, payload);

  if (!config_.enabled) {
    CallResult r;
    r.ok = false;
    r.httpCode = 0;
    r.error = "tool_gateway_disabled";
    r.body = "{\"error\":\"tool_gateway_disabled\"}";
    Serial.printf("ToolGateway disabled call tool=%s\n", toolName.c_str());
    return r;
  }

  return postJson(joinUrl("/call"), payload);
}

String ToolGatewayClient::defaultToolDeclarationsJson() const {
  JsonDocument doc;
  auto arr = doc.to<JsonArray>();

  auto searchMemory = arr.add<JsonObject>();
  searchMemory["name"] = "search_memory";
  searchMemory["description"] = "Search StackChan/Hermes long-term memory for relevant snippets.";
  auto smParams = searchMemory["parameters"].to<JsonObject>();
  smParams["type"] = "object";
  auto smProps = smParams["properties"].to<JsonObject>();
  smProps["query"]["type"] = "string";
  smProps["query"]["description"] = "Memory search query.";
  smParams["required"].add("query");

  auto askHermes = arr.add<JsonObject>();
  askHermes["name"] = "ask_hermes";
  askHermes["description"] = "Ask Hermes Agent on the home server to perform a complex task or use its tools.";
  auto ahParams = askHermes["parameters"].to<JsonObject>();
  ahParams["type"] = "object";
  auto ahProps = ahParams["properties"].to<JsonObject>();
  ahProps["question"]["type"] = "string";
  ahProps["question"]["description"] = "Question or task for Hermes.";
  ahParams["required"].add("question");

  auto robotStatus = arr.add<JsonObject>();
  robotStatus["name"] = "get_robot_status";
  robotStatus["description"] = "Get StackChan robot status: battery, network, state, and local memory health.";
  auto rsParams = robotStatus["parameters"].to<JsonObject>();
  rsParams["type"] = "object";
  rsParams["properties"].to<JsonObject>();

  auto setEmotion = arr.add<JsonObject>();
  setEmotion["name"] = "set_emotion";
  setEmotion["description"] = "Set StackChan face/emotion state.";
  auto seParams = setEmotion["parameters"].to<JsonObject>();
  seParams["type"] = "object";
  auto seProps = seParams["properties"].to<JsonObject>();
  seProps["emotion"]["type"] = "string";
  seProps["emotion"]["description"] = "One of neutral,happy,thinking,speaking,found,error,sleep.";
  seParams["required"].add("emotion");

  String out;
  serializeJson(arr, out);
  return out;
}

String ToolGatewayClient::joinUrl(const char* suffix) const {
  return config_.baseUrl + String(suffix);
}

ToolGatewayClient::CallResult ToolGatewayClient::getJson(const String& url) {
  CallResult result;
  HTTPClient http;
  WiFiClient client;
  http.setTimeout(config_.timeoutMs);
  if (!http.begin(client, url)) {
    result.error = "http_begin_failed";
    return result;
  }
  int code = http.GET();
  result.httpCode = code;
  if (code > 0) {
    result.body = http.getString();
    if (result.body.length() > config_.maxResponseBytes) {
      result.body = result.body.substring(0, config_.maxResponseBytes);
      result.error = "response_truncated";
    }
    result.ok = (code >= 200 && code < 300);
  } else {
    result.error = String("http_get_failed:") + String(code);
  }
  http.end();
  if (!result.ok) Serial.printf("ToolGateway GET failed code=%d err=%s\n", code, sanitizeForLog(result.error).c_str());
  return result;
}

ToolGatewayClient::CallResult ToolGatewayClient::postJson(const String& url, const String& payload) {
  CallResult result;
  HTTPClient http;
  WiFiClient client;
  http.setTimeout(config_.timeoutMs);
  if (!http.begin(client, url)) {
    result.error = "http_begin_failed";
    return result;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  result.httpCode = code;
  if (code > 0) {
    result.body = http.getString();
    if (result.body.length() > config_.maxResponseBytes) {
      result.body = result.body.substring(0, config_.maxResponseBytes);
      result.error = "response_truncated";
    }
    result.ok = (code >= 200 && code < 300);
  } else {
    result.error = String("http_post_failed:") + String(code);
  }
  http.end();
  if (!result.ok) Serial.printf("ToolGateway POST failed code=%d err=%s\n", code, sanitizeForLog(result.error).c_str());
  return result;
}

String ToolGatewayClient::sanitizeForLog(const String& input) const {
  String out = input;
  out.replace("Bearer ", "Bearer [REDACTED]");
  out.replace("api_key", "api_key[REDACTED]");
  out.replace("password", "password[REDACTED]");
  out.replace("token", "token[REDACTED]");
  return out;
}
