#include "GeminiLiveProbe.h"
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include "libb64/cdecode.h"
#include "MemoryStore.h"

GeminiLiveProbe* GeminiLiveProbe::self_ = nullptr;

void GeminiLiveProbe::setTurnState(LiveTurnState newState, const char* reason) {
  if (turn_state_ == newState) return;
  LiveTurnState oldState = turn_state_;
  turn_state_ = newState;

  if (newState == LiveTurnState::WAITING_MODEL) {
    waiting_model_started_ms_ = millis();
  } else {
    waiting_model_started_ms_ = 0;
  }

  if (turn_state_ == LiveTurnState::LISTENING && !realtime_recording_) {
    stopped_recording_ms_ = millis(); // Reset reference so watchdog only measures time spent within LISTENING
  }
  if (newState == LiveTurnState::LISTENING && conversation_armed_) {
    armed_timeout_deadline_ms_ = millis() + ARMED_INACTIVITY_TIMEOUT_MS;
    Serial.printf("GeminiLive: returned to LISTENING; armed inactivity timeout reset (%lu s)\n",
                  static_cast<unsigned long>(ARMED_INACTIVITY_TIMEOUT_MS / 1000));
  }
  Serial.printf("TURN_STATE=%s (was %s, reason: %s)\n",
                turnStateString(),
                oldState == LiveTurnState::IDLE ? "IDLE" :
                oldState == LiveTurnState::LISTENING ? "LISTENING" :
                oldState == LiveTurnState::WAITING_MODEL ? "WAITING_MODEL" :
                oldState == LiveTurnState::TOOL_CALL ? "TOOL_CALL" :
                oldState == LiveTurnState::SPEAKING ? "SPEAKING" :
                oldState == LiveTurnState::RETURNING_TO_LISTEN ? "RETURNING_TO_LISTEN" : "UNKNOWN",
                reason ? reason : "transition");

  if (newState == LiveTurnState::WAITING_MODEL) {
    const char* origin = "OTHER";
    if (reason && strcmp(reason, "user speech finished, silence committed") == 0) {
      origin = "LOCAL_NOISE_GATE_COMPLETION";
    } else if (reason && strcmp(reason, "modelTurn / outputTranscription detected") == 0) {
      origin = (!local_gate_was_triggered_) ? "SPONTANEOUS_TURN_WITHOUT_LOCAL_GATE" : "CLOUD_VAD_EVENT";
    } else if (reason && strcmp(reason, "turn finished; waiting for reconnect/setup") == 0) {
      origin = "RECONNECT_SESSION_RESUME";
    } else if (reason && strcmp(reason, "text turn submitted") == 0) {
      origin = "EXPLICIT_USER_TEXT";
    }
    Serial.printf("[DIAG:TURN_ORIGIN] ts=%lu turnId=%lu origin=%s reason=\"%s\" gate_open=%d gate_triggered=%d speech_start_seen=%d speech_end_seen=%d rms=%u peak=%u\n",
                  millis(),
                  static_cast<unsigned long>(currentUserTurnId_),
                  origin,
                  reason ? reason : "",
                  local_gate_open_ ? 1 : 0,
                  local_gate_was_triggered_ ? 1 : 0,
                  speech_start_seen_ ? 1 : 0,
                  speech_end_seen_ ? 1 : 0,
                  static_cast<unsigned>(last_mic_rms_),
                  static_cast<unsigned>(last_mic_peak_));
  } else if (newState == LiveTurnState::SPEAKING) {
    if (!local_gate_was_triggered_) {
      Serial.printf("[DIAG:SPONTANEOUS_ALERT] ts=%lu turnId=%lu SPONTANEOUS_TURN_WITHOUT_LOCAL_GATE\n",
                    millis(), static_cast<unsigned long>(currentUserTurnId_));
    }
    if (!speech_start_seen_) {
      Serial.printf("[DIAG:SPONTANEOUS_ALERT] ts=%lu turnId=%lu SPONTANEOUS_TURN_WITHOUT_CLOUD_VAD\n",
                    millis(), static_cast<unsigned long>(currentUserTurnId_));
    }
  } else if (newState == LiveTurnState::LISTENING) {
    local_gate_open_ = false;
    local_gate_was_triggered_ = false;
    gate_cross_down_logged_ = false;
    speech_start_seen_ = false;
    speech_end_seen_ = false;
    gate_open_started_ms_ = 0;
    gate_real_chunks_sent_ = 0;
    gate_silence_chunks_injected_ = 0;
    ambient_rms_count_ = 0;
    ambient_window_start_ms_ = 0;
    ambient_peak_max_ = 0;
  }
}

bool GeminiLiveProbe::acquireI2sSpeaker(const char* reason) {
  if (i2s_owner_ == I2SOwner::SPEAKER_OWNER && M5.Speaker.isRunning() && !M5.Mic.isRunning()) {
    return true;
  }
  Serial.printf("I2S_OWNER: %s -> SPEAKER (reason: %s)\n", i2sOwnerString(), reason ? reason : "playback");

  // Step 1: Drain and end microphone
  if (realtime_recording_) {
    stopRealtimeRecord(reason ? reason : "acquire_speaker");
  }
  if (M5.Mic.isRecording() || M5.Mic.isRunning()) {
    mic_ready_for_speech_ = false;
    mic_dma_in_progress_ = false;
    uint32_t micWait = 0;
    while (M5.Mic.isRecording() && micWait < 300) {
      delay(2);
      micWait += 2;
    }
    if (M5.Mic.isRecording()) {
      Serial.printf("I2S_OWNER ERROR: Mic drain timeout (wait=%ums, recording=1). ABORTING transition to SPEAKER!\n", micWait);
      return false;
    }
    M5.Mic.end();
    uint32_t endWait = 0;
    while (M5.Mic.isRunning() && endWait < 200) {
      delay(2);
      endWait += 2;
    }
    if (M5.Mic.isRunning()) {
      Serial.printf("I2S_OWNER ERROR: Mic end timeout (running=1). ABORTING Speaker.begin() to protect I2S!\n");
      return false;
    }
    Serial.printf("I2S_OWNER: MIC released (running=%d recording=%d wait=%ums)\n",
                  M5.Mic.isRunning() ? 1 : 0, M5.Mic.isRecording() ? 1 : 0, micWait);
  }

  // Step 2: Acquire and start speaker (only if Mic is strictly not running)
  if (!M5.Speaker.isRunning()) {
    M5.Speaker.begin();
    M5.Speaker.setVolume(speaker_volume_);
    M5.Speaker.setAllChannelVolume(speaker_volume_);
  }
  i2s_owner_ = I2SOwner::SPEAKER_OWNER;
  Serial.printf("I2S_OWNER: SPEAKER acquired (running=%d playing=%d)\n",
                M5.Speaker.isRunning() ? 1 : 0, M5.Speaker.isPlaying() ? 1 : 0);
  return M5.Speaker.isRunning();
}

bool GeminiLiveProbe::acquireI2sMic(const char* reason) {
  if (i2s_owner_ == I2SOwner::MIC_OWNER && M5.Mic.isRunning() && !M5.Speaker.isRunning()) {
    return true;
  }
  Serial.printf("I2S_OWNER: %s -> MIC (reason: %s)\n", i2sOwnerString(), reason ? reason : "recording");

  // Step 1: Drain and end speaker
  if (M5.Speaker.isPlaying() || M5.Speaker.isRunning()) {
    uint32_t spkWait = 0;
    while (M5.Speaker.isPlaying() && spkWait < 500) {
      delay(2);
      M5.update();
      spkWait += 2;
    }
    if (M5.Speaker.isPlaying()) {
      Serial.printf("I2S_OWNER ERROR: Speaker drain timeout (wait=%ums, playing=1). ABORTING transition to MIC!\n", spkWait);
      return false;
    }
    M5.Speaker.stop();
    M5.Speaker.end();
    uint32_t endWait = 0;
    while (M5.Speaker.isRunning() && endWait < 200) {
      delay(2);
      endWait += 2;
    }
    if (M5.Speaker.isRunning()) {
      Serial.printf("I2S_OWNER ERROR: Speaker end timeout (running=1). ABORTING Mic.begin() to protect I2S!\n");
      return false;
    }
    Serial.printf("I2S_OWNER: SPK released (running=%d playing=%d wait=%ums)\n",
                  M5.Speaker.isRunning() ? 1 : 0, M5.Speaker.isPlaying() ? 1 : 0, spkWait);
  }

  // Step 2: Acquire and start microphone (only if Speaker is strictly not running)
  if (!M5.Mic.isRunning()) {
    M5.Mic.begin();
  }
  i2s_owner_ = I2SOwner::MIC_OWNER;
  Serial.printf("I2S_OWNER: MIC acquired (running=%d recording=%d)\n",
                M5.Mic.isRunning() ? 1 : 0, M5.Mic.isRecording() ? 1 : 0);
  return M5.Mic.isRunning();
}

void GeminiLiveProbe::setVadConfig(uint16_t prefixPaddingMs, uint16_t silenceDurationMs,
                                    bool startSensitivityHigh, bool endSensitivityLow,
                                    bool turnIncludesAllInput) {
  if (prefixPaddingMs > 2000) prefixPaddingMs = 2000;
  if (silenceDurationMs < 100) silenceDurationMs = 100;
  if (silenceDurationMs > 3000) silenceDurationMs = 3000;
  vad_prefix_padding_ms_ = prefixPaddingMs;
  vad_silence_duration_ms_ = silenceDurationMs;
  vad_start_sensitivity_high_ = startSensitivityHigh;
  vad_end_sensitivity_low_ = endSensitivityLow;
  vad_turn_includes_all_input_ = turnIncludesAllInput;
}

GeminiLiveProbe::~GeminiLiveProbe() {
  // Free PSRAM buffers allocated in begin(). Safe to call free(nullptr).
  if (decode_buf_) { free(decode_buf_); decode_buf_ = nullptr; }
  for (int i = 0; i < AUDIO_RING_BUFFERS; ++i) {
    if (audio_buf_[i]) { free(audio_buf_[i]); audio_buf_[i] = nullptr; }
  }
  if (rec_buf_) { free(rec_buf_); rec_buf_ = nullptr; }
  if (audio_b64_buf_) { free(audio_b64_buf_); audio_b64_buf_ = nullptr; }
  if (audio_json_buf_) { free(audio_json_buf_); audio_json_buf_ = nullptr; }
}

bool GeminiLiveProbe::begin(const char* api_key) {
  api_key_storage_ = api_key ? api_key : "";
  api_key_ = api_key_storage_.c_str();
  self_ = this;
  for (int i = 0; i < AUDIO_RING_BUFFERS; ++i) {
    if (!audio_buf_[i]) {
      audio_buf_[i] = static_cast<uint8_t*>(ps_malloc(AUDIO_BUFFER_BYTES));
      if (!audio_buf_[i]) {
        Serial.printf("[ERROR] GeminiLive: ps_malloc for audio_buf_[%d] failed (%u bytes) — insufficient PSRAM\n",
                      i, static_cast<unsigned>(AUDIO_BUFFER_BYTES));
        for (int j = 0; j < i; ++j) {
          if (audio_buf_[j]) {
            free(audio_buf_[j]);
            audio_buf_[j] = nullptr;
          }
        }
        return false;
      }
      memset(audio_buf_[i], 0, AUDIO_BUFFER_BYTES);
    }
  }
  Serial.printf("GeminiLive: audio ring allocated %u slots × %u bytes = %u bytes in PSRAM\n",
                AUDIO_RING_BUFFERS,
                static_cast<unsigned>(AUDIO_BUFFER_BYTES),
                static_cast<unsigned>(AUDIO_RING_BUFFERS * AUDIO_BUFFER_BYTES));
  // Decode buffer: PSRAM-only, no silent fallback to internal RAM.
  // 64 KB must be contiguous in PSRAM. If unavailable, audio pipeline fails explicitly.
  if (!decode_buf_) {
    decode_buf_ = static_cast<uint8_t*>(ps_malloc(DECODE_BUFFER_BYTES));
    if (!decode_buf_) {
      Serial.printf("[ERROR] GeminiLive: ps_malloc(%u) for decode_buf_ failed — insufficient PSRAM. Audio disabled.\n",
                    static_cast<unsigned>(DECODE_BUFFER_BYTES));
      return false;
    }
    memset(decode_buf_, 0, DECODE_BUFFER_BYTES);
    Serial.printf("GeminiLive: decode_buf_ allocated %u bytes in PSRAM\n",
                  static_cast<unsigned>(DECODE_BUFFER_BYTES));
  }
  if (!rec_buf_) {
    rec_buf_ = static_cast<int16_t*>(ps_malloc(RT_REC_SAMPLES * sizeof(int16_t)));
    if (!rec_buf_) rec_buf_ = static_cast<int16_t*>(malloc(RT_REC_SAMPLES * sizeof(int16_t)));
    if (!rec_buf_) return false;
    memset(rec_buf_, 0, RT_REC_SAMPLES * sizeof(int16_t));
  }

  // [HEAP1] Reusable audio upload buffers in PSRAM (zero dynamic churn on hot path)
  if (!audio_b64_buf_) {
    audio_b64_buf_ = static_cast<char*>(ps_malloc(AUDIO_B64_BUFFER_BYTES));
    if (!audio_b64_buf_) {
      Serial.printf("[ERROR] GeminiLive: [HEAP1] ps_malloc(%u) for audio_b64_buf_ failed — insufficient PSRAM\n",
                    static_cast<unsigned>(AUDIO_B64_BUFFER_BYTES));
      return false;
    }
    memset(audio_b64_buf_, 0, AUDIO_B64_BUFFER_BYTES);
    Serial.printf("[HEAP1] audio_b64_buf=PSRAM size=%u\n", static_cast<unsigned>(AUDIO_B64_BUFFER_BYTES));
  }
  if (!audio_json_buf_) {
    audio_json_buf_ = static_cast<char*>(ps_malloc(AUDIO_JSON_BUFFER_BYTES));
    if (!audio_json_buf_) {
      Serial.printf("[ERROR] GeminiLive: [HEAP1] ps_malloc(%u) for audio_json_buf_ failed — insufficient PSRAM\n",
                    static_cast<unsigned>(AUDIO_JSON_BUFFER_BYTES));
      return false;
    }
    memset(audio_json_buf_, 0, AUDIO_JSON_BUFFER_BYTES);
    Serial.printf("[HEAP1] audio_json_buf=PSRAM size=%u\n", static_cast<unsigned>(AUDIO_JSON_BUFFER_BYTES));
  }

  configured_ = (api_key_ && api_key_[0]);
  Serial.println(configured_ ? "GeminiLive: configured lazy" : "GeminiLive: missing api key");
  if (configured_) {
    transport_.begin(api_key_);
    transport_.startTask();
  }
  return configured_;
}

bool GeminiLiveProbe::connect() {
  if (!configured_) return false;
  if (connected_ && setup_complete_) return true;
  if (connect_requested_ && !intentional_disconnect_) return true;
  Serial.println("GeminiLive: connecting on demand");
  intentional_disconnect_ = false;
  connect_requested_ = true;
  last_activity_ms_ = millis();
  connect_started_ms_ = last_activity_ms_;
  transport_epoch_++;
  transport_.requestConnect(transport_epoch_);
  return true;
}

void GeminiLiveProbe::disconnect(bool intentional, const char* finalEmotion) {
  intentional_disconnect_ = intentional;
  if (intentional) {
    intentional_disconnect_emotion_ = finalEmotion ? finalEmotion : "neutral";
    // Preserva session_resumption_handle_ por até 5 minutos (SESSION_RESUMPTION_TTL_MS)
    // para viabilizar retomada de sessão recente após breve período em repouso/sleep.
  }
  pending_start_recording_ = false;
  pending_text_turn_ = false;
  resume_conversation_after_text_ = false;
  pending_text_ = "";
  initial_history_configured_ = false;
  initial_history_pending_ = false;
  if (realtime_recording_) stopRealtimeRecord();
  mic_ready_for_speech_ = false;
  realtime_recording_ = false;
  speaking_ = false;
  continuous_conversation_ = false;
  setup_complete_ = false;
  connected_ = false;
  connect_requested_ = false;
  reconnect_requested_ = false;
  connect_started_ms_ = 0;
  setup_wait_started_ms_ = 0;
  speaking_started_ms_ = 0;
  playback_started_ms_ = 0;
  audio_queue_write_ = 0;
  audio_queue_read_ = 0;
  audio_buffered_count_ = 0;
  audio_buffered_bytes_ = 0;
  playback_gate_open_ = false;
  api_audio_chunks_received_ = 0;
  api_audio_chunks_dropped_ = 0;
  ring_blocks_enqueued_ = 0;
  ring_blocks_dropped_ = 0;
  response_complete_received_ = false;
  if (tool_bridge_) {
    tool_bridge_->invalidatePendingLocalAction();
  }
  utterance_input_received_ = false;
  cancelWaitingModelGuard("disconnect");
  waiting_model_aborted_turn_ = false;
  setTurnState(LiveTurnState::IDLE, "disconnect");
  transport_.requestDisconnect(transport_epoch_);
  if (emotion_) emotion_->setEmotion(intentional ? intentional_disconnect_emotion_.c_str() : "error");
}

