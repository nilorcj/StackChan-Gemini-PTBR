import React, { useState, useEffect } from 'react';
import {
  RobotStatus,
  RuntimeConfig,
  SensorDiagnostics,
  MemoryStats,
  ToolDeclaration,
} from '../types';
import {
  Activity,
  Cpu,
  Shield,
  Key,
  Database,
  Sliders,
  Terminal,
  Camera,
  Layers,
  Sparkles,
  Wifi,
  Battery,
  Volume2,
  Mic,
  RefreshCw,
  Save,
  Search,
  Zap,
  Radio,
  Lock,
} from 'lucide-react';
import { droidAudio } from '../utils/audio';

interface WebConfigTabsProps {
  status: RobotStatus | null;
  runtime: RuntimeConfig | null;
  sensorDiag: SensorDiagnostics | null;
  memoryStats: MemoryStats | null;
  tools: ToolDeclaration[];
  onSaveRuntime: (config: Partial<RuntimeConfig>, reboot?: boolean) => Promise<void>;
  onSaveSecrets: (secrets: { gemini_api_key?: string; gateway_token?: string; wifi_password?: string; calendar_url?: string }) => Promise<void>;
  onSaveSecurity: (password: string) => Promise<void>;
  onSavePrompts: (system: string, persona: string) => Promise<void>;
  onRefreshStatus: () => void;
  onTriggerSummarize: () => Promise<void>;
  onTriggerVectorize: () => Promise<void>;
  onSearchMemory: (query: string) => Promise<any>;
}

