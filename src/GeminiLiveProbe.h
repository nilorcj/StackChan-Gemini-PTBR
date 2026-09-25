#pragma once
#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <M5StackChan.h>
#include "GeminiToolBridge.h"
#include "EmotionController.h"

class MemoryStore;

// Compile-only Gemini Live skeleton for StackChan-BSP-based firmware.
// Secrets are intentionally not embedded here. Tomorrow this should load
// Wi-Fi/API key/system prompt from SD/NVS, with redacted serial logs.
class GeminiLiveProbe {
 public:
  static constexpr int AUDIO_RING_BUFFERS = 128;
  static constexpr size_t AUDIO_BUFFER_BYTES = 8 * 1024;   // Sub-block size for the ring (8 KB = ~170 ms @ 24kHz/16-bit)
  static constexpr size_t DECODE_BUFFER_BYTES = 64 * 1024; // Temporary decode buffer – must hold largest API chunk safely
  static constexpr size_t PREBUFFER_BYTES = 32768;         // 32 KB real PCM bytes (~682 ms @ 24kHz/16-bit mono)
  static constexpr uint8_t PREBUFFER_CHUNKS = 4;  // 4 sub-blocks × 170 ms ≈ 682 ms jitter absorption

  ~GeminiLiveProbe();
  bool begin(const char* api_key);
  bool connect();
  void disconnect(bool intentional = true, const char* finalEmotion = nullptr);
  void setToolBridge(GeminiToolBridge* bridge) { tool_bridge_ = bridge; }
  void setEmotionController(EmotionController* emotion) { emotion_ = emotion; }
  void setMemoryStore(MemoryStore* memory) { memory_ = memory; }
  void setSpeakerVolume(uint8_t volume) { speaker_volume_ = volume; }
  void setVadConfig(uint16_t prefixPaddingMs, uint16_t silenceDurationMs,
                    bool startSensitivityHigh, bool endSensitivityLow,
                    bool turnIncludesAllInput);
  uint16_t vadPrefixPaddingMs() const { return vad_prefix_padding_ms_; }
  uint16_t vadSilenceDurationMs() const { return vad_silence_duration_ms_; }
  bool vadStartSensitivityHigh() const { return vad_start_sensitivity_high_; }
  bool vadEndSensitivityLow() const { return vad_end_sensitivity_low_; }
  bool vadTurnIncludesAllInput() const { return vad_turn_includes_all_input_; }
  void setModel(const String& model) { if (model.length()) model_ = model; }
  void setVoiceName(const String& voiceName) { if (voiceName.length()) voice_name_ = voiceName; }
  void setSystemPrompt(const String& systemPrompt) { system_prompt_ = systemPrompt; system_prompt_.trim(); }
  void setPersonaPrompt(const String& personaPrompt) { persona_prompt_ = personaPrompt; persona_prompt_.trim(); }
  void loop();
  void sendSetup();
  void sendTextTurn(const String& text);
  bool requestTextTurn(const String& text);
  bool sendImageTurn(const String& imageBase64, const String& prompt);
  void streamAudioDeltaBase64(const char* b64, size_t b64Len = 0);
  void streamAudioDeltaBase64(const String& b64) { streamAudioDeltaBase64(b64.c_str(), b64.length()); }
  bool isReady() const { return connected_ && setup_complete_; }
  bool isConnected() const { return connected_; }
  bool isRecording() const { return realtime_recording_; }
  bool isSpeaking() const { return speaking_ || audio_buffered_bytes_ > 0 || (M5.Speaker.isRunning() && M5.Speaker.isPlaying(1)); }
  bool streamAborted() const { return stream_aborted_; }
  bool playbackGateOpen() const { return playback_gate_open_; }
  size_t audioBufferedBytes() const { return audio_buffered_bytes_; }
  bool isInPostSpeechDebounce() const {
    return speech_finished_ms_ > 0 && (millis() - speech_finished_ms_ < 400);
  }
  bool isTurnBusy() const {
    return isSpeaking() ||
           pending_text_turn_ ||
           turn_state_ == LiveTurnState::WAITING_MODEL ||
           turn_state_ == LiveTurnState::TOOL_CALL ||
           turn_state_ == LiveTurnState::SPEAKING ||
           turn_state_ == LiveTurnState::RETURNING_TO_LISTEN ||
           isInPostSpeechDebounce();
  }
  bool continuousConversation() const { return continuous_conversation_; }
  bool isMicReadyForSpeech() const { return mic_ready_for_speech_; }
  uint16_t lastMicRms() const { return last_mic_rms_; }
  uint16_t lastMicPeak() const { return last_mic_peak_; }
  uint32_t micChunksSent() const { return mic_chunks_sent_; }
  uint32_t micRecordFalseCount() const { return mic_record_false_count_; }
  uint32_t audioChunksPlayed() const { return audio_chunks_; }
  uint32_t audioChunksDropped() const { return audio_dropped_; }
  uint32_t audioBackpressureWaitMs() const { return audio_backpressure_wait_ms_; }
  uint16_t maxAudioBufferedCount() const { return max_audio_buffered_; }
  uint32_t maxLoopDurationMs() const { return max_loop_duration_ms_; }
  // Hybrid audio architecture metrics (API-level vs ring-level)
  uint32_t apiAudioChunksReceived() const { return api_audio_chunks_received_; }
  uint32_t apiAudioChunksDropped() const { return api_audio_chunks_dropped_; }
  uint32_t ringBlocksEnqueued() const { return ring_blocks_enqueued_; }
  uint32_t ringBlocksDropped() const { return ring_blocks_dropped_; }
  enum class I2SOwner : uint8_t {
    NONE = 0,
    MIC_OWNER,
    SPEAKER_OWNER
  };

