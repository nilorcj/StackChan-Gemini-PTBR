import React, { useState, useEffect, useCallback } from 'react';
import { StackChanFace } from './components/StackChanFace';
import { ServoController } from './components/ServoController';
import { VoiceDialogueTerminal } from './components/VoiceDialogueTerminal';
import { WebConfigTabs } from './components/WebConfigTabs';
import {
  RobotStatus,
  RuntimeConfig,
  SensorDiagnostics,
  MemoryStats,
  ToolDeclaration,
  EmotionType,
  GestureType,
  HeadMotionStep,
} from './types';
import { droidAudio } from './utils/audio';
import {
  Bot,
  Sliders,
  Battery,
  Volume2,
  VolumeX,
  Cpu,
  Sparkles,
} from 'lucide-react';

const SHAKE_PHRASES_MILD = [
  'Eii!! Tá achando que eu sou o que para me sacudir?!',
  'Ei, para com isso! Meu giroscópio tá girando!',
  'Eita! Cuidado com os meus circuitos!',
];
const SHAKE_PHRASES_MEDIUM = [
  'Para de me chacoalhar! Fiquei até tonta!',
  'Ai ai ai! Eu sou um robô, não um chocalho!',
  'Socorro! Quer que meus parafusos caiam todos?!',
];
const SHAKE_PHRASES_SEVERE = [
  'JÁ CHEGA! Se me balançar de novo vou travar o sistema!',
  'MUITO IRRITANTE! Você não tem nada melhor pra fazer?!',
  'PARE! Meu sistema tá entrando em modo de erro!',
];

const LIFT_PHRASES = [
  'Ei!! Me coloca no chão!',
  'Opa opa! Perdi o chão, me desce!',
  'Socorro! Estou voando, me coloca na mesa!',
];

const WAKEUP_PRESENCE_PHRASES = [
  'Oi! Você voltou!',
  'E aí, tá na área?',
  'Opa! Senti você aqui perto!',
  'Oi! Sentiu minha falta?',
  'Acordei! Tô de olho em você.',
];

const PROXIMITY_CLOSE_PHRASES = [
  'Ei, você tá muito perto hein! O que tá olhando?',
  'Opa! Quase colou na minha carcaça, dá um espacinho!',
  'Eii, tá me examinando tão de pertinho por quê? Haha!',
];

const PROACTIVE_HOT_PHRASES = [
  'Nossa, esquentou aqui na mesa, né? Tô quase ligando meu próprio cooler!',
  'Que calorzão! Se for pegar uma água gelada na cozinha, lembra de mim!',
  'Uff, tá quente! Já bebeu água ou quer que eu desidrate aqui?',
];

const PROACTIVE_COLD_PHRASES = [
  'Brrr! Que ventinho gelado aqui na sala... Já colocou um casaco?',
  'Nossa, deu uma esfriada gostosa! Meu processador agradece o ar fresco.',
];

const PROACTIVE_DRY_PHRASES = [
  'Nossa, tá seco pra caramba hoje! Meu sensor tá estalando de secura. Bebe água!',
  'Alerta de umidade baixa na mesa! Hidratação já pra você aguentar o ritmo.',
];

const PROACTIVE_COMPANION_PHRASES = [
  'Só passando pra ver se você tá focado aí ou já abriu o feed de fofoca!',
  'Tô aqui quietinha na mesa, mas qualquer coisa é só me chamar, viu?',
  'Trabalha trabalha! Mas não esquece de dar uma respirada e esticar a coluna!',
];

const AIR_PETTING_PHRASES = [
  'Ai, que delícia de carinho!',
  'Hummm... carinho gostoso no meu sensor! Assim eu derreto todinha.',
  'Own, que cafuné bom! Continua que eu tô amando!',
  'Adoro quando você faz carinho na minha cabecinha!',
];

const PEEKABOO_PHRASES = [
  'Achouuu! Hahaha, quase que fiquei no escuro!',
  'Cadê eu? Achei você! Brincando de esconde-esconde comigo?',
  'Ufa, achei que alguém tinha apagado a luz do mundo! Hahaha!',
];

const SNEEZE_PHRASES = [
  'Atchim! Eita, acho que entrou poeira no meu sensor de umidade!',
  'Atchim! Desculpa, esse tempo seco me fez espirrar um parafuso!',
  'Atchim! Nossa, até meus servomotores sacudiram agora!',
];

const POMODORO_PHRASES = [
  'Ei! Dei uma vibradinha aqui na mesa pra te avisar: já faz tempo que você tá aí focado. Que tal esticar as pernas e beber água?',
  'Alerta de postura! Vibrei aqui pra te lembrar de esticar a coluna e respirar um pouco!',
];

