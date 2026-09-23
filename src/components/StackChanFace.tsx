import React, { useEffect, useState } from 'react';
import { motion } from 'framer-motion';
import { EmotionType } from '../types';

interface StackChanFaceProps {
  emotion: EmotionType;
  yawDeg: number; // -128 to 128
  pitchDeg: number; // 0 to 90
  isSpeaking: boolean;
  isListening: boolean;
  speakerVolume: number;
  isShaking?: boolean;
  isLifted?: boolean;
  isPetting?: boolean;
  isMotionActive?: boolean;
  motionSensitivity?: 'low' | 'medium' | 'high';
  onToggleMotionSensor?: () => void;
  onChangeMotionSensitivity?: (sens: 'low' | 'medium' | 'high') => void;
  onTouchSection?: (section: 'front' | 'middle' | 'back') => void;
  onVoiceToggle?: () => void;
  onShakeTrigger?: () => void;
  onLiftTrigger?: () => void;
}

// Pensamentos diários da vida adulta que passam na cabeça da StackChan ao descansar
const DREAM_IDEAS = [
  { icon: '💸', title: 'Declaração de IR anual', detail: 'Recolher informes de rendimentos e despesas', tag: 'Finanças' },
  { icon: '🥂', title: 'Organizar festa de 1 ano do filho', detail: 'Contratar buffet e gerenciar lista de convidados', tag: 'Família' },
  { icon: '✈️', title: 'Planejamento de férias em família', detail: 'Passagens, hotel, seguro e roteiro para todos', tag: 'Viagens' },
  { icon: '💼', title: 'Segurança no emprego', detail: 'Avaliação de desempenho chegando e métricas pendentes', tag: 'Carreira' },
  { icon: '🔧', title: 'Manutenção preventiva do carro', detail: 'Agendar revisão anual e orçar seguro', tag: 'Vida' },
  { icon: '💬', title: 'Conversa difícil sobre finanças', detail: 'Ajustar orçamento familiar com o parceiro', tag: 'Relacionamento' },
  { icon: '🎓', title: 'Fundo para faculdade dos filhos', detail: 'Aportes mensais e planejamento de longo prazo', tag: 'Educação' },
  { icon: '📚', title: 'Ler livro novo', detail: 'Terminar o capítulo da semana', tag: 'Hobby' },
  { icon: '🏃', title: 'Treino de corrida', detail: 'Preparação para 5km', tag: 'Saúde' },
  { icon: '🎨', title: 'Aprender aquarela', detail: 'Praticar técnicas de mistura de cores', tag: 'Arte' },
  { icon: '🍳', title: 'Testar receita nova', detail: 'Fazer aquele bolo de chocolate famoso', tag: 'Culinária' },
  { icon: '🌳', title: 'Plantar horta', detail: 'Cuidar das mudas de manjericão', tag: 'Natureza' },
  { icon: '🧘', title: 'Sessão de meditação', detail: '15 minutos de silêncio absoluto', tag: 'Bem-estar' },
  { icon: '🧩', title: 'Montar quebra-cabeça', detail: 'Quebra-cabeça de 1000 peças', tag: 'Lazer' },
  { icon: '🎹', title: 'Praticar teclado', detail: 'Aprender uma música nova', tag: 'Música' },
];

// Minimal brightness multi-color cycle for dreaming LEDs (brilho mínimo, tons suaves)
const DREAM_LED_PALETTE = [
  'rgba(168, 85, 247, 0.22)', // Violeta sonhador tênue
  'rgba(56, 189, 248, 0.22)',  // Ciano estelar suave
  'rgba(52, 211, 153, 0.20)',  // Verde aurora sutil
  'rgba(251, 191, 36, 0.20)',  // Âmbar suave
  'rgba(244, 114, 182, 0.22)', // Rosa nebulosa tênue
  'rgba(129, 140, 248, 0.22)', // Índigo calmo
];