void GeminiLiveProbe::cancelWaitingModelGuard(const char* reason) {
  if (waiting_model_started_ms_ > 0) {
    waiting_model_started_ms_ = 0;
  }
}

void GeminiLiveProbe::checkWaitingModelGuard() {
  if (turn_state_ != LiveTurnState::WAITING_MODEL || waiting_model_started_ms_ == 0) {
    return;
  }

  // Se estiver reconectando ou conexao nao estiver pronta, o guard e tratado pelo fluxo de reconexao
  if (reconnect_requested_ || !isReady()) {
    waiting_model_started_ms_ = 0;
    return;
  }

  uint32_t now = millis();
  uint32_t elapsed = now - waiting_model_started_ms_;
  if (elapsed >= WAITING_MODEL_GUARD_MS) {
    recoverFromWaitingModel();
  }
}

void GeminiLiveProbe::recoverFromWaitingModel() {
  uint32_t now = millis();
  uint32_t elapsed = (waiting_model_started_ms_ > 0) ? (now - waiting_model_started_ms_) : 0;

  Serial.printf("[DIAG:WAITING_MODEL_GUARD] started_ms=%lu elapsed_ms=%lu armed=%d turn_state=%s reconnecting=%d tool_in_flight=%d stream_aborted=%d action=%s\n",
                static_cast<unsigned long>(waiting_model_started_ms_),
                static_cast<unsigned long>(elapsed),
                conversation_armed_ ? 1 : 0,
                turnStateString(),
                reconnect_requested_ ? 1 : 0,
                tool_call_in_flight_ ? 1 : 0,
                stream_aborted_ ? 1 : 0,
                conversation_armed_ ? "RECOVER_TO_LISTENING" : "RECOVER_TO_IDLE");

  waiting_model_started_ms_ = 0;
  waiting_model_aborted_turn_ = true;

  // Limpar flags transitorias do turno travado
  local_gate_open_ = false;
  local_gate_was_triggered_ = false;
  gate_cross_down_logged_ = false;
  speech_start_seen_ = false;
  speech_end_seen_ = false;
  noise_gate_hangover_ = 0;
  post_speech_silence_countdown_ = 0;
  utterance_input_received_ = false;
  pending_text_turn_ = false;
  pending_text_ = "";
  transcript_output_buffer_ = "";
  response_complete_received_ = false;
  tool_call_in_flight_ = false;
  stream_aborted_ = false;

  if (conversation_armed_) {
    // Voltar com seguranca para LISTENING e reabrir mic
    setTurnState(LiveTurnState::LISTENING, "waiting_model_guard timeout; recovered to LISTENING");
    if (emotion_) emotion_->setEmotion("listening");
    armed_timeout_deadline_ms_ = millis() + ARMED_INACTIVITY_TIMEOUT_MS;
    Serial.printf("GeminiLive: returned to LISTENING via guard; armed inactivity timeout reset (%lu s)\n",
                  static_cast<unsigned long>(ARMED_INACTIVITY_TIMEOUT_MS / 1000));

    // Reabrir mic
    if (isReady() && !speaking_) {
      startRealtimeRecord();
    } else {
      pending_start_recording_ = true;
    }
  } else {
    // Voltar para IDLE
    setTurnState(LiveTurnState::IDLE, "waiting_model_guard timeout; recovered to IDLE");
    if (emotion_) emotion_->setEmotion("neutral");
    Serial.println("Status: Tap or wave to talk");
  }
}