export function App() {
  const [viewMode, setViewMode] = useState<'studio' | 'config'>('studio');

  // Core State
  const [status, setStatus] = useState<RobotStatus | null>(null);
  const [runtime, setRuntime] = useState<RuntimeConfig | null>(null);
  const [sensorDiag, setSensorDiag] = useState<SensorDiagnostics | null>(null);
  const [memoryStats, setMemoryStats] = useState<MemoryStats | null>(null);
  const [tools, setTools] = useState<ToolDeclaration[]>([]);

  // Interactive Live State
  const [emotion, setEmotion] = useState<EmotionType>('neutral');
  const [yawDeg, setYawDeg] = useState(0);
  const [pitchDeg, setPitchDeg] = useState(12);
  const [servoActive, setServoActive] = useState(false);
  const [currentGesture, setCurrentGesture] = useState('idle');
  const [isSpeaking, setIsSpeaking] = useState(false);
  const [isListening, setIsListening] = useState(false);
  const [isShaking, setIsShaking] = useState(false);
  const [isLifted, setIsLifted] = useState(false);
  const [isPetting, setIsPetting] = useState(false);
  const [soundMuted, setSoundMuted] = useState(!droidAudio.isSoundEnabled());
  const [isMotionActive, setIsMotionActive] = useState<boolean>(false);
  const [motionSensitivity, setMotionSensitivity] = useState<'low' | 'medium' | 'high'>('low');
  const [lastShakeTime, setLastShakeTime] = useState<number>(0);
  const [irritationLevel, setIrritationLevel] = useState<number>(0);
  const [lastPresenceState, setLastPresenceState] = useState<boolean>(true);
  const [lastPresenceChangeTs, setLastPresenceChangeTs] = useState<number>(Date.now());
  const [lastProximityAlertTs, setLastProximityAlertTs] = useState<number>(0);
  const [lastEnvCommentTs, setLastEnvCommentTs] = useState<number>(Date.now());
  const [lastRecordedTemp, setLastRecordedTemp] = useState<number>(28.5);
  const [lastRecordedHum, setLastRecordedHum] = useState<number>(42.0);
  const [lastAirPettingTs, setLastAirPettingTs] = useState<number>(0);
  const [tofCoveredStartTs, setTofCoveredStartTs] = useState<number | null>(null);
  const [lastSneezeTs, setLastSneezeTs] = useState<number>(Date.now());
  const [presenceSittingStartTs, setPresenceSittingStartTs] = useState<number>(Date.now());
  const [lastPomodoroTs, setLastPomodoroTs] = useState<number>(Date.now());

  // Fetch initial system state
  const fetchAllData = useCallback(async () => {
    try {
      const [stRes, rtRes, snRes, memRes, tlRes] = await Promise.all([
        fetch('/api/status').then((r) => r.json()),
        fetch('/api/runtime').then((r) => r.json()),
        fetch('/api/sensors').then((r) => r.json()),
        fetch('/api/memory/stats').then((r) => r.json()),
        fetch('/api/gateway/tools').then((r) => r.json()),
      ]);

      if (stRes?.ok) {
        setStatus(stRes);
        setEmotion(stRes.emotion || 'neutral');
        setYawDeg(stRes.servo?.x_deg ?? 0);
        setPitchDeg(stRes.servo?.y_deg ?? 12);
        setIsListening(stRes.gemini?.recording ?? false);
        setIsSpeaking(stRes.gemini?.speaking ?? false);
      }
      if (rtRes) setRuntime(rtRes);
      if (snRes) {
        setSensorDiag(snRes);

        // Check TOF Presence & Distance Behavior
        const tofDist = snRes?.pahub?.channels?.ch0_tof90?.distance_mm || 400;
        const presence = tofDist < 750;
        const now = Date.now();

        // Check ENV Sensor (Temperature & Humidity)
        const currentTemp = snRes?.pahub?.channels?.ch1_env?.temp_c ?? snRes?.pahub?.channels?.ch2_env?.temperature ?? 28.5;
        const currentHum = snRes?.pahub?.channels?.ch1_env?.humidity_pct ?? snRes?.pahub?.channels?.ch2_env?.humidity ?? 42.0;

        // 1. Return/Wakeup Detection: Was away (or in sleep mode) and now returns in front of TOF
        if (presence && !lastPresenceState && now - lastPresenceChangeTs > 4000) {
          fetch('/api/robot/gesture', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ gesture: 'look_at_user' })
          }).catch(console.error);

          setLastPresenceState(true);
          setLastPresenceChangeTs(now);
          setPresenceSittingStartTs(now);
          setEmotion('happy');
          droidAudio.playHappyChime(0.28);
          const phrase = WAKEUP_PRESENCE_PHRASES[Math.floor(Math.random() * WAKEUP_PRESENCE_PHRASES.length)];
          setTimeout(() => {
            droidAudio.speakText(phrase, rtRes?.gemini_voice || 'Aoede');
          }, 350);
          // Little nod
          setPitchDeg(22);
          setTimeout(() => setPitchDeg(12), 400);
        }
        // 2. User left desk: After quiet time without presence, go into gentle sleep to save power
        else if (!presence && lastPresenceState) {
          setLastPresenceState(false);
          setLastPresenceChangeTs(now);
        }

        // 3. Peek-a-boo: User covers the sensor (< 40mm) for at least 1.2s and reveals it (> 220mm)
        if (tofDist > 0 && tofDist <= 40) {
          if (!tofCoveredStartTs) {
            setTofCoveredStartTs(now);
          }
        } else if (tofDist > 220 && tofCoveredStartTs) {
          const coveredDuration = now - tofCoveredStartTs;
          setTofCoveredStartTs(null);
          if (coveredDuration >= 1000 && coveredDuration <= 10000) {
            setEmotion('happy');
            droidAudio.playPeekabooGiggle(0.28);
            droidAudio.playVibrationHum(280);
            setPitchDeg(26);
            setTimeout(() => setPitchDeg(12), 600);
            const peekPhrase = PEEKABOO_PHRASES[Math.floor(Math.random() * PEEKABOO_PHRASES.length)];
            setTimeout(() => {
              droidAudio.speakText(peekPhrase, rtRes?.gemini_voice || 'Aoede');
            }, 300);
          }
        } else if (tofDist > 40 && tofDist <= 220 && tofCoveredStartTs && now - tofCoveredStartTs > 12000) {
          // Timeout reset
          setTofCoveredStartTs(null);
        }

        // 4. Air Petting (Carinho no Ar / Cafuné): User hovers hand smoothly 45mm - 130mm from TOF
        if (tofDist >= 45 && tofDist <= 135 && now - lastAirPettingTs > 14000) {
          setLastAirPettingTs(now);
          setEmotion('happy');
          setIsPetting(true);
          droidAudio.playPettingSound(0.28);
          droidAudio.playPurrSound(0.24);
          droidAudio.playVibrationHum(450);
          setPitchDeg(28);
          setTimeout(() => {
            setIsPetting(false);
            setPitchDeg(12);
          }, 2400);
          const petPhrase = AIR_PETTING_PHRASES[Math.floor(Math.random() * AIR_PETTING_PHRASES.length)];
          setTimeout(() => {
            droidAudio.speakText(petPhrase, rtRes?.gemini_voice || 'Aoede');
          }, 350);
        }

        // 5. Proximity Alert: User gets very close (140mm - 190mm)
        else if (tofDist > 140 && tofDist < 195 && now - lastProximityAlertTs > 10000) {
          setLastProximityAlertTs(now);
          setEmotion('angry');
          droidAudio.playGrumpyGrumble(0.2);
          const closePhrase = PROXIMITY_CLOSE_PHRASES[Math.floor(Math.random() * PROXIMITY_CLOSE_PHRASES.length)];
          setTimeout(() => {
            droidAudio.speakText(closePhrase, rtRes?.gemini_voice || 'Aoede');
          }, 250);
          // Lean back a bit
          setPitchDeg(2);
          setTimeout(() => setPitchDeg(12), 1200);
        }

        // 6. Spontaneous Mechanical Sneeze ("Atchim!"): Occasional trigger on dry air or ambient tickle
        if (presence && (currentHum <= 35 || Math.random() < 0.05) && now - lastSneezeTs > 75000) {
          setLastSneezeTs(now);
          setEmotion('surprised');
          droidAudio.playSneezeSound(0.3);
          droidAudio.playVibrationHum(350);
          // Rapid sneeze shake
          setYawDeg(14);
          setPitchDeg(4);
          setTimeout(() => {
            setYawDeg(-12);
            setPitchDeg(24);
            setTimeout(() => {
              setYawDeg(0);
              setPitchDeg(12);
              setEmotion('neutral');
            }, 350);
          }, 280);
          const sneezePhrase = SNEEZE_PHRASES[Math.floor(Math.random() * SNEEZE_PHRASES.length)];
          setTimeout(() => {
            droidAudio.speakText(sneezePhrase, rtRes?.gemini_voice || 'Aoede');
          }, 450);
        }

        // 7. Pomodoro / Posture Vibration Alert: After 45+ minutes continuous presence
        if (presence && now - presenceSittingStartTs > 45 * 60 * 1000 && now - lastPomodoroTs > 45 * 60 * 1000) {
          setLastPomodoroTs(now);
          setEmotion('speaking');
          droidAudio.playVibrationHum(800);
          droidAudio.playWakeChirp(0.26);
          const pomoPhrase = POMODORO_PHRASES[Math.floor(Math.random() * POMODORO_PHRASES.length)];
          setTimeout(() => {
            droidAudio.speakText(pomoPhrase, rtRes?.gemini_voice || 'Aoede');
          }, 400);
        }

        // 8. Proactive Sensory Reactions: Sudden Heat, Cold or Dryness changes (Human-like spontaneous observation)
        if (presence && now - lastEnvCommentTs > 45000) {
          const tempDelta = currentTemp - lastRecordedTemp;
          const humDelta = currentHum - lastRecordedHum;

          // Hot or temperature rise
          if ((currentTemp >= 30.5 || tempDelta >= 1.5) && now - lastEnvCommentTs > 45000) {
            setLastEnvCommentTs(now);
            setLastRecordedTemp(currentTemp);
            setEmotion('speaking');
            const phrase = PROACTIVE_HOT_PHRASES[Math.floor(Math.random() * PROACTIVE_HOT_PHRASES.length)];
            droidAudio.playWakeChirp(0.25);
            setTimeout(() => {
              droidAudio.speakText(phrase, rtRes?.gemini_voice || 'Aoede');
            }, 300);
          }
          // Very Dry or humidity drop
          else if ((currentHum <= 32 || humDelta <= -6) && now - lastEnvCommentTs > 45000) {
            setLastEnvCommentTs(now);
            setLastRecordedHum(currentHum);
            setEmotion('speaking');
            const phrase = PROACTIVE_DRY_PHRASES[Math.floor(Math.random() * PROACTIVE_DRY_PHRASES.length)];
            droidAudio.playWakeChirp(0.25);
            setTimeout(() => {
              droidAudio.speakText(phrase, rtRes?.gemini_voice || 'Aoede');
            }, 300);
          }
          // Cold room
          else if (currentTemp <= 21.0 && now - lastEnvCommentTs > 60000) {
            setLastEnvCommentTs(now);
            setLastRecordedTemp(currentTemp);
            setEmotion('speaking');
            const phrase = PROACTIVE_COLD_PHRASES[Math.floor(Math.random() * PROACTIVE_COLD_PHRASES.length)];
            droidAudio.playWakeChirp(0.25);
            setTimeout(() => {
              droidAudio.speakText(phrase, rtRes?.gemini_voice || 'Aoede');
            }, 300);
          }
        }
      }
      if (memRes) setMemoryStats(memRes);
      if (tlRes?.tools) setTools(tlRes.tools);
    } catch (err) {
      console.warn('Data fetch error:', err);
    }
  }, [
    lastPresenceState,
    lastPresenceChangeTs,
    lastProximityAlertTs,
    lastEnvCommentTs,
    lastRecordedTemp,
    lastRecordedHum,
    lastAirPettingTs,
    tofCoveredStartTs,
    lastSneezeTs,
    presenceSittingStartTs,
    lastPomodoroTs,
  ]);

  useEffect(() => {
    fetchAllData();
    // Play initial subtle startup sound (soft ascending marimba chime)
    droidAudio.playBootWhistle(0.2);

    const interval = setInterval(fetchAllData, 5000);
    return () => clearInterval(interval);
  }, [fetchAllData]);

  // Handlers
  const handleMoveServo = async (x: number, y: number, speed: number) => {
    setYawDeg(x / 10);
    setPitchDeg(y / 10);
    setServoActive(true);
    setCurrentGesture('manual_move');
    droidAudio.playBeep(440, 0.04);

    try {
      await fetch('/api/servo/move', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ x, y, speed }),
      });
    } catch (e) {
      console.error(e);
    }

    setTimeout(() => {
      setServoActive(false);
      setCurrentGesture('idle');
    }, 400);
  };

  const handleQueueGesture = async (gestureName: GestureType) => {
    setCurrentGesture(gestureName);
    setServoActive(true);
    if (gestureName === 'angry_tantrum') {
      droidAudio.playGrumpyGrumble();
      setEmotion('angry');
    } else if (gestureName === 'lifted_protest') {
      droidAudio.playLiftedSound();
      setEmotion('angry');
    } else if (gestureName === 'vibrate') {
      droidAudio.playVibrationHum(400);
    } else if (gestureName === 'purr_vibration') {
      droidAudio.playPurrSound();
      droidAudio.playVibrationHum(500);
      setEmotion('happy');
    } else if (gestureName === 'sneeze_shake') {
      droidAudio.playSneezeSound();
      droidAudio.playVibrationHum(300);
      setEmotion('surprised');
    } else if (gestureName === 'peekaboo_nod') {
      droidAudio.playPeekabooGiggle();
      droidAudio.playVibrationHum(250);
      setEmotion('happy');
    } else if (gestureName === 'petting_tilt') {
      droidAudio.playPettingSound();
      droidAudio.playPurrSound();
      droidAudio.playVibrationHum(450);
      setEmotion('happy');
    } else if (gestureName === 'pomodoro_alert') {
      droidAudio.playVibrationHum(800);
      droidAudio.playWakeChirp();
    } else {
      droidAudio.playWakeChirp(0.3);
    }

    try {
      const res = await fetch('/api/servo/gesture', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ gesture: gestureName }),
      }).then((r) => r.json());

      if (res?.result) {
        if (res.result.x_deg !== undefined) setYawDeg(res.result.x_deg);
        if (res.result.y_deg !== undefined) setPitchDeg(res.result.y_deg);
      }
    } catch (e) {
      console.error(e);
    }

    setTimeout(() => {
      setServoActive(false);
      setCurrentGesture('idle');
    }, 1200);
  };

  const handleExecuteHeadMotion = async (name: string, steps: HeadMotionStep[]) => {
    setCurrentGesture(name);
    setServoActive(true);
    droidAudio.playBeep(600, 0.08);

    try {
      await fetch('/api/gateway/call', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          tool_name: 'head_motion',
          arguments: { motion_name: name, steps },
        }),
      });
      fetchAllData();
    } catch (e) {
      console.error(e);
    }

    setTimeout(() => {
      setServoActive(false);
      setCurrentGesture('idle');
    }, 1000);
  };

  const handleTouchSection = async (section: 'front' | 'middle' | 'back') => {
    droidAudio.playWakeChirp(0.4);
    if (section === 'middle') {
      setIsPetting(true);
      setEmotion('happy');
      droidAudio.playPettingSound(0.38);

      // Trigger physical haptic vibration pattern on mobile/browser if supported
      if (typeof navigator !== 'undefined' && 'vibrate' in navigator) {
        try {
          // Soft purring rumble pulse: [vib, pause, vib, pause, vib]
          navigator.vibrate([35, 45, 40, 45, 65, 40, 85]);
        } catch {
          // Ignore vibration permission or browser limitations
        }
      }

      // Adorable physical snuggle gesture with pan-tilt head (tilts up & leans into the touch)
      setYawDeg(14);
      setPitchDeg(24);
      setTimeout(() => {
        setYawDeg(-10);
        setPitchDeg(20);
      }, 400);
      setTimeout(() => {
        setYawDeg(4);
        setPitchDeg(15);
      }, 1100);
      setTimeout(() => {
        setYawDeg(0);
        setPitchDeg(12);
        setIsPetting(false);
      }, 2400);
    }

    try {
      await fetch('/api/sensors/touch', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ [section]: 100 }),
      });
      fetchAllData();
    } catch (e) {
      console.error(e);
    }
  };

  // Physical Interaction Triggers: Shake / Lift
  const handleTriggerShake = useCallback(async () => {
    setIsShaking(true);
    setEmotion('angry');
    
    const now = Date.now();
    // Reset if last shake was more than 10 seconds ago
    const newIrritationLevel = (now - lastShakeTime < 10000) 
        ? Math.min(irritationLevel + 1, 2) 
        : 0;
    
    setIrritationLevel(newIrritationLevel);
    setLastShakeTime(now);
    
    // Pick phrase based on level
    let phraseList = SHAKE_PHRASES_MILD;
    if (newIrritationLevel === 1) phraseList = SHAKE_PHRASES_MEDIUM;
    else if (newIrritationLevel === 2) phraseList = SHAKE_PHRASES_SEVERE;
    
    const randomPhrase = phraseList[Math.floor(Math.random() * phraseList.length)];
    
    droidAudio.playGrumpyGrumble(0.26);
    setTimeout(() => {
      droidAudio.speakText(randomPhrase, runtime?.gemini_voice || 'Aoede');
    }, 180);

    // Intense fast rattling vibration pattern for shaking / annoyance
    if (typeof navigator !== 'undefined' && 'vibrate' in navigator) {
      try {
        navigator.vibrate([120, 40, 100, 40, 140, 50, 160]);
      } catch {
        // Ignore vibration permission or browser limitations
      }
    }

    try {
      await fetch('/api/sensors/shake', { method: 'POST' });
      fetchAllData();
    } catch (e) {
      console.error(e);
    }

    // Shake head vigorously with pan-tilt
    setYawDeg(-25);
    setTimeout(() => setYawDeg(25), 100);
    setTimeout(() => setYawDeg(-20), 200);
    setTimeout(() => setYawDeg(20), 300);
    setTimeout(() => {
      setYawDeg(0);
      setIsShaking(false);
    }, 650);
  }, [fetchAllData, runtime?.gemini_voice, lastShakeTime, irritationLevel]);

  // Request accelerometer permissions & toggle mobile sensor
  const handleToggleMotionSensor = async () => {
    if (isMotionActive) {
      setIsMotionActive(false);
      return;
    }

    if (typeof (DeviceMotionEvent as any)?.requestPermission === 'function') {
      try {
        const res = await (DeviceMotionEvent as any).requestPermission();
        if (res === 'granted') {
          setIsMotionActive(true);
          droidAudio.playHappyChime(0.2);
        }
      } catch (err) {
        console.warn('Error requesting device motion permission:', err);
      }
    } else if (typeof window !== 'undefined' && 'DeviceMotionEvent' in window) {
      setIsMotionActive(true);
      droidAudio.playHappyChime(0.2);
    }
  };

  // EILIK-STYLE MOTION RECOGNITION (Pure Physics & Vector Decomposition)
  useEffect(() => {
    if (!isMotionActive) return;

    let lastX = 0;
    let lastY = 0;
    let lastZ = 0;
    let lastTime = 0;

    // Shake Detection State: requires rapid acceleration reversals (sloshing/oscillation)
    let shakeReversals = 0;
    let lastVelDirX = 0;
    let lastVelDirY = 0;
    let lastShakeTriggerTime = 0;
    let shakeWindowStart = 0;

    // Lift Detection State: detect transition between flat table surface and being held / lifted
    let flatBaselineFrames = 0;
    let handheldFrames = 0;
    let currentLiftedState = isLifted;
    let lastLiftTriggerTime = 0;

    // Jerk thresholds for shake:
    // Standard Eilik requires a distinct, rapid flick/shake of the wrist, not gentle tilt
    const jerkThresholds = {
      low: 45,    // Very strict: needs a strong vigorous shake
      medium: 30, // Balanced
      high: 18,   // Sensitive
    };
    const JERK_REQ = jerkThresholds[motionSensitivity] || 45;

    const handleDeviceMotion = (e: DeviceMotionEvent) => {
      // Use accelerationIncludingGravity for orientation/lift, and linear acceleration if available
      const accG = e.accelerationIncludingGravity;
      if (!accG || accG.x === null || accG.y === null || accG.z === null) return;

      const now = Date.now();
      if (now - lastTime < 50) return; // Sample at ~20Hz
      const dt = (now - lastTime) / 1000;
      lastTime = now;

      const x = accG.x;
      const y = accG.y;
      const z = accG.z;

      // -------------------------------------------------------------
      // 1. LEVANTAR DA MESA (LIFT DETECTION) - EILIK BEHAVIOR
      // -------------------------------------------------------------
      // On a flat desk/table:
      // Phone is flat horizontal -> Z is around ~9.0 - 10.2 m/s² (1g), X and Y are near 0 (< 2.2 m/s²)
      // When picked up / lifted:
      // User holds the phone tilted (X or Y > 3.8 m/s²) or lifts vertically with pitch.
      const isFlatOnDesk = Math.abs(x) < 2.5 && Math.abs(y) < 2.5 && Math.abs(z) > 8.2;
      const isHandheldOrTilted = Math.abs(x) > 3.8 || Math.abs(y) > 3.8 || Math.abs(z) < 6.8;

      if (isFlatOnDesk) {
        flatBaselineFrames = Math.min(25, flatBaselineFrames + 1);
        handheldFrames = Math.max(0, handheldFrames - 2);
      } else if (isHandheldOrTilted) {
        handheldFrames = Math.min(25, handheldFrames + 1);
        flatBaselineFrames = Math.max(0, flatBaselineFrames - 2);
      }

      if (now - lastLiftTriggerTime > 1800) {
        // Condition to trigger LIFT: was verified resting on desk (>6 frames flat) and now picked up
        if (!currentLiftedState && handheldFrames >= 3 && flatBaselineFrames >= 6) {
          currentLiftedState = true;
          lastLiftTriggerTime = now;
          setIsLifted(true);
          setEmotion('angry');
          droidAudio.playLiftedSound(0.25);
          setTimeout(() => {
            const liftMsg = LIFT_PHRASES[Math.floor(Math.random() * LIFT_PHRASES.length)];
            droidAudio.speakText(liftMsg, runtime?.gemini_voice || 'Aoede');
          }, 200);
          setPitchDeg(35);

          if (typeof navigator !== 'undefined' && 'vibrate' in navigator) {
            try {
              navigator.vibrate([70, 60, 110]); // Startled sudden pulse
            } catch {
              // Ignore
            }
          }

          fetch('/api/sensors/lift', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ lifted: true }),
          }).then(() => fetchAllData()).catch(console.error);
        }
        // Condition to trigger PUT DOWN ON DESK: was lifted, now placed flat and stable on desk
        else if (currentLiftedState && flatBaselineFrames >= 8) {
          currentLiftedState = false;
          lastLiftTriggerTime = now;
          setIsLifted(false);
          setEmotion('happy');
          droidAudio.playHappyChime(0.22);
          setPitchDeg(12);

          if (typeof navigator !== 'undefined' && 'vibrate' in navigator) {
            try {
              navigator.vibrate([40, 50, 40]); // Gentle landing thud pulse
            } catch {
              // Ignore
            }
          }

          fetch('/api/sensors/lift', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ lifted: false }),
          }).then(() => fetchAllData()).catch(console.error);
        }
      }

      // -------------------------------------------------------------
      // 2. SACUDIR (SHAKE DETECTION) - EILIK TANTRUM
      // -------------------------------------------------------------
      // A shake MUST be rapid bidirectional oscillation (reversing directions within 500ms).
      // A smooth lift or rotation has only 0 or 1 directional change and will NEVER trigger shake.
      const dX = (x - lastX) / dt;
      const dY = (y - lastY) / dt;

      if (now - shakeWindowStart > 600) {
        shakeReversals = 0;
        shakeWindowStart = now;
      }

      const dirX = Math.sign(dX);
      const dirY = Math.sign(dY);

      if (Math.abs(dX) > JERK_REQ) {
        if (dirX !== 0 && lastVelDirX !== 0 && dirX !== lastVelDirX) {
          shakeReversals++;
        }
        lastVelDirX = dirX;
      }

      if (Math.abs(dY) > JERK_REQ) {
        if (dirY !== 0 && lastVelDirY !== 0 && dirY !== lastVelDirY) {
          shakeReversals++;
        }
        lastVelDirY = dirY;
      }

      // Require at least 3 distinct rapid direction changes (left-right-left or up-down-up)
      if (shakeReversals >= 3 && now - lastShakeTriggerTime > 2500) {
        shakeReversals = 0;
        lastVelDirX = 0;
        lastVelDirY = 0;
        lastShakeTriggerTime = now;
        handleTriggerShake();
      }

      lastX = x;
      lastY = y;
      lastZ = z;
    };

    window.addEventListener('devicemotion', handleDeviceMotion);
    return () => {
      window.removeEventListener('devicemotion', handleDeviceMotion);
    };
  }, [isMotionActive, motionSensitivity, handleTriggerShake, isLifted, fetchAllData]);

  const handleTriggerLift = async () => {
    const nextLifted = !isLifted;
    setIsLifted(nextLifted);

    if (nextLifted) {
      setEmotion('angry');
      droidAudio.playLiftedSound(0.24);
      setTimeout(() => {
        const liftMsg = LIFT_PHRASES[Math.floor(Math.random() * LIFT_PHRASES.length)];
        droidAudio.speakText(liftMsg, runtime?.gemini_voice || 'Aoede');
      }, 200);
      setPitchDeg(35);

      if (typeof navigator !== 'undefined' && 'vibrate' in navigator) {
        try {
          navigator.vibrate([70, 60, 110]); // Startled lift pulse
        } catch {
          // Ignore
        }
      }
    } else {
      setEmotion('happy');
      droidAudio.playHappyChime(0.22);
      setPitchDeg(12);

      if (typeof navigator !== 'undefined' && 'vibrate' in navigator) {
        try {
          navigator.vibrate([40, 50, 40]); // Landing pulse
        } catch {
          // Ignore
        }
      }
    }

    try {
      await fetch('/api/sensors/lift', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ lifted: nextLifted }),
      });
      fetchAllData();
    } catch (e) {
      console.error(e);
    }
  };

  const handleToggleVoice = async () => {
    droidAudio.playWakeChirp(0.4);
    try {
      const res = await fetch('/api/voice/toggle').then((r) => r.json());
      if (res?.ok) {
        setIsListening(res.voice_listening);
        setEmotion(res.emotion);
      }
    } catch (e) {
      console.error(e);
    }
  };

  const handleSendText = async (text: string) => {
    setEmotion('thinking');
    try {
      const res = await fetch('/api/gemini/text', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ text }),
      }).then((r) => r.json());

      if (res?.ok) {
        setEmotion(res.emotion as EmotionType);
        if (res.gesture) setCurrentGesture(res.gesture);
        setIsSpeaking(true);

        // Calculate approximate speech duration based on text length (avg 14 chars/sec, min 2s)
        const approxDurationMs = Math.max(2200, Math.min(18000, (res.response.length / 13) * 1000));
        const safetyTimer = setTimeout(() => {
          setIsSpeaking(false);
        }, approxDurationMs);

        // Play vocal audio with pleasant voice
        droidAudio.speakText(res.response, runtime?.gemini_voice || 'Aoede', () => {
          clearTimeout(safetyTimer);
          setIsSpeaking(false);
        });
        fetchAllData();
        return res;
      }
    } catch (e) {
      console.error(e);
    }
    return null;
  };

  const handleSaveRuntime = async (cfg: Partial<RuntimeConfig>, reboot = false) => {
    await fetch('/api/runtime', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ...cfg, reboot }),
    });
    if (reboot) {
      droidAudio.playBootWhistle();
    }
    fetchAllData();
  };

  const handleSaveSecrets = async (secrets: any) => {
    await fetch('/api/secrets', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(secrets),
    });
    fetchAllData();
  };

  const handleSaveSecurity = async (password: string) => {
    await fetch('/api/security', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ web_password: password }),
    });
    fetchAllData();
  };

  const handleSavePrompts = async (system_prompt: string, persona_prompt: string) => {
    await fetch('/api/prompts', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ system_prompt, persona_prompt }),
    });
    fetchAllData();
  };

  const handleTriggerSummarize = async () => {
    await fetch('/api/memory/compact', { method: 'POST' });
    fetchAllData();
  };

  const handleTriggerVectorize = async () => {
    await fetch('/api/memory/vectorize', { method: 'POST' });
    fetchAllData();
  };

  const handleSearchMemory = async (query: string) => {
    return await fetch('/api/memory/search', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ query }),
    }).then((r) => r.json());
  };

  return (
    <div className="min-h-screen bg-gray-950 text-gray-100 font-sans antialiased pb-12 selection:bg-amber-500 selection:text-gray-950">
      {/* Top Application Bar */}
      <header className="sticky top-0 z-50 bg-gray-900/80 backdrop-blur-md border-b border-gray-800/80 px-4 lg:px-8 py-3">
        <div className="max-w-7xl mx-auto flex items-center justify-between">
          {/* Logo & Title */}
          <div className="flex items-center gap-3">
            <div className="w-9 h-9 rounded-2xl bg-blue-500/20 border border-blue-400/30 flex items-center justify-center text-blue-400 shadow-md shadow-blue-500/10">
              <Bot className="w-5 h-5" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h1 className="font-bold text-gray-100 text-sm sm:text-base tracking-tight">
                  StackChan CoreS3 ♀
                </h1>
                <span className="text-[10px] px-2 py-0.5 rounded-full font-mono bg-pink-500/10 text-pink-300 border border-pink-500/20">
                  Companheira Feminina
                </span>
              </div>
              <p className="text-[11px] text-gray-400 font-mono hidden sm:block">
                ID: {status?.robot_id || 'stackchan'} • Modelo: {runtime?.gemini_model || 'gemini-3.5-flash-lite'}
              </p>
            </div>
          </div>

          {/* Right Status Badges & Controls */}
          <div className="flex items-center gap-2 sm:gap-4">
            {/* Battery Indicator */}
            <div className="flex items-center gap-1.5 px-2.5 py-1 rounded-xl bg-gray-950 border border-gray-800 text-xs font-mono text-gray-300">
              <Battery className="w-3.5 h-3.5 text-emerald-400" />
              <span>{status?.battery_pct || 94}%</span>
            </div>

            {/* Audio Mute/Unmute Toggle */}
            <button
              onClick={() => {
                const nextMuted = !soundMuted;
                setSoundMuted(nextMuted);
                droidAudio.setSoundEnabled(!nextMuted);
                if (!nextMuted) {
                  droidAudio.playHappyChime(0.15);
                }
              }}
              className={`p-2 rounded-xl border transition ${
                soundMuted
                  ? 'bg-red-500/20 text-red-400 border-red-500/40'
                  : 'bg-gray-800 hover:bg-gray-700 text-amber-400 border-gray-700'
              }`}
              title={soundMuted ? 'Áudio desativado (Clique para ativar sons)' : 'Áudio ativado (Clique para mutar)'}
            >
              {soundMuted ? <VolumeX className="w-4 h-4" /> : <Volume2 className="w-4 h-4" />}
            </button>

            {/* Primary View Switcher */}
            <div className="flex p-1 bg-gray-950 rounded-xl border border-gray-800 text-xs">
              <button
                id="view-mode-studio"
                onClick={() => setViewMode('studio')}
                className={`px-3 py-1.5 rounded-lg font-semibold transition flex items-center gap-1.5 ${
                  viewMode === 'studio'
                    ? 'bg-amber-500 text-gray-950 shadow'
                    : 'text-gray-400 hover:text-gray-200'
                }`}
              >
                <Bot className="w-3.5 h-3.5" />
                Estúdio do Robô
              </button>
              <button
                id="view-mode-config"
                onClick={() => setViewMode('config')}
                className={`px-3 py-1.5 rounded-lg font-semibold transition flex items-center gap-1.5 ${
                  viewMode === 'config'
                    ? 'bg-amber-500 text-gray-950 shadow'
                    : 'text-gray-400 hover:text-gray-200'
                }`}
              >
                <Sliders className="w-3.5 h-3.5" />
                Configurações Web
              </button>
            </div>
          </div>
        </div>
      </header>

      {/* Main Container */}
      <main className="max-w-7xl mx-auto px-4 lg:px-8 mt-6">
        {viewMode === 'studio' ? (
          <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
            {/* Left Column: Robot Hardware Avatar & Pan-Tilt Joystick (5 cols) */}
            <div className="lg:col-span-5 space-y-6">
              {/* StackChan Face Display Unit */}
              <div className="bg-gray-900/90 border border-gray-800 rounded-2xl p-6 shadow-xl flex flex-col items-center justify-center">
                <StackChanFace
                  emotion={emotion}
                  yawDeg={yawDeg}
                  pitchDeg={pitchDeg}
                  isSpeaking={isSpeaking}
                  isListening={isListening}
                  speakerVolume={runtime?.speaker_volume || 200}
                  isShaking={isShaking}
                  isLifted={isLifted}
                  isPetting={isPetting}
                  isMotionActive={isMotionActive}
                  motionSensitivity={motionSensitivity}
                  onToggleMotionSensor={handleToggleMotionSensor}
                  onChangeMotionSensitivity={setMotionSensitivity}
                  onTouchSection={handleTouchSection}
                  onVoiceToggle={handleToggleVoice}
                  onShakeTrigger={handleTriggerShake}
                  onLiftTrigger={handleTriggerLift}
                />

                {/* Quick Emotion Picker */}
                <div className="w-full mt-6 pt-4 border-t border-gray-800">
                  <div className="flex items-center justify-between text-xs text-gray-400 mb-2">
                    <span className="font-mono text-[10px] uppercase text-gray-400">
                      🧪 Testador de Expressões
                    </span>
                    <div className="flex items-center gap-2">
                      <span className="font-mono text-[10px] text-amber-400">EMOÇÃO: {emotion}</span>
                      {emotion !== 'neutral' && (
                        <button
                          onClick={() => setEmotion('neutral')}
                          className="text-[9px] font-mono px-1.5 py-0.5 rounded bg-gray-800 hover:bg-gray-700 text-gray-300 border border-gray-700"
                          title="Restaurar modo autônomo da StackChan"
                        >
                          Auto ↺
                        </button>
                      )}
                    </div>
                  </div>
                  <div className="flex flex-wrap gap-1.5 justify-center">
                    {(
                      [
                        'neutral',
                        'happy',
                        'listening',
                        'speaking',
                        'thinking',
                        'looking',
                        'angry',
                        'found',
                        'error',
                        'sleep',
                      ] as EmotionType[]
                    ).map((emo) => (
                      <button
                        key={emo}
                        onClick={() => {
                          setEmotion(emo);
                          if (emo === 'sleep') {
                            droidAudio.playSleepChime();
                          } else if (emo === 'happy') {
                            droidAudio.playHappyChime();
                          } else if (emo === 'angry') {
                            droidAudio.playGrumpyGrumble();
                          } else {
                            droidAudio.playBeep(520, 0.05);
                          }
                        }}
                        className={`px-2.5 py-1 rounded-lg text-xs font-mono capitalize transition ${
                          emotion === emo
                            ? 'bg-amber-500 text-gray-950 font-bold shadow'
                            : 'bg-gray-950 hover:bg-gray-800 text-gray-300 border border-gray-800'
                        }`}
                      >
                        {emo === 'neutral'
                          ? 'Neutro'
                          : emo === 'happy'
                          ? 'Feliz'
                          : emo === 'listening'
                          ? 'Ouvindo'
                          : emo === 'speaking'
                          ? 'Falando'
                          : emo === 'thinking'
                          ? 'Pensando'
                          : emo === 'looking'
                          ? 'Olhando'
                          : emo === 'angry'
                          ? 'Irritada 💢'
                          : emo === 'found'
                          ? 'Achou'
                          : emo === 'error'
                          ? 'Erro'
                          : emo === 'sleep'
                          ? 'Sonhando 💭'
                          : emo}
                      </button>
                    ))}
                  </div>
                </div>
              </div>

              {/* Servo Pan-Tilt Quick Status */}
              <div className="p-4 bg-gray-900/90 border border-gray-800 rounded-2xl flex items-center justify-between font-mono text-xs text-gray-300">
                <div className="flex items-center gap-2">
                  <Cpu className="w-4 h-4 text-sky-400" />
                  <span>SERVOMOTORES (PAN-TILT):</span>
                </div>
                <div className="flex items-center gap-3">
                  <span className="text-amber-400">PAN (X): {yawDeg.toFixed(1)}°</span>
                  <span className="text-sky-400">TILT (Y): {pitchDeg.toFixed(1)}°</span>
                </div>
              </div>
            </div>

            {/* Right Column: Voice Terminal & Full Servo Controller (7 cols) */}
            <div className="lg:col-span-7 space-y-6">
              {/* Voice & Dialogue Terminal */}
              <VoiceDialogueTerminal
                isListening={isListening}
                isSpeaking={isSpeaking}
                voiceName={runtime?.gemini_voice || 'Aoede'}
                geminiModel={runtime?.gemini_model || 'gemini-3.5-flash-lite'}
                onToggleVoice={handleToggleVoice}
                onSendText={handleSendText}
              />

              {/* Pan-Tilt & Gesture Controller */}
              <ServoController
                currentYawDeg={yawDeg}
                currentPitchDeg={pitchDeg}
                isActive={servoActive}
                currentGesture={currentGesture}
                onMoveServo={handleMoveServo}
                onQueueGesture={handleQueueGesture}
                onExecuteHeadMotion={handleExecuteHeadMotion}
              />
            </div>
          </div>
        ) : (
          /* Firmware WebConfigServer Tabs */
          <WebConfigTabs
            status={status}
            runtime={runtime}
            sensorDiag={sensorDiag}
            memoryStats={memoryStats}
            tools={tools}
            onSaveRuntime={handleSaveRuntime}
            onSaveSecrets={handleSaveSecrets}
            onSaveSecurity={handleSaveSecurity}
            onSavePrompts={handleSavePrompts}
            onRefreshStatus={fetchAllData}
            onTriggerSummarize={handleTriggerSummarize}
            onTriggerVectorize={handleTriggerVectorize}
            onSearchMemory={handleSearchMemory}
          />
        )}
      </main>
    </div>
  );
}
export default App;
