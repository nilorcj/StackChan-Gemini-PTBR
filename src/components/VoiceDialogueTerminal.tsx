import React, { useState, useEffect, useRef } from 'react';
import { Mic, MicOff, Send, Sparkles, Volume2, Bot, User, Radio, Sliders, Check } from 'lucide-react';
import { droidAudio } from '../utils/audio';

interface Message {
  id: string;
  sender: 'user' | 'assistant' | 'system';
  text: string;
  emotion?: string;
  gesture?: string;
  timestamp: string;
}

interface VoiceDialogueTerminalProps {
  isListening: boolean;
  isSpeaking: boolean;
  voiceName: string;
  geminiModel: string;
  onToggleVoice: () => void;
  onSendText: (text: string) => Promise<{ response: string; emotion: string; gesture: string } | null>;
}

export const VoiceDialogueTerminal: React.FC<VoiceDialogueTerminalProps> = ({
  isListening,
  isSpeaking,
  voiceName,
  geminiModel,
  onToggleVoice,
  onSendText,
}) => {
  const [messages, setMessages] = useState<Message[]>([
    {
      id: 'm1',
      sender: 'system',
      text: 'Canal Gemini Live da StackChan conectado. Sessão de voz pronta.',
      timestamp: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
    },
    {
      id: 'm2',
      sender: 'assistant',
      text: 'E aí! Pronta pra te ouvir, te dar uns conselhos de verdade ou rir um pouco das suas ideias. O que manda hoje?',
      emotion: 'happy',
      timestamp: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
    },
  ]);
  const [inputText, setInputText] = useState('');
  const [interimTranscript, setInterimTranscript] = useState('');
  const [isProcessing, setIsProcessing] = useState(false);
  const [micRecognizing, setMicRecognizing] = useState(false);
  const [micError, setMicError] = useState<string | null>(null);
  const [showVoiceSettings, setShowVoiceSettings] = useState(false);
  const [availableVoices, setAvailableVoices] = useState<SpeechSynthesisVoice[]>([]);
  const [selectedVoiceUri, setSelectedVoiceUri] = useState<string>('');
  const [pitch, setPitch] = useState<number>(droidAudio.getVoicePitch());
  const [rate, setRate] = useState<number>(droidAudio.getVoiceRate());
  const [voiceTestFeedback, setVoiceTestFeedback] = useState(false);

  const recognitionRef = useRef<any>(null);
  const chatEndRef = useRef<HTMLDivElement>(null);
  const isListeningRef = useRef(isListening);
  isListeningRef.current = isListening;

  // Auto scroll to bottom
  useEffect(() => {
    chatEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages, isProcessing, interimTranscript]);

  // Load available system/browser voices
  useEffect(() => {
    const updateVoices = () => {
      const vList = droidAudio.getAvailableVoices();
      setAvailableVoices(vList);
      const currentUri = droidAudio.getSelectedVoiceUri();
      if (currentUri) setSelectedVoiceUri(currentUri);
    };

    updateVoices();
    if (typeof window !== 'undefined' && 'speechSynthesis' in window) {
      window.speechSynthesis.onvoiceschanged = updateVoices;
    }
  }, []);

  // Web Speech API initialization with interim results & auto-continuous handling
  useEffect(() => {
    const SpeechRecognitionClass = (window as any).SpeechRecognition || (window as any).webkitSpeechRecognition;
    if (SpeechRecognitionClass) {
      const recognition = new SpeechRecognitionClass();
      recognition.continuous = true;
      recognition.interimResults = true;
      recognition.lang = 'pt-BR';

      recognition.onstart = () => {
        setMicRecognizing(true);
        setMicError(null);
      };

      recognition.onresult = (event: any) => {
        let interim = '';
        let final = '';

        for (let i = event.resultIndex; i < event.results.length; ++i) {
          if (event.results[i].isFinal) {
            final += event.results[i][0].transcript;
          } else {
            interim += event.results[i][0].transcript;
          }
        }

        setInterimTranscript(interim);

        if (final.trim()) {
          setInterimTranscript('');
          handleSendMessage(final.trim());
        }
      };

      recognition.onerror = (event: any) => {
        console.warn('Speech recognition event:', event.error);
        if (event.error === 'not-allowed') {
          setMicError('Permissão de microfone negada no navegador. Permita o microfone ou digite na barra abaixo.');
        } else if (event.error === 'no-speech') {
          // Normal timeout when quiet
        } else {
          setMicError(`Status de voz: ${event.error}`);
        }
      };

      recognition.onend = () => {
        setMicRecognizing(false);
        setInterimTranscript('');
        // If listening mode is still active, restart seamlessly
        if (isListeningRef.current) {
          try {
            recognition.start();
          } catch {
            // Ignore if busy
          }
        }
      };

      recognitionRef.current = recognition;
    }
  }, []);

  // Sync speech recognition with robot listening toggle
  useEffect(() => {
    if (!recognitionRef.current) return;
    if (isListening && !micRecognizing) {
      try {
        recognitionRef.current.start();
        setMicRecognizing(true);
      } catch {
        // Ignore
      }
    } else if (!isListening && micRecognizing) {
      try {
        recognitionRef.current.stop();
        setMicRecognizing(false);
      } catch {
        // Ignore
      }
    }
  }, [isListening]);

  const toggleMicRecognition = () => {
    if (micRecognizing) {
      recognitionRef.current?.stop();
      setMicRecognizing(false);
      setInterimTranscript('');
    } else {
      droidAudio.playWakeChirp();
      try {
        recognitionRef.current?.start();
        setMicRecognizing(true);
        setMicError(null);
      } catch (err) {
        console.warn('Could not start recognition:', err);
      }
    }
  };

  const handleTestVoice = (customPhrase?: string) => {
    setVoiceTestFeedback(true);
    droidAudio.testVoice(customPhrase);
    setTimeout(() => setVoiceTestFeedback(false), 2500);
  };

  const handleVoiceChange = (uri: string) => {
    setSelectedVoiceUri(uri);
    droidAudio.setSelectedVoiceUri(uri || null);
    droidAudio.testVoice('Oi! Essa é a minha nova voz de companheira.');
  };

  const handlePitchChange = (newPitch: number) => {
    setPitch(newPitch);
    droidAudio.setVoicePitch(newPitch);
  };

  const handleRateChange = (newRate: number) => {
    setRate(newRate);
    droidAudio.setVoiceRate(newRate);
  };

  const handleSendMessage = async (textToSend?: string) => {
    const text = textToSend || inputText;
    if (!text.trim() || isProcessing) return;

    const userMsg: Message = {
      id: 'msg_' + Date.now().toString(36),
      sender: 'user',
      text,
      timestamp: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
    };

    setMessages((prev) => [...prev, userMsg]);
    setInputText('');
    setIsProcessing(true);

    droidAudio.playBeep(880, 0.05);

    try {
      const res = await onSendText(text);
      if (res) {
        const botMsg: Message = {
          id: 'msg_' + (Date.now() + 1).toString(36),
          sender: 'assistant',
          text: res.response,
          emotion: res.emotion,
          gesture: res.gesture,
          timestamp: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
        };
        setMessages((prev) => [...prev, botMsg]);
      }
    } catch (err) {
      console.error('Send error:', err);
    } finally {
      setIsProcessing(false);
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSendMessage();
    }
  };

  return (
    <div className="bg-gray-900/90 border border-gray-800 rounded-2xl p-5 shadow-xl flex flex-col h-[530px]">
      {/* Header with voice status & quick controls */}
      <div className="flex items-center justify-between pb-3 border-b border-gray-800">
        <div className="flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-xl bg-pink-500/20 border border-pink-500/30 flex items-center justify-center text-pink-400">
            <Radio className="w-4 h-4 animate-pulse" />
          </div>
          <div>
            <h3 className="font-semibold text-gray-100 text-sm flex items-center gap-2">
              <span>Gemini Live — Voz e Diálogo</span>
              <span className="text-[10px] px-1.5 py-0.5 rounded bg-pink-500/20 text-pink-300 font-mono">♀ Feminina</span>
            </h3>
            <div className="flex items-center gap-2 text-[11px] text-gray-400 font-mono">
              <span className="text-pink-300 font-semibold">{voiceName}</span>
              <span>•</span>
              <span className="text-amber-400/90">{geminiModel}</span>
            </div>
          </div>
        </div>

        <div className="flex items-center gap-2">
          {/* Test Voice Button */}
          <button
            id="test-female-voice-btn"
            onClick={() => handleTestVoice()}
            className={`px-2.5 py-1.5 rounded-xl text-xs font-semibold flex items-center gap-1.5 transition ${
              voiceTestFeedback
                ? 'bg-pink-500 text-gray-950 font-bold shadow'
                : 'bg-gray-800 hover:bg-gray-700 text-pink-300 border border-pink-500/30'
            }`}
            title="Ouvir amostra da voz feminina"
          >
            <Volume2 className="w-3.5 h-3.5" />
            <span className="hidden sm:inline">Testar Voz</span>
          </button>

          {/* Voice Tuning Config Toggle */}
          <button
            onClick={() => setShowVoiceSettings(!showVoiceSettings)}
            className={`p-1.5 rounded-xl border text-xs transition ${
              showVoiceSettings
                ? 'bg-pink-500 text-gray-950 border-pink-400 font-bold'
                : 'bg-gray-800 hover:bg-gray-700 text-gray-300 border-gray-700'
            }`}
            title="Ajustar timbre, tom (pitch) e selecionar voz feminina"
          >
            <Sliders className="w-3.5 h-3.5" />
          </button>

          {/* Wake Mic Toggle */}
          <button
            id="voice-wake-toggle-btn"
            onClick={onToggleVoice}
            className={`px-3 py-1.5 rounded-xl text-xs font-semibold flex items-center gap-1.5 transition ${
              isListening
                ? 'bg-sky-500 text-gray-950 shadow-md shadow-sky-500/30 animate-pulse'
                : 'bg-gray-800 hover:bg-gray-700 text-gray-300 border border-gray-700'
            }`}
          >
            {isListening ? <Mic className="w-3.5 h-3.5" /> : <MicOff className="w-3.5 h-3.5" />}
            {isListening ? 'Ouvindo' : 'Acordar'}
          </button>
        </div>
      </div>

      {/* Expandable Voice Settings Drawer */}
      {showVoiceSettings && (
        <div className="p-3.5 my-2 bg-gray-950 rounded-xl border border-pink-500/40 space-y-3 animate-fadeIn">
          <div className="flex items-center justify-between">
            <span className="text-xs font-bold text-pink-300 flex items-center gap-1.5">
              <span>🎙️ Ajustes da Síntese de Voz (PT-BR Feminina)</span>
            </span>
            <button
              onClick={() => handleTestVoice('Voz feminina da StackChan calibrada!')}
              className="text-[11px] px-2 py-0.5 rounded bg-pink-500/20 text-pink-300 hover:bg-pink-500/30 border border-pink-500/40"
            >
              Testar Calibração
            </button>
          </div>

          {/* Voice Selector Dropdown */}
          <div>
            <label className="block text-[11px] text-gray-400 mb-1">
              Voz do Navegador / Sistema Operacional:
            </label>
            <select
              value={selectedVoiceUri}
              onChange={(e) => handleVoiceChange(e.target.value)}
              className="w-full px-2.5 py-1.5 bg-gray-900 border border-gray-700 rounded-lg text-xs font-mono text-gray-200"
            >
              <option value="">✨ Seleção Automática Inteligente (Filtro Feminino Estrito)</option>
              {availableVoices
                .filter((v) => v.lang.startsWith('pt') || v.lang.startsWith('en'))
                .map((v) => {
                  const isFemaleName = [
                    'francisca',
                    'thalita',
                    'luciana',
                    'helena',
                    'yara',
                    'maria',
                    'brenda',
                    'leticia',
                    'letícia',
                    'camila',
                    'vitoria',
                    'vitória',
                    'female',
                    'mulher',
                    'samantha',
                    'victoria',
                    'zira',
                  ].some((f) => v.name.toLowerCase().includes(f));

                  return (
                    <option key={v.voiceURI} value={v.voiceURI}>
                      {isFemaleName ? '♀ [Feminina] ' : ''}
                      {v.name} ({v.lang})
                    </option>
                  );
                })}
            </select>
          </div>

          {/* Pitch and Speed Controls */}
          <div className="grid grid-cols-2 gap-3">
            <div>
              <div className="flex justify-between text-[11px] text-gray-300 mb-1">
                <span>Tom / Agudo (Pitch):</span>
                <span className="font-mono text-pink-400">{pitch.toFixed(2)}x</span>
              </div>
              <input
                type="range"
                min="0.8"
                max="1.8"
                step="0.05"
                value={pitch}
                onChange={(e) => handlePitchChange(parseFloat(e.target.value))}
                className="w-full accent-pink-500 h-1.5 bg-gray-800 rounded cursor-pointer"
              />
              <span className="text-[10px] text-gray-500">Valores altos (1.30+) dão timbre jovem e feminino</span>
            </div>

            <div>
              <div className="flex justify-between text-[11px] text-gray-300 mb-1">
                <span>Velocidade (Rate):</span>
                <span className="font-mono text-amber-400">{rate.toFixed(2)}x</span>
              </div>
              <input
                type="range"
                min="0.8"
                max="1.5"
                step="0.05"
                value={rate}
                onChange={(e) => handleRateChange(parseFloat(e.target.value))}
                className="w-full accent-amber-500 h-1.5 bg-gray-800 rounded cursor-pointer"
              />
              <span className="text-[10px] text-gray-500">Ritmo de fala dinâmico</span>
            </div>
          </div>
        </div>
      )}

      {/* Messages Scroll Area */}
      <div className="flex-1 overflow-y-auto py-4 space-y-3.5 pr-1">
        {messages.map((m) => (
          <div
            key={m.id}
            className={`flex gap-2.5 ${m.sender === 'user' ? 'justify-end' : 'justify-start'}`}
          >
            {m.sender === 'assistant' && (
              <div className="w-7 h-7 rounded-lg bg-pink-500/20 border border-pink-500/30 flex items-center justify-center text-pink-300 shrink-0 mt-0.5">
                <Bot className="w-4 h-4" />
              </div>
            )}

            <div
              className={`max-w-[80%] rounded-2xl px-4 py-2.5 text-xs leading-relaxed ${
                m.sender === 'user'
                  ? 'bg-amber-500 text-gray-950 font-medium rounded-tr-xs'
                  : m.sender === 'assistant'
                  ? 'bg-gray-800 border border-gray-700 text-gray-100 rounded-tl-xs'
                  : 'bg-gray-950 border border-gray-800 text-gray-400 font-mono text-[11px] w-full text-center'
              }`}
            >
              <p>{m.text}</p>

              {(m.emotion || m.gesture) && (
                <div className="mt-1 pt-1 border-t border-gray-700/60 flex items-center gap-2 text-[10px] font-mono text-pink-300/80">
                  {m.emotion && <span>EMOÇÃO: {m.emotion}</span>}
                  {m.gesture && <span>GESTO: {m.gesture}</span>}
                </div>
              )}
            </div>

            {m.sender === 'user' && (
              <div className="w-7 h-7 rounded-lg bg-gray-800 border border-gray-700 flex items-center justify-center text-gray-300 shrink-0 mt-0.5">
                <User className="w-4 h-4" />
              </div>
            )}
          </div>
        ))}

        {isProcessing && (
          <div className="flex items-center gap-2 text-xs text-pink-400 font-mono">
            <div className="w-2 h-2 rounded-full bg-pink-400 animate-ping"></div>
            <span>Pix está pensando e formulando a resposta...</span>
          </div>
        )}

        {interimTranscript && (
          <div className="flex items-center justify-end gap-2">
            <div className="bg-amber-500/30 border border-amber-400/40 text-amber-200 rounded-2xl px-4 py-2 text-xs italic flex items-center gap-2">
              <span className="w-2 h-2 rounded-full bg-amber-400 animate-pulse"></span>
              <span>"{interimTranscript}..."</span>
            </div>
          </div>
        )}

        {micRecognizing && !interimTranscript && (
          <div className="flex items-center gap-2 text-xs text-emerald-400 font-mono">
            <div className="w-2 h-2 rounded-full bg-emerald-400 animate-ping"></div>
            <span>🎤 Microfone ativo! Pode falar que a StackChan está te ouvindo...</span>
          </div>
        )}

        {micError && (
          <div className="p-2 bg-red-950/40 border border-red-800/60 rounded-xl text-[11px] text-red-300 flex items-center justify-between">
            <span>{micError}</span>
            <button onClick={() => setMicError(null)} className="text-red-400 font-bold ml-2">✕</button>
          </div>
        )}

        <div ref={chatEndRef} />
      </div>

      {/* Suggested Quick Prompts */}
      <div className="flex gap-2 overflow-x-auto pb-2 text-[11px]">
        <button
          onClick={() => handleSendMessage('StackChan, como tá a temperatura e o clima aqui na sala agora?')}
          className="px-2.5 py-1 rounded-full bg-amber-500/20 hover:bg-amber-500/30 text-amber-300 border border-amber-500/30 whitespace-nowrap font-medium"
        >
          🌡️ Está quente hoje?
        </button>
        <button
          onClick={() => handleSendMessage('E a umidade do ar, tá seca ou agradável?')}
          className="px-2.5 py-1 rounded-full bg-sky-500/20 hover:bg-sky-500/30 text-sky-300 border border-sky-500/30 whitespace-nowrap font-medium"
        >
          💧 Como tá a umidade?
        </button>
        <button
          onClick={() => handleSendMessage('Você tá me vendo? A que distância eu tô de você?')}
          className="px-2.5 py-1 rounded-full bg-purple-500/20 hover:bg-purple-500/30 text-purple-300 border border-purple-500/30 whitespace-nowrap font-medium"
        >
          📏 O que seu sensor TOF vê?
        </button>
        <button
          onClick={() => handleSendMessage('StackChan, quais são as principais notícias de tecnologia e do mundo de hoje?')}
          className="px-2.5 py-1 rounded-full bg-emerald-500/20 hover:bg-emerald-500/30 text-emerald-300 border border-emerald-500/30 whitespace-nowrap font-medium"
        >
          📰 Principais notícias de hoje
        </button>
        <button
          onClick={() => handleSendMessage('Me conta uma curiosidade aleatória e surpreendente do mundo!')}
          className="px-2.5 py-1 rounded-full bg-pink-500/20 hover:bg-pink-500/30 text-pink-300 border border-pink-500/30 whitespace-nowrap font-medium"
        >
          ✨ Me conta uma curiosidade
        </button>
        <button
          onClick={() => handleSendMessage('StackChan, preciso de um conselho sincero de amiga!')}
          className="px-2.5 py-1 rounded-full bg-gray-800/80 hover:bg-gray-700 text-gray-300 border border-gray-700 whitespace-nowrap"
        >
          💡 Me dá um conselho
        </button>
        <button
          onClick={() => handleSendMessage('Tô pensando em comprar um negócio por impulso, me ajuda a não gastar')}
          className="px-2.5 py-1 rounded-full bg-gray-800/80 hover:bg-gray-700 text-gray-300 border border-gray-700 whitespace-nowrap"
        >
          💸 Puxa minha orelha
        </button>
      </div>

      {/* Input Bar */}
      <div className="pt-2 border-t border-gray-800 flex gap-2 items-center">
        {/* Live Mic Recognition Button */}
        <button
          id="mic-speech-btn"
          onClick={toggleMicRecognition}
          className={`p-2.5 rounded-xl border transition ${
            micRecognizing
              ? 'bg-red-500 text-white border-red-400 animate-pulse'
              : 'bg-gray-800 hover:bg-gray-700 text-gray-300 border-gray-700'
          }`}
          title="Falar pelo Microfone (Web Speech)"
        >
          <Mic className="w-4 h-4" />
        </button>

        <input
          id="dialogue-text-input"
          type="text"
          value={inputText}
          onChange={(e) => setInputText(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder="Faça uma pergunta para a StackChan ou envie um comando..."
          className="flex-1 px-4 py-2.5 bg-gray-950 border border-gray-800 rounded-xl text-xs text-gray-100 placeholder-gray-500 focus:outline-none focus:border-amber-500"
        />

        <button
          id="dialogue-send-btn"
          onClick={() => handleSendMessage()}
          disabled={!inputText.trim() || isProcessing}
          className="px-4 py-2.5 rounded-xl bg-amber-500 hover:bg-amber-400 disabled:opacity-50 text-gray-950 font-bold text-xs flex items-center gap-1.5 transition shadow"
        >
          <Send className="w-3.5 h-3.5" />
          Enviar
        </button>
      </div>
    </div>
  );
};
