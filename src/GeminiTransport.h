#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/ringbuf.h>
#include <WebSocketsClient.h>

enum class TransportCmdType : uint8_t {
  CONNECT,
  DISCONNECT,
  SHUTDOWN
};

struct TransportCmd {
  TransportCmdType type;
  uint32_t epoch;
};

enum class TransportState : uint8_t {
  DOWN,
  CONNECTING,
  OPEN,
  BACKOFF
};

enum class InboundEventType : uint8_t {
  CONNECTED,
  DISCONNECTED,
  TEXT_FRAME,
  BINARY_FRAME,
  ERROR_FRAME,
  PONG_FRAME
};

struct InboundEventHeader {
  InboundEventType type;
  uint32_t epoch;
  size_t length;
};

enum class OutboundKind : uint8_t {
  CTRL,
  AUDIO,
  LARGE
};

struct OutboundHeader {
  OutboundKind kind;
  uint32_t epoch;
  size_t length;
};

class GeminiTransport {
public:
  static constexpr size_t INBOUND_BUFFER_BYTES = 512 * 1024;
  static constexpr size_t OUTBOUND_BUFFER_BYTES = 512 * 1024;
  static constexpr size_t CMD_QUEUE_DEPTH = 8;
  static constexpr size_t TASK_STACK_SIZE = 16384;
  static constexpr UBaseType_t TASK_PRIORITY = 2;
  static constexpr BaseType_t TASK_CORE = 0;
  static constexpr uint32_t RETRY_BACKOFF_MS = 5000;

  GeminiTransport();
  ~GeminiTransport();

  bool begin(const String& apiKey);
  bool startTask();
  void stopTask();

  // Core 1 non-blocking APIs
  bool requestConnect(uint32_t epoch);
  bool requestDisconnect(uint32_t epoch);
  bool enqueue(OutboundKind kind, const char* data, size_t len, uint32_t epoch);

  // Inbound retrieval for Core 1 (non-blocking)
  void* receiveInbound(size_t* item_size);
  void returnInboundItem(void* item);

  bool isConnected() const { return is_connected_; }
  TransportState getState() const { return state_; }

  // Diagnostics
  size_t getInboundMaxItemSize() const { return inbound_max_item_size_; }
  size_t getOutboundMaxItemSize() const { return outbound_max_item_size_; }
  uint32_t getRxDropped() const { return rx_dropped_; }
  uint32_t getMicTxDropped() const { return mic_tx_dropped_; }
  uint32_t getCtrlFail() const { return ctrl_fail_; }
  size_t getRxHwm() const { return rx_hwm_; }
  size_t getTxHwm() const { return tx_hwm_; }
  UBaseType_t getTaskStackHwm() const;

private:
  static void taskEntry(void* arg);
  void taskLoop();

  static void wsEventStatic(WStype_t type, uint8_t* payload, size_t length);
  void handleWsEvent(WStype_t type, uint8_t* payload, size_t length);

  void handleCmd(const TransportCmd& cmd);
  void startConnectAttempt();
  void drainOutbound();

  WebSocketsClient ws_; // Owned EXCLUSIVELY by Core 0 task
  String api_key_;

  TaskHandle_t task_handle_ = nullptr;
  QueueHandle_t cmd_queue_ = nullptr;

  RingbufHandle_t inbound_ring_ = nullptr;
  uint8_t* inbound_storage_ = nullptr;
  StaticRingbuffer_t* inbound_struct_ = nullptr;

  RingbufHandle_t outbound_ring_ = nullptr;
  uint8_t* outbound_storage_ = nullptr;
  StaticRingbuffer_t* outbound_struct_ = nullptr;

  size_t inbound_max_item_size_ = 0;
  size_t outbound_max_item_size_ = 0;

  volatile bool running_ = false;
  volatile bool desired_connected_ = false;
  volatile bool is_connected_ = false;
  bool connection_was_open_ = false;
  TransportState state_ = TransportState::DOWN;
  uint32_t current_epoch_ = 0;
  uint32_t next_retry_ms_ = 0;
  uint32_t retry_count_ = 0;
  bool in_tls_cleanup_ = false;
  bool has_initialized_ = false;
  volatile size_t outbound_audio_bytes_queued_ = 0;

  // Diagnostics counters
  volatile uint32_t rx_dropped_ = 0;
  volatile uint32_t mic_tx_dropped_ = 0;
  volatile uint32_t ctrl_fail_ = 0;
  volatile size_t rx_hwm_ = 0;
  volatile size_t tx_hwm_ = 0;

  static GeminiTransport* instance_;
};