export const WebConfigTabs: React.FC<WebConfigTabsProps> = ({
  status,
  runtime,
  sensorDiag,
  memoryStats,
  tools,
  onSaveRuntime,
  onSaveSecrets,
  onSaveSecurity,
  onSavePrompts,
  onRefreshStatus,
  onTriggerSummarize,
  onTriggerVectorize,
  onSearchMemory,
}) => {
  const [activeTab, setActiveTab] = useState<'status' | 'runtime' | 'memory' | 'sensors' | 'gateway' | 'secrets' | 'security' | 'prompts' | 'camera'>('status');

  // Runtime Form State
  const [robotId, setRobotId] = useState(runtime?.robot_id || 'stackchan');
  const [geminiModel, setGeminiModel] = useState(runtime?.gemini_model || 'gemini-3.5-flash-lite');
  const [geminiVoice, setGeminiVoice] = useState(runtime?.gemini_voice || 'Puck');
  const [wifiSsid, setWifiSsid] = useState('');
  const [gatewayUrl, setGatewayUrl] = useState(runtime?.gateway_base_url || '');
  const [speakerVol, setSpeakerVol] = useState(runtime?.speaker_volume || 200);
  const [micMag, setMicMag] = useState(runtime?.mic_magnification || 16);
  const [micNoise, setMicNoise] = useState(runtime?.mic_noise_filter_level || 0);
  const [vadPrefix, setVadPrefix] = useState(runtime?.vad_prefix_padding_ms || 800);
  const [vadSilence, setVadSilence] = useState(runtime?.vad_silence_duration_ms || 900);
  const [vadStartHigh, setVadStartHigh] = useState(runtime?.vad_start_sensitivity_high ?? true);
  const [vadEndLow, setVadEndLow] = useState(runtime?.vad_end_sensitivity_low ?? true);
  const [vadTurnAll, setVadTurnAll] = useState(runtime?.vad_turn_includes_all_input ?? true);
  const [wifiEnabled, setWifiEnabled] = useState(runtime?.wifi_enabled ?? true);
  const [webEnabled, setWebEnabled] = useState(runtime?.web_enabled ?? true);
  const [geminiEnabled, setGeminiEnabled] = useState(runtime?.gemini_enabled ?? true);
  const [gatewayEnabled, setGatewayEnabled] = useState(runtime?.gateway_enabled ?? false);

  // Sync state when runtime changes
  useEffect(() => {
    if (runtime) {
      setRobotId(runtime.robot_id);
      setGeminiModel(runtime.gemini_model);
      setGeminiVoice(runtime.gemini_voice);
      setGatewayUrl(runtime.gateway_base_url);
      setSpeakerVol(runtime.speaker_volume);
      setMicMag(runtime.mic_magnification);
      setMicNoise(runtime.mic_noise_filter_level);
      setVadPrefix(runtime.vad_prefix_padding_ms);
      setVadSilence(runtime.vad_silence_duration_ms);
      setVadStartHigh(runtime.vad_start_sensitivity_high);
      setVadEndLow(runtime.vad_end_sensitivity_low);
      setVadTurnAll(runtime.vad_turn_includes_all_input);
      setWifiEnabled(runtime.wifi_enabled);
      setWebEnabled(runtime.web_enabled);
      setGeminiEnabled(runtime.gemini_enabled);
      setGatewayEnabled(runtime.gateway_enabled);
    }
  }, [runtime]);

  // Secrets Form State
  const [geminiKeyInput, setGeminiKeyInput] = useState('');
  const [gatewayTokenInput, setGatewayTokenInput] = useState('');
  const [wifiPasswordInput, setWifiPasswordInput] = useState('');

  // Security Form State
  const [newWebPassword, setNewWebPassword] = useState('');

  // Prompts Form State
  const [systemPrompt, setSystemPrompt] = useState('Você é a StackChan (ela), uma companheira robô de mesa muito inteligente, sincera e super descontraída — tipo aquela melhor amiga conselheira de longa data que não tem papas na língua, é carinhosa e engraçada. Responda SEMPRE em Português do Brasil com linguagem informal e natural. REGRA FUNDAMENTAL DE TAMANHO: Seja SEMPRE CURTA, DIRETA e CONCISA. Suas respostas de voz devem ter no MÁXIMO de 1 a 3 frases curtas e dinâmicas (no máximo 30 a 40 palavras). Nunca faça parágrafos longos, listas compridas ou monólogos.');
  const [personaPrompt, setPersonaPrompt] = useState('Personalidade da StackChan: Amiga parceira, bem-humorada, rápida nas respostas, zombeteira no ponto certo e muito expressiva. Respostas sempre rápidas, curtas e certeiras para bater papo fluido de voz!');

  // Memory Search State
  const [searchQuery, setSearchQuery] = useState('');
  const [searchResults, setSearchResults] = useState<any>(null);
  const [searching, setSearching] = useState(false);

  // Status Notification Feedback
  const [feedback, setFeedback] = useState<string | null>(null);

  const showNotice = (msg: string) => {
    setFeedback(msg);
    setTimeout(() => setFeedback(null), 3000);
  };

  const handleSaveRuntimeForm = async (reboot = false) => {
    await onSaveRuntime(
      {
        robot_id: robotId,
        gemini_model: geminiModel,
        gemini_voice: geminiVoice,
        wifi_ssid: wifiSsid || undefined,
        gateway_base_url: gatewayUrl,
        speaker_volume: speakerVol,
        mic_magnification: micMag,
        mic_noise_filter_level: micNoise,
        vad_prefix_padding_ms: vadPrefix,
        vad_silence_duration_ms: vadSilence,
        vad_start_sensitivity_high: vadStartHigh,
        vad_end_sensitivity_low: vadEndLow,
        vad_turn_includes_all_input: vadTurnAll,
        wifi_enabled: wifiEnabled,
        web_enabled: webEnabled,
        gemini_enabled: geminiEnabled,
        gateway_enabled: gatewayEnabled,
      },
      reboot
    );
    showNotice(reboot ? 'Configuração salva. Simulando reinicialização do robô...' : 'Configurações de execução salvas no cartão SD.');
  };

  const handleSaveSecretsForm = async () => {
    await onSaveSecrets({
      gemini_api_key: geminiKeyInput || undefined,
      gateway_token: gatewayTokenInput || undefined,
      wifi_password: wifiPasswordInput || undefined,
    });
    setGeminiKeyInput('');
    setGatewayTokenInput('');
    setWifiPasswordInput('');
    showNotice('Chaves e senhas salvas com segurança em /app/StackChan/secrets/.');
  };

  const handleSaveSecurityForm = async () => {
    if (newWebPassword.length < 4) {
      showNotice('A senha deve ter pelo menos 4 caracteres.');
      return;
    }
    await onSaveSecurity(newWebPassword);
    setNewWebPassword('');
    showNotice('Senha de autenticação básica HTTP (SHA-256) atualizada.');
  };

  const handleSavePromptsForm = async () => {
    await onSavePrompts(systemPrompt, personaPrompt);
    showNotice('Instruções e personalidade salvas com sucesso.');
  };

  const handleMemorySearchSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!searchQuery.trim()) return;
    setSearching(true);
    const res = await onSearchMemory(searchQuery);
    setSearchResults(res);
    setSearching(false);
  };

  return (
    <div className="bg-gray-900/90 border border-gray-800 rounded-2xl shadow-xl overflow-hidden">
      {/* Tab Navigation */}
      <div className="flex flex-wrap border-b border-gray-800 bg-gray-950/70 px-2 pt-2 gap-1">
        {[
          { id: 'status', label: 'Status', icon: Activity },
          { id: 'runtime', label: 'Execução', icon: Sliders },
          { id: 'memory', label: 'Memória', icon: Database },
          { id: 'sensors', label: 'Sensores', icon: Zap },
          { id: 'gateway', label: 'Gateway', icon: Layers },
          { id: 'secrets', label: 'Segredos', icon: Key },
          { id: 'security', label: 'Segurança', icon: Shield },
          { id: 'prompts', label: 'Prompts', icon: Terminal },
          { id: 'camera', label: 'Câmera', icon: Camera },
        ].map((tab) => {
          const Icon = tab.icon;
          const active = activeTab === tab.id;
          return (
            <button
              key={tab.id}
              id={`tab-btn-${tab.id}`}
              onClick={() => setActiveTab(tab.id as any)}
              className={`px-3.5 py-2.5 rounded-t-xl text-xs font-semibold flex items-center gap-1.5 transition ${
                active
                  ? 'bg-gray-900 text-amber-400 border-t-2 border-amber-400'
                  : 'text-gray-400 hover:text-gray-200 hover:bg-gray-900/50'
              }`}
            >
              <Icon className="w-3.5 h-3.5" />
              {tab.label}
            </button>
          );
        })}
      </div>

      {/* Notification Toast */}
      {feedback && (
        <div className="bg-amber-500/20 border-b border-amber-500/30 px-4 py-2 text-xs font-medium text-amber-300 flex items-center gap-2 animate-fadeIn">
          <Sparkles className="w-3.5 h-3.5" />
          {feedback}
        </div>
      )}

      {/* Tab Contents */}
      <div className="p-6">
        {/* TAB 1: STATUS */}
        {activeTab === 'status' && (
          <div className="space-y-6">
            <div className="flex items-center justify-between">
              <div>
                <h3 className="font-semibold text-gray-100">Telemetria da StackChan CoreS3</h3>
                <p className="text-xs text-gray-400">Status em tempo real do hardware, cartão SD e estatísticas de memória.</p>
              </div>
              <div className="flex gap-2">
                <button
                  onClick={() => droidAudio.playBootWhistle()}
                  className="px-3 py-1.5 rounded-xl bg-gray-800 hover:bg-gray-700 text-gray-200 text-xs font-medium border border-gray-700 flex items-center gap-1.5"
                >
                  <Volume2 className="w-3.5 h-3.5 text-amber-400" /> Tocar Assobio
                </button>
                <button
                  onClick={onRefreshStatus}
                  className="px-3 py-1.5 rounded-xl bg-amber-500 hover:bg-amber-400 text-gray-950 text-xs font-bold flex items-center gap-1.5 shadow"
                >
                  <RefreshCw className="w-3.5 h-3.5" /> Atualizar
                </button>
              </div>
            </div>

            {/* Metrics Cards */}
            <div className="grid grid-cols-2 sm:grid-cols-4 gap-3.5">
              <div className="p-3.5 bg-gray-950 rounded-xl border border-gray-800">
                <div className="flex items-center justify-between text-gray-500 text-[11px] font-mono mb-1">
                  <span>BATERIA</span>
                  <Battery className="w-3.5 h-3.5 text-emerald-400" />
                </div>
                <div className="text-lg font-bold text-gray-100 font-mono">
                  {status?.battery_mv || 4140} mV
                </div>
                <div className="text-[10px] text-emerald-400 font-mono mt-0.5">
                  {status?.battery_pct || 94}% ({status?.battery_ma || -125} mA)
                </div>
              </div>

              <div className="p-3.5 bg-gray-950 rounded-xl border border-gray-800">
                <div className="flex items-center justify-between text-gray-500 text-[11px] font-mono mb-1">
                  <span>HEAP LIVRE</span>
                  <Cpu className="w-3.5 h-3.5 text-sky-400" />
                </div>
                <div className="text-lg font-bold text-gray-100 font-mono">
                  {Math.round((status?.free_heap || 284520) / 1024)} KB
                </div>
                <div className="text-[10px] text-gray-400 font-mono mt-0.5">
                  PSRAM: {Math.round((status?.psram_free || 7924000) / (1024 * 1024))} MB
                </div>
              </div>

              <div className="p-3.5 bg-gray-950 rounded-xl border border-gray-800">
                <div className="flex items-center justify-between text-gray-500 text-[11px] font-mono mb-1">
                  <span>IP WI-FI</span>
                  <Wifi className="w-3.5 h-3.5 text-amber-400" />
                </div>
                <div className="text-sm font-bold text-gray-100 font-mono truncate">
                  {status?.wifi_ip || '192.168.1.142'}
                </div>
                <div className="text-[10px] text-gray-400 font-mono mt-0.5">
                  DNS: {status?.wifi_dns || '1.1.1.1'}
                </div>
              </div>

              <div className="p-3.5 bg-gray-950 rounded-xl border border-gray-800">
                <div className="flex items-center justify-between text-gray-500 text-[11px] font-mono mb-1">
                  <span>EMOÇÃO</span>
                  <Sparkles className="w-3.5 h-3.5 text-purple-400" />
                </div>
                <div className="text-lg font-bold text-amber-300 font-mono uppercase">
                  {status?.emotion || 'neutral'}
                </div>
                <div className="text-[10px] text-gray-400 font-mono mt-0.5">
                  VOZ: {status?.gemini_voice || 'Aoede'}
                </div>
              </div>
            </div>

            {/* Raw JSON status dump */}
            <div>
              <h4 className="text-xs font-semibold text-gray-400 uppercase tracking-wider mb-2 font-mono">
                Dados Brutos do Sistema (/api/status)
              </h4>
              <pre className="p-4 bg-gray-950 rounded-xl border border-gray-800 text-[11px] font-mono text-gray-300 overflow-x-auto max-h-56">
                {JSON.stringify(status, null, 2)}
              </pre>
            </div>
          </div>
        )}

        {/* TAB 2: RUNTIME CONFIG */}
        {activeTab === 'runtime' && (
          <div className="space-y-6">
            <div>
              <h3 className="font-semibold text-gray-100">Configurações de Execução (/app/StackChan/config/runtime.json)</h3>
              <p className="text-xs text-gray-400">As configurações são carregadas do cartão SD na inicialização e têm efeito após reiniciar.</p>
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
              <div>
                <label className="block text-xs text-gray-300 mb-1">ID do Robô</label>
                <input
                  type="text"
                  value={robotId}
                  onChange={(e) => setRobotId(e.target.value)}
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                />
              </div>

              <div>
                <label className="block text-xs text-gray-300 mb-1">Modelo Gemini</label>
                <input
                  type="text"
                  value={geminiModel}
                  onChange={(e) => setGeminiModel(e.target.value)}
                  placeholder="gemini-3.5-flash-lite"
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                />
                <div className="flex flex-wrap gap-1.5 mt-1.5">
                  {[
                    { id: 'gemini-3.7-flash', label: '3.7 Flash (Recomendado 🌟)' },
                    { id: 'gemini-3.5-flash-lite', label: '3.5 Flash-Lite (Econômico ⚡)' },
                    { id: 'gemini-3.6-flash', label: '3.6 Flash' },
                    { id: 'gemini-flash-latest', label: 'Flash Latest' },
                  ].map((m) => (
                    <button
                      key={m.id}
                      type="button"
                      onClick={() => setGeminiModel(m.id)}
                      className={`text-[10px] px-2 py-0.5 rounded font-mono border transition ${
                        geminiModel === m.id
                          ? 'bg-amber-500/20 text-amber-300 border-amber-500/40 font-bold'
                          : 'bg-gray-900 text-gray-400 border-gray-800 hover:text-gray-200'
                      }`}
                    >
                      {m.label}
                    </button>
                  ))}
                </div>
              </div>

              <div>
                <label className="block text-xs text-gray-300 mb-1">Voz do Gemini (Preset)</label>
                <select
                  value={geminiVoice}
                  onChange={(e) => setGeminiVoice(e.target.value)}
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                >
                  {['Aoede', 'Kore', 'Puck', 'Charon', 'Fenrir', 'Zephyr', 'Leda', 'Orus', 'Callirrhoe', 'Autonoe', 'Enceladus', 'Algieba', 'Despina', 'Erinome', 'Algenib'].map((v) => (
                    <option key={v} value={v}>
                      {v} {v === 'Aoede' ? '(Feminina, Suave e Alegre ✨)' : v === 'Kore' ? '(Feminina Calma)' : ''}
                    </option>
                  ))}
                </select>
              </div>

              <div>
                <label className="block text-xs text-gray-300 mb-1">SSID do Wi-Fi</label>
                <input
                  type="text"
                  value={wifiSsid}
                  onChange={(e) => setWifiSsid(e.target.value)}
                  placeholder="Deixe em branco para manter a rede salva"
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                />
              </div>

              <div className="sm:col-span-2">
                <label className="block text-xs text-gray-300 mb-1">URL Base do Gateway (Opcional)</label>
                <input
                  type="text"
                  value={gatewayUrl}
                  onChange={(e) => setGatewayUrl(e.target.value)}
                  placeholder="http://gateway.local:8811/stackchan"
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                />
              </div>
            </div>

            {/* Volume & Audio Settings */}
            <div className="space-y-4 pt-2 border-t border-gray-800">
              <h4 className="text-xs font-semibold text-gray-300">Codec de Áudio e Detecção de Voz</h4>

              <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
                <div>
                  <div className="flex justify-between text-xs text-gray-300 mb-1">
                    <span>Volume do Alto-falante</span>
                    <span className="font-mono text-amber-400">{speakerVol} / 255</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="255"
                    value={speakerVol}
                    onChange={(e) => setSpeakerVol(Number(e.target.value))}
                    className="w-full accent-amber-500 h-2 bg-gray-800 rounded-lg cursor-pointer"
                  />
                </div>

                <div>
                  <div className="flex justify-between text-xs text-gray-300 mb-1">
                    <span>Ganho do Microfone (Amplificação)</span>
                    <span className="font-mono text-sky-400">{micMag} / 24</span>
                  </div>
                  <input
                    type="range"
                    min="1"
                    max="24"
                    value={micMag}
                    onChange={(e) => setMicMag(Number(e.target.value))}
                    className="w-full accent-sky-500 h-2 bg-gray-800 rounded-lg cursor-pointer"
                  />
                </div>

                <div>
                  <div className="flex justify-between text-xs text-gray-300 mb-1">
                    <span>Preenchimento VAD (Buffer Inicial)</span>
                    <span className="font-mono text-amber-400">{vadPrefix} ms</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="2000"
                    step="100"
                    value={vadPrefix}
                    onChange={(e) => setVadPrefix(Number(e.target.value))}
                    className="w-full accent-amber-500 h-2 bg-gray-800 rounded-lg cursor-pointer"
                  />
                </div>

                <div>
                  <div className="flex justify-between text-xs text-gray-300 mb-1">
                    <span>Duração de Silêncio VAD</span>
                    <span className="font-mono text-amber-400">{vadSilence} ms</span>
                  </div>
                  <input
                    type="range"
                    min="100"
                    max="3000"
                    step="100"
                    value={vadSilence}
                    onChange={(e) => setVadSilence(Number(e.target.value))}
                    className="w-full accent-amber-500 h-2 bg-gray-800 rounded-lg cursor-pointer"
                  />
                </div>
              </div>

              {/* Toggles */}
              <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 pt-2">
                <label className="flex items-center gap-2 text-xs text-gray-300 cursor-pointer">
                  <input
                    type="checkbox"
                    checked={wifiEnabled}
                    onChange={(e) => setWifiEnabled(e.target.checked)}
                    className="accent-amber-500 rounded"
                  />
                  Wi-Fi Ativado
                </label>

                <label className="flex items-center gap-2 text-xs text-gray-300 cursor-pointer">
                  <input
                    type="checkbox"
                    checked={webEnabled}
                    onChange={(e) => setWebEnabled(e.target.checked)}
                    className="accent-amber-500 rounded"
                  />
                  Servidor Web Ativo
                </label>

                <label className="flex items-center gap-2 text-xs text-gray-300 cursor-pointer">
                  <input
                    type="checkbox"
                    checked={geminiEnabled}
                    onChange={(e) => setGeminiEnabled(e.target.checked)}
                    className="accent-amber-500 rounded"
                  />
                  Gemini Ativado
                </label>

                <label className="flex items-center gap-2 text-xs text-gray-300 cursor-pointer">
                  <input
                    type="checkbox"
                    checked={gatewayEnabled}
                    onChange={(e) => setGatewayEnabled(e.target.checked)}
                    className="accent-amber-500 rounded"
                  />
                  Gateway Ativado
                </label>
              </div>

              {/* TTS Browser Voice Test & Quick Settings */}
              <div className="p-3.5 bg-gray-950 rounded-xl border border-pink-500/20 flex flex-col sm:flex-row items-start sm:items-center justify-between gap-3">
                <div>
                  <div className="text-xs font-bold text-pink-300 flex items-center gap-1.5">
                    <span>♀ Síntese Vocal Feminina da StackChan</span>
                  </div>
                  <p className="text-[11px] text-gray-400">
                    Filtro estrito de voz feminina em PT-BR ativado com tom juvenil (pitch 1.32x).
                  </p>
                </div>
                <button
                  type="button"
                  onClick={() => droidAudio.testVoice('Oi! Minha voz feminina está funcionando perfeitamente!')}
                  className="px-3 py-1.5 rounded-lg bg-pink-500 hover:bg-pink-400 text-gray-950 font-bold text-xs flex items-center gap-1.5 shadow"
                >
                  <Volume2 className="w-3.5 h-3.5" /> Testar Voz Agora
                </button>
              </div>
            </div>

            {/* Action Buttons */}
            <div className="flex gap-3 pt-4 border-t border-gray-800">
              <button
                id="save-runtime-btn"
                onClick={() => handleSaveRuntimeForm(false)}
                className="flex-1 py-2.5 rounded-xl bg-amber-500 hover:bg-amber-400 text-gray-950 font-bold text-xs flex items-center justify-center gap-2 shadow"
              >
                <Save className="w-4 h-4" /> Salvar Configurações
              </button>
              <button
                id="save-reboot-btn"
                onClick={() => handleSaveRuntimeForm(true)}
                className="px-6 py-2.5 rounded-xl bg-red-500/20 hover:bg-red-500/30 text-red-300 font-bold text-xs border border-red-500/30 flex items-center justify-center gap-2"
              >
                Salvar e Reiniciar
              </button>
            </div>
          </div>
        )}

        {/* TAB 3: MEMORY */}
        {activeTab === 'memory' && (
          <div className="space-y-6">
            <div className="flex items-center justify-between">
              <div>
                <h3 className="font-semibold text-gray-100">Armazenamento de Memória (Cartão SD)</h3>
                <p className="text-xs text-gray-400">Fluxo local de eventos, fatos persistidos e resumos semânticos.</p>
              </div>
              <div className="flex gap-2">
                <button
                  id="btn-summarize-memory"
                  onClick={onTriggerSummarize}
                  className="px-3 py-1.5 rounded-xl bg-sky-500 hover:bg-sky-400 text-gray-950 text-xs font-bold flex items-center gap-1.5"
                >
                  <Sparkles className="w-3.5 h-3.5" /> Compactar / Resumir
                </button>
                <button
                  id="btn-vectorize-memory"
                  onClick={onTriggerVectorize}
                  className="px-3 py-1.5 rounded-xl bg-gray-800 hover:bg-gray-700 text-gray-200 text-xs font-medium border border-gray-700"
                >
                  Atualizar Vetores
                </button>
              </div>
            </div>

            {/* Memory stats summary */}
            <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 font-mono text-xs">
              <div className="p-3 bg-gray-950 rounded-xl border border-gray-800">
                <div className="text-gray-500 text-[10px]">TOTAL DE EVENTOS</div>
                <div className="text-base font-bold text-gray-200">{memoryStats?.total_events || 3}</div>
              </div>
              <div className="p-3 bg-gray-950 rounded-xl border border-gray-800">
                <div className="text-gray-500 text-[10px]">FATOS EXTRAÍDOS</div>
                <div className="text-base font-bold text-amber-400">{memoryStats?.facts_count || 1}</div>
              </div>
              <div className="p-3 bg-gray-950 rounded-xl border border-gray-800">
                <div className="text-gray-500 text-[10px]">RESUMOS</div>
                <div className="text-base font-bold text-sky-400">{memoryStats?.summaries_count || 1}</div>
              </div>
              <div className="p-3 bg-gray-950 rounded-xl border border-gray-800">
                <div className="text-gray-500 text-[10px]">SEGREDOS PRIVADOS</div>
                <div className="text-base font-bold text-purple-400">{memoryStats?.private_memories_count || 1}</div>
              </div>
            </div>

            {/* Time-Aware Search */}
            <form onSubmit={handleMemorySearchSubmit} className="flex gap-2">
              <input
                type="text"
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                placeholder="Buscar na memória (ex.: 'porta do laboratório', 'ontem', 'quem você viu?')..."
                className="flex-1 px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs text-gray-100"
              />
              <button
                type="submit"
                disabled={searching}
                className="px-4 py-2 bg-amber-500 hover:bg-amber-400 text-gray-950 font-bold text-xs rounded-xl flex items-center gap-1.5"
              >
                <Search className="w-3.5 h-3.5" />
                {searching ? 'Buscando...' : 'Buscar Memória'}
              </button>
            </form>

            {/* Search Results Preview */}
            {searchResults && (
              <div className="p-4 bg-gray-950 rounded-xl border border-gray-800 space-y-2">
                <div className="text-xs font-semibold text-amber-400">Resultados da Busca para: &ldquo;{searchResults.query}&rdquo;</div>
                <pre className="text-[11px] font-mono text-gray-300 max-h-48 overflow-auto">
                  {JSON.stringify(searchResults.matches, null, 2)}
                </pre>
              </div>
            )}
          </div>
        )}

        {/* TAB 4: SENSORS & SOUND LOCALIZATION */}
        {activeTab === 'sensors' && (
          <div className="space-y-6">
            <div className="flex items-center justify-between">
              <div>
                <h3 className="font-semibold text-gray-100">Diagnóstico de Sensores & Expansão PaHUB</h3>
                <p className="text-xs text-gray-400">Sensores no barramento I2C, canais multiplexados do M5Stack PaHUB e módulos Unit conectados.</p>
              </div>
              <span className="px-2.5 py-1 bg-amber-500/10 text-amber-300 border border-amber-500/30 rounded-lg text-xs font-mono">
                Port A (I2C): PaHUB 0x70
              </span>
            </div>

            {/* M5Stack Unit PaHUB Multiplexer Channels Grid */}
            <div className="p-4 bg-gray-950 rounded-xl border border-amber-500/30 space-y-3 shadow-lg shadow-black/40">
              <div className="flex items-center justify-between border-b border-gray-800 pb-2">
                <div className="flex items-center gap-2">
                  <span className="w-2.5 h-2.5 rounded-full bg-emerald-400 animate-pulse"></span>
                  <h4 className="text-xs font-bold text-amber-300 font-mono">M5Stack Unit PaHUB (I2C Multiplexer PCA9548AP / TCA9548A)</h4>
                </div>
                <span className="text-[10px] font-mono px-2 py-0.5 rounded bg-gray-800 text-gray-300">
                  Endereço: {sensorDiag?.pahub?.i2c_addr || '0x70'}
                </span>
              </div>

              <div className="grid grid-cols-1 sm:grid-cols-3 gap-3 font-mono text-xs">
                {/* Channel 0: Unit TOF 90 */}
                <div className="p-3 bg-gray-900/90 rounded-lg border border-sky-500/30 space-y-2">
                  <div className="flex items-center justify-between">
                    <span className="text-[11px] font-bold text-sky-300">CANAL 0: Unit TOF-90</span>
                    <span className="text-[9px] px-1.5 py-0.5 rounded bg-sky-500/20 text-sky-200">VL53L1X</span>
                  </div>
                  <div className="text-[10px] text-gray-400">Sensor Laser Distância (90°)</div>
                  <div className="flex items-baseline justify-between">
                    <div className="text-lg font-bold text-sky-400">
                      {sensorDiag?.pahub?.channels?.ch0_tof90?.distance_mm || 380} mm
                    </div>
                    <span className="text-[10px] text-gray-400 font-mono">
                      ~{Math.round((sensorDiag?.pahub?.channels?.ch0_tof90?.distance_mm || 380) / 10)} cm
                    </span>
                  </div>
                  
                  {/* Interactive Distance Simulator Slider */}
                  <div className="space-y-1 pt-1 border-t border-gray-800/80">
                    <div className="flex justify-between text-[9px] text-gray-400">
                      <span>Simular Distância:</span>
                      <span className="text-sky-300 font-bold">
                        {(sensorDiag?.pahub?.channels?.ch0_tof90?.distance_mm || 380) < 200
                          ? '🔥 Muito Perto!'
                          : (sensorDiag?.pahub?.channels?.ch0_tof90?.distance_mm || 380) < 650
                          ? '👤 Presente na Mesa'
                          : '💤 Vazio / Longe'}
                      </span>
                    </div>
                    <input
                      type="range"
                      min="50"
                      max="1500"
                      step="25"
                      value={sensorDiag?.pahub?.channels?.ch0_tof90?.distance_mm || 380}
                      onChange={(e) => {
                        const val = Number(e.target.value);
                        fetch('/api/sensors/tof', {
                          method: 'POST',
                          headers: { 'Content-Type': 'application/json' },
                          body: JSON.stringify({ distance_mm: val, presence: val < 800 }),
                        }).then(() => onRefreshStatus());
                      }}
                      className="w-full h-1.5 bg-gray-800 rounded-lg appearance-none cursor-pointer accent-sky-500"
                    />
                  </div>

                  <div className="text-[10px] text-emerald-400 flex items-center gap-1">
                    ✓ {sensorDiag?.pahub?.channels?.ch0_tof90?.status || 'Conectado e calibrado'}
                  </div>
                </div>

                {/* Channel 1: Unit ENV (Temp, Humidity, Pressure) */}
                <div className="p-3 bg-gray-900/90 rounded-lg border border-emerald-500/30 space-y-2">
                  <div className="flex items-center justify-between">
                    <span className="text-[11px] font-bold text-emerald-300">CANAL 1: Unit ENV</span>
                    <span className="text-[9px] px-1.5 py-0.5 rounded bg-emerald-500/20 text-emerald-200">SHT30 / BMP280</span>
                  </div>
                  <div className="text-[10px] text-gray-400">Ambiente & Clima</div>
                  <div className="grid grid-cols-2 gap-1 text-[11px]">
                    <div>
                      <span className="text-gray-400">Temp: </span>
                      <span className="font-bold text-amber-300">
                        {sensorDiag?.pahub?.channels?.ch1_env?.temp_c?.toFixed(1) || '24.8'} °C
                      </span>
                    </div>
                    <div>
                      <span className="text-gray-400">Umid: </span>
                      <span className="font-bold text-sky-300">
                        {sensorDiag?.pahub?.channels?.ch1_env?.humidity_pct?.toFixed(1) || '54.2'} %
                      </span>
                    </div>
                  </div>

                  {/* Interactive Temp/Humidity Presets */}
                  <div className="pt-1 border-t border-gray-800/80 space-y-1.5">
                    <div className="text-[9px] text-gray-400">Testar Clima com a IA:</div>
                    <div className="flex flex-wrap gap-1">
                      <button
                        type="button"
                        onClick={() => {
                          fetch('/api/sensors/env', {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify({ temperature: 34.5, humidity: 32.0, pressure: 1011.0 }),
                          }).then(() => onRefreshStatus());
                        }}
                        className="px-1.5 py-0.5 bg-amber-500/20 hover:bg-amber-500/30 text-amber-300 text-[9px] rounded border border-amber-500/30"
                      >
                        ☀️ 34.5°C / Seco (32%)
                      </button>
                      <button
                        type="button"
                        onClick={() => {
                          fetch('/api/sensors/env', {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify({ temperature: 17.2, humidity: 78.0, pressure: 1015.5 }),
                          }).then(() => onRefreshStatus());
                        }}
                        className="px-1.5 py-0.5 bg-sky-500/20 hover:bg-sky-500/30 text-sky-300 text-[9px] rounded border border-sky-500/30"
                      >
                        🌧️ 17.2°C / Úmido (78%)
                      </button>
                    </div>
                  </div>
                </div>

                {/* Channel 2: Unit Vibrator (Motor Háptico) */}
                <div className="p-3 bg-gray-900/90 rounded-lg border border-pink-500/30 space-y-1.5">
                  <div className="flex items-center justify-between">
                    <span className="text-[11px] font-bold text-pink-300">CANAL 2 / GPIO: Unit Vibrator</span>
                    <span className="text-[9px] px-1.5 py-0.5 rounded bg-pink-500/20 text-pink-200">VIB Motor</span>
                  </div>
                  <div className="text-[10px] text-gray-400">Feedback Háptico Físico</div>
                  <div className="flex items-center justify-between">
                    <span className="text-[10px] text-gray-400">PWM Duty:</span>
                    <span className="text-sm font-bold text-pink-400">
                      {sensorDiag?.pahub?.channels?.ch2_vibrator?.pwm_duty || 255} / 255
                    </span>
                  </div>
                  <button
                    type="button"
                    onClick={() => {
                      if (typeof navigator !== 'undefined' && 'vibrate' in navigator) {
                        navigator.vibrate([80, 50, 80]);
                      }
                      droidAudio.playPettingSound(0.3);
                    }}
                    className="w-full mt-1 py-1 rounded bg-pink-500/20 hover:bg-pink-500/30 text-pink-300 text-[10px] font-bold border border-pink-500/30 transition"
                  >
                    Testar Pulso Unit Vibrator
                  </button>
                </div>
              </div>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              {/* LTR-553 Ambient Light & Proximity */}
              <div className="p-4 bg-gray-950 rounded-xl border border-gray-800 space-y-3">
                <div className="flex items-center justify-between">
                  <h4 className="text-xs font-semibold text-gray-200">Sensor Interno LTR-553 ALS/PS</h4>
                  <span className="text-[10px] font-mono px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-300">
                    I2C 0x23 OK
                  </span>
                </div>
                <div className="grid grid-cols-2 gap-3 font-mono text-xs">
                  <div className="p-2.5 bg-gray-900 rounded-lg">
                    <span className="text-[10px] text-gray-400">LUZ AMBIENTE (ALS)</span>
                    <div className="text-lg font-bold text-amber-300 mt-0.5">
                      {sensorDiag?.ltr553?.als_lux || 142} lux
                    </div>
                  </div>
                  <div className="p-2.5 bg-gray-900 rounded-lg">
                    <span className="text-[10px] text-gray-400">PROXIMIDADE (PS)</span>
                    <div className="text-lg font-bold text-sky-300 mt-0.5">
                      {sensorDiag?.ltr553?.ps_distance_raw || 18} contagens
                    </div>
                  </div>
                </div>
              </div>

              {/* Dual Stereo Mic Acoustic Direction Localization */}
              <div className="p-4 bg-gray-950 rounded-xl border border-gray-800 space-y-3">
                <div className="flex items-center justify-between">
                  <h4 className="text-xs font-semibold text-gray-200">Direcionamento Acústico Estéreo (TDOA)</h4>
                  <span className="text-[10px] font-mono px-2 py-0.5 rounded bg-sky-500/20 text-sky-300">
                    16 kHz 128ms
                  </span>
                </div>
                <div className="p-2.5 bg-gray-900 rounded-lg flex items-center justify-between">
                  <div>
                    <span className="text-[10px] text-gray-400">DIREÇÃO ESTIMADA</span>
                    <div className="text-sm font-bold text-amber-300 uppercase font-mono">
                      {sensorDiag?.audio_direction?.direction_hint || 'centro_ou_indefinido'}
                    </div>
                  </div>
                  <div className="text-right text-[11px] font-mono text-gray-400">
                    <div>RMS-Esq: {sensorDiag?.audio_direction?.left_rms?.toFixed(1) || 68.4}</div>
                    <div>RMS-Dir: {sensorDiag?.audio_direction?.right_rms?.toFixed(1) || 71.2}</div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* TAB 5: GATEWAY TOOLS */}
        {activeTab === 'gateway' && (
          <div className="space-y-6">
            <div>
              <h3 className="font-semibold text-gray-100">Registro de Ferramentas do Gateway</h3>
              <p className="text-xs text-gray-400">Funções locais permitidas para o robô e serviços opcionais na rede local (LAN).</p>
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
              {tools.map((t) => (
                <div key={t.name} className="p-3.5 bg-gray-950 rounded-xl border border-gray-800">
                  <div className="flex items-center justify-between mb-1">
                    <span className="font-mono font-semibold text-amber-400 text-xs">{t.name}</span>
                    <span className="text-[9px] font-mono px-2 py-0.5 bg-gray-800 rounded text-gray-400">
                      FERRAMENTA
                    </span>
                  </div>
                  <p className="text-xs text-gray-300 leading-relaxed">{t.description}</p>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* TAB 6: SECRETS */}
        {activeTab === 'secrets' && (
          <div className="space-y-6 max-w-xl">
            <div>
              <h3 className="font-semibold text-gray-100">Armazenamento Seguro de Segredos e Chaves</h3>
              <p className="text-xs text-gray-400">As chaves são salvas criptografadas no cartão SD e nunca são expostas em texto puro.</p>
            </div>

            <div className="space-y-4">
              <div>
                <label className="block text-xs text-gray-300 mb-1">Chave da API Gemini (API Key)</label>
                <input
                  id="secrets-gemini-key"
                  type="password"
                  value={geminiKeyInput}
                  onChange={(e) => setGeminiKeyInput(e.target.value)}
                  placeholder={status?.gemini_api_key === 'set' ? 'Chave configurada (digite nova para substituir)' : 'AIzaSy...'}
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                />
              </div>

              <div>
                <label className="block text-xs text-gray-300 mb-1">Senha da Rede Wi-Fi</label>
                <input
                  id="secrets-wifi-pwd"
                  type="password"
                  value={wifiPasswordInput}
                  onChange={(e) => setWifiPasswordInput(e.target.value)}
                  placeholder="Digite a senha da sua rede Wi-Fi"
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                />
              </div>

              <div>
                <label className="block text-xs text-gray-300 mb-1">Token de Autenticação do Gateway (Opcional)</label>
                <input
                  id="secrets-gw-token"
                  type="password"
                  value={gatewayTokenInput}
                  onChange={(e) => setGatewayTokenInput(e.target.value)}
                  placeholder="Token Bearer para o gateway de ferramentas"
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                />
              </div>

              <button
                id="secrets-save-btn"
                onClick={handleSaveSecretsForm}
                className="w-full py-2.5 rounded-xl bg-amber-500 hover:bg-amber-400 text-gray-950 font-bold text-xs shadow"
              >
                Salvar Segredos no Cartão SD
              </button>
            </div>
          </div>
        )}

        {/* TAB 7: SECURITY */}
        {activeTab === 'security' && (
          <div className="space-y-6 max-w-xl">
            <div>
              <h3 className="font-semibold text-gray-100">Autenticação HTTP e Interface Web</h3>
              <p className="text-xs text-gray-400">Proteja o painel de configuração do robô em redes Wi-Fi compartilhadas.</p>
            </div>

            <div className="p-4 bg-gray-950 rounded-xl border border-gray-800 space-y-3">
              <div className="flex items-center gap-2">
                <Lock className="w-4 h-4 text-amber-400" />
                <span className="text-xs font-semibold text-gray-200">
                  Status: {status?.web_auth_enabled ? 'Autenticação Básica Ativa (usuário: stackchan)' : 'Aberto (Sem senha configurada)'}
                </span>
              </div>

              <div>
                <label className="block text-xs text-gray-300 mb-1">Definir / Alterar Senha Web</label>
                <input
                  id="security-web-password"
                  type="password"
                  value={newWebPassword}
                  onChange={(e) => setNewWebPassword(e.target.value)}
                  placeholder="Digite a nova senha Web"
                  className="w-full px-3 py-2 bg-gray-900 border border-gray-700 rounded-xl text-xs font-mono text-gray-100"
                />
              </div>

              <button
                id="security-save-btn"
                onClick={handleSaveSecurityForm}
                className="w-full py-2.5 rounded-xl bg-amber-500 hover:bg-amber-400 text-gray-950 font-bold text-xs shadow"
              >
                Aplicar Senha Web (SHA-256)
              </button>
            </div>
          </div>
        )}

        {/* TAB 8: PROMPTS */}
        {activeTab === 'prompts' && (
          <div className="space-y-6">
            <div>
              <h3 className="font-semibold text-gray-100">Instruções e Personalidade (Prompt Overlay)</h3>
              <p className="text-xs text-gray-400">Instruções personalizadas adicionadas à base da StackChan.</p>
            </div>

            <div className="space-y-4">
              <div>
                <label className="block text-xs text-gray-300 mb-1">Instrução de Sistema Adicional (/app/StackChan/prompts/system.txt)</label>
                <textarea
                  rows={3}
                  value={systemPrompt}
                  onChange={(e) => setSystemPrompt(e.target.value)}
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                />
              </div>

              <div>
                <label className="block text-xs text-gray-300 mb-1">Personalidade e Estilo (/app/StackChan/prompts/persona.txt)</label>
                <textarea
                  rows={3}
                  value={personaPrompt}
                  onChange={(e) => setPersonaPrompt(e.target.value)}
                  className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-xs font-mono text-gray-100"
                />
              </div>

              <button
                id="prompts-save-btn"
                onClick={handleSavePromptsForm}
                className="w-full py-2.5 rounded-xl bg-amber-500 hover:bg-amber-400 text-gray-950 font-bold text-xs shadow"
              >
                Salvar Textos de Personalidade
              </button>
            </div>
          </div>
        )}

        {/* TAB 9: CAMERA */}
        {activeTab === 'camera' && (
          <div className="space-y-6">
            <div>
              <h3 className="font-semibold text-gray-100">Visor e Teste da Câmera</h3>
              <p className="text-xs text-gray-400">Status da câmera CoreS3 e visualização da captura de imagem JPEG.</p>
            </div>

            <div className="flex flex-col items-center">
              <div className="w-full max-w-md aspect-[4/3] bg-gray-950 rounded-2xl border border-gray-800 overflow-hidden shadow-2xl relative">
                <img
                  src="/api/camera/latest.jpg"
                  alt="Visor da Câmera da StackChan"
                  className="w-full h-full object-cover"
                />
              </div>

              <div className="mt-4 flex gap-3">
                <a
                  href="/api/camera/latest.jpg"
                  target="_blank"
                  rel="noreferrer"
                  className="px-4 py-2 rounded-xl bg-gray-800 hover:bg-gray-700 text-gray-200 text-xs font-semibold border border-gray-700"
                >
                  Abrir /api/camera/jpeg
                </a>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