  I2SOwner i2sOwner() const { return i2s_owner_; }
  const char* i2sOwnerString() const {
    switch (i2s_owner_) {
      case I2SOwner::MIC_OWNER: return "MIC_OWNER";
      case I2SOwner::SPEAKER_OWNER: return "SPEAKER_OWNER";
      default: return "NONE";
    }
  }

  bool acquireI2sSpeaker(const char* reason = nullptr);
  bool acquireI2sMic(const char* reason = nullptr);

  enum class LiveTurnState : uint8_t {
    IDLE = 0,
    LISTENING,
    WAITING_MODEL,
    TOOL_CALL,
    SPEAKING,
    RETURNING_TO_LISTEN
  };

  LiveTurnState turnState() const { return turn_state_; }
  const char* turnStateString() const {
    switch (turn_state_) {
      case LiveTurnState::IDLE: return "IDLE";
      case LiveTurnState::LISTENING: return "LISTENING";
      case LiveTurnState::WAITING_MODEL: return "WAITING_MODEL";
      case LiveTurnState::TOOL_CALL: return "TOOL_CALL";
      case LiveTurnState::SPEAKING: return "SPEAKING";
      case LiveTurnState::RETURNING_TO_LISTEN: return "RETURNING_TO_LISTEN";
      default: return "UNKNOWN";
    }
  }

  void setContinuousConversation(bool enabled) { continuous_conversation_ = enabled; }
  bool requestConversationStart();
  void stopConversation();
  bool armConversation(const char* source = nullptr);
  void disarmConversation(const char* reason = nullptr);
  bool isConversationArmed() const { return conversation_armed_; }
  uint32_t audioSentWhileDisarmed() const { return audio_sent_while_disarmed_; }
  uint32_t armCountGesture() const { return arm_count_gesture_; }
  uint32_t armCountTouch() const { return arm_count_touch_; }
  uint32_t disarmCountTimeout() const { return disarm_count_timeout_; }
  uint32_t rmsCrossUpCount() const { return rms_cross_up_count_; }
  uint32_t rmsCrossDownCount() const { return rms_cross_down_count_; }
  uint32_t gateForceCloseCount() const { return gate_force_close_count_; }
  float noiseFloorRms() const { return noise_floor_rms_; }
  void requestSessionEnd(const String& reason = "");
  void startRealtimeRecord();
  void stopRealtimeRecord();
  void toggleRealtimeRecord();

  bool isDoNotDisturb() const { return dnd_until_ms_ > 0 && millis() < dnd_until_ms_; }
  uint32_t dndRemainingSeconds() const {
    uint32_t now = millis();
    return (dnd_until_ms_ > now) ? (dnd_until_ms_ - now) / 1000 : 0;
  }
  void setDoNotDisturb(uint16_t minutes) {
    dnd_until_ms_ = millis() + (static_cast<uint32_t>(minutes) * 60000UL);
    Serial.printf("DND: activated for %u min (until %lu)\n", minutes, static_cast<unsigned long>(dnd_until_ms_));
  }
  void clearDoNotDisturb() {
    if (physical_mute_) {
      Serial.println("DND: clear ignored because physical mute is active");
      return;
    }
    dnd_until_ms_ = 0;
    Serial.println("DND: cleared");
  }

  void setPhysicalMute(bool muted) {
    physical_mute_ = muted;
    if (muted) {
      dnd_until_ms_ = millis() + (1440UL * 60000UL);
    }
  }
  bool isPhysicalMute() const { return physical_mute_; }

  uint32_t currentUserTurnId() const { return currentUserTurnId_; }
  void notifyMicGainChanged(uint8_t oldGain, uint8_t newGain);