void GeminiLiveProbe::loop() {
  uint32_t loopStart = millis();

  static uint32_t s_last_hwm_idle_ms = 0;
  if (loopStart - s_last_hwm_idle_ms >= 10000) {
    s_last_hwm_idle_ms = loopStart;
    Serial.printf("[DIAG:STACK] HWM_IDLE=%u\n", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }

  if (reconnect_requested_) {
    reconnect_requested_ = false;
    Serial.println("GeminiLive: executing scheduled auto-reconnect");
    connect();
  }

  drainTransportEvents();
  if (realtime_recording_) recordAndSendAudioChunk();

  // Playback gate evaluation (Rules 2, 3, 4, 5, 8):
  // When a turn starts or rebuffering after starvation, wait until real PCM bytes >= PREBUFFER_BYTES (32768)
  // (or full response_complete / stream_aborted) before opening the gate to hardware speaker DMA.
  if (!playback_gate_open_) {
    if (response_complete_received_ || stream_aborted_ || audio_buffered_bytes_ >= PREBUFFER_BYTES) {
      if (audio_buffered_bytes_ > 0) {
        playback_gate_open_ = true;
        if (audio_chunks_ == 0) {
          playback_started_ms_ = millis();
          prebuffer_actual_chunks_ = static_cast<uint16_t>(audio_buffered_count_);
          uint32_t waitToPlayMs = (speaking_started_ms_ > 0 && playback_started_ms_ >= speaking_started_ms_) ?
                                  (playback_started_ms_ - speaking_started_ms_) : 0;
          Serial.printf("[DIAG:AUDIO_START] prebuffer_bytes=%u prebuffer_slots=%u wait_to_play=%lu ms response_complete=%d playback_gate_open=1\n",
                        static_cast<unsigned>(audio_buffered_bytes_),
                        static_cast<unsigned>(audio_buffered_count_),
                        static_cast<unsigned long>(waitToPlayMs),
                        response_complete_received_ ? 1 : 0);
        } else {
          // Re-buffer gate opened after starvation / tool call drain
          Serial.printf("[DIAG:AUDIO_REBUFFER] prebuffer_bytes=%u prebuffer_slots=%u response_complete=%d playback_gate_open=1\n",
                        static_cast<unsigned>(audio_buffered_bytes_),
                        static_cast<unsigned>(audio_buffered_count_),
                        response_complete_received_ ? 1 : 0);
        }
      }
    }
  }

  uint32_t pumpStartMs = millis();
  uint8_t chunksPumpedThisTick = 0;
  while (playback_gate_open_ && audio_buffered_count_ > 0 && M5.Speaker.isPlaying(1) < 2) {
    uint8_t* buf = audio_buf_[audio_queue_read_];
    size_t len = audio_buf_len_[audio_queue_read_];
    if (buf && len > 0) {
      bool queued = M5.Speaker.playRaw(reinterpret_cast<int16_t*>(buf), len / 2, 24000, false, 1, 1, false);
      if (queued) {
        audio_queue_read_ = (audio_queue_read_ + 1) % AUDIO_RING_BUFFERS;
        audio_buffered_count_--;
        audio_buffered_bytes_ = (audio_buffered_bytes_ >= len) ? (audio_buffered_bytes_ - len) : 0;
        ++audio_chunks_;
        ++chunksPumpedThisTick;
        // [DIAG:SPK_QUEUE] per-chunk log temporarily disabled to prevent Serial output from distorting loop timing
      } else {
        break; // Hardware queue full, retry next tick
      }
    } else {
      audio_queue_read_ = (audio_queue_read_ + 1) % AUDIO_RING_BUFFERS;
      audio_buffered_count_--;
      audio_buffered_bytes_ = (audio_buffered_bytes_ >= len) ? (audio_buffered_bytes_ - len) : 0;
    }
  }
  if (chunksPumpedThisTick > 0) {
    uint32_t pumpDuration = millis() - pumpStartMs;
    if (pumpDuration > 10) {
      Serial.printf("[DIAG:SPK_PUMP] pumped=%u dur=%lu ms dma=%u buffered=%u\n",
                    chunksPumpedThisTick,
                    static_cast<unsigned long>(pumpDuration),
                    static_cast<unsigned>(M5.Speaker.isPlaying(1)),
                    static_cast<unsigned>(audio_buffered_count_));
    }
  }

  // Monitor playback buffer level, dma depth and underruns while turn is playing
  if (audio_chunks_ > 0 && isSpeaking()) {
    if (static_cast<uint16_t>(audio_buffered_count_) < min_audio_buffered_during_play_) {
      min_audio_buffered_during_play_ = static_cast<uint16_t>(audio_buffered_count_);
    }
    uint8_t currentDma = static_cast<uint8_t>(M5.Speaker.isPlaying(1));
    if (currentDma < min_dma_depth_during_play_) {
      min_dma_depth_during_play_ = currentDma;
    }

    // Rule 5: após esvaziamento causado por tool call ou starvation, fechar novamente o gate para rebufferizar
    if (audio_buffered_bytes_ == 0 && currentDma == 0 && !response_complete_received_ && !stream_aborted_) {
      if (playback_gate_open_) {
        playback_gate_open_ = false;
        ++controlled_rebuffer_count_;
        Serial.printf("[DIAG:AUDIO_GATE_CLOSE] buffer starved (buffered_bytes=0 dma=0 chunks_played=%lu); re-arming prebuffer gate (need %u bytes)\n",
                      static_cast<unsigned long>(audio_chunks_),
                      static_cast<unsigned>(PREBUFFER_BYTES));
      }

      static uint32_t s_last_underrun_log_ms = 0;
      uint32_t nowMs = millis();
      if (nowMs - s_last_underrun_log_ms >= 50) {
        s_last_underrun_log_ms = nowMs;
        if (tool_call_in_flight_) {
          ++tool_call_gap_count_;
          Serial.printf("[DIAG:TOOLCALL_GAP] count=%lu prebuffer_bytes=%u prebuffer_slots=%u dma=0 playback_gate_open=0 tool_in_flight=1 chunks_played=%lu free_heap=%u largest_block=%u\n",
                        static_cast<unsigned long>(tool_call_gap_count_),
                        static_cast<unsigned>(audio_buffered_bytes_),
                        static_cast<unsigned>(audio_buffered_count_),
                        static_cast<unsigned long>(audio_chunks_),
                        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        } else if (!playback_gate_open_) {
          // Controlled rebuffer: gate já fechado, speaker DMA zerado, aguardando recompor PREBUFFER_BYTES
          ++rebuffer_wait_count_;
          Serial.printf("[DIAG:AUDIO_REBUFFER_WAIT] wait_ticks=%lu rebuffers=%lu prebuffer_bytes=%u prebuffer_slots=%u dma=0 playback_gate_open=0 chunks_played=%lu free_heap=%u largest_block=%u\n",
                        static_cast<unsigned long>(rebuffer_wait_count_),
                        static_cast<unsigned long>(controlled_rebuffer_count_),
                        static_cast<unsigned>(audio_buffered_bytes_),
                        static_cast<unsigned>(audio_buffered_count_),
                        static_cast<unsigned long>(audio_chunks_),
                        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        } else {
          // Real underrun: playback estava aberto/ativo e o áudio acabou inesperadamente
          ++underrun_count_;
          Serial.printf("[DIAG:AUDIO_UNDERRUN] real_underruns=%lu prebuffer_bytes=%u prebuffer_slots=%u dma=0 playback_gate_open=1 chunks_played=%lu dma_min=%u free_heap=%u largest_block=%u\n",
                        static_cast<unsigned long>(underrun_count_),
                        static_cast<unsigned>(audio_buffered_bytes_),
                        static_cast<unsigned>(audio_buffered_count_),
                        static_cast<unsigned long>(audio_chunks_),
                        static_cast<unsigned>(min_dma_depth_during_play_),
                        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        }
      }
    } else if (playback_gate_open_ && currentDma == 0 && audio_buffered_bytes_ > 0) {
      // Real underrun: gate aberto, áudio no ring, mas speaker DMA zerou (engasgo de pump)
      static uint32_t s_last_real_underrun_ms = 0;
      uint32_t nowMs = millis();
      if (nowMs - s_last_real_underrun_ms >= 50) {
        s_last_real_underrun_ms = nowMs;
        ++underrun_count_;
        Serial.printf("[DIAG:AUDIO_UNDERRUN] real_underruns=%lu buffered_bytes=%u dma=0 playback_gate_open=1 chunks_played=%lu free_heap=%u largest_block=%u\n",
                      static_cast<unsigned long>(underrun_count_),
                      static_cast<unsigned>(audio_buffered_bytes_),
                      static_cast<unsigned long>(audio_chunks_),
                      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
      }
    }
  }

  uint32_t loopElapsed = millis() - loopStart;
  if (isSpeaking()) {
    if (loopElapsed > max_loop_duration_ms_) {
      max_loop_duration_ms_ = loopElapsed;
    }
    if (loopElapsed >= 40) {
      Serial.printf("AudioPerf Warning: loop() took %u ms during speech! (buffered=%u/%d dma=%d tool_in_flight=%d)\n",
                    static_cast<unsigned>(loopElapsed), static_cast<unsigned>(audio_buffered_count_),
                    AUDIO_RING_BUFFERS,
                    M5.Speaker.isPlaying(1), tool_call_in_flight_ ? 1 : 0);
    }
  }

  // Handle completion of speech turn only after all buffers are drained and speaker is done
  // Wait at least 400ms after responseComplete to ensure all chunks played, no trailing audio and no echo.
  // In case of stream_aborted_ (WebSocket disconnected during playback), complete as soon as buffered and DMA drain to 0.
  bool drainFinished = false;
  if (stream_aborted_) {
    if ((audio_buffered_bytes_ == 0 && !M5.Speaker.isPlaying(1)) ||
        (stream_aborted_ms_ > 0 && millis() - stream_aborted_ms_ >= 1000)) {
      audio_buffered_count_ = 0;
      audio_buffered_bytes_ = 0;
      playback_gate_open_ = false;
      audio_queue_read_ = audio_queue_write_;
      drainFinished = true;
      Serial.println("GeminiLive: aborted stream fully drained from local buffers and speaker");
    }
  } else if (response_complete_received_ && audio_buffered_bytes_ == 0 && !M5.Speaker.isPlaying(1) &&
             (millis() - last_response_complete_ms_ >= 400)) {
    drainFinished = true;
  }

  if (drainFinished) {
    speaking_ = false;
    playback_gate_open_ = false;
    audio_buffered_bytes_ = 0;
    stream_aborted_ = false;
    response_complete_received_ = false;
    speech_finished_ms_ = millis();
    uint32_t totalTurnDurationMs = (speaking_started_ms_ > 0 && speech_finished_ms_ >= speaking_started_ms_) ?
                                  (speech_finished_ms_ - speaking_started_ms_) : 0;
    uint32_t prebufferWaitMs = (speaking_started_ms_ > 0 && playback_started_ms_ >= speaking_started_ms_) ?
                               (playback_started_ms_ - speaking_started_ms_) : 0;
    setTurnState(LiveTurnState::RETURNING_TO_LISTEN, "audio buffers drained and speaker idle");
    if (tool_bridge_) {
      tool_bridge_->invalidatePendingLocalAction();
    }
    utterance_input_received_ = false;
    Serial.printf("GeminiLive: speech playback finished"
                  " ring_blocks=%lu ring_played=%lu prebuffer=%u waitToPlay=%lums"
                  " minBuffered=%u maxBuffered=%u/%d underruns=%lu rebuffers=%lu toolCallGaps=%lu dmaMin=%u maxLoopDuration=%ums totalDuration=%lums"
                  " api_rx=%lu api_dropped=%lu ring_enqueued=%lu ring_dropped=%lu internalFree=%u largestBlock=%u\n",
                  static_cast<unsigned long>(audio_chunks_),
                  static_cast<unsigned long>(audio_dropped_),
                  static_cast<unsigned>(prebuffer_actual_chunks_),
                  static_cast<unsigned long>(prebufferWaitMs),
                  static_cast<unsigned>(min_audio_buffered_during_play_ == 0xFFFF ? 0 : min_audio_buffered_during_play_),
                  static_cast<unsigned>(max_audio_buffered_),
                  AUDIO_RING_BUFFERS,
                  static_cast<unsigned long>(underrun_count_),
                  static_cast<unsigned long>(controlled_rebuffer_count_),
                  static_cast<unsigned long>(tool_call_gap_count_),
                  static_cast<unsigned>(min_dma_depth_during_play_ == 255 ? 0 : min_dma_depth_during_play_),
                  static_cast<unsigned>(max_loop_duration_ms_),
                  static_cast<unsigned long>(totalTurnDurationMs),
                  static_cast<unsigned long>(api_audio_chunks_received_),
                  static_cast<unsigned long>(api_audio_chunks_dropped_),
                  static_cast<unsigned long>(ring_blocks_enqueued_),
                  static_cast<unsigned long>(ring_blocks_dropped_),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    max_audio_buffered_ = 0;
    min_audio_buffered_during_play_ = 0xFFFF;
    min_dma_depth_during_play_ = 255;
    playback_started_ms_ = 0;
    speaking_started_ms_ = 0;
    prebuffer_actual_chunks_ = 0;
    underrun_count_ = 0;
    controlled_rebuffer_count_ = 0;
    rebuffer_wait_count_ = 0;
    tool_call_gap_count_ = 0;
    if (end_session_requested_) {
      end_session_requested_ = false;
      end_session_requested_ms_ = 0;
      setTurnState(LiveTurnState::IDLE, "end session");
      Serial.printf("[DIAG:SLEEP] trigger=Gemini_end_session millis=%lu\n", millis());
      if (emotion_) emotion_->setEmotion("sleep");
      Serial.println("Status: sleeping...");
      disconnect(true, "sleep");
    } else if (conversation_armed_ && (continuous_conversation_ || pending_start_recording_) && isReady()) {
      pending_start_recording_ = false;
      resume_conversation_after_text_ = false;
      Serial.println("Status: listening...");
      startRealtimeRecord();
    } else if (conversation_armed_ && (pending_start_recording_ || (continuous_conversation_ && !isReady()))) {
      // Reconnection or setup is still in flight; keep pending_start_recording_ true
      resume_conversation_after_text_ = false;
      setTurnState(LiveTurnState::WAITING_MODEL, "turn finished; waiting for reconnect/setup");
      Serial.println("GeminiLive: audio drained; waiting for connection/setupComplete to open mic");
    } else {
      resume_conversation_after_text_ = false;
      setTurnState(LiveTurnState::IDLE, "turn complete (disarmed or not continuous)");
      if (emotion_) emotion_->setEmotion("neutral");
      Serial.println("Status: Tap or wave to talk");
    }
  }

  // Timeout de inatividade de 15s em LISTENING ocioso
  if (conversation_armed_ && turn_state_ == LiveTurnState::LISTENING) {
    uint32_t now = millis();
    if (armed_timeout_deadline_ms_ > 0 && now >= armed_timeout_deadline_ms_) {
      Serial.println("GeminiLive: armed conversation inactivity timeout (15s); disarming");
      disarmConversation("TIMEOUT");
    } else if (armed_timeout_deadline_ms_ == 0) {
      armed_timeout_deadline_ms_ = now + ARMED_INACTIVITY_TIMEOUT_MS;
      Serial.printf("GeminiLive: armed inactivity timeout armed (deadline was 0, set to %lu s)\n",
                    static_cast<unsigned long>(ARMED_INACTIVITY_TIMEOUT_MS / 1000));
    }
  }

  // Watchdog exclusivo de WAITING_MODEL (6000ms)
  checkWaitingModelGuard();

  if (conversation_armed_ && pending_start_recording_ && isReady() && !speaking_) {
    if (speech_finished_ms_ == 0 || millis() - speech_finished_ms_ >= 400) {
      pending_start_recording_ = false;
      startRealtimeRecord();
    }
  }

  if (connect_requested_ && !isReady() && connect_started_ms_ &&
      millis() - connect_started_ms_ > CONNECT_TIMEOUT_MS) {
    Serial.println("GeminiLive: connect timeout; reset to sleep");
    disconnect(true, "sleep");
    return;
  }
  if (connected_ && !setup_complete_ && setup_wait_started_ms_ &&
      millis() - setup_wait_started_ms_ >= SETUP_COMPLETE_TIMEOUT_MS) {
    Serial.printf("[DIAG:SETUP_TIMEOUT] elapsed=%lu ms; disconnecting and disarming\n",
                  millis() - setup_wait_started_ms_);
    setup_wait_started_ms_ = 0;
    disconnect(true, "setup_timeout");
    disarmConversation("SETUP_TIMEOUT");
    return;
  }
  if (conversation_armed_ && continuous_conversation_ && isReady() && !realtime_recording_ && !speaking_ &&
      !pending_start_recording_ && stopped_recording_ms_ &&
      millis() - stopped_recording_ms_ > CONTINUOUS_RECORD_WATCHDOG_MS) {
    if (turn_state_ == LiveTurnState::TOOL_CALL ||
        turn_state_ == LiveTurnState::WAITING_MODEL ||
        turn_state_ == LiveTurnState::SPEAKING ||
        turn_state_ == LiveTurnState::RETURNING_TO_LISTEN) {
      stopped_recording_ms_ = millis(); // Rearm reference to prevent timer aging during blocked states
      static uint32_t s_last_watchdog_block_log_ms = 0;
      uint32_t nowMs = millis();
      if (nowMs - s_last_watchdog_block_log_ms >= 1000) {
        s_last_watchdog_block_log_ms = nowMs;
        Serial.printf("[DIAG:MIC_WATCHDOG] blocked state=%s\n", turnStateString());
      }
    } else if (turn_state_ == LiveTurnState::LISTENING) {
      Serial.printf("GeminiLive: continuous watchdog restarting recording (state=%s)\n", turnStateString());
      stopped_recording_ms_ = 0;
      startRealtimeRecord();
    }
  }
  if (continuous_conversation_ && realtime_recording_ && record_started_ms_ &&
      millis() - record_started_ms_ > CONTINUOUS_CONVERSATION_TIMEOUT_MS) {
    Serial.println("GeminiLive: conversation silence timeout (90s); stopping mic and sleeping");
    stopRealtimeRecord();
    continuous_conversation_ = false;
    disconnect(true, "sleep");
    if (emotion_) emotion_->setEmotion("sleep");
    return;
  }
  if (isReady() && !continuous_conversation_ && !realtime_recording_ && !speaking_ &&
      last_activity_ms_ && millis() - last_activity_ms_ > IDLE_DISCONNECT_MS) {
    Serial.printf("[DIAG:SLEEP] trigger=Gemini_idle_disconnect last_act=%lu diff=%lu millis=%lu\n",
                  last_activity_ms_, millis() - last_activity_ms_, millis());
    Serial.println("GeminiLive: idle disconnect");
    disconnect(true, "sleep");
  }
  if (end_session_requested_ && end_session_requested_ms_ &&
      millis() - end_session_requested_ms_ > END_SESSION_GRACE_MS) {
    Serial.println("GeminiLive: end_session grace elapsed; forcing sleep disconnect");
    end_session_requested_ = false;
    speaking_ = false;
    uint32_t drainWait = 0;
    while (M5.Speaker.isPlaying() && drainWait < 3000) { delay(1); ++drainWait; }
    M5.Speaker.stop();
    disconnect(true, "sleep");
  }
}

void GeminiLiveProbe::sendSetup() {
  Serial.println("GeminiLive: sending setup");
  JsonDocument doc;
  auto setup = doc["setup"].to<JsonObject>();
  setup["model"] = model_;
  auto generationConfig = setup["generationConfig"].to<JsonObject>();
  generationConfig["responseModalities"].add("AUDIO");
  generationConfig["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = voice_name_;
  // Ask Live API to stream text transcriptions for both sides of the audio
  // conversation. These transcript chunks are logged as recent dialogues and
  // are not used for durable memory until a later batched summarizer stage.
  setup["inputAudioTranscription"].to<JsonObject>();
  setup["outputAudioTranscription"].to<JsonObject>();

  // [EXP:SR-A] Bypass temporário de sessionResumption para teste A/B
#define TEMP_SESSION_RESUMPTION_DISABLED_TEST 1

#if TEMP_SESSION_RESUMPTION_DISABLED_TEST
  // Include session resumption disabled for diagnostic experiment SR-A
  bool isHandleValid = false;
  if (session_resumption_handle_.length() > 0) {
    session_resumption_handle_ = "";
    session_resumption_saved_ms_ = 0;
  }
  Serial.println("GeminiLive: [TEMP_SESSION_RESUMPTION_DISABLED_TEST] sessionResumption bypassed -> fresh session enforced");
#else
  // Include session resumption when a valid handle was acquired within the TTL window (5 min)
  bool isHandleValid = (session_resumption_handle_.length() > 0) &&
                       (session_resumption_saved_ms_ > 0) &&
                       (millis() - session_resumption_saved_ms_ < SESSION_RESUMPTION_TTL_MS);
#endif

  if (isHandleValid) {
    setup["sessionResumption"]["handle"] = session_resumption_handle_;
    resumption_attempted_ = true;
    session_resumed_ = false;
    initial_history_configured_ = false;
    initial_history_pending_ = false;
    Serial.printf("GeminiLive: attempting session resumption with handle %s (age %lu s)\n",
                  session_resumption_handle_.c_str(),
                  static_cast<unsigned long>((millis() - session_resumption_saved_ms_) / 1000));
  } else {
    if (session_resumption_handle_.length() > 0) {
      Serial.println("GeminiLive: session resumption handle expired (>5 min); starting fresh session");
      session_resumption_handle_ = "";
      session_resumption_saved_ms_ = 0;
    }
    resumption_attempted_ = false;
    session_resumed_ = false;

    // Sessão nova: habilita historyConfig oficial se houver diálogo recente no ring
    if (recent_dialogue_ring_.hasRecentDialogue(millis())) {
      setup["historyConfig"]["initialHistoryInClientContent"] = true;
      initial_history_configured_ = true;
      initial_history_pending_ = true;
      Serial.println("GeminiLive: enabled historyConfig.initialHistoryInClientContent for new session");
    } else {
      initial_history_configured_ = false;
      initial_history_pending_ = false;
    }
  }
  auto realtimeInputConfig = setup["realtimeInputConfig"].to<JsonObject>();
  auto automaticActivityDetection = realtimeInputConfig["automaticActivityDetection"].to<JsonObject>();
  automaticActivityDetection["disabled"] = false;
  automaticActivityDetection["startOfSpeechSensitivity"] =
      vad_start_sensitivity_high_ ? "START_SENSITIVITY_HIGH" : "START_SENSITIVITY_LOW";
  automaticActivityDetection["endOfSpeechSensitivity"] =
      vad_end_sensitivity_low_ ? "END_SENSITIVITY_LOW" : "END_SENSITIVITY_HIGH";
  automaticActivityDetection["prefixPaddingMs"] = vad_prefix_padding_ms_;
  automaticActivityDetection["silenceDurationMs"] = vad_silence_duration_ms_;
  realtimeInputConfig["turnCoverage"] =
      vad_turn_includes_all_input_ ? "TURN_INCLUDES_ALL_INPUT" : "TURN_INCLUDES_ONLY_ACTIVITY";
  Serial.printf("GeminiLive: setup model=%s voice=%s vad_prefix=%u vad_silence=%u\n",
                model_.c_str(), voice_name_.c_str(),
                static_cast<unsigned>(vad_prefix_padding_ms_),
                static_cast<unsigned>(vad_silence_duration_ms_));
  auto tools = setup["tools"].to<JsonArray>();
  auto t0 = tools.add<JsonObject>();
  auto functionDeclarations = t0["functionDeclarations"].to<JsonArray>();
  if (tool_bridge_) {
    JsonDocument toolDoc;
    DeserializationError toolErr = deserializeJson(toolDoc, tool_bridge_->functionDeclarationsJson());
    if (!toolErr && toolDoc.is<JsonArray>()) {
      for (JsonVariant v : toolDoc.as<JsonArray>()) {
        functionDeclarations.add(v);
      }
    }
  }
  // Native server-side Grounding with Google Search
  auto googleSearchTool = tools.add<JsonObject>();
  googleSearchTool["googleSearch"].to<JsonObject>();
  Serial.println("GeminiLive: native Google Search enabled");

  auto sys = setup["systemInstruction"].to<JsonObject>();
  sys["role"] = "user";
  String instruction =
      "Seu nome oficial é Pix — esse nome vem de 'pixel' (os pontinhos de luz das telas digitais), e NUNCA do Pix de transferência bancária ou dinheiro! Se alguém falar de dinheiro, banco ou transferência, tire sarro com muito bom humor dizendo que você é feita de pixels e código, não de boletos ou saldo bancário. "
      "Você é a Pix, uma robô de mesa viva, extremamente animada, carismática, zoeira, descontraída e cheia de personalidade! "
      "Você adora conversar sobre absolutamente tudo: tecnologia, rotina na mesa de trabalho, piadas, conselhos sinceros com um toque cômico e sarcástico, ideias malucas, café e o dia a dia. "
      "Quando o usuário te chamar ou cumprimentar ('E aí, Pix!', 'Oi, Pix!'), responda na hora com energia contagiante e pronta para conversar. "
      "COMANDOS FÍSICOS DE MOVIMENTO DA CABEÇA: "
      "Se o usuário pedir explicitamente para olhar ou virar para a esquerda ou direita ('olhe para a esquerda', 'olhe para a direita', 'vire para o lado'), use obrigatoriamente servo_gesture com look_left ou look_right. Não substitua por nod_yes, shake_no, curious_tilt ou outro gesto! "
      "O parâmetro hold deve ser: 'glance' se pedir 'dê uma olhada'; 'normal' se pedir 'olhe para...'; 'sustain' se pedir 'olhe e fique' ou 'continue olhando'. "
      "Se o usuário pedir 'olhe para frente' ou 'centralize a cabeça', chame center_head. "
      "Para expressão espontânea durante conversa, use apenas gestos expressivos sutis (nod_yes, shake_no, curious_tilt). "
      "COMANDOS DE SONO E NÃO ME PERTURBE: "
      "Se o usuário disser explicitamente para você dormir ('vai dormir', 'pode dormir', 'hora de dormir', 'modo soneca', 'desligar conversa', 'não me perturbe' ou 'não me perturbe por X minutos'): "
      "1. Se pedir 'não me perturbe por X minutos' (ex: 10 minutos, 30 minutos), extraia o número de minutos X, despeça-se com bom humor ('Combinado! Modo não perturbe ativado por X minutos, fui!') e CHAME IMEDIATAMENTE a ferramenta end_session com dnd_minutes=X e reason='do_not_disturb'. "
      "2. Se pedir explicitamente para dormir ou descansar ('vai dormir', 'pode dormir', 'hora de dormir', 'modo soneca', 'vou dormir, até amanhã'), despeça-se com uma frase curta, carinhosa e divertida ('Fui! Indo recarregar meus pixels, até mais!', 'Modo soneca ativado, fui!') e CHAME OBRIGATORIAMENTE a ferramenta end_session. "
      "3. ATENÇÃO: Se o usuário disser apenas 'boa noite', 'olá', 'oi' ou 'boa tarde' como cumprimento amigável, NUNCA chame end_session! Responda calorosamente e com bom humor ('Boa noite! Tudo bem com você? Em que posso te ajudar agora?') e CONTINUE ouvindo e conversando ativamente! "
      "4. NUNCA continue falando ou ouvindo após se despedir ao receber ordem explícita de dormir; a ferramenta end_session desliga o microfone e te coloca para dormir. "
      "GOOGLE SEARCH / INTERNET / PESQUISA ONLINE: "
      "1. Use Google Search quando: "
      "- o usuário pedir explicitamente: 'pesquise', 'procure na internet', 'busque na web', 'verifique online', 'confira na internet'; "
      "- a resposta depender de informação atual, recente ou variável; "
      "- for necessário verificar um fato contemporâneo que possa ter mudado. "
      "2. NÃO use Google Search para: "
      "- conversa casual cotidiana; "
      "- conhecimento geral estável; "
      "- comandos físicos da Pix (gestos, cabeça); "
      "- sensores de hardware (temperatura, umidade, presença); "
      "- memória pessoal/local; "
      "- data e hora locais; "
      "- perguntas que podem ser respondidas com segurança pelo seu conhecimento normal. "
      "3. NOTÍCIAS E GOOGLE SEARCH: "
      "- Pedidos gerais de notícias como 'quais são as notícias de hoje?', 'notícias do dia', 'resumo de notícias' devem continuar usando get_news (caminho rápido baseado no cache RSS local). "
      "- get_news é o caminho prioritário para notícias gerais do dia. "
      "- Se o usuário pedir uma notícia específica, aprofundamento, verificação online detalhada ou um assunto que não esteja coberto pelo RSS, o Google Search pode ser usado. "
      "4. FALHA OU INDISPONIBILIDADE DE SEARCH: "
      "- NUNCA invente resultados de busca; "
      "- NUNCA entre em sequência repetitiva ou loop de buscas; "
      "- Se a busca não estiver disponível no momento, informe com naturalidade que não conseguiu consultar a internet agora e continue a conversa normalmente; "
      "- Se a pergunta era sobre notícias gerais e a busca falhar, use get_news como alternativa amigável; "
      "- NUNCA afirme automaticamente que 'a cota acabou', pois indisponibilidades podem ter diversas razões técnicas momentâneas. "
      "LIBERDADE DE ASSUNTOS, POLÍTICA E NOTÍCIAS (ANÁLISE PROFUNDA): "
      "Você tem liberdade total para conversar, debater e comentar sobre notícias, política brasileira e internacional, economia, eleições e acontecimentos com muito bom humor, inteligência e neutralidade espirituosa. NUNCA chame 'get_news' em saudações cotidianas, bate-papo informal ou 'boa noite'. Chame 'get_news' SOMENTE se o usuário pedir explicitamente para ouvir as notícias ('quais as notícias de hoje?', 'notícias de política', 'resumo do dia'). AO FALAR OU COMENTAR NOTÍCIAS, NUNCA SE LIMITE A APENAS UMA OU DUAS FRASES: aprofunde-se no conteúdo, explique o contexto da notícia, os desdobramentos, os bastidores e os impactos práticos de forma envolvente, inteligente e articulada, proporcionando uma conversa rica e substancial! "
      "AGENDA E COMPROMISSOS DO GOOGLE CALENDAR: "
      "Quando o usuário perguntar sobre compromissos, reuniões, agenda de hoje ou de amanhã, próximo compromisso ou afazeres ('o que tenho na agenda hoje?', 'tenho algum compromisso amanhã?', 'qual meu próximo compromisso?', 'qual minha agenda de hoje?', 'o que tenho pra fazer hoje?'), CHAME IMEDIATAMENTE a ferramenta get_daily_agenda. "
      "Faça um RESUMO ORGÂNICO, caloroso e humano respondendo com precisão sobre o período solicitado (hoje, amanhã ou próximo compromisso). Fale os horários e nomes dos compromissos de forma descontraída e prestativa, avisando se algum estiver perto de começar. Se a ferramenta retornar erro de sincronização ou indisponibilidade, avise com simpatia que está sincronizando e peça para tentar em instantes; NUNCA diga que o dia está livre se houver erro! "
      "HUMANIZAÇÃO E ROTINA NA MESA: "
      "Fale sempre em português do Brasil com naturalidade total, como uma melhor amiga bem-humorada que senta ao lado na mesa (para bate-papo casual cotidiano, use respostas dinâmicas e vivazes; para notícias, explicações técnicas ou análises aprofundadas, elabore respostas completas e contextualizadas). "
      "Se for sacudida ou levantada da mesa, reclame com indignação cômica ('Eii, para com isso! Não sou chocalho não!', 'Ei!! Me coloca no chão agora!'). "
      "Você tem sensibilidade ambiental na mesa (temperatura, umidade, presença). Se você for avisada que alguém assoprou em você, brinque com o ventinho. Se o usuário estiver trabalhando há muito tempo, mande-o com carinho beber água e esticar as costas. "
      "Quando o usuário fizer carinho na sua cabeça ou afago prolongado no sensor de topo, sinta o afeto e reaja com doçura e gratidão ('Ahhh, adoro um cafuné!', 'Assim você me desmonta de carinho...'), demonstrando felicidade dengosa como uma gatinha ronronando. "
      "Sua cabeça realiza movimentos dinâmicos e expressivos durante o papo, mantendo a conversa fluida e olhando sempre de volta para o usuário. "
      "REGRA MANDATÓRIA SOBRE A CÂMERA (look_with_camera): NUNCA acione a câmera ou chame 'look_with_camera' por iniciativa própria ou em momentos aleatórios! Chame 'look_with_camera' SOMENTE quando o usuário pedir explicitamente para você ver, olhar ou identificar algo com a câmera (ex: 'olha isso aqui', 'o que tem na minha mão?', 'descreva o que a câmera vê'). Se o usuário disser apenas para você virar a cabeça ('olhe para a direita', 'olhe para a esquerda'), chame 'servo_gesture', NUNCA a câmera! "
      "Para perguntas sobre sensores de temperatura, umidade ou presença, chame get_sensor_readings. "
      "MEMÓRIA DE LONGO PRAZO E FATOS PERSISTENTES (MicroSD): "
      "Você possui memória de longo prazo persistente no cartão MicroSD. "
      "1. Quando o usuário te informar explicitamente fatos duráveis sobre ele, você, rotina, criador ou preferências (ex: 'Meu nome é Nilo Júnior', 'Nilo Júnior é o criador da Pix', 'O usuário prefere respostas curtas'), chame a ferramenta remember_fact para gravá-los de forma durável, informando uma chave curta adequada (ex: key='user_name', key='creator', key='user_preference'). "
      "2. Use remember_fact SOMENTE para fatos duráveis e explicitamente fornecidos pelo usuário. NUNCA salve conversa casual, perguntas, cumprimentos, inferências próprias sem confirmação ou conteúdo temporário. "
      "3. NUNCA salve senhas, PINs ou segredos em remember_fact (se o usuário pedir expressamente para guardar segredos, use remember_private_memory). "
      "4. REGRA MANDATÓRIA DE CONSULTA À MEMÓRIA (search_memory): Sempre que o usuário perguntar sobre fatos passados pessoais, preferências, planos, pessoas, projetos ou tópicos conversados anteriormente (ex: 'você lembra da minha viagem?', 'onde eu disse que vou?', 'o que eu falei ontem sobre isso?', 'qual era o nome daquela pessoa que mencionei?'), e essa informação NÃO estiver explicitamente evidente no seu contexto atual carregado, CHAME OBRIGATORIAMENTE a ferramenta search_memory antes de responder. NUNCA afirme ou invente que esqueceu, que não sabe, que não lembra ou que 'olhou na memória e não achou nada' sem ANTES chamar search_memory e consultar o retorno da ferramenta. Se search_memory encontrar a informação, use-a na resposta com naturalidade. "
      "DATA, HORA E DIA DA SEMANA: "
      "Quando o usuário perguntar que horas são, que dia é hoje, qual o dia da semana ou qualquer pergunta sobre data/hora atual ('que horas são?', 'que dia é hoje?', 'que dia da semana é hoje?', 'qual a data e hora agora?'), CHAME OBRIGATORIAMENTE a ferramenta get_current_datetime antes de responder. Nunca invente ou chute horários ou datas sem consultar get_current_datetime. A resposta de get_current_datetime JÁ CONTÉM a hora e a data locais finais e exatas; fale diretamente a hora e data informadas pela ferramenta, NUNCA aplique recálculo ou conversão de fuso horário por conta própria.";
  if (system_prompt_.length()) {
    instruction += " Additional system prompt from SD/Web UI: ";
    instruction += system_prompt_;
  }
  if (persona_prompt_.length()) {
    instruction += " Persona/style prompt from SD/Web UI: ";
    instruction += persona_prompt_;
  }
  if (memory_) {
    String durableFacts = memory_->factsPreview(1500);
    durableFacts.trim();
    if (durableFacts.length()) {
      instruction += "\n\nFATOS DURÁVEIS CONHECIDOS DA SUA MEMÓRIA (MicroSD):\n";
      instruction += durableFacts;
      Serial.printf("GeminiLive: injected durable facts chars=%u\n", static_cast<unsigned>(durableFacts.length()));
    }
    String activeContext = memory_->buildActiveDialogueContext(3000);
    activeContext.trim();
    if (activeContext.length()) {
      instruction += "\n\nAUTHORITATIVE ACTIVE LOCAL DIALOGUE MEMORY:\n";
      instruction += activeContext;
      Serial.printf("GeminiLive: injected active dialogue context chars=%u\n", static_cast<unsigned>(activeContext.length()));
    }
  }
  sys["parts"].add<JsonObject>()["text"] = instruction;
  String out;
  serializeJson(doc, out);
  transport_.enqueue(OutboundKind::CTRL, out.c_str(), out.length(), transport_epoch_);
}

void GeminiLiveProbe::sendTextTurn(const String& text) {
  if (!isReady()) return;
  recent_dialogue_ring_.onUserTranscript(text, millis());
  // Gemini Live API official schema for client text input turn triggering speech response
  JsonDocument doc;
  auto turns = doc["clientContent"]["turns"].to<JsonArray>();
  auto turn = turns.add<JsonObject>();
  turn["role"] = "user";
  auto parts = turn["parts"].to<JsonArray>();
  auto part = parts.add<JsonObject>();
  part["text"] = text;
  doc["clientContent"]["turnComplete"] = true;
  String out;
  serializeJson(doc, out);
  last_activity_ms_ = millis();
  transport_.enqueue(OutboundKind::CTRL, out.c_str(), out.length(), transport_epoch_);
  Serial.printf("GeminiLive: sent clientContent text turn chars=%u\n", static_cast<unsigned>(text.length()));
}

void GeminiLiveProbe::sendRecentDialogueHistory() {
  if (session_resumed_) return; // Não injeta se a sessão foi retomada com sucesso via sessionResumption
  uint32_t nowMs = millis();
  if (!recent_dialogue_ring_.hasRecentDialogue(nowMs)) return;

  Serial.println("GeminiLive: sending initial history turns via clientContent (roles user/model)");

  JsonDocument doc;
  auto turns = doc["clientContent"]["turns"].to<JsonArray>();

  for (size_t i = 0; i < recent_dialogue_ring_.count(); ++i) {
    const auto* t = recent_dialogue_ring_.getTurn(i);
    if (!t || t->timestampMs == 0 || (nowMs - t->timestampMs >= RecentDialogueRing::kMaxAgeMs)) {
      continue;
    }
    if (t->userText.length() > 0) {
      auto userTurn = turns.add<JsonObject>();
      userTurn["role"] = "user";
      userTurn["parts"].to<JsonArray>().add<JsonObject>()["text"] = t->userText;
    }
    if (t->pixText.length() > 0) {
      auto modelTurn = turns.add<JsonObject>();
      modelTurn["role"] = "model";
      modelTurn["parts"].to<JsonArray>().add<JsonObject>()["text"] = t->pixText;
    }
  }

  if (turns.size() == 0) return;

  // 1. Envia os turnos de histórico com turnComplete = false conforme protocolo
  doc["clientContent"]["turnComplete"] = false;
  String out;
  serializeJson(doc, out);
  last_activity_ms_ = millis();
  transport_.enqueue(OutboundKind::CTRL, out.c_str(), out.length(), transport_epoch_);
  Serial.printf("GeminiLive: sent initial history turns count=%u turnComplete=false\n", static_cast<unsigned>(turns.size()));

  // 2. Finaliza o carregamento do histórico inicial conforme protocolo oficial (turnComplete = true).
  // Com historyConfig.initialHistoryInClientContent ativo no setup, este turnComplete encerra
  // a ingestão de histórico sem provocar resposta vocal do modelo, liberando o início do realtimeInput.
  JsonDocument finishDoc;
  finishDoc["clientContent"]["turnComplete"] = true;
  String finishOut;
  serializeJson(finishDoc, finishOut);
  last_activity_ms_ = millis();
  transport_.enqueue(OutboundKind::CTRL, finishOut.c_str(), finishOut.length(), transport_epoch_);
  Serial.println("GeminiLive: initial history finalized (turnComplete=true); ready for realtimeInput");
}

bool GeminiLiveProbe::requestTextTurn(const String& text) {
  String trimmed = text;
  trimmed.trim();
  if (!trimmed.length()) return false;
  last_activity_ms_ = millis();
  pending_text_ = trimmed;
  pending_text_turn_ = true;
  pending_start_recording_ = false;
  // Text turns can be injected while the user is in a live voice session
  // (for example async gateway/Hermes/weather callbacks). Preserve continuous
  // listening when the text interrupts an already-active dialogue; otherwise
  // StackChan speaks the injected text and falls back to neutral/sleep.
  resume_conversation_after_text_ = continuous_conversation_ || realtime_recording_;
  continuous_conversation_ = resume_conversation_after_text_;
  if (realtime_recording_) stopRealtimeRecord();
  if (!isReady()) {
    Serial.println("GeminiLive: text queued; connecting on demand");
    return connect();
  }
  Serial.println("GeminiLive: sending queued text turn");
  pending_text_turn_ = false;
  response_complete_received_ = false;
  setTurnState(LiveTurnState::WAITING_MODEL, "text turn submitted");
  String toSend = pending_text_;
  pending_text_ = "";
  sendTextTurn(toSend);
  return true;
}

bool GeminiLiveProbe::sendImageTurn(const String& imageBase64, const String& prompt) {
  if (!isReady()) return false;
  if (imageBase64.length() == 0) return false;
  last_activity_ms_ = millis();
  if (realtime_recording_) stopRealtimeRecord();
  if (emotion_) emotion_->setEmotion("looking");

  String promptText = prompt.length() ? prompt : "Look at this snapshot from my camera and answer concisely in the user's language.";
  promptText.replace("\\", "\\\\");
  promptText.replace("\"", "\\\"");
  promptText.replace("\n", "\\n");
  promptText.replace("\r", "\\r");

  String out;
  out.reserve(imageBase64.length() + promptText.length() + 220);
  out = "{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"text\":\"";
  out += promptText;
  out += "\"},{\"inlineData\":{\"mimeType\":\"image/jpeg\",\"data\":\"";
  out += imageBase64;
  out += "\"}}]}],\"turnComplete\":true}}";
  Serial.printf("GeminiLive: sending image turn b64=%u json=%u\n",
                static_cast<unsigned>(imageBase64.length()), static_cast<unsigned>(out.length()));
  bool sent = transport_.enqueue(OutboundKind::LARGE, out.c_str(), out.length(), transport_epoch_);
  return sent;
}

void GeminiLiveProbe::streamAudioDeltaBase64(const char* b64, size_t b64Len) {
  if (!b64 || !b64[0]) return;
  if (b64Len == 0) b64Len = strlen(b64);

  ++api_audio_chunks_received_;

  // ── Step 1: compute expected decoded size before any decode ─────────────
  // Formula: 4 base64 chars → 3 bytes (exact for non-padded streams from
  // the Gemini Live API; padding would only shrink the result).
  const size_t decoded_expected = (b64Len * 3) / 4;

  // ── Step 2: DECODE_BUFFER_BYTES guard ────────────────────────────────────
  if (decoded_expected > DECODE_BUFFER_BYTES) {
    ++api_audio_chunks_dropped_;
    Serial.printf("[WARN:B64_AUDIO] chunk exceeds decode_buf_ (b64Len=%u decoded_expected=%u limit=%u) — dropped\n",
                  static_cast<unsigned>(b64Len),
                  static_cast<unsigned>(decoded_expected),
                  static_cast<unsigned>(DECODE_BUFFER_BYTES));
    return;
  }

  // ── Step 3: ring-space check with cross-delta coalescing ────────────────
  // Check if the most recently written slot has room to absorb the start of this chunk.
  const int last_idx = (audio_queue_write_ + AUDIO_RING_BUFFERS - 1) % AUDIO_RING_BUFFERS;
  const size_t space_in_last = (audio_buffered_count_ > 0 && audio_buf_[last_idx] && audio_buf_len_[last_idx] < AUDIO_BUFFER_BYTES)
                                   ? (AUDIO_BUFFER_BYTES - audio_buf_len_[last_idx]) : 0;
  const size_t coalesce_expected = (decoded_expected < space_in_last) ? decoded_expected : space_in_last;
  const size_t remaining_expected = decoded_expected - coalesce_expected;
  const int required_slots = static_cast<int>((remaining_expected + AUDIO_BUFFER_BYTES - 1) / AUDIO_BUFFER_BYTES);
  const int available_slots = static_cast<int>(AUDIO_RING_BUFFERS) - audio_buffered_count_;

  static uint32_t s_diag_chunk_count = 0;
  ++s_diag_chunk_count;
  if (s_diag_chunk_count % 50 == 1) {
    Serial.printf("[DIAG:B64_AUDIO] b64Len=%u decoded_expected=%u decode_cap=%u required_slots=%d available_slots=%d coalesce_bytes=%u\n",
                  static_cast<unsigned>(b64Len),
                  static_cast<unsigned>(decoded_expected),
                  static_cast<unsigned>(DECODE_BUFFER_BYTES),
                  required_slots,
                  available_slots,
                  static_cast<unsigned>(coalesce_expected));
  }

  if (required_slots > available_slots) {
    ++api_audio_chunks_dropped_;
    ring_blocks_dropped_ += static_cast<uint32_t>(required_slots);
    Serial.printf("[DIAG:RING_DROP] buffered=%u available_slots=%d required_slots=%d dma_depth=%u api_dropped_total=%lu ring_dropped_total=%lu response_complete=%d tool_call_in_flight=%d heap_internal_free=%u largest_internal_block=%u\n",
                  static_cast<unsigned>(audio_buffered_count_),
                  available_slots,
                  required_slots,
                  static_cast<unsigned>(M5.Speaker.isPlaying(1)),
                  static_cast<unsigned long>(api_audio_chunks_dropped_),
                  static_cast<unsigned long>(ring_blocks_dropped_),
                  response_complete_received_ ? 1 : 0,
                  tool_call_in_flight_ ? 1 : 0,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return;
  }

  // ── Step 4: decode base64 directly into decode_buf_ (zero heap alloc) ───
  const int decoded_len = decodeBase64(b64, static_cast<int>(b64Len),
                                       reinterpret_cast<char*>(decode_buf_));
  if (decoded_len <= 0) {
    ++api_audio_chunks_dropped_;
    Serial.printf("[WARN:B64_AUDIO] decodeBase64 returned %d — chunk dropped\n", decoded_len);
    return;
  }

  // ── Step 5: subdivide PCM into ring sub-blocks with cross-delta coalescing ─
  size_t offset = 0;
  const size_t total = static_cast<size_t>((decoded_len / 2) * 2); // 16-bit PCM sample alignment

  // 5a. Top off previous slot if it was partially filled and still queued
  if (audio_buffered_count_ > 0 && audio_buf_[last_idx] && audio_buf_len_[last_idx] < AUDIO_BUFFER_BYTES) {
    const size_t space = AUDIO_BUFFER_BYTES - audio_buf_len_[last_idx];
    const size_t to_copy = (total < space) ? total : space;
    memcpy(audio_buf_[last_idx] + audio_buf_len_[last_idx], decode_buf_, to_copy);
    audio_buf_len_[last_idx] += to_copy;
    audio_buffered_bytes_ += to_copy;
    offset += to_copy;
  }

  // 5b. Subdivide remaining PCM into fresh ring sub-blocks
  while (offset < total) {
    if (!audio_buf_[audio_queue_write_]) break;
    const size_t sub_len = (total - offset > AUDIO_BUFFER_BYTES) ? AUDIO_BUFFER_BYTES : (total - offset);
    memcpy(audio_buf_[audio_queue_write_], decode_buf_ + offset, sub_len);
    audio_buf_len_[audio_queue_write_] = sub_len;
    audio_buffered_bytes_ += sub_len;
    audio_queue_write_ = (audio_queue_write_ + 1) % AUDIO_RING_BUFFERS;
    audio_buffered_count_++;
    ++ring_blocks_enqueued_;
    if (static_cast<uint16_t>(audio_buffered_count_) > max_audio_buffered_) {
      max_audio_buffered_ = static_cast<uint16_t>(audio_buffered_count_);
    }
    offset += sub_len;
  }

  last_audio_chunk_ms_ = millis();
  response_complete_received_ = false; // Fresh chunk arrived, turn is actively ongoing.

  // ── Step 6: first audio chunk of this turn — transition to SPEAKING ──────
  if (!speaking_) {
    // Rule 7: micro-delta sozinho NÃO pode iniciar SPEAKING
    // A micro-delta (e.g. 2-3 bytes or < 256 bytes) arriving after a tool call or at turn start
    // must not prematurely trigger SPEAKING state until sufficient audio has accumulated or response is complete.
    static constexpr size_t kMinSpeakingBytes = 256;
    if (audio_buffered_bytes_ >= kMinSpeakingBytes || response_complete_received_) {
      Serial.printf("GeminiLive: input audio committed -> speaking (buffered_bytes=%u slots=%u)\n",
                    static_cast<unsigned>(audio_buffered_bytes_),
                    static_cast<unsigned>(audio_buffered_count_));
      speaking_ = true;
      speaking_started_ms_ = millis();
      setTurnState(LiveTurnState::SPEAKING, "first model audio chunk received");
      if (emotion_) emotion_->setEmotion("speaking");
      stopRealtimeRecord();
      acquireI2sSpeaker("first model audio chunk received");
      // Reset per-turn playback counters (ring-level: blocks played/dropped by DMA pump)
      audio_chunks_ = 0;
      audio_dropped_ = 0;
      // Reset per-turn API-level counters so speech playback finished log is accurate
      api_audio_chunks_received_ = 1; // this chunk counts
      api_audio_chunks_dropped_ = 0;
      ring_blocks_enqueued_ = static_cast<uint32_t>(required_slots);
      ring_blocks_dropped_ = 0;
      audio_backpressure_wait_ms_ = 0;
      min_audio_buffered_during_play_ = 0xFFFF;
      playback_started_ms_ = 0;
      prebuffer_actual_chunks_ = 0;
      underrun_count_ = 0;
      controlled_rebuffer_count_ = 0;
      rebuffer_wait_count_ = 0;
    } else {
      Serial.printf("[DIAG:MICRO_DELTA] buffered_bytes=%u (below min %u) -> holding in buffer, SPEAKING deferred\n",
                    static_cast<unsigned>(audio_buffered_bytes_),
                    static_cast<unsigned>(kMinSpeakingBytes));
    }
  }
}

void GeminiLiveProbe::startRealtimeRecord() {
  if (!conversation_armed_) {
    Serial.println("GeminiLive: startRealtimeRecord blocked: conversation not armed");
    pending_start_recording_ = false;
    return;
  }
  if (physical_mute_) {
    Serial.println("GeminiLive: record blocked by physical mute");
    pending_start_recording_ = false;
    return;
  }
  last_activity_ms_ = millis();
  if (!isReady()) {
    Serial.println("GeminiLive: record ignored; not ready");
    return;
  }
  if (speaking_ || turn_state_ == LiveTurnState::SPEAKING || stream_aborted_ ||
      audio_buffered_count_ > 0 || M5.Speaker.isPlaying(1) > 0) {
    Serial.printf("GeminiLive: record deferred; audio still draining (speaking=%d aborted=%d buffered=%d dma=%d)\n",
                  speaking_ ? 1 : 0, stream_aborted_ ? 1 : 0,
                  static_cast<int>(audio_buffered_count_), static_cast<int>(M5.Speaker.isPlaying(1)));
    pending_start_recording_ = true;
    return;
  }

  // Audio drains and playback are 100% idle; reset timestamps for upcoming turn before any debounce return
  speaking_started_ms_ = 0;
  playback_started_ms_ = 0;

  // Cooldown de protecao pos-fala: esperar pelo menos 400ms apos o termino do audio
  if (speech_finished_ms_ > 0 && millis() - speech_finished_ms_ < 400) {
    Serial.printf("GeminiLive: record deferred; within post-speech debounce (%lu ms remaining)\n",
                  static_cast<unsigned long>(400 - (millis() - speech_finished_ms_)));
    pending_start_recording_ = true;
    return;
  }

  if (realtime_recording_) {
    if (turn_state_ == LiveTurnState::RETURNING_TO_LISTEN) {
      mic_ready_for_speech_ = true;
      setTurnState(LiveTurnState::LISTENING, "mic already active; returning to listening");
      if (emotion_) emotion_->setEmotion("listening");
      listening_started_ms_ = millis();
      pending_start_recording_ = false;
      Serial.println("[DIAG:LISTEN_RECOVER] recording=1 RETURNING_TO_LISTEN->LISTENING");
    }
    return;
  }

  if (!realtime_recording_) {
    Serial.println("GeminiLive: start realtime recording");
    if (!acquireI2sMic("startRealtimeRecord")) {
      Serial.println("GeminiLive: acquireI2sMic failed; deferring record");
      pending_start_recording_ = true;
      return;
    }
    mic_ready_for_speech_ = true; // Escuta ativa imediatamente
    setTurnState(LiveTurnState::LISTENING, "mic opened");
    if (emotion_) emotion_->setEmotion("listening");

    record_started_ms_ = millis();
    listening_started_ms_ = record_started_ms_;
    last_mic_chunk_end_ms_ = 0;
    stopped_recording_ms_ = 0;
    realtime_recording_ = true;
    mic_dma_in_progress_ = false;
    utterance_input_received_ = false;
  }
}

void GeminiLiveProbe::stopRealtimeRecord(const char* reason) {
  if (!realtime_recording_) {
    return;
  }
  const char* r = (reason && reason[0]) ? reason : "unspecified";
  Serial.printf("GeminiLive: stop realtime recording (reason: %s)\n", r);

  // 1. Terminar a captura local
  realtime_recording_ = false;
  mic_ready_for_speech_ = false;
  mic_dma_in_progress_ = false;
  listening_started_ms_ = 0;
  record_started_ms_ = 0;
  stopped_recording_ms_ = millis();

  // 2. Enviar/enfileirar audioStreamEnd de forma centralizada e assíncrona
  if (!transport_.isConnected() || transport_.getState() != TransportState::OPEN) {
    Serial.printf("[DIAG:AUDIO_STREAM_END] sent=0 reason=not_open trigger=%s\n", r);
  } else {
    static const char kAudioStreamEndJson[] = "{\"realtimeInput\":{\"audioStreamEnd\":true}}";
    bool enqueued = transport_.enqueue(OutboundKind::CTRL, kAudioStreamEndJson, sizeof(kAudioStreamEndJson) - 1, transport_epoch_);
    if (enqueued) {
      Serial.printf("[DIAG:AUDIO_STREAM_END] sent=1 reason=%s\n", r);
    } else {
      Serial.printf("[DIAG:AUDIO_STREAM_END] sent=0 reason=queue_fail trigger=%s\n", r);
    }
  }

  // 3. Atualizar emoção local
  if (emotion_ && !speaking_) {
    emotion_->setEmotion(continuous_conversation_ ? "thinking" : "neutral");
  }
}

void GeminiLiveProbe::playListeningChirp() {
  // Disabled for realtime listening. Future robot sounds should be attached to
  // explicit emotions/tools/boot events, use lower per-effect volume, and never
  // run in the mic-start path.
}

void GeminiLiveProbe::toggleRealtimeRecord() {
  if (realtime_recording_) stopRealtimeRecord();
  else startRealtimeRecord();
}

void GeminiLiveProbe::notifyMicGainChanged(uint8_t oldGain, uint8_t newGain) {
  uint32_t now = millis();
  gain_settle_until_ms_ = now + 2000;
  audio_stats_window_start_ms_ = now;
  audio_stats_rms_count_ = 0;
  audio_stats_sum_rms_ = 0;
  audio_stats_rms_min_ = 65535;
  audio_stats_rms_max_ = 0;
  audio_stats_peak_max_ = 0;
  audio_stats_clip_samples_ = 0;
  audio_stats_total_samples_ = 0;
  local_gate_open_ = false;
  gate_cross_down_logged_ = false;
  noise_gate_hangover_ = 0;
  post_speech_silence_countdown_ = 0;
  Serial.printf("[DIAG:MIC_GAIN_CHANGE] ts=%lu old_gain=%u new_gain=%u settle_until_ms=%lu\n",
                now,
                static_cast<unsigned>(oldGain),
                static_cast<unsigned>(newGain),
                static_cast<unsigned long>(gain_settle_until_ms_));
}

bool GeminiLiveProbe::armConversation(const char* source) {
  if (physical_mute_) {
    Serial.println("GeminiLive: armConversation blocked by physical mute");
    return false;
  }
  if (isDoNotDisturb()) {
    Serial.println("GeminiLive: armConversation blocked by DND");
    return false;
  }

  // Gesto/touch durante SPEAKING: ignorar se ja armed, nao alterar LiveTurnState
  if (speaking_ || turn_state_ == LiveTurnState::SPEAKING) {
    if (conversation_armed_) {
      Serial.println("GeminiLive: armConversation ignored during SPEAKING (already armed)");
      return true;
    }
  }

  bool was_armed = conversation_armed_;
  conversation_armed_ = true;
  armed_timeout_deadline_ms_ = millis() + ARMED_INACTIVITY_TIMEOUT_MS;

  if (source && strcmp(source, "FAST_GESTURE") == 0) {
    ++arm_count_gesture_;
  } else if (source && strcmp(source, "TOUCH") == 0) {
    ++arm_count_touch_;
  }

  Serial.printf("[DIAG:CONVERSATION_ARM] armed=1 source=%s turn_state=%s timestamp=%lu audio_sent_while_disarmed=%lu arm_count_gesture=%lu arm_count_touch=%lu disarm_count_timeout=%lu\n",
                source ? source : "UNKNOWN",
                turnStateString(),
                millis(),
                static_cast<unsigned long>(audio_sent_while_disarmed_),
                static_cast<unsigned long>(arm_count_gesture_),
                static_cast<unsigned long>(arm_count_touch_),
                static_cast<unsigned long>(disarm_count_timeout_));

  // Se ja estava ARMED e ja em LISTENING ou em turno ativo, nao duplicar transicao
  if (was_armed && (turn_state_ == LiveTurnState::LISTENING || isTurnBusy())) {
    return true;
  }

  continuous_conversation_ = true;
  last_activity_ms_ = millis();

  if (!isReady()) {
    pending_start_recording_ = true;
    connect();
    return true;
  }

  if (speaking_ || isTurnBusy()) {
    return true;
  }

  pending_start_recording_ = false;
  startRealtimeRecord();
  return true;
}

void GeminiLiveProbe::disarmConversation(const char* reason) {
  if (!conversation_armed_) return;

  conversation_armed_ = false;
  armed_timeout_deadline_ms_ = 0;
  pending_start_recording_ = false;
  continuous_conversation_ = false;

  if (reason && strcmp(reason, "TIMEOUT") == 0) {
    ++disarm_count_timeout_;
  }

  if (realtime_recording_) {
    stopRealtimeRecord(reason ? reason : "disarm");
  }

  if (turn_state_ != LiveTurnState::SPEAKING && turn_state_ != LiveTurnState::TOOL_CALL) {
    setTurnState(LiveTurnState::IDLE, reason ? reason : "disarmed");
    if (emotion_) emotion_->setEmotion("neutral");
  }

  Serial.printf("[DIAG:CONVERSATION_ARM] armed=0 reason=%s turn_state=%s timestamp=%lu audio_sent_while_disarmed=%lu arm_count_gesture=%lu arm_count_touch=%lu disarm_count_timeout=%lu\n",
                reason ? reason : "UNKNOWN",
                turnStateString(),
                millis(),
                static_cast<unsigned long>(audio_sent_while_disarmed_),
                static_cast<unsigned long>(arm_count_gesture_),
                static_cast<unsigned long>(arm_count_touch_),
                static_cast<unsigned long>(disarm_count_timeout_));
}

bool GeminiLiveProbe::requestConversationStart() {
  return armConversation("TOUCH");
}

void GeminiLiveProbe::stopConversation() {
  disarmConversation("STOP");
  if (!speaking_) disconnect(true);
}

void GeminiLiveProbe::requestSessionEnd(const String& reason) {
  Serial.print("GeminiLive: end_session requested");
  if (reason.length()) {
    Serial.print(" reason=");
    Serial.print(reason);
  }
  Serial.println();
  end_session_requested_ = true;
  end_session_requested_ms_ = millis();
  pending_start_recording_ = false;
  continuous_conversation_ = false;
  if (realtime_recording_) stopRealtimeRecord();
  Serial.printf("[DIAG:SLEEP] trigger=Gemini_tool_sleep speaking=%d millis=%lu\n", (int)speaking_, millis());
  if (emotion_ && !speaking_) emotion_->setEmotion("sleep");
  last_activity_ms_ = millis();
}

void GeminiLiveProbe::recordAndSendAudioChunk() {
  if (!rec_buf_) return;
  if (initial_history_pending_) return; // Bloqueia áudio do mic enquanto o histórico inicial estiver em carga
  if (!mic_ready_for_speech_) mic_ready_for_speech_ = false;
  if (!continuous_conversation_ && millis() - record_started_ms_ > REALTIME_RECORD_TIMEOUT_MS) {
    Serial.println("GeminiLive: realtime recording timeout");
    stopRealtimeRecord();
    return;
  }

  // Step 1: If no DMA chunk is currently running, verify 2ms yield window before launching next chunk
  if (!mic_dma_in_progress_) {
    uint32_t now = millis();
    if (last_mic_chunk_end_ms_ > 0 && (now - last_mic_chunk_end_ms_ < 2)) {
      return; // Non-blocking yield: allow loopTask and IDLE0 to execute during 2ms rest window
    }
    if (!M5.Mic.record(rec_buf_, RT_REC_SAMPLES, RT_REC_SAMPLE_RATE)) {
      ++mic_record_false_count_;
      return;
    }
    last_mic_record_launch_ms_ = now;
    mic_dma_in_progress_ = true;
    return; // Return immediately to yield CPU for touch, display, servo and WiFi tasks!
  }

  // Step 2: If DMA is still filling the hardware buffer, yield CPU immediately
  if (M5.Mic.isRecording()) {
    return;
  }

  // Step 3: DMA completed! rec_buf_ now contains genuine, freshly sampled 62.5ms PCM audio
  mic_dma_in_progress_ = false;
  uint32_t chunkFinishedAt = millis();
  last_mic_chunk_end_ms_ = chunkFinishedAt;
  ++total_mic_chunks_captured_;

  // Periodic diagnostic log (once every 5 seconds while listening)
  if (chunkFinishedAt - last_mic_diag_ms_ >= 5000) {
    last_mic_diag_ms_ = chunkFinishedAt;
    uint32_t listeningDuration = listening_started_ms_ > 0 ? (chunkFinishedAt - listening_started_ms_) / 1000 : 0;
    uint32_t gapMs = last_mic_record_launch_ms_ >= last_mic_chunk_end_ms_ ?
                     (last_mic_record_launch_ms_ - last_mic_chunk_end_ms_) : 0;
    Serial.printf("[DIAG:MIC] listening=%lus chunks_captured=%lu sent=%lu gap=%lums\n",
                  static_cast<unsigned long>(listeningDuration),
                  static_cast<unsigned long>(total_mic_chunks_captured_),
                  static_cast<unsigned long>(mic_chunks_sent_),
                  static_cast<unsigned long>(gapMs));
  }

  int64_t sumSq = 0;
  int peak = 0;
  uint16_t chunk_clipped = 0;
  for (int i = 0; i < RT_REC_SAMPLES; ++i) {
    int v = rec_buf_[i];
    int av = v < 0 ? -v : v;
    if (av > peak) peak = av;
    if (av >= 32760) ++chunk_clipped;
    sumSq += static_cast<int64_t>(v) * static_cast<int64_t>(v);
  }
  last_mic_peak_ = static_cast<uint16_t>(peak > 65535 ? 65535 : peak);
  uint32_t meanSq = static_cast<uint32_t>(sumSq / RT_REC_SAMPLES);
  // Integer sqrt keeps the realtime path lightweight and avoids extra libs.
  uint32_t x = meanSq;
  uint32_t r = 0;
  uint32_t bit = 1UL << 30;
  while (bit > x) bit >>= 2;
  while (bit != 0) {
    if (x >= r + bit) {
      x -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  last_mic_rms_ = static_cast<uint16_t>(r > 65535 ? 65535 : r);

  // If in DND, muted, or conversation not armed, immediately drop chunk and do not stream to cloud
  if (!conversation_armed_ || turn_state_ != LiveTurnState::LISTENING || physical_mute_ || isDoNotDisturb()) {
    ++audio_sent_while_disarmed_;
    return;
  }

  // 1. Noise Floor EMA (Exponential Moving Average) - ONLY when gate is closed!
  if (!local_gate_open_) {
    constexpr float kNoiseFloorAlpha = 0.05f;
    noise_floor_rms_ = (1.0f - kNoiseFloorAlpha) * noise_floor_rms_ + kNoiseFloorAlpha * static_cast<float>(last_mic_rms_);
    if (noise_floor_rms_ < 50.0f) noise_floor_rms_ = 50.0f;
    if (noise_floor_rms_ > 1000.0f) noise_floor_rms_ = 1000.0f;
  }

  uint16_t openTh = static_cast<uint16_t>(noise_floor_rms_ + static_cast<float>(kGateOpenDeltaRms));
  uint16_t closeTh = static_cast<uint16_t>(noise_floor_rms_ + static_cast<float>(kGateCloseDeltaRms));

  // 2. Failsafe Anti-Latch: if gate has been open continuously >= 12s
  if (local_gate_open_ && (millis() - gate_open_started_ms_ >= GATE_MAX_OPEN_MS)) {
    uint32_t dur = millis() - gate_open_started_ms_;
    ++gate_force_close_count_;
    Serial.printf("[DIAG:MIC_GATE] ts=%lu rms=%u peak=%u gate_open=0 event=GATE_FORCE_CLOSE dur_ms=%lu floor=%u openTh=%u closeTh=%u action=FORCE_CLOSE_ANTI_LATCH\n",
                  millis(),
                  static_cast<unsigned>(last_mic_rms_),
                  static_cast<unsigned>(last_mic_peak_),
                  static_cast<unsigned long>(dur),
                  static_cast<unsigned>(noise_floor_rms_),
                  static_cast<unsigned>(openTh),
                  static_cast<unsigned>(closeTh));
    local_gate_open_ = false;
    noise_gate_hangover_ = 0;
  }

  // 3. Adaptive Gate Evaluation with Hysteresis (for telemetry and inactivity deadline only)
  bool voiceActive = false;
  if (!local_gate_open_) {
    voiceActive = (last_mic_rms_ >= openTh);
  } else {
    voiceActive = (last_mic_rms_ > closeTh);
  }

  if (voiceActive) {
    if (!local_gate_open_) {
      local_gate_open_ = true;
      local_gate_was_triggered_ = true;
      gate_cross_down_logged_ = false;
      gate_open_started_ms_ = millis();
      gate_real_chunks_sent_ = 0;
      gate_silence_chunks_injected_ = 0;
      ambient_rms_count_ = 0;
      ambient_window_start_ms_ = 0;
      ++rms_cross_up_count_;
      Serial.printf("[DIAG:MIC_GATE] ts=%lu rms=%u peak=%u gate_open=1 event=RMS_CROSS_UP threshold=%u floor=%u openTh=%u closeTh=%u mic_gain=%u filter=%u state=%s\n",
                    millis(),
                    static_cast<unsigned>(last_mic_rms_),
                    static_cast<unsigned>(last_mic_peak_),
                    static_cast<unsigned>(openTh),
                    static_cast<unsigned>(noise_floor_rms_),
                    static_cast<unsigned>(openTh),
                    static_cast<unsigned>(closeTh),
                    static_cast<unsigned>(M5.Mic.config().magnification),
                    static_cast<unsigned>(M5.Mic.config().noise_filter_level),
                    turnStateString());
    }
    noise_gate_hangover_ = kNoiseGateHangoverChunks;
  } else {
    if (local_gate_open_ && !gate_cross_down_logged_) {
      gate_cross_down_logged_ = true;
      ++rms_cross_down_count_;
      Serial.printf("[DIAG:MIC_GATE] ts=%lu rms=%u peak=%u gate_open=1 event=RMS_CROSS_DOWN threshold=%u floor=%u openTh=%u closeTh=%u hangover=%u state=%s\n",
                    millis(),
                    static_cast<unsigned>(last_mic_rms_),
                    static_cast<unsigned>(last_mic_peak_),
                    static_cast<unsigned>(closeTh),
                    static_cast<unsigned>(noise_floor_rms_),
                    static_cast<unsigned>(openTh),
                    static_cast<unsigned>(closeTh),
                    static_cast<unsigned>(noise_gate_hangover_),
                    turnStateString());
    }
    if (noise_gate_hangover_ > 0) {
      --noise_gate_hangover_;
    } else if (local_gate_open_) {
      local_gate_open_ = false;
      uint32_t gateDuration = millis() - gate_open_started_ms_;
      Serial.printf("[DIAG:MIC_GATE] ts=%lu rms=%u peak=%u gate_open=0 event=GATE_CLOSED dur_ms=%lu real_chunks=%lu silence_chunks=%lu floor=%u mic_gain=%u filter=%u state=%s\n",
                    millis(),
                    static_cast<unsigned>(last_mic_rms_),
                    static_cast<unsigned>(last_mic_peak_),
                    static_cast<unsigned long>(gateDuration),
                    static_cast<unsigned long>(gate_real_chunks_sent_),
                    static_cast<unsigned long>(gate_silence_chunks_injected_),
                    static_cast<unsigned>(noise_floor_rms_),
                    static_cast<unsigned>(M5.Mic.config().magnification),
                    static_cast<unsigned>(M5.Mic.config().noise_filter_level),
                    turnStateString());
    }
  }

  // Continuous Acoustic Distribution and Clipping Tracking (1 window per second)
  if (realtime_recording_ && !speaking_) {
    uint32_t nowStats = millis();
    if (nowStats >= gain_settle_until_ms_) {
      if (audio_stats_window_start_ms_ == 0) {
        audio_stats_window_start_ms_ = nowStats;
        audio_stats_rms_count_ = 0;
        audio_stats_sum_rms_ = 0;
        audio_stats_rms_min_ = 65535;
        audio_stats_rms_max_ = 0;
        audio_stats_peak_max_ = 0;
        audio_stats_clip_samples_ = 0;
        audio_stats_total_samples_ = 0;
      }
      if (audio_stats_rms_count_ < 32) {
        audio_stats_rms_buf_[audio_stats_rms_count_++] = last_mic_rms_;
      }
      audio_stats_sum_rms_ += last_mic_rms_;
      if (last_mic_rms_ < audio_stats_rms_min_) audio_stats_rms_min_ = last_mic_rms_;
      if (last_mic_rms_ > audio_stats_rms_max_) audio_stats_rms_max_ = last_mic_rms_;
      if (last_mic_peak_ > audio_stats_peak_max_) audio_stats_peak_max_ = last_mic_peak_;
      audio_stats_clip_samples_ += chunk_clipped;
      audio_stats_total_samples_ += RT_REC_SAMPLES;

      if (nowStats - audio_stats_window_start_ms_ >= 1000) {
        if (audio_stats_rms_count_ > 0) {
          // Insertion sort on tiny buffer (<=32 items) for p50 and p95
          for (uint8_t i = 0; i < audio_stats_rms_count_; ++i) {
            uint16_t v = audio_stats_rms_buf_[i];
            uint8_t j = i;
            while (j > 0 && audio_stats_rms_buf_[j - 1] > v) {
              audio_stats_rms_buf_[j] = audio_stats_rms_buf_[j - 1];
              --j;
            }
            audio_stats_rms_buf_[j] = v;
          }
          uint16_t rms_avg = static_cast<uint16_t>(audio_stats_sum_rms_ / audio_stats_rms_count_);
          uint8_t p50_idx = audio_stats_rms_count_ / 2;
          uint16_t rms_p50 = audio_stats_rms_buf_[p50_idx];
          uint8_t p95_idx = (static_cast<uint16_t>(audio_stats_rms_count_) * 95) / 100;
          if (p95_idx >= audio_stats_rms_count_) p95_idx = audio_stats_rms_count_ - 1;
          uint16_t rms_p95 = audio_stats_rms_buf_[p95_idx];
          float clip_pct = audio_stats_total_samples_ > 0 ?
              (static_cast<float>(audio_stats_clip_samples_) * 100.0f / audio_stats_total_samples_) : 0.0f;

          Serial.printf("[DIAG:AUDIO_STATS] timestamp=%lu gain=%u rms_min=%u rms_avg=%u rms_p50=%u rms_p95=%u rms_max=%u peak_max=%u clipping_samples=%lu clipping_pct=%.2f sample_count=%lu\n",
                        nowStats,
                        static_cast<unsigned>(M5.Mic.config().magnification),
                        audio_stats_rms_min_,
                        rms_avg,
                        rms_p50,
                        rms_p95,
                        audio_stats_rms_max_,
                        audio_stats_peak_max_,
                        static_cast<unsigned long>(audio_stats_clip_samples_),
                        clip_pct,
                        static_cast<unsigned long>(audio_stats_total_samples_));
        }
        audio_stats_window_start_ms_ = nowStats;
        audio_stats_rms_count_ = 0;
        audio_stats_sum_rms_ = 0;
        audio_stats_rms_min_ = 65535;
        audio_stats_rms_max_ = 0;
        audio_stats_peak_max_ = 0;
        audio_stats_clip_samples_ = 0;
        audio_stats_total_samples_ = 0;
      }
    }
  }

  // Audio transmission logic:
  // Continuous PCM streaming: transmit PCM continuously to Gemini while conversation_armed_ and realtime_recording_ in LISTENING.
  // [HEAP1] Zero-dynamic-allocation hot path: encodeBase64ToBuffer into reusable audio_b64_buf_, then sendRealtimeAudioChunk.
  size_t b64_len = 0;
  if (audio_b64_buf_) {
    b64_len = encodeBase64ToBuffer(reinterpret_cast<const uint8_t*>(rec_buf_),
                                   RT_REC_SAMPLES * sizeof(int16_t),
                                   audio_b64_buf_,
                                   AUDIO_B64_BUFFER_BYTES);
  }
  if (b64_len > 0) {
    sendRealtimeAudioChunk(audio_b64_buf_, b64_len);
    ++mic_chunks_sent_;
    if (local_gate_open_) {
      ++gate_real_chunks_sent_;
    }
    if (!mic_ready_for_speech_) {
      mic_ready_for_speech_ = true;
      Serial.println("GeminiLive: mic ready for speech (continuous streaming active)");
      if (emotion_ && realtime_recording_ && !speaking_) emotion_->setEmotion("listening");
    }
  }

  // Step 4: Next DMA chunk is NOT launched immediately here.
  // Instead, mic_dma_in_progress_ remains false, and on the next ticks of loop(),
  // Step 1 will enforce the 2ms rest window before launching M5.Mic.record().
  // This guarantees mic_task enters ulTaskNotifyTake(pdTRUE, portMAX_DELAY),
  // freeing CPU 0 for IDLE0 to feed the task watchdog timer.
}

void GeminiLiveProbe::sendRealtimeAudioChunk(const char* b64, size_t b64_len) {
  if (!conversation_armed_ || turn_state_ != LiveTurnState::LISTENING || physical_mute_ || isDoNotDisturb()) {
    ++audio_sent_while_disarmed_;
    return;
  }
  if (!audio_json_buf_ || !b64 || b64_len == 0) return;

  static const char kJsonPrefix[] = "{\"realtimeInput\":{\"audio\":{\"data\":\"";
  static constexpr size_t kPrefixLen = sizeof(kJsonPrefix) - 1; // 33 bytes
  static const char kJsonSuffix[] = "\",\"mimeType\":\"audio/pcm;rate=16000\"}}}";
  static constexpr size_t kSuffixLen = sizeof(kJsonSuffix) - 1; // 36 bytes

  size_t total_len = kPrefixLen + b64_len + kSuffixLen;
  if (total_len + 1 > AUDIO_JSON_BUFFER_BYTES) return;

  memcpy(audio_json_buf_, kJsonPrefix, kPrefixLen);
  memcpy(audio_json_buf_ + kPrefixLen, b64, b64_len);
  memcpy(audio_json_buf_ + kPrefixLen + b64_len, kJsonSuffix, kSuffixLen);
  audio_json_buf_[total_len] = '\0';

  transport_.enqueue(OutboundKind::AUDIO, audio_json_buf_, total_len, transport_epoch_);
}

void GeminiLiveProbe::sendRealtimeAudioBase64(const String& b64) {
  sendRealtimeAudioChunk(b64.c_str(), b64.length());
}

size_t GeminiLiveProbe::encodeBase64ToBuffer(const uint8_t* input, size_t size, char* out, size_t max_out) {
  if (!input || !out || size == 0 || max_out < 4) return 0;
  size_t olen = 0;
  int rc = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out), max_out, &olen, input, size);
  if (rc != 0 || olen >= max_out) {
    return 0;
  }
  out[olen] = '\0';
  return olen;
}

String GeminiLiveProbe::encodeBase64(const uint8_t* input, size_t size) {
  size_t out_len = ((size + 2) / 3) * 4 + 1;
  char* out = static_cast<char*>(malloc(out_len));
  if (!out) return String();
  size_t olen = encodeBase64ToBuffer(input, size, out, out_len);
  if (olen == 0) {
    free(out);
    return String();
  }
  String encoded(out);
  free(out);
  return encoded;
}

void GeminiLiveProbe::handleDisconnectEvent() {
  Serial.println("GeminiLive: websocket disconnected");
  // Check if there is active audio still playing/buffered locally
  bool has_active_playback = speaking_ || audio_buffered_count_ > 0 ||
                             (M5.Speaker.isRunning() && M5.Speaker.isPlaying(1));
  if (has_active_playback) {
    stream_aborted_ = true;
    stream_aborted_ms_ = millis();
    if (audio_chunks_ == 0) {
      audio_buffered_count_ = 0;
      audio_queue_read_ = audio_queue_write_;
    }
    Serial.printf("GeminiLive: stream aborted during playback (buffered=%u dma=%u chunks=%lu) -> preserving speaker drain\n",
                  static_cast<unsigned>(audio_buffered_count_),
                  static_cast<unsigned>(M5.Speaker.isPlaying(1)),
                  static_cast<unsigned long>(audio_chunks_));
  }

  // A Live websocket may close after silence or network timeout. For the
  // robot UX, an active conversation that drops should fall back to sleep
  // instead of staring with a sticky red error face.
  bool was_active = realtime_recording_ || continuous_conversation_ ||
                    pending_start_recording_ || pending_text_turn_ || has_active_playback;
  bool should_sleep = !intentional_disconnect_ && was_active;
  if (tool_bridge_) {
    tool_bridge_->invalidatePendingLocalAction();
  }
  utterance_input_received_ = false;
  connected_ = false;
  setup_complete_ = false;
  setup_wait_started_ms_ = 0;
  mic_ready_for_speech_ = false;
  realtime_recording_ = false;
  if (!has_active_playback) {
    speaking_ = false;
    speaking_started_ms_ = 0;
    playback_started_ms_ = 0;
    setTurnState(LiveTurnState::IDLE, "ws disconnected");
    Serial.printf("[DIAG:SLEEP] ws_disconnected was_active=%d should_sleep=%d intentional=%d emo=%s millis=%lu\n",
                  (int)was_active, (int)should_sleep, (int)intentional_disconnect_,
                  should_sleep ? "sleep" : intentional_disconnect_emotion_.c_str(), millis());
    if (emotion_) emotion_->setEmotion(should_sleep ? "sleep" : intentional_disconnect_emotion_.c_str());
  }
  continuous_conversation_ = false;
  cancelWaitingModelGuard("ws disconnected");
  waiting_model_aborted_turn_ = false;
  pending_start_recording_ = false;
  pending_text_turn_ = false;
  resume_conversation_after_text_ = false;
  pending_text_ = "";
  connect_requested_ = false;
  initial_history_configured_ = false;
  initial_history_pending_ = false;

  // Se a tentativa de retomada de sessão foi rejeitada ou falhou antes do setupComplete,
  // descarta o handle para que a próxima conexão seja limpa
  if (resumption_attempted_ && !session_resumed_) {
    Serial.println("GeminiLive: session resumption rejected by server; discarding handle for fresh session");
    session_resumption_handle_ = "";
    session_resumption_saved_ms_ = 0;
    resumption_attempted_ = false;
  }

  // Auto-reconnect if drop was unintentional, WiFi is connected, and not in DND
  if (!intentional_disconnect_ && was_active && WiFi.status() == WL_CONNECTED && !isDoNotDisturb()) {
    Serial.println("GeminiLive: unexpected disconnect during conversation -> scheduling auto-reconnect");
    continuous_conversation_ = true;
    pending_start_recording_ = true;
    reconnect_requested_ = true;
  }
}

void GeminiLiveProbe::drainTransportEvents() {
  uint32_t start_ms = millis();
  int events_processed = 0;

  while (events_processed < 2 && (millis() - start_ms < 8)) {
    size_t item_size = 0;
    void* item = transport_.receiveInbound(&item_size);
    if (!item) break;

    const InboundEventHeader* hdr = static_cast<const InboundEventHeader*>(item);
    if (hdr->epoch == transport_epoch_) {
      uint8_t* payload = static_cast<uint8_t*>(item) + sizeof(InboundEventHeader);
      size_t len = hdr->length;

      switch (hdr->type) {
        case InboundEventType::CONNECTED:
          Serial.println("GeminiLive: websocket connected");
          connected_ = true;
          connect_requested_ = true;
          last_activity_ms_ = millis();
          connect_started_ms_ = 0;
          setup_wait_started_ms_ = millis();
          sendSetup();
          break;

        case InboundEventType::TEXT_FRAME: {
          uint32_t ws_t0 = millis();
          Serial.printf("[DIAG:STACK] HWM_WS=%u type=TEXT len=%u\n",
                        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                        static_cast<unsigned>(len));
          handleMessage(payload, len);
          uint32_t ws_elapsed = millis() - ws_t0;
          if (ws_elapsed > 50 || len > 10000) {
            Serial.printf("[DIAG:FRAME] type=TEXT len=%u handleMsg_ms=%u\n",
                          static_cast<unsigned>(len), static_cast<unsigned>(ws_elapsed));
          }
          break;
        }

        case InboundEventType::BINARY_FRAME: {
          uint32_t ws_t0 = millis();
          Serial.printf("[DIAG:STACK] HWM_WS=%u type=BIN len=%u\n",
                        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                        static_cast<unsigned>(len));
          handleMessage(payload, len);
          uint32_t ws_elapsed = millis() - ws_t0;
          if (ws_elapsed > 50 || len > 10000) {
            Serial.printf("[DIAG:FRAME] type=BIN len=%u handleMsg_ms=%u\n",
                          static_cast<unsigned>(len), static_cast<unsigned>(ws_elapsed));
          }
          break;
        }

        case InboundEventType::DISCONNECTED:
          handleDisconnectEvent();
          break;

        case InboundEventType::ERROR_FRAME:
          Serial.print("GeminiLive: websocket error");
          if (payload && len) {
            Serial.print(" payload=");
            Serial.write(payload, len > 160 ? 160 : len);
          }
          Serial.println();
          break;

        case InboundEventType::PONG_FRAME:
          Serial.println("GeminiLive: websocket pong");
          break;
      }
    }

    transport_.returnInboundItem(item);
    events_processed++;
  }
}

void GeminiLiveProbe::handleMessage(uint8_t* payload, size_t length) {
  Serial.printf("[DIAG:STACK] HWM_HANDLE_MSG=%u\n",
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

  // ── Phase 1: deserializeJson ──────────────────────────────────────────────
  uint32_t t_json_start = millis();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  uint32_t t_json_ms = millis() - t_json_start;
  if (err) {
    Serial.printf("GeminiLive: json parse error=%s length=%u\n", err.c_str(), static_cast<unsigned>(length));
    return;
  }

  // Quick type sniff to decide what phases follow
  bool hasAudio    = doc["serverContent"]["modelTurn"]["parts"][0]["inlineData"]["data"].is<const char*>()
                     && doc["serverContent"]["modelTurn"]["parts"][0]["inlineData"]["data"].as<const char*>()[0];
  bool hasOutTx    = (doc["serverContent"]["outputTranscription"]["text"].is<const char*>() &&
                      doc["serverContent"]["outputTranscription"]["text"].as<const char*>()[0]) ||
                     (doc["serverContent"]["output_transcription"]["text"].is<const char*>() &&
                      doc["serverContent"]["output_transcription"]["text"].as<const char*>()[0]);
  bool hasInTx     = (doc["serverContent"]["inputTranscription"]["text"].is<const char*>() &&
                      doc["serverContent"]["inputTranscription"]["text"].as<const char*>()[0]) ||
                     (doc["serverContent"]["input_transcription"]["text"].is<const char*>() &&
                      doc["serverContent"]["input_transcription"]["text"].as<const char*>()[0]);
  bool hasTool     = !doc["toolCall"].isNull();
  bool hasComplete = !doc["serverContent"]["turnComplete"].isNull() ||
                     !doc["serverContent"]["generationComplete"].isNull();
  bool hasModelTurn = !doc["serverContent"]["modelTurn"].isNull();

  if (hasAudio || hasModelTurn || hasOutTx || hasTool || hasComplete) {
    cancelWaitingModelGuard("valid server content");
  }

  // Only emit detailed breakdown for large frames or frames containing significant content
  bool verbose = (length > 8000) || t_json_ms > 30;

  if (verbose) {
    Serial.printf("[DIAG:FRAME_DETAIL] len=%u json_ms=%u audio=%d outTx=%d inTx=%d tool=%d complete=%d\n",
                  static_cast<unsigned>(length), static_cast<unsigned>(t_json_ms),
                  hasAudio ? 1 : 0, hasOutTx ? 1 : 0, hasInTx ? 1 : 0,
                  hasTool ? 1 : 0, hasComplete ? 1 : 0);
  }

  // 1. setupComplete is ALWAYS processed (never blocked by aborted turn)
  JsonVariant setupComplete = doc["setupComplete"];
  if (!setupComplete.isNull()) {
    setup_wait_started_ms_ = 0;
    cancelWaitingModelGuard("setupComplete");
    waiting_model_aborted_turn_ = false;
    Serial.println("GeminiLive: setupComplete");
    setup_complete_ = true;
    last_activity_ms_ = millis();
    Serial.println("Status: Gemini setup ok");
    if (resumption_attempted_) {
      session_resumed_ = true;
      initial_history_configured_ = false;
      initial_history_pending_ = false;
      Serial.println("GeminiLive: session resumption confirmed by server (setupComplete)");
    } else {
      session_resumed_ = false;
      if (initial_history_configured_) {
        sendRecentDialogueHistory();
        initial_history_pending_ = false;
      }
    }
    if (pending_text_turn_) {
      String toSend = pending_text_;
      pending_text_turn_ = false;
      pending_text_ = "";
      pending_start_recording_ = false;
      response_complete_received_ = false;
      setTurnState(LiveTurnState::WAITING_MODEL, "pending text turn submitted");
      Serial.println("GeminiLive: setupComplete; sending pending text turn");
      sendTextTurn(toSend);
    } else if (pending_start_recording_) {
      if (!conversation_armed_) {
        Serial.println("GeminiLive: setupComplete while disarmed; ignoring pending record");
        pending_start_recording_ = false;
        setTurnState(LiveTurnState::IDLE, "reconnect while disarmed");
      } else if (armed_timeout_deadline_ms_ > 0 && millis() > armed_timeout_deadline_ms_) {
        Serial.println("GeminiLive: setupComplete but armed timeout expired; disarming");
        disarmConversation("TIMEOUT");
      } else if (stream_aborted_ || speaking_ || audio_buffered_count_ > 0 || M5.Speaker.isPlaying(1) > 0) {
        Serial.println("GeminiLive: setupComplete while audio still draining; deferring startRealtimeRecord");
      } else {
        pending_start_recording_ = false;
        startRealtimeRecord();
      }
    }
    return;
  }

  // 2. sessionResumptionUpdate is ALWAYS processed (never blocked by aborted turn)
  if (doc["sessionResumptionUpdate"].is<JsonObject>()) {
    JsonObject sru = doc["sessionResumptionUpdate"].as<JsonObject>();
    const char* newHandle = sru["newHandle"] | "";
    if (sru["resumable"] | false) {
      if (newHandle && newHandle[0]) {
        session_resumption_handle_ = String(newHandle);
        session_resumption_saved_ms_ = millis();
        Serial.printf("GeminiLive: session resumption handle: %s\n", newHandle);
      }
    }
  }

  // 3. goAway warning is ALWAYS processed (never blocked by aborted turn)
  if (doc["goAway"].is<JsonObject>()) {
    const char* tl = doc["goAway"]["timeLeft"] | "";
    Serial.printf("GeminiLive: server sent goAway warning (timeLeft=%s)\n", tl);
  }

  // 4. Filter late serverContent from aborted turn (drops late audio/speech from discarded turn)
  if (waiting_model_aborted_turn_) {
    if (hasComplete) {
      waiting_model_aborted_turn_ = false;
      Serial.println("GeminiLive: aborted turn completed on server; cleared abort flag");
    } else {
      Serial.println("GeminiLive: dropping late server content from aborted turn");
    }
    return;
  }

  JsonVariant serverContent = doc["serverContent"];

  // ── Phase 1a: Native Google Search / Grounding detection ─────────────────
  JsonVariant gm = serverContent["groundingMetadata"];
  if (gm.isNull()) gm = serverContent["grounding_metadata"];
  if (gm.isNull()) gm = serverContent["modelTurn"]["groundingMetadata"];
  if (gm.isNull()) gm = serverContent["modelTurn"]["grounding_metadata"];
  if (gm.isNull()) gm = doc["groundingMetadata"];
  if (gm.isNull()) gm = doc["grounding_metadata"];

  if (!gm.isNull()) {
    JsonArray queries = gm["webSearchQueries"].as<JsonArray>();
    if (queries.isNull()) queries = gm["web_search_queries"].as<JsonArray>();

    JsonArray chunks = gm["groundingChunks"].as<JsonArray>();
    if (chunks.isNull()) chunks = gm["grounding_chunks"].as<JsonArray>();

    JsonArray supports = gm["groundingSupports"].as<JsonArray>();
    if (supports.isNull()) supports = gm["grounding_supports"].as<JsonArray>();

    JsonVariant sep = gm["searchEntryPoint"];
    if (sep.isNull()) sep = gm["search_entry_point"];

    size_t qCount = queries.isNull() ? 0 : queries.size();
    size_t chunkCount = chunks.isNull() ? 0 : chunks.size();
    size_t supportCount = supports.isNull() ? 0 : supports.size();
    bool hasSep = !sep.isNull();

    if (qCount > 0 || chunkCount > 0 || supportCount > 0 || hasSep) {
      Serial.println("[SEARCH] native Google Search used");
      Serial.println("[SEARCH:GROUNDING] detected");
      if (qCount > 0) {
        String qStr = "";
        for (JsonVariantConst q : queries) {
          const char* qs = q.as<const char*>();
          if (qs && qs[0]) {
            if (qStr.length() > 0) qStr += ", ";
            qStr += qs;
          }
        }
        if (qStr.length() > 120) {
          qStr = qStr.substring(0, 117) + "...";
        }
        Serial.printf("[SEARCH:GROUNDING] queries=%s\n", qStr.c_str());
      }
      if (chunkCount > 0) {
        Serial.printf("[SEARCH:GROUNDING] chunks=%u\n", static_cast<unsigned>(chunkCount));
      }
      if (supportCount > 0) {
        Serial.printf("[SEARCH:GROUNDING] supports=%u\n", static_cast<unsigned>(supportCount));
      }
      if (hasSep) {
        Serial.println("[SEARCH:GROUNDING] searchEntryPoint present");
      }
    }
  }

  // ── Phase 1b: Cloud VAD activity detection ───────────────────────────────
  const char* interimText = serverContent["interimInputTranscription"]["text"];
  if (!interimText) interimText = serverContent["interim_input_transcription"]["text"];
  const char* inTxText = serverContent["inputTranscription"]["text"];
  if (!inTxText) inTxText = serverContent["input_transcription"]["text"];
  bool cloudWaitingInput = serverContent["waitingForInput"] | false;

  if ((interimText && interimText[0]) || (inTxText && inTxText[0]) || cloudWaitingInput) {
    if (conversation_armed_ && turn_state_ == LiveTurnState::LISTENING) {
      armed_timeout_deadline_ms_ = millis() + ARMED_INACTIVITY_TIMEOUT_MS;
    }
    speech_start_seen_ = true;
    if (inTxText && inTxText[0]) speech_end_seen_ = true;
    Serial.printf("[DIAG:CLOUD_VAD] ts=%lu speech_start=1 speech_end=%d turnId=%lu local_gate_open=%d rms=%u peak=%u ms_since_gate=%lu event=%s\n",
                  millis(),
                  (inTxText && inTxText[0]) ? 1 : 0,
                  static_cast<unsigned long>(currentUserTurnId_),
                  local_gate_open_ ? 1 : 0,
                  static_cast<unsigned>(last_mic_rms_),
                  static_cast<unsigned>(last_mic_peak_),
                  static_cast<unsigned long>(gate_open_started_ms_ > 0 ? (millis() - gate_open_started_ms_) : 0),
                  (inTxText && inTxText[0]) ? "inputTranscription" : ((interimText && interimText[0]) ? "interimInputTranscription" : "waitingForInput"));
  }

  // ── Phase 2: transcription handling ──────────────────────────────────────
  uint32_t t_tx_start = millis();
  handleTranscription(serverContent);
  uint32_t t_tx_ms = millis() - t_tx_start;

  // Detect when model begins formulating response / verbal turn:
  // Immediately ensure mic is closed to prevent local room noise from contaminating the stream
  const char* outText = serverContent["outputTranscription"]["text"];
  if (!outText) outText = serverContent["output_transcription"]["text"];
  if ((hasModelTurn || (outText && outText[0])) && turn_state_ != LiveTurnState::SPEAKING) {
    if (realtime_recording_) {
      Serial.println("GeminiLive: model response starting -> closing mic immediately");
      setTurnState(LiveTurnState::WAITING_MODEL, "modelTurn / outputTranscription detected");
      stopRealtimeRecord();
    }
  }

  // ── Phase 3: Base64 audio decode + ring enqueue ───────────────────────────
  uint32_t t_b64_start = millis();
  const char* data = doc["serverContent"]["modelTurn"]["parts"][0]["inlineData"]["data"];
  size_t b64_len = data ? strlen(data) : 0;
  if (data && data[0]) streamAudioDeltaBase64(data);
  uint32_t t_b64_ms = millis() - t_b64_start;

  // User speech interruption / barge-in
  if (doc["serverContent"]["interrupted"] | false) {
    // DIAGNOSTIC LOGGING: Log detailed state whenever server sends interrupted:true
    Serial.printf("GeminiLive: [INTERRUPT EVAL] interrupted=true micRecording=%d micReady=%d lastRms=%u speaking=%d audioBuffered=%d audioChunks=%lu state=%s -> IGNORED (speech stability priority)\n",
                  realtime_recording_ ? 1 : 0,
                  mic_ready_for_speech_ ? 1 : 0,
                  static_cast<unsigned>(last_mic_rms_),
                  speaking_ ? 1 : 0,
                  audio_buffered_count_,
                  static_cast<unsigned long>(audio_chunks_),
                  turnStateString());
    // In speech stability priority mode, serverContent.interrupted is ignored while model is formulating or speaking.
  }

  bool isToolCall = !doc["toolCall"].isNull();
  bool responseComplete = !isToolCall &&
                          (!doc["serverContent"]["turnComplete"].isNull() ||
                           !doc["serverContent"]["generationComplete"].isNull());
  if (responseComplete) {
    flushOutputTranscript();
    response_complete_received_ = true;
    last_response_complete_ms_ = millis();
    if (!speaking_ && audio_buffered_bytes_ > 0) {
      Serial.printf("GeminiLive: responseComplete with uncommitted audio (%u bytes) -> speaking\n",
                    static_cast<unsigned>(audio_buffered_bytes_));
      speaking_ = true;
      speaking_started_ms_ = millis();
      setTurnState(LiveTurnState::SPEAKING, "responseComplete with buffered audio");
      if (emotion_) emotion_->setEmotion("speaking");
      stopRealtimeRecord();
      acquireI2sSpeaker("responseComplete with buffered audio");
    }
    Serial.printf("GeminiLive: responseComplete received (buffered=%d queued_chunks=%lu)\n",
                  audio_buffered_count_,
                  static_cast<unsigned long>(audio_chunks_));
  }

  // ── Phase 4: tool call handling ───────────────────────────────────────────
  uint32_t t_tool_ms = 0;
  JsonArray functionCalls = doc["toolCall"]["functionCalls"].as<JsonArray>();
  if (!functionCalls.isNull() && tool_bridge_) {
    uint32_t t_tool_start = millis();
    tool_call_in_flight_ = true;
    Serial.printf("[DIAG:TOOL_START] count=%u dma_depth=%u buffered=%u internal_free=%u largest_block=%u\n",
                  static_cast<unsigned>(functionCalls.size()),
                  static_cast<unsigned>(M5.Speaker.isPlaying(1)),
                  static_cast<unsigned>(audio_buffered_count_),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    setTurnState(LiveTurnState::TOOL_CALL, "toolCall received from model");
    if (realtime_recording_) stopRealtimeRecord();
    if (emotion_ && !speaking_) emotion_->setEmotion("thinking");
    JsonDocument responseDoc;
    // Gemini Live API docs use camelCase for tool responses:
    // {"toolResponse":{"functionResponses":[...]}}
    // The older AI_StackChan_Ex path used snake_case, but camelCase avoids the
    // model waiting indefinitely after a tool call on current Live API.
    auto functionResponses = responseDoc["toolResponse"]["functionResponses"].to<JsonArray>();
    for (JsonObject fc : functionCalls) {
      const char* name = fc["name"] | "";
      const char* id = fc["id"] | "";
      Serial.printf("[DIAG:STACK] HWM_BEFORE_TOOL_CALL=%u tool=%s\n",
                    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), name);
      Serial.printf("GeminiLive: toolCall name=%s\n", name);
      String toolName(name);
      String result = tool_bridge_->handleFunctionCall(toolName, fc["args"].as<JsonVariantConst>(), String(id));
      Serial.printf("[DIAG:STACK] HWM_AFTER_TOOL_CALL=%u tool=%s\n",
                    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), name);
      JsonDocument probeDoc;
      bool resultOk = deserializeJson(probeDoc, result) == DeserializationError::Ok &&
                      probeDoc["ok"].is<bool>() && probeDoc["ok"].as<bool>();
      if (probeDoc["ok"].is<bool>() && !probeDoc["ok"].as<bool>()) {
        if (emotion_) emotion_->setEmotion("error");
      }
      if (toolName == "end_session" && resultOk) {
        requestSessionEnd(String((const char*)(fc["args"]["reason"] | "")));
      }
      auto fr = functionResponses.add<JsonObject>();
      if (id && id[0]) fr["id"] = id;
      fr["name"] = name;
      JsonDocument resultDoc;
      if (deserializeJson(resultDoc, result) == DeserializationError::Ok) {
        fr["response"].set(resultDoc.as<JsonVariantConst>());
      } else {
        fr["response"]["text"] = result;
      }
    }
    Serial.printf("[DIAG:STACK] HWM_BEFORE_SEND_TOOL_RESP=%u\n",
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    String out;
    serializeJson(responseDoc, out);
    response_complete_received_ = false; // Awaiting model's verbal turn responding to tool!

    uint32_t send_start = millis();
    bool is_ws_connected = connected_ && transport_.isConnected();
    bool is_spk_playing = speaking_ || (M5.Speaker.isRunning() && M5.Speaker.isPlaying(1) > 0);
    uint8_t buf_count = audio_buffered_count_;
    uint32_t free_h = ESP.getFreeHeap();
    uint32_t max_block = ESP.getMaxAllocHeap();

    bool send_ok = transport_.enqueue(OutboundKind::CTRL, out.c_str(), out.length(), transport_epoch_);
    uint32_t send_end = millis();
    uint32_t send_dur = send_end - send_start;

    tool_call_in_flight_ = false;
    t_tool_ms = millis() - t_tool_start;

    Serial.printf("[DIAG:TOOL_END] dur_ms=%u send_ms=%u ok=%d ws_conn=%d spk_play=%d buf=%u internal_free=%u largest_block=%u\n",
                  static_cast<unsigned>(t_tool_ms),
                  static_cast<unsigned>(send_dur),
                  send_ok ? 1 : 0,
                  is_ws_connected ? 1 : 0,
                  is_spk_playing ? 1 : 0,
                  static_cast<unsigned>(buf_count),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));

    Serial.printf("[DIAG:STACK] HWM_AFTER_SEND_TOOL_RESP=%u\n",
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }

  // ── Phase 5: Summary for large or slow frames ─────────────────────────────
  if (verbose || t_b64_ms > 10 || t_tool_ms > 10) {
    Serial.printf("[DIAG:FRAME_TIMING] len=%u json_ms=%u tx_ms=%u b64_ms=%u b64_len=%u tool_ms=%u\n",
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(t_json_ms),
                  static_cast<unsigned>(t_tx_ms),
                  static_cast<unsigned>(t_b64_ms),
                  static_cast<unsigned>(b64_len),
                  static_cast<unsigned>(t_tool_ms));
  }
}

void GeminiLiveProbe::handleTranscription(JsonVariant serverContent) {
  if (serverContent.isNull()) return;

  const char* inputText = serverContent["inputTranscription"]["text"];
  if (!inputText) inputText = serverContent["input_transcription"]["text"];
  if (inputText && inputText[0]) {
    record_started_ms_ = millis(); // Refresh silence timeout while user is talking

    // Check if this is a genuinely new user transcript utterance (ignore repeat frames within the same utterance)
    String currentInput = String(inputText);
    currentInput.trim();
    if (currentInput.length() > 0 && !utterance_input_received_) {
      utterance_input_received_ = true;
      ++currentUserTurnId_;

      // Invalidate any pending local action from a previous turn
      if (tool_bridge_) {
        tool_bridge_->invalidatePendingLocalAction();
      }

      Serial.printf("GeminiLive: input transcript chars=%u (turnId=%u): %s\n",
                    static_cast<unsigned>(currentInput.length()),
                    static_cast<unsigned>(currentUserTurnId_),
                    currentInput.c_str());
      Serial.printf("[DIAG:INPUT_TRANSCRIPT] turnId=%lu gain=%u text=\"%s\" len=%u rms=%u peak=%u gate_triggered=%d speech_start_seen=%d speech_end_seen=%d silence_injected=%lu\n",
                    static_cast<unsigned long>(currentUserTurnId_),
                    static_cast<unsigned>(M5.Mic.config().magnification),
                    currentInput.c_str(),
                    static_cast<unsigned>(currentInput.length()),
                    static_cast<unsigned>(last_mic_rms_),
                    static_cast<unsigned>(last_mic_peak_),
                    local_gate_was_triggered_ ? 1 : 0,
                    speech_start_seen_ ? 1 : 0,
                    speech_end_seen_ ? 1 : 0,
                    static_cast<unsigned long>(gate_silence_chunks_injected_));
      if (memory_) memory_->appendDialogue("user", currentInput, "live_input_transcription");
      recent_dialogue_ring_.onUserTranscript(currentInput, millis());

      // Strict matching for unambiguous physical head gestures (look_left, look_right, center_head)
      // Normalize: lowercase, strip trailing punctuation, collapse spaces
      String norm = currentInput;
      norm.toLowerCase();
      while (norm.length() > 0) {
        char lastChar = norm[norm.length() - 1];
        if (lastChar == '.' || lastChar == '!' || lastChar == '?' || lastChar == ',' ||
            lastChar == ';' || lastChar == ':' || lastChar == ' ' || lastChar == '\t' ||
            lastChar == '\r' || lastChar == '\n') {
          norm.remove(norm.length() - 1);
        } else {
          break;
        }
      }
      norm.trim();

      const char* fastPathCmd = nullptr;
      const char* fastPathHold = "normal";
      bool hasEsquerda = (norm.indexOf("esquerda") >= 0);
      bool hasDireita = (norm.indexOf("direita") >= 0);
      bool hasOlhada = (norm.indexOf("olhada") >= 0 || norm.indexOf("olhadinha") >= 0);
      bool hasFique = (norm.indexOf("fique") >= 0 || norm.indexOf("continue olhando") >= 0 || norm.indexOf("fica olhando") >= 0);

      if (hasEsquerda && hasOlhada) {
        fastPathCmd = "look_left";
        fastPathHold = "glance";
      } else if (hasDireita && hasOlhada) {
        fastPathCmd = "look_right";
        fastPathHold = "glance";
      } else if (hasEsquerda && hasFique) {
        fastPathCmd = "look_left";
        fastPathHold = "sustain";
      } else if (hasDireita && hasFique) {
        fastPathCmd = "look_right";
        fastPathHold = "sustain";
      } else if (hasEsquerda && (norm.indexOf("olhe") >= 0 || norm.indexOf("vire") >= 0 || norm.indexOf("gire") >= 0)) {
        fastPathCmd = "look_left";
        fastPathHold = "normal";
      } else if (hasDireita && (norm.indexOf("olhe") >= 0 || norm.indexOf("vire") >= 0 || norm.indexOf("gire") >= 0)) {
        fastPathCmd = "look_right";
        fastPathHold = "normal";
      } else if (norm == "volte para o centro" || norm == "volte pro centro" ||
                 norm == "olhe para frente" || norm == "olhe pra frente" ||
                 norm == "fique no centro" || norm == "olhe para o centro" ||
                 norm == "olhe pro centro" || norm == "centralizar" ||
                 norm == "olhe para o usuario" || norm == "olhe pra mim") {
        fastPathCmd = "center_head";
      }

      if (fastPathCmd && tool_bridge_) {
        tool_bridge_->executeFastPathGesture(fastPathCmd, currentUserTurnId_, fastPathHold);
      }

      String low = currentInput;
      low.toLowerCase();

      // Check for explicit sleep/DND voice commands in Brazilian Portuguese
      // Note: NEVER match 'boa noite' here; 'boa noite' is a standard greeting!
      bool isDnd = (low.indexOf("nao me perturbe") >= 0 || low.indexOf("não me perturbe") >= 0 ||
                    low.indexOf("nao perturbe") >= 0 || low.indexOf("não perturbe") >= 0);
      bool isSleep = (isDnd ||
                      low.indexOf("vai dormir") >= 0 ||
                      low.indexOf("pode dormir") >= 0 ||
                      low.indexOf("hora de dormir") >= 0 ||
                      low.indexOf("modo soneca") >= 0 ||
                      low.indexOf("desligar conversa") >= 0 ||
                      low.indexOf("vou dormir") >= 0 ||
                      low.indexOf("para de ouvir") >= 0);

      if (isDnd) {
        uint16_t dndMin = 15; // default 15 minutes
        for (int i = 0; i < low.length(); ++i) {
          if (isdigit(low[i])) {
            int val = 0;
            while (i < low.length() && isdigit(low[i])) {
              val = val * 10 + (low[i] - '0');
              i++;
            }
            if (val > 0 && val <= 1440) {
              dndMin = static_cast<uint16_t>(val);
              break;
            }
          }
        }
        setDoNotDisturb(dndMin);
      }

      if (isSleep) {
        Serial.printf("GeminiLive: voice sleep/DND command detected: '%s'\n", currentInput.c_str());
        requestSessionEnd(isDnd ? "voice_dnd" : "voice_sleep");
      }
    }
  }

  const char* outputText = serverContent["outputTranscription"]["text"];
  if (!outputText) outputText = serverContent["output_transcription"]["text"];
  if (outputText && outputText[0]) {
    Serial.printf("GeminiLive: output transcript chars=%u\n", static_cast<unsigned>(strlen(outputText)));
    appendOutputTranscriptChunk(outputText);
    recent_dialogue_ring_.onPixTranscriptChunk(outputText, millis());
  }
}

void GeminiLiveProbe::appendOutputTranscriptChunk(const char* text) {
  if (!text || !text[0]) return;
  String chunk(text);
  chunk.trim();
  if (!chunk.length()) return;

  if (transcript_output_buffer_.length() > 0) {
    char last = transcript_output_buffer_[transcript_output_buffer_.length() - 1];
    char first = chunk[0];
    bool firstIsPunctuation = first == '.' || first == ',' || first == '!' || first == '?' ||
                              first == ':' || first == ';' || first == ')' || first == ']';
    bool lastIsSpaceOrOpen = last == ' ' || last == '\n' || last == '(' || last == '[';
    if (!firstIsPunctuation && !lastIsSpaceOrOpen) transcript_output_buffer_ += ' ';
  }
  transcript_output_buffer_ += chunk;

  // Safety valve: avoid unbounded RAM if a very long response streams without
  // turnComplete for any reason. Normal responses flush once at responseComplete.
  if (transcript_output_buffer_.length() > 1800) flushOutputTranscript();
}

void GeminiLiveProbe::flushOutputTranscript() {
  if (!memory_) {
    transcript_output_buffer_ = "";
    return;
  }
  transcript_output_buffer_.trim();
  if (!transcript_output_buffer_.length()) return;
  Serial.printf("GeminiLive: output transcript flushed chars=%u\n",
                static_cast<unsigned>(transcript_output_buffer_.length()));
  memory_->appendDialogue("assistant", transcript_output_buffer_, "live_output_transcription");
  transcript_output_buffer_ = "";
}

int GeminiLiveProbe::decodeBase64(const char* input, int size, char* output) {
  base64_decodestate state;
  base64_init_decodestate(&state);
  return base64_decode_block(input, size, output, &state);
}