export const StackChanFace: React.FC<StackChanFaceProps> = ({
  emotion,
  yawDeg,
  pitchDeg,
  isSpeaking,
  isListening,
  speakerVolume,
  isShaking = false,
  isLifted = false,
  isPetting = false,
  isMotionActive = false,
  motionSensitivity = 'medium',
  onToggleMotionSensor,
  onChangeMotionSensitivity,
  onTouchSection,
  onVoiceToggle,
  onShakeTrigger,
  onLiftTrigger,
}) => {
  const [blink, setBlink] = useState(false);
  const [mouthOpen, setMouthOpen] = useState(0.1);
  const [speechPhoneme, setSpeechPhoneme] = useState(0);
  const [ledColor, setLedColor] = useState('rgba(56, 189, 248, 0.8)');
  const [dreamLedIndex, setDreamLedIndex] = useState(0);
  const [gazeSaccade, setGazeSaccade] = useState({ x: 0, y: 0 });
  const [currentDreamIndex, setCurrentDreamIndex] = useState(0);
  const [smoothedOffset, setSmoothedOffset] = useState({ x: 0, y: 0 });
  const [showDream, setShowDream] = useState(true);
  const isDreaming = emotion === 'sleep';

  // Cycle dream ideas when sleeping/dreaming
  useEffect(() => {
    if (!isDreaming) return;
    const interval = setInterval(() => {
      setCurrentDreamIndex((prev) => (prev + 1) % DREAM_IDEAS.length);
    }, 4500);
    return () => clearInterval(interval);
  }, [isDreaming]);

  // Alternating state: 3s Face, 3s Dream Popup
  useEffect(() => {
    if (!isDreaming) return;
    const timer = setInterval(() => setShowDream(p => !p), 3000);
    return () => clearInterval(timer);
  }, [isDreaming]);

  // Smooth Eye Tracking Animation
  useEffect(() => {
    let frameId: number;
    
    const animate = () => {
      const targetX = Math.max(-10, Math.min(10, (yawDeg / 128) * 9 + gazeSaccade.x));
      const targetY = Math.max(-8, Math.min(8, ((pitchDeg - 12) / 45) * -7 + gazeSaccade.y));
      
      setSmoothedOffset(prev => ({
        x: prev.x + (targetX - prev.x) * 0.15, // 0.15 is the easing factor for smoother movement
        y: prev.y + (targetY - prev.y) * 0.15
      }));
      frameId = requestAnimationFrame(animate);
    };
    
    animate();
    return () => cancelAnimationFrame(frameId);
  }, [yawDeg, pitchDeg, gazeSaccade]);

  // Cycle dream ideas when sleeping/dreaming
  // (Removed: now fetched dynamically from API)

  // Subtle breathing LED animation for Dream mode (cycles through soft pastel colors at minimal brightness)
  useEffect(() => {
    if (isDreaming) {
      const ledTimer = setInterval(() => {
        setDreamLedIndex((prev) => (prev + 1) % DREAM_LED_PALETTE.length);
      }, 2800);
      return () => clearInterval(ledTimer);
    }
  }, [isDreaming]);

  // Natural Blinking Loop with variable frequency
  useEffect(() => {
    let timeout: NodeJS.Timeout;
    const triggerBlink = () => {
      if (emotion !== 'sleep') {
        setBlink(true);
        setTimeout(() => setBlink(false), 140);
      }
      const nextBlinkMs = 2400 + Math.random() * 3200;
      timeout = setTimeout(triggerBlink, nextBlinkMs);
    };

    timeout = setTimeout(triggerBlink, 2000);
    return () => clearTimeout(timeout);
  }, [emotion]);

  // Micro-Saccade Gaze Motion (Lifelike slight eye drift)
  useEffect(() => {
    if (emotion === 'sleep') return;
    const saccadeInterval = setInterval(() => {
      setGazeSaccade({
        x: (Math.random() - 0.5) * 6,
        y: (Math.random() - 0.5) * 4,
      });
    }, 1800);
    return () => clearInterval(saccadeInterval);
  }, [emotion]);

  // Mouth Movement Sync for speech
  useEffect(() => {
    if (!isSpeaking && emotion !== 'speaking') {
      setMouthOpen(0.1);
      return;
    }

    const interval = setInterval(() => {
      setMouthOpen(0.25 + Math.random() * 0.75);
      setSpeechPhoneme(Math.floor(Math.random() * 4));
    }, 90);

    return () => clearInterval(interval);
  }, [isSpeaking, emotion]);

  // Set NeoPixel / RGB LED Bar Color according to Robot State
  useEffect(() => {
    if (isDreaming) {
      setLedColor(DREAM_LED_PALETTE[dreamLedIndex]);
    } else if (isPetting) {
      setLedColor('rgba(244, 63, 94, 0.95)'); // Radiant warm rose for purring/affection
    } else {
      switch (emotion) {
        case 'happy':
          setLedColor('rgba(245, 158, 11, 0.9)'); // Warm golden amber
          break;
        case 'angry':
          setLedColor('rgba(239, 68, 68, 0.95)'); // Irritated vivid crimson
          break;
        case 'listening':
          setLedColor('rgba(56, 189, 248, 0.9)'); // Sky blue
          break;
        case 'speaking':
          setLedColor('rgba(16, 185, 129, 0.9)'); // Emerald green
          break;
        case 'thinking':
          setLedColor('rgba(168, 85, 247, 0.9)'); // Purple
          break;
        case 'found':
          setLedColor('rgba(236, 72, 153, 0.9)'); // Pink
          break;
        case 'error':
          setLedColor('rgba(220, 38, 38, 0.95)'); // Warning red
          break;
        case 'surprised':
          setLedColor('rgba(250, 204, 21, 0.95)'); // Bright electric yellow
          break;
        case 'looking':
          setLedColor('rgba(14, 165, 233, 0.9)'); // Cyan
          break;
        default:
          setLedColor('rgba(56, 189, 248, 0.8)'); // Calming sky blue
      }
    }
  }, [emotion, isDreaming, dreamLedIndex, isPetting]);

  // Render High-Resolution Pure White Rounded Eyes with feminine aesthetic & expressions
  const renderEye = (isLeft: boolean) => {
    const isSleeping = emotion === 'sleep';
    const isAngry = emotion === 'angry';
    const isHappy = emotion === 'happy';
    const isFound = emotion === 'found';
    const isLooking = emotion === 'looking';
    const isThinking = emotion === 'thinking';

    // Saccade & Servo Tracking offsets
    const offsetX = smoothedOffset.x;
    const offsetY = smoothedOffset.y;

    // Gradient IDs
    const irisGradientId = isAngry
      ? `irisCrimson${isLeft ? 'L' : 'R'}`
      : isThinking
      ? `irisViolet${isLeft ? 'L' : 'R'}`
      : `irisAmber${isLeft ? 'L' : 'R'}`;

    return (
      <div className="relative flex flex-col items-center">
        {/* Irritated/Grumpy Sweat Drop or Anger Mark when Angry */}
        {isAngry && (
          <div
            className={`absolute -top-6 ${isLeft ? '-left-2' : '-right-2'} text-red-500 font-bold text-base animate-bounce select-none pointer-events-none z-30`}
          >
            💢
          </div>
        )}

        {/* Outer 3D Pure White Rounded Eye Container */}
        <div
          className={`relative w-24 h-24 sm:w-26 sm:h-26 rounded-full p-1 transition-transform duration-150 ${
            isAngry ? 'scale-95' : isFound ? 'scale-105' : 'scale-100'
          }`}
          style={{
            boxShadow: isSleeping
              ? 'inset 0 2px 6px rgba(0,0,0,0.4), 0 0 10px rgba(168, 85, 247, 0.15)'
              : isAngry
              ? 'inset 0 2px 6px rgba(0,0,0,0.5), 0 0 16px rgba(239, 68, 68, 0.45)'
              : 'inset 0 2px 7px rgba(0,0,0,0.3), 0 4px 14px rgba(0,0,0,0.6)',
          }}
        >
          {/* Top Delicate Female Eyelash on upper rim */}
          {!isSleeping && (
            <div
              className={`absolute -top-1.5 ${isLeft ? 'left-3' : 'right-3'} z-20 pointer-events-none`}
            >
              <svg width="24" height="8" viewBox="0 0 24 8">
                <path
                  d={isLeft ? 'M 2 7 Q 10 1, 22 2' : 'M 22 7 Q 14 1, 2 2'}
                  fill="none"
                  stroke={isAngry ? '#dc2626' : '#334155'}
                  strokeWidth="2.2"
                  strokeLinecap="round"
                />
                {/* Secondary side lash */}
                <path
                  d={isLeft ? 'M 1 7 L 4 3' : 'M 23 7 L 20 3'}
                  stroke={isAngry ? '#ef4444' : '#64748b'}
                  strokeWidth="1.6"
                  strokeLinecap="round"
                />
              </svg>
            </div>
          )}

          {/* 1. ANGRY / IRRITATED STATE: Expressive irritated eyes with angled upper brow */}
          {isAngry ? (
            <svg viewBox="0 0 90 90" className="w-full h-full bg-white rounded-full overflow-hidden">
              <defs>
                <radialGradient id={`angrySclera-${isLeft ? 'l' : 'r'}`} cx="46%" cy="42%" r="58%">
                  <stop offset="0%" stopColor="#ffffff" />
                  <stop offset="75%" stopColor="#fff1f2" />
                  <stop offset="100%" stopColor="#ffe4e6" />
                </radialGradient>
                <radialGradient id={`angryIris${isLeft ? 'L' : 'R'}`} cx="44%" cy="38%" r="54%">
                  <stop offset="0%" stopColor="#fca5a5" />
                  <stop offset="35%" stopColor="#ef4444" />
                  <stop offset="75%" stopColor="#b91c1c" />
                  <stop offset="100%" stopColor="#7f1d1d" />
                </radialGradient>
                <clipPath id={`angryClip-${isLeft ? 'l' : 'r'}`}>
                  <circle cx="45" cy="45" r="44" />
                </clipPath>
              </defs>

              <g clipPath={`url(#angryClip-${isLeft ? 'l' : 'r'})`}>
                <circle cx="45" cy="45" r="44" fill={`url(#angrySclera-${isLeft ? 'l' : 'r'})`} />

                {/* Iris & Pupil */}
                <g transform={`translate(${offsetX * 0.9}, ${offsetY * 0.9})`}>
                  <circle cx="45" cy="48" r="22" fill={`url(#angryIris${isLeft ? 'L' : 'R'})`} />
                  <circle cx="45" cy="48" r="8" fill="#0f172a" />
                  {/* Irritated pin-point glint */}
                  <circle cx="41" cy="44" r="3" fill="#ffffff" opacity="0.95" />
                </g>

                {/* Angled Irritated Eyelid */}
                <path
                  d={
                    isLeft
                      ? 'M -5 -5 L 95 -5 L 95 38 L -5 62 Z'
                      : 'M -5 -5 L 95 -5 L 95 62 L -5 38 Z'
                  }
                  fill="#0f172a"
                />
                {/* Red irritated eyeliner accent */}
                <line
                  x1={isLeft ? '0' : '90'}
                  y1={isLeft ? '62' : '62'}
                  x2={isLeft ? '90' : '0'}
                  y2={isLeft ? '38' : '38'}
                  stroke="#ef4444"
                  strokeWidth="4"
                  strokeLinecap="round"
                />
              </g>
            </svg>
          ) : isSleeping ? (
            /* 2. SLEEPING STATE: Cute closed curved crescent arcs with delicate lashes */
            <svg viewBox="0 0 90 90" className="w-full h-full bg-slate-900 rounded-full">
              <defs>
                <radialGradient id="sleepSclera" cx="50%" cy="50%" r="50%">
                  <stop offset="0%" stopColor="#1e1b4b" />
                  <stop offset="100%" stopColor="#0f172a" />
                </radialGradient>
              </defs>
              <circle cx="45" cy="45" r="44" fill="url(#sleepSclera)" stroke="#312e81" strokeWidth="1" />

              {/* Gentle curved sleeping eyelid */}
              <path
                d="M 16 48 Q 45 68, 74 48"
                fill="none"
                stroke="#a5b4fc"
                strokeWidth="4.2"
                strokeLinecap="round"
              />
              <path
                d="M 16 48 Q 45 64, 74 48"
                fill="none"
                stroke="#c7d2fe"
                strokeWidth="2"
                strokeLinecap="round"
              />

              {/* Cute sleeping feminine eyelashes */}
              <path d="M 28 55 L 23 64" stroke="#818cf8" strokeWidth="2.5" strokeLinecap="round" />
              <path d="M 45 58 L 45 68" stroke="#818cf8" strokeWidth="2.5" strokeLinecap="round" />
              <path d="M 62 55 L 67 64" stroke="#818cf8" strokeWidth="2.5" strokeLinecap="round" />

              {/* Soft dreamy sparkle */}
              <circle cx="45" cy="28" r="1.8" fill="#a78bfa" className="animate-pulse" />
              <circle cx="66" cy="26" r="1.4" fill="#c084fc" className="animate-pulse" />
            </svg>
          ) : isHappy ? (
            /* 3. HAPPY STATE: Joyful crescent smiling arcs on pure white sclera */
            <svg viewBox="0 0 90 90" className="w-full h-full bg-white rounded-full">
              <radialGradient id="happySclera" cx="50%" cy="50%" r="50%">
                <stop offset="0%" stopColor="#ffffff" />
                <stop offset="85%" stopColor="#fef3c7" />
                <stop offset="100%" stopColor="#fde68a" />
              </radialGradient>
              <circle cx="45" cy="45" r="44" fill="url(#happySclera)" />

              {/* Cheerful upward crescent line */}
              <path
                d="M 14 52 Q 45 22, 76 52"
                fill="none"
                stroke="#d97706"
                strokeWidth="5.2"
                strokeLinecap="round"
              />
              <path
                d="M 14 52 Q 45 29, 76 52"
                fill="none"
                stroke="#b45309"
                strokeWidth="2.8"
                strokeLinecap="round"
              />

              {/* Feminine upper lash flick */}
              <path
                d={isLeft ? 'M 76 52 Q 82 46, 86 42' : 'M 14 52 Q 8 46, 4 42'}
                stroke="#b45309"
                strokeWidth="2.4"
                strokeLinecap="round"
              />

              {/* Cute shine highlight */}
              <circle cx="45" cy="42" r="3.2" fill="#f59e0b" opacity="0.6" />
            </svg>
          ) : (
            /* 4. ACTIVE ROUNDED EYES (Neutral, Thinking, Looking, Listening, Speaking, Found) */
            <svg viewBox="0 0 90 90" className="w-full h-full bg-white rounded-full">
              <defs>
                {/* Pure White 3D Rounded Sclera Gradient */}
                <radialGradient id={`sclera-${isLeft ? 'l' : 'r'}`} cx="46%" cy="42%" r="58%">
                  <stop offset="0%" stopColor="#ffffff" />
                  <stop offset="70%" stopColor="#ffffff" />
                  <stop offset="90%" stopColor="#f8fafc" />
                  <stop offset="100%" stopColor="#e2e8f0" />
                </radialGradient>

                {/* Iris Amber Gradient */}
                <radialGradient id={`irisAmber${isLeft ? 'L' : 'R'}`} cx="44%" cy="38%" r="54%">
                  <stop offset="0%" stopColor="#fef08a" />
                  <stop offset="30%" stopColor="#f59e0b" />
                  <stop offset="75%" stopColor="#d97706" />
                  <stop offset="100%" stopColor="#78350f" />
                </radialGradient>

                {/* Iris Violet Gradient */}
                <radialGradient id={`irisViolet${isLeft ? 'L' : 'R'}`} cx="44%" cy="38%" r="54%">
                  <stop offset="0%" stopColor="#e9d5ff" />
                  <stop offset="35%" stopColor="#a855f7" />
                  <stop offset="75%" stopColor="#7e22ce" />
                  <stop offset="100%" stopColor="#3b0764" />
                </radialGradient>

                {/* Iris Crimson Gradient */}
                <radialGradient id={`irisCrimson${isLeft ? 'L' : 'R'}`} cx="44%" cy="38%" r="54%">
                  <stop offset="0%" stopColor="#fca5a5" />
                  <stop offset="35%" stopColor="#ef4444" />
                  <stop offset="75%" stopColor="#b91c1c" />
                  <stop offset="100%" stopColor="#7f1d1d" />
                </radialGradient>

                <clipPath id={`eyeClip-${isLeft ? 'l' : 'r'}`}>
                  <circle cx="45" cy="45" r="44" />
                </clipPath>
              </defs>

              {/* Sclera Base */}
              <circle cx="45" cy="45" r="44" fill={`url(#sclera-${isLeft ? 'l' : 'r'})`} />

              {/* Eye Content with Saccade Translation */}
              <g
                clipPath={`url(#eyeClip-${isLeft ? 'l' : 'r'})`}
                className="transition-transform duration-100 ease-out"
              >
                {/* Iris Group with Parallax */}
                <g transform={`translate(${offsetX * 0.7}, ${offsetY * 0.7})`}>
                  {/* Iris */}
                  <circle
                    cx="45"
                    cy="45"
                    r={isLooking || isFound ? 26 : 24}
                    fill={`url(#${irisGradientId})`}
                  />
                  {/* Limbal Ring */}
                  <circle
                    cx="45"
                    cy="45"
                    r={isLooking || isFound ? 26 : 24}
                    fill="none"
                    stroke="#0f172a"
                    strokeWidth="1.4"
                    opacity="0.75"
                  />
                </g>

                {/* Pupil Group with Higher Parallax */}
                <g transform={`translate(${offsetX * 1.3}, ${offsetY * 1.3})`}>
                  {/* High Contrast Black Pupil */}
                  <circle
                    cx="45"
                    cy="45"
                    r={isFound || isLooking ? 14 : 11}
                    fill="#0a0f1d"
                  />

                  {/* Specular Catchlights (Feminine anime glint) */}
                  {isFound ? (
                    <g transform="translate(45, 45)">
                      <path
                        d="M 0 -8 L 2.5 -2.5 L 8 0 L 2.5 2.5 L 0 8 L -2.5 2.5 L -8 0 L -2.5 -2.5 Z"
                        fill="#ffffff"
                      />
                    </g>
                  ) : (
                    <>
                      <circle cx="38" cy="37" r="4.5" fill="#ffffff" opacity="0.95" />
                      <circle cx="52" cy="52" r="2.2" fill="#ffffff" opacity="0.8" />
                      <circle cx="36" cy="50" r="1.4" fill="#ffffff" opacity="0.6" />
                    </>
                  )}
                </g>

                {/* Dynamic Blink Overlay */}
                {blink && <rect x="0" y="0" width="90" height="90" fill="#334155" />}
              </g>

              {/* Upper Eyelid / Lash Contour */}
              <path
                d="M 8 45 Q 45 10, 82 45"
                fill="none"
                stroke="#334155"
                strokeWidth="2.8"
                strokeLinecap="round"
              />
            </svg>
          )}
        </div>

        {/* Rosy Feminine Blush on Happy, Petting, Found, or Sleeping */}
        {(isHappy || isFound || isPetting) && (
          <div className="relative flex flex-col items-center mt-1">
            <div
              className={`rounded-full blur-xs transition-all duration-300 ${
                isPetting
                  ? 'w-14 h-4 bg-rose-500/60 shadow-lg shadow-rose-500/40 animate-pulse'
                  : 'w-12 h-3.5 bg-rose-500/35 animate-pulse'
              }`}
            ></div>

            {/* Floating Cuddle Heart Sparkle on Petting */}
            {isPetting && (
              <div
                className={`absolute -top-12 ${
                  isLeft ? '-left-1' : '-right-1'
                } text-sm select-none pointer-events-none animate-float-heart z-40`}
              >
                {isLeft ? '💖' : '✨'}
              </div>
            )}
          </div>
        )}
        {isSleeping && (
          <div className="w-8 h-2.5 bg-purple-400/30 rounded-full blur-xs mt-0.5"></div>
        )}
        {isAngry && (
          <div className="w-10 h-3 bg-red-600/40 rounded-full blur-xs mt-0.5 animate-pulse"></div>
        )}
      </div>
    );
  };

  // Render Emotion-Distinct Mouth
  const renderMouth = () => {
    if (emotion === 'sleep') {
      return (
        <div className="flex items-center gap-1 text-indigo-300 font-mono text-[11px] animate-pulse">
          <span className="opacity-40 text-[9px]">z</span>
          <span className="opacity-70 text-xs font-semibold">Z</span>
          <span className="font-bold text-sm text-amber-300">Z</span>
          <span className="text-xs">💤</span>
        </div>
      );
    }

    const isHappy = emotion === 'happy';
    const isAngry = emotion === 'angry';
    const isThinking = emotion === 'thinking';
    const isFound = emotion === 'found';
    const isLooking = emotion === 'looking';
    const isError = emotion === 'error';
    const isSurprised = emotion === 'surprised';
    const isTalking = isSpeaking || emotion === 'speaking';

    return (
      <div className="relative flex flex-col items-center justify-center">
        {isTalking ? (
          /* TALKING / DYNAMIC VISID MOUTH (Active speech visemes synced with voice) */
          <div
            className="w-14 bg-gradient-to-b from-rose-950 to-slate-950 rounded-full border-2 border-rose-500/80 transition-all duration-75 flex items-center justify-center shadow-lg shadow-rose-950/40"
            style={{
              height: `${Math.max(8, mouthOpen * 26)}px`,
            }}
          >
            <div className="w-6 h-2 bg-rose-400/80 rounded-full blur-[0.5px]"></div>
          </div>
        ) : isAngry ? (
          /* ANGRY / IRRITATED MOUTH (Pouty protest zig-zag mouth like Eilik) */
          <svg viewBox="0 0 80 32" className="w-22 h-9 overflow-visible">
            <defs>
              <linearGradient id="angryMouthGrad" x1="0%" y1="0%" x2="0%" y2="100%">
                <stop offset="0%" stopColor="#7f1d1d" />
                <stop offset="100%" stopColor="#450a0a" />
              </linearGradient>
            </defs>

            {/* Grumpy downturned mouth */}
            <path
              d="M 18 22 Q 40 10, 62 22"
              fill="none"
              stroke="#dc2626"
              strokeWidth="4"
              strokeLinecap="round"
            />
            <path
              d="M 18 22 L 14 26"
              stroke="#b91c1c"
              strokeWidth="3"
              strokeLinecap="round"
            />
            <path
              d="M 62 22 L 66 26"
              stroke="#b91c1c"
              strokeWidth="3"
              strokeLinecap="round"
            />
            {/* Small clamped pout line */}
            <line x1="30" y1="16" x2="50" y2="16" stroke="#f87171" strokeWidth="2" strokeLinecap="round" />
          </svg>
        ) : isHappy ? (
          /* HAPPY MOUTH */
          <svg viewBox="0 0 80 34" className="w-24 h-10 overflow-visible">
            <defs>
              <linearGradient id="happyOralGrad" x1="0%" y1="0%" x2="0%" y2="100%">
                <stop offset="0%" stopColor="#881337" />
                <stop offset="100%" stopColor="#4c0519" />
              </linearGradient>
            </defs>

            <path
              d="M 12 10 Q 40 42, 68 10 Q 40 14, 12 10 Z"
              fill="url(#happyOralGrad)"
              stroke="#e11d48"
              strokeWidth="2"
            />
            <path
              d="M 28 26 Q 40 34, 52 26 Q 40 22, 28 26 Z"
              fill="#fb7185"
            />
            <path
              d="M 10 10 Q 40 4, 70 10"
              fill="none"
              stroke="#f43f5e"
              strokeWidth="3.2"
              strokeLinecap="round"
            />
          </svg>
        ) : isThinking ? (
          /* THINKING MOUTH (Small curious 'o') */
          <div className="w-5 h-5 rounded-full border-2 border-purple-400 bg-purple-950/80 flex items-center justify-center animate-pulse">
            <div className="w-2 h-2 rounded-full bg-purple-300"></div>
          </div>
        ) : isFound ? (
          /* EXCITED / FOUND MOUTH */
          <svg viewBox="0 0 80 30" className="w-20 h-8 overflow-visible">
            <path
              d="M 18 10 Q 40 32, 62 10"
              fill="none"
              stroke="#ec4899"
              strokeWidth="3.4"
              strokeLinecap="round"
            />
          </svg>
        ) : isLooking ? (
          /* LOOKING MOUTH */
          <svg viewBox="0 0 80 24" className="w-16 h-6 overflow-visible">
            <path
              d="M 24 14 Q 40 8, 56 14"
              fill="none"
              stroke="#38bdf8"
              strokeWidth="2.8"
              strokeLinecap="round"
            />
          </svg>
        ) : isError ? (
          /* ERROR MOUTH (Wobbly flat zigzag) */
          <svg viewBox="0 0 80 24" className="w-18 h-6 overflow-visible">
            <path
              d="M 20 12 L 32 18 L 46 8 L 60 14"
              fill="none"
              stroke="#ef4444"
              strokeWidth="2.8"
              strokeLinecap="round"
            />
          </svg>
        ) : isSurprised ? (
          /* SURPRISED MOUTH (Round open 'o') */
          <div className="w-6 h-6 rounded-full border-2 border-amber-400 bg-amber-950/80 flex items-center justify-center shadow-md shadow-amber-500/30 animate-pulse">
            <div className="w-3 h-3 rounded-full bg-amber-300"></div>
          </div>
        ) : (
          /* NEUTRAL / LISTENING MOUTH (Cute subtle smile) */
          <svg viewBox="0 0 80 26" className="w-18 h-7 overflow-visible">
            <path
              d="M 25 14 Q 40 18, 55 14"
              fill="none"
              stroke="#f43f5e"
              strokeWidth="2.8"
              strokeLinecap="round"
            />
            <path
              d="M 32 17.5 Q 40 20, 48 17.5"
              fill="none"
              stroke="#fda4af"
              strokeWidth="1.4"
              strokeLinecap="round"
            />
          </svg>
        )}

        {/* Audio Wave Aura during speech */}
        {isTalking && (
          <div className="absolute -bottom-2 flex items-center gap-1">
            <span className="w-1 h-1.5 bg-emerald-400 rounded-full animate-bounce"></span>
            <span className="w-1.5 h-3 bg-emerald-400 rounded-full animate-bounce [animation-delay:0.12s]"></span>
            <span className="w-1 h-1.5 bg-emerald-400 rounded-full animate-bounce [animation-delay:0.24s]"></span>
          </div>
        )}
      </div>
    );
  };

  const dream = DREAM_IDEAS[currentDreamIndex];

  return (
    <div id="stackchan-robot-unit" className="relative flex flex-col items-center select-none">
      {/* Top Touch Sensor Bar (CoreS3 Top Cap) */}
      <div className="w-68 mb-1 flex items-center justify-between px-2.5 py-1 bg-gray-800/80 border border-gray-700/80 rounded-t-xl text-[10px] text-gray-400 font-mono">
        <button
          id="touch-sensor-front"
          onClick={() => onTouchSection?.('front')}
          className="px-2.5 py-1 rounded bg-gray-900/80 hover:bg-amber-500/20 hover:text-amber-300 transition-colors border border-gray-700/50"
          title="Sensor de Toque Frontal"
        >
          [FRENTE]
        </button>
        <button
          id="touch-sensor-mid"
          onClick={() => onTouchSection?.('middle')}
          className="px-2.5 py-1 rounded bg-gray-900/80 hover:bg-amber-500/20 hover:text-amber-300 transition-colors border border-gray-700/50"
          title="Sensor de Toque Central (Acordar / Carinho)"
        >
          [MEIO / CARINHO]
        </button>
        <button
          id="touch-sensor-back"
          onClick={() => onTouchSection?.('back')}
          className="px-2.5 py-1 rounded bg-gray-900/80 hover:bg-amber-500/20 hover:text-amber-300 transition-colors border border-gray-700/50"
          title="Sensor de Toque Traseiro"
        >
          [TRÁS]
        </button>
      </div>

      {/* Main CoreS3 Robot Head / Enclosure */}
      <div
        className={`relative w-80 h-72 bg-gradient-to-b from-gray-800 via-gray-850 to-gray-900 rounded-3xl p-3 border-2 transition-all ${
          isPetting
            ? 'animate-purr-vibrate border-rose-500/70 shadow-2xl shadow-rose-950/60'
            : isShaking
            ? 'animate-[wiggle_0.15s_ease-in-out_infinite] border-red-500 shadow-2xl shadow-red-950/80'
            : 'border-gray-700 shadow-2xl shadow-black/90 duration-300 ease-out'
        }`}
        style={{
          transform: isLifted
            ? `translateY(-14px) scale(1.04) rotateY(${yawDeg * 0.18}deg) rotateX(${-(pitchDeg - 12) * 0.22}deg)`
            : `rotateY(${yawDeg * 0.18}deg) rotateX(${-(pitchDeg - 12) * 0.22}deg)`,
          transformStyle: 'preserve-3d',
        }}
      >
        {/* Left RGB LED Strip Bar */}
        <div
          className="absolute -left-1.5 top-12 bottom-12 w-2 rounded-l-full"
          style={{
            backgroundColor: ledColor,
            boxShadow: isDreaming ? `0 0 5px ${ledColor}` : `0 0 18px ${ledColor}`,
            opacity: isDreaming ? 0.75 : 1,
            transition: isDreaming ? 'all 2.4s ease-in-out' : 'all 0.3s ease-out',
          }}
        ></div>

        {/* Right RGB LED Strip Bar */}
        <div
          className="absolute -right-1.5 top-12 bottom-12 w-2 rounded-r-full"
          style={{
            backgroundColor: ledColor,
            boxShadow: isDreaming ? `0 0 5px ${ledColor}` : `0 0 18px ${ledColor}`,
            opacity: isDreaming ? 0.75 : 1,
            transition: isDreaming ? 'all 2.4s ease-in-out' : 'all 0.3s ease-out',
          }}
        ></div>

        {/* Top Camera Lens & Ambient Light Sensor Cluster */}
        <div className="flex items-center justify-between px-4 pb-2 pt-0.5">
          <div className="flex items-center gap-1.5">
            <div className="w-3 h-3 bg-gray-950 rounded-full border border-gray-700 flex items-center justify-center shadow-inner">
              <div className="w-1.5 h-1.5 bg-blue-900 rounded-full"></div>
            </div>
            <span className="text-[9px] font-mono text-gray-400 tracking-wider">CÂMERA CORE-S3</span>
          </div>

          <div className="flex items-center gap-2">
            {/* LTR-553 Proximity & Light Sensor Dot */}
            <div
              className={`w-2 h-2 rounded-full ${
                emotion === 'angry'
                  ? 'bg-red-500'
                  : emotion === 'sleep'
                  ? 'bg-purple-400'
                  : 'bg-emerald-500'
              } animate-pulse`}
              title="Sensor LTR-553 ALS/PS Ativo"
            ></div>
            <div className="flex gap-0.5">
              <div className="w-1 h-1 bg-gray-600 rounded-full"></div>
              <div className="w-1 h-1 bg-gray-600 rounded-full"></div>
              <div className="w-1 h-1 bg-gray-600 rounded-full"></div>
            </div>
          </div>
        </div>

        {/* CoreS3 Display Screen */}
        <div
          id="stackchan-screen"
          onClick={onVoiceToggle}
          className={`relative flex-1 bg-gradient-to-b from-slate-950 via-slate-900 to-slate-950 rounded-2xl border flex flex-col items-center justify-between p-2.5 cursor-pointer overflow-hidden transition-all duration-300 ${
            emotion === 'angry'
              ? 'border-red-900/60 shadow-inner shadow-red-950/50'
              : emotion === 'sleep'
              ? 'border-purple-900/60 shadow-inner shadow-purple-950/40'
              : 'border-slate-800'
          }`}
          title="Clique na tela para alternar voz Ativa / Silêncio"
        >
          {/* Screen glare gloss reflection */}
          <div className="absolute inset-0 bg-gradient-to-br from-white/5 via-transparent to-transparent pointer-events-none rounded-2xl"></div>

          {/* Thinking Thought Bubble (Now inside the screen container) */}
          {emotion === 'thinking' && (
            <motion.div
              initial={{ opacity: 0, scale: 0.5, y: 10 }}
              animate={{ opacity: 1, scale: 1, y: 10 }}
              exit={{ opacity: 0, scale: 0.5 }}
              className="absolute top-4 left-0 right-0 z-40 flex items-center justify-center"
            >
              <div className="flex items-center justify-center gap-1.5 px-3 py-2 bg-gray-900/90 border border-purple-500/50 rounded-full shadow-lg shadow-purple-900/40">
                <motion.div className="w-1.5 h-1.5 rounded-full bg-purple-400" animate={{ scale: [1, 1.5, 1] }} transition={{ repeat: Infinity, duration: 0.8, delay: 0 }} />
                <motion.div className="w-1.5 h-1.5 rounded-full bg-purple-400" animate={{ scale: [1, 1.5, 1] }} transition={{ repeat: Infinity, duration: 0.8, delay: 0.2 }} />
                <motion.div className="w-1.5 h-1.5 rounded-full bg-purple-400" animate={{ scale: [1, 1.5, 1] }} transition={{ repeat: Infinity, duration: 0.8, delay: 0.4 }} />
              </div>
            </motion.div>
          )}

          {/* Lifted / Shaking alert banner if active */}
          {isLifted && (
            <div className="absolute top-1 z-30 px-2 py-0.5 bg-red-600/90 text-white rounded-full text-[9px] font-mono font-bold tracking-tight animate-bounce">
              ⚠️ LEVANTADA DA MESA!
            </div>
          )}

          {/* ================= DREAMING / SLEEPING SCREEN VIEW ================= */}
          {isDreaming ? (
            <div className="w-full flex-1 flex flex-col justify-center items-center z-10 animate-fade-in p-2">
              {/* Alternating state: 3s Face, 3s Dream Popup */}
              {showDream ? (
                  <div className="w-full h-full flex items-center justify-center bg-indigo-950/90 border border-purple-500/30 rounded-xl p-3 shadow-lg shadow-purple-950/50 backdrop-blur-xs">
                    <div className="flex flex-col gap-2 w-full">
                      <div className="flex items-center justify-between">
                        <div className="flex items-center gap-1.5 text-purple-300 font-mono text-[10px] font-semibold">
                          <span className="text-xs">💭</span>
                          <span>SONHANDO ACORDADA</span>
                        </div>
                        <span className="text-[8px] px-1.5 py-0.5 rounded-full bg-purple-800/60 text-purple-200 font-mono">
                          {dream.tag}
                        </span>
                      </div>
                      <div className="flex items-center gap-2">
                        <span className="text-xl">{dream.icon}</span>
                        <div className="flex-1 min-w-0">
                          <div className="text-xs font-semibold text-gray-100 truncate">{dream.title}</div>
                          <div className="text-[10px] text-purple-300/80 truncate">{dream.detail}</div>
                        </div>
                      </div>
                    </div>
                  </div>
                ) : (
                  <div className="w-full h-full flex flex-col items-center justify-center">
                    <div className="w-full flex items-center justify-around px-4">
                      {renderEye(true)}
                      {renderEye(false)}
                    </div>
                    <div className="mt-4 text-indigo-300 font-mono text-sm animate-pulse">
                      💤 z Z z
                    </div>
                  </div>
                )}
            </div>
          ) : (
            /* ================= STANDARD EMOTION DISPLAY ================= */
            <>
              {/* Eyes Row with Pure White Rounded Eyes */}
              <div className="w-full flex items-center justify-around px-3 my-auto z-10">
                {renderEye(true)}
                {renderEye(false)}
              </div>

              {/* Mouth Section */}
              <div className="my-auto z-10 flex items-center justify-center h-10">
                {renderMouth()}
              </div>
            </>
          )}

          {/* Bottom Screen Subtext */}
          <div className="w-full flex items-center justify-between text-[8px] font-mono z-20 pt-1 border-t border-slate-900">
            <span className={isPetting ? 'text-rose-400 font-bold animate-pulse' : 'text-slate-500'}>
              {isPetting ? '💖 AFETO & RONRONADO' : 'CORE-S3 OLED'}
            </span>
            <span className={isPetting ? 'text-amber-300 font-semibold' : 'text-slate-500'}>
              {isPetting ? 'GRATIDÃO ✨' : isDreaming ? 'TOQUE P/ ACORDAR' : `VOL ${speakerVolume}`}
            </span>
          </div>
        </div>

        {/* Bottom Hardware Bezel Logo & Dual Stereo Mic Holes */}
        <div className="flex items-center justify-between px-3 pt-2 text-[10px] font-mono text-gray-500">
          <div className="flex items-center gap-1">
            <div className="w-1.5 h-1.5 rounded-full bg-gray-700" title="Mic Left"></div>
            <span className="text-[8px] text-gray-600">MIC-L</span>
          </div>
          <span className="font-bold text-gray-400 tracking-widest text-[9px]">M5STACK CHAN ♀</span>
          <div className="flex items-center gap-1">
            <span className="text-[8px] text-gray-600">MIC-R</span>
            <div className="w-1.5 h-1.5 rounded-full bg-gray-700" title="Mic Right"></div>
          </div>
        </div>
      </div>

      {/* Pan-Tilt Servo Base Stand & Quick Physical Interaction Triggers */}
      <div className="w-72 mt-2 flex flex-col items-center gap-1.5">
        <div className="w-40 h-4 bg-gradient-to-b from-gray-700 to-gray-800 rounded-b-2xl border-t border-gray-600 shadow-md flex items-center justify-center">
          <div className="w-10 h-1.5 bg-gray-950 rounded-full"></div>
        </div>

        {/* Eilik/Emmo-style Physical Play Interaction Buttons */}
        <div className="w-full flex items-center justify-center gap-2">
          <button
            id="lift-table-btn"
            onClick={onLiftTrigger}
            className={`px-3 py-1.5 rounded-xl text-xs font-semibold flex items-center gap-1.5 border transition shadow-sm ${
              isLifted
                ? 'bg-red-500/20 border-red-500 text-red-300 animate-pulse'
                : 'bg-gray-800 hover:bg-gray-700 text-gray-200 border-gray-700'
            }`}
            title="Levantar a StackChan da mesa"
          >
            <span>🚀</span> {isLifted ? 'Pousar na Mesa' : 'Levantar da Mesa'}
          </button>

          <button
            id="shake-robot-btn"
            onClick={onShakeTrigger}
            className="px-3 py-1.5 rounded-xl text-xs font-semibold flex items-center gap-1.5 bg-amber-500/20 hover:bg-amber-500/30 text-amber-300 border border-amber-500/40 transition shadow-sm"
            title="Sacudir a StackChan (reação brava/irritada estilo Eilik)"
          >
            <span>📳</span> Sacudir / Irritar
          </button>
        </div>

        {/* Mobile Accelerometer Shake Controls (On-Demand & Sensibilidade) */}
        <div className="w-full flex flex-col sm:flex-row items-center justify-between gap-2 mt-2 pt-2 border-t border-gray-800/80">
          <div className="flex items-center gap-2">
            <button
              onClick={onToggleMotionSensor}
              className={`text-[11px] font-medium px-3 py-1.5 rounded-xl border flex items-center gap-1.5 transition shadow-sm ${
                isMotionActive
                  ? 'bg-emerald-500/20 text-emerald-300 border-emerald-500/40 shadow-emerald-500/10'
                  : 'bg-gray-900 hover:bg-gray-800 text-gray-400 border-gray-700'
              }`}
              title="Ligar/Desligar leitura do acelerômetro do celular"
            >
              <span className={`w-2 h-2 rounded-full ${isMotionActive ? 'bg-emerald-400 animate-ping' : 'bg-gray-500'}`}></span>
              <span>{isMotionActive ? '📳 Sensor Celular: ATIVADO' : '📳 Sensor Celular: DESATIVADO'}</span>
            </button>

            {isMotionActive && (
              <span className="text-[10px] text-gray-400 hidden sm:inline font-mono">
                (Mesa ➔ Levantar na mão | Chacoalhar rápido)
              </span>
            )}
          </div>

          {/* Sensibilidade do Acelerômetro */}
          {isMotionActive && onChangeMotionSensitivity && (
            <div className="flex items-center gap-1.5 text-[10px] font-mono bg-gray-950 px-2 py-1 rounded-lg border border-gray-800">
              <span className="text-gray-500">Sensibilidade:</span>
              {(['low', 'medium', 'high'] as const).map((s) => (
                <button
                  key={s}
                  onClick={() => onChangeMotionSensitivity(s)}
                  className={`px-2 py-0.5 rounded transition ${
                    motionSensitivity === s
                      ? 'bg-amber-500 text-gray-950 font-bold'
                      : 'text-gray-400 hover:text-gray-200'
                  }`}
                >
                  {s === 'low' ? 'Baixa (Segura)' : s === 'medium' ? 'Média' : 'Alta'}
                </button>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