 private:
  static constexpr int RT_REC_SAMPLE_RATE = 16000;
  static constexpr int RT_REC_SAMPLES = 1000;  // 62.5ms chunks keep mic_task responsive.
  static constexpr uint32_t REALTIME_RECORD_TIMEOUT_MS = 30000;
  static constexpr uint32_t IDLE_DISCONNECT_MS = 120000;
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
  static constexpr uint32_t CONTINUOUS_CONVERSATION_TIMEOUT_MS = 90000; // 90s silence timeout: comfortable conversational pacing, allows natural pauses to think
  static constexpr uint32_t CONTINUOUS_RECORD_WATCHDOG_MS = 2500;
  static constexpr uint32_t END_SESSION_GRACE_MS = 8000;
  static constexpr uint32_t ARMED_INACTIVITY_TIMEOUT_MS = 15000;
  static constexpr uint32_t WAITING_MODEL_GUARD_MS = 6000;
  static constexpr uint32_t GATE_MAX_OPEN_MS = 12000; // 12s anti-latch failsafe
  static constexpr uint16_t kGateOpenDeltaRms = 300;
  static constexpr uint16_t kGateCloseDeltaRms = 150;
  static constexpr uint8_t kNoiseGateHangoverChunks = 4; // ~250ms natural speech tail
  static constexpr uint8_t kPostSpeechSilenceChunks = 5; // ~312ms explicit digital silence to commit turn instantly

