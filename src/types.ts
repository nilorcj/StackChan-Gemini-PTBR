export type EmotionType =
  | 'neutral'
  | 'listening'
  | 'speaking'
  | 'thinking'
  | 'looking'
  | 'happy'
  | 'angry'
  | 'found'
  | 'error'
  | 'surprised'
  | 'sleep';

export type GestureType =
  | 'center_head'
  | 'look_at_user'
  | 'look_left_small'
  | 'look_right_small'
  | 'look_left'
  | 'look_right'
  | 'look_up'
  | 'look_down'
  | 'look_top_left'
  | 'look_top_right'
  | 'search_left_wide'
  | 'search_left'
  | 'search_center'
  | 'search_right'
  | 'search_right_wide'
  | 'shake_no'
  | 'nod_yes'
  | 'curious_tilt'
  | 'angry_tantrum'
  | 'dizzy_spin'
  | 'lifted_protest'
  | 'speaking_micro_motion'
  | 'vibrate'
  | 'purr_vibration'
  | 'sneeze_shake'
  | 'peekaboo_nod'
  | 'petting_tilt'
  | 'pomodoro_alert'
  | 'stop_motion';

export interface HeadMotionStep {
  x_deg: number;
  y_deg: number;
  speed: number;
  hold_ms: number;
  relative?: boolean;
}

export interface RuntimeConfig {
  robot_id: string;
  gemini_model: string;
  gemini_voice: string;
  wifi_enabled: boolean;
  web_enabled: boolean;
  gemini_enabled: boolean;
  gateway_enabled: boolean;
  gateway_base_url: string;
  wifi_ssid: string;
  speaker_volume: number;
  mic_magnification: number;
  mic_noise_filter_level: number;
  vad_prefix_padding_ms: number;
  vad_silence_duration_ms: number;
  vad_start_sensitivity_high: boolean;
  vad_end_sensitivity_low: boolean;
  vad_turn_includes_all_input: boolean;
  wifi_password?: string;
  gemini_api_key?: string;
  gateway_token?: string;
  calendar_url?: string;
  secrets?: string;
  apply?: string;
}

export interface RobotStatus {
  ok: boolean;
  robot_id: string;
  gemini_voice: string;
  web_enabled: boolean;
  gateway_enabled: boolean;
  web_auth_enabled: boolean;
  sd_config_dir: string;
  gemini_api_key: string;
  gateway_token: string;
  calendar_url?: string;
  free_heap: number;
  psram_free: number;
  battery_mv: number;
  battery_ma: number;
  battery_pct?: number;
  wifi_ip: string;
  wifi_dns: string;
  emotion: EmotionType;
  servo: {
    x: number;
    y: number;
    x_deg: number;
    y_deg: number;
    active: boolean;
    current_gesture: string;
    anchor_x: number;
    anchor_y: number;
  };
  gemini: {
    ready: boolean;
    recording: boolean;
    speaking: boolean;
    continuous: boolean;
    mic_ready_for_speech: boolean;
    audio_chunks: number;
    audio_dropped: number;
    audio_wait_ms: number;
    vad_prefix_padding_ms: number;
    vad_silence_duration_ms: number;
    vad_start_sensitivity_high: boolean;
    vad_end_sensitivity_low: boolean;
    vad_turn_includes_all_input: boolean;
  };
  mic: {
    running: boolean;
    is_recording_queue: number;
    magnification: number;
    noise_filter_level: number;
    last_rms: number;
    last_peak: number;
    chunks_sent: number;
    record_false_count: number;
  };
  camera: {
    enabled: boolean;
    ready: boolean;
    last_error: string;
    last_jpeg_bytes: number;
    last_base64_bytes: number;
    last_capture_ts?: string;
  };
  touch_sensor: {
    front: number;
    middle: number;
    back: number;
  };
}

export interface DialogueItem {
  id: string;
  ts: string;
  session: string;
  role: 'user' | 'assistant' | 'system' | 'tool';
  text: string;
  source?: string;
}

export interface FactItem {
  id: string;
  fact: string;
  source: string;
  confidence: number;
  observedDate: string;
  tags?: string[];
}

export interface SummaryItem {
  id: string;
  ts: string;
  scope: string;
  summary: string;
  model?: string;
}

export interface PrivateMemoryItem {
  id: string;
  kind: string;
  label: string;
  value: string;
  ts: string;
}

export interface MemoryStats {
  ok: boolean;
  total_events: number;
  active_context_chars: number;
  dialogues_count: number;
  facts_count: number;
  summaries_count: number;
  private_memories_count: number;
  vector_index_status: string;
  last_summarized_ts: string;
}

export interface SensorDiagnostics {
  ok: boolean;
  ltr553: {
    configured: boolean;
    als_lux: number;
    ps_distance_raw: number;
    proximity_detected: boolean;
    last_error: string;
  };
  pahub: {
    enabled: boolean;
    i2c_addr: string;
    channels: {
      ch0_tof90: { connected: boolean; distance_mm: number; status: string };
      ch1_env: { connected: boolean; temp_c: number; humidity_pct: number; pressure_hpa: number };
      ch2_vibrator: { connected: boolean; pin: number; active: boolean; pwm_duty: number };
      ch3_aux: { connected: boolean; status: string };
    };
  };
  audio_direction: {
    enabled: boolean;
    sample_rate: number;
    stereo_request: boolean;
    duration_ms: number;
    left_rms: number;
    right_rms: number;
    left_peak: number;
    right_peak: number;
    balance_r_minus_l: number;
    tdoa_lag_samples: number;
    tdoa_us: number;
    corr: number;
    direction_hint: 'left_louder' | 'right_louder' | 'center_or_uncertain' | 'quiet';
  };
  power: {
    battery_mv: number;
    battery_ma: number;
    battery_level_pct: number;
    external_power: boolean;
  };
  touch: {
    front: number;
    middle: number;
    back: number;
    pressed: {
      front: boolean;
      middle: boolean;
      back: boolean;
    };
  };
}

export interface ToolDeclaration {
  name: string;
  description: string;
  parameters: {
    type: string;
    properties: Record<string, any>;
    required?: string[];
  };
}