  WebSocketsClient ws_;
  LiveTurnState turn_state_ = LiveTurnState::IDLE;
  I2SOwner i2s_owner_ = I2SOwner::NONE;
  void setTurnState(LiveTurnState newState, const char* reason = nullptr);
  uint8_t* audio_buf_[AUDIO_RING_BUFFERS] = {nullptr};
  size_t audio_buf_len_[AUDIO_RING_BUFFERS] = {0};
  uint8_t* decode_buf_ = nullptr;  // 64 KB PSRAM-only decode scratch (allocated once in begin())
  volatile int audio_queue_write_ = 0;
  volatile int audio_queue_read_ = 0;
  volatile int audio_buffered_count_ = 0;
  volatile size_t audio_buffered_bytes_ = 0;
  bool playback_gate_open_ = false;
  // Hybrid audio architecture metrics
  uint32_t api_audio_chunks_received_ = 0;  // API-level chunks received (each WebSocket audio message = 1)
  uint32_t api_audio_chunks_dropped_ = 0;   // API-level chunks dropped (oversize or no ring space)
  uint32_t ring_blocks_enqueued_ = 0;       // Sub-blocks written to the ring across all chunks
  uint32_t ring_blocks_dropped_ = 0;        // Sub-blocks that couldn't be written (ring atomically full)
  bool response_complete_received_ = false;
  uint32_t last_audio_chunk_ms_ = 0;
  uint32_t last_response_complete_ms_ = 0;
  uint32_t speaking_started_ms_ = 0;
  uint32_t speech_finished_ms_ = 0;
  int16_t* rec_buf_ = nullptr;
  int next_audio_buf_ = 0;
  bool connected_ = false;
  bool setup_complete_ = false;
  bool realtime_recording_ = false;
  bool conversation_armed_ = false;
  uint32_t armed_timeout_deadline_ms_ = 0;
  uint32_t waiting_model_started_ms_ = 0;
  bool waiting_model_aborted_turn_ = false;
  uint32_t audio_sent_while_disarmed_ = 0;
  uint32_t arm_count_gesture_ = 0;
  uint32_t arm_count_touch_ = 0;
  uint32_t disarm_count_timeout_ = 0;
  uint32_t rms_cross_up_count_ = 0;
  uint32_t rms_cross_down_count_ = 0;
  uint32_t gate_force_close_count_ = 0;
  float noise_floor_rms_ = 180.0f; // Baseline noise floor at gain=8
  bool speaking_ = false;
  bool stream_aborted_ = false;
  uint32_t stream_aborted_ms_ = 0;
  bool continuous_conversation_ = false;
  bool configured_ = false;
  bool connect_requested_ = false;
  bool reconnect_requested_ = false;
  bool intentional_disconnect_ = false;
  bool pending_start_recording_ = false;
  bool pending_text_turn_ = false;
  bool resume_conversation_after_text_ = false;
  bool mic_ready_for_speech_ = false;
  bool mic_dma_in_progress_ = false;
  bool end_session_requested_ = false;
  bool physical_mute_ = false;
  uint32_t record_started_ms_ = 0;
  uint32_t last_activity_ms_ = 0;
  uint32_t connect_started_ms_ = 0;
  uint32_t stopped_recording_ms_ = 0;
  uint32_t end_session_requested_ms_ = 0;
  uint32_t dnd_until_ms_ = 0;
  uint32_t audio_chunks_ = 0;
  uint32_t audio_dropped_ = 0;
  uint16_t max_audio_buffered_ = 0;
  uint16_t min_audio_buffered_during_play_ = 0xFFFF;
  uint32_t playback_started_ms_ = 0;
  uint16_t prebuffer_actual_chunks_ = 0;
  uint32_t underrun_count_ = 0;
  uint32_t controlled_rebuffer_count_ = 0;
  uint32_t rebuffer_wait_count_ = 0;
  uint32_t tool_call_gap_count_ = 0;
  uint32_t max_loop_duration_ms_ = 0;
  uint32_t last_ws_loop_ms_ = 0;
  uint32_t max_ws_loop_gap_ms_ = 0;
  bool tool_call_in_flight_ = false;
  uint8_t min_dma_depth_during_play_ = 255;
  uint32_t audio_backpressure_wait_ms_ = 0;
  uint16_t last_mic_rms_ = 0;
  uint16_t last_mic_peak_ = 0;
  uint8_t noise_gate_hangover_ = 0;
  uint8_t post_speech_silence_countdown_ = 0;
  uint32_t mic_chunks_sent_ = 0;
  uint32_t mic_record_false_count_ = 0;
  uint32_t last_mic_chunk_end_ms_ = 0;
  uint32_t last_mic_record_launch_ms_ = 0;
  uint32_t total_mic_chunks_captured_ = 0;
  uint32_t listening_started_ms_ = 0;
  uint32_t last_mic_diag_ms_ = 0;
  // VAD and Noise Gate instrumentation (Phase 1)
  bool local_gate_open_ = false;
  bool local_gate_was_triggered_ = false;
  bool gate_cross_down_logged_ = false;
  bool speech_start_seen_ = false;
  bool speech_end_seen_ = false;
  uint32_t gate_open_started_ms_ = 0;
  uint32_t gate_real_chunks_sent_ = 0;
  uint32_t gate_silence_chunks_injected_ = 0;
  uint32_t ambient_window_start_ms_ = 0;
  uint8_t ambient_rms_count_ = 0;
  uint16_t ambient_peak_max_ = 0;
  // Dynamic audio stats and clipping tracking (Phase 3)
  uint32_t gain_settle_until_ms_ = 0;
  uint32_t audio_stats_window_start_ms_ = 0;
  uint16_t audio_stats_rms_buf_[32] = {0};
  uint8_t audio_stats_rms_count_ = 0;
  uint32_t audio_stats_sum_rms_ = 0;
  uint16_t audio_stats_rms_min_ = 65535;
  uint16_t audio_stats_rms_max_ = 0;
  uint16_t audio_stats_peak_max_ = 0;
  uint32_t audio_stats_clip_samples_ = 0;
  uint32_t audio_stats_total_samples_ = 0;
  uint8_t speaker_volume_ = 200;
  uint16_t vad_prefix_padding_ms_ = 200;
  uint16_t vad_silence_duration_ms_ = 280;
  bool vad_start_sensitivity_high_ = false; // START_SENSITIVITY_LOW (Consolidated post-audio)
  bool vad_end_sensitivity_low_ = false; // Fast immediate cut-off
  bool vad_turn_includes_all_input_ = true;
  String api_key_storage_;
  String model_ = "models/gemini-3.1-flash-live-preview";
  String voice_name_ = "Aoede";
  String system_prompt_;
  String persona_prompt_;
  String intentional_disconnect_emotion_ = "neutral";
  String pending_text_;
  String transcript_output_buffer_;
  String session_resumption_handle_;
  const char* api_key_ = nullptr;
  uint32_t currentUserTurnId_ = 0;
  bool utterance_input_received_ = false;
  GeminiToolBridge* tool_bridge_ = nullptr;
  EmotionController* emotion_ = nullptr;
  MemoryStore* memory_ = nullptr;

  static GeminiLiveProbe* self_;
  static void wsEvent(WStype_t type, uint8_t* payload, size_t length);
  void handleMessage(uint8_t* payload, size_t length);
  void handleTranscription(JsonVariant serverContent);
  void appendOutputTranscriptChunk(const char* text);
  void flushOutputTranscript();
  void recordAndSendAudioChunk();
  void playListeningChirp();
  void sendRealtimeAudioBase64(const String& b64);
  void cancelWaitingModelGuard(const char* reason = nullptr);
  void checkWaitingModelGuard();
  void recoverFromWaitingModel();
  String encodeBase64(const uint8_t* input, size_t size);
  int decodeBase64(const char* input, int size, char* output);
};
