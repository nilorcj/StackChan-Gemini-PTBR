import React, { useState } from 'react';
import { GestureType, HeadMotionStep } from '../types';
import { Move, Play, RefreshCw, Compass, Sliders, Layers, CornerUpRight, RotateCcw } from 'lucide-react';

interface ServoControllerProps {
  currentYawDeg: number;
  currentPitchDeg: number;
  isActive: boolean;
  currentGesture: string;
  onMoveServo: (x: number, y: number, speed: number) => void;
  onQueueGesture: (gesture: GestureType) => void;
  onExecuteHeadMotion: (name: string, steps: HeadMotionStep[]) => void;
}

const GESTURE_PRESETS: { name: GestureType; label: string; category: 'look' | 'search' | 'emotion' }[] = [
  { name: 'center_head', label: 'Centralizar Cabeça (Início)', category: 'look' },
  { name: 'look_at_user', label: 'Olhar para o Usuário', category: 'look' },
  { name: 'look_left_small', label: 'Olhar Esquerda (Leve)', category: 'look' },
  { name: 'look_right_small', label: 'Olhar Direita (Leve)', category: 'look' },
  { name: 'look_left', label: 'Olhar Esquerda (Amplo)', category: 'look' },
  { name: 'look_right', label: 'Olhar Direita (Amplo)', category: 'look' },
  { name: 'look_up', label: 'Olhar para Cima', category: 'look' },
  { name: 'look_down', label: 'Olhar para Baixo', category: 'look' },
  { name: 'look_top_left', label: 'Olhar Superior Esquerdo', category: 'look' },
  { name: 'look_top_right', label: 'Olhar Superior Direito', category: 'look' },

  { name: 'search_left_wide', label: 'Varrer Setor: Esquerda Ampla', category: 'search' },
  { name: 'search_left', label: 'Varrer Setor: Esquerda', category: 'search' },
  { name: 'search_center', label: 'Varrer Setor: Centro', category: 'search' },
  { name: 'search_right', label: 'Varrer Setor: Direita', category: 'search' },
  { name: 'search_right_wide', label: 'Varrer Setor: Direita Ampla', category: 'search' },

  { name: 'nod_yes', label: 'Acenar Sim (Afirmativo)', category: 'emotion' },
  { name: 'shake_no', label: 'Balançar Não (Negativo)', category: 'emotion' },
  { name: 'curious_tilt', label: 'Inclinação Curiosa', category: 'emotion' },
  { name: 'angry_tantrum', label: 'Birra / Protesto (Sacudida)', category: 'emotion' },
  { name: 'dizzy_spin', label: 'Tontura (Olhar Girando)', category: 'emotion' },
  { name: 'lifted_protest', label: 'Levantada da Mesa (Olhar Baixo)', category: 'emotion' },
  { name: 'speaking_micro_motion', label: 'Micro-movimentos ao Falar', category: 'emotion' },
  { name: 'vibrate', label: '📳 Vibração Rápida (Haptic)', category: 'emotion' },
  { name: 'purr_vibration', label: '😻 Ronronar com Vibração', category: 'emotion' },
  { name: 'sneeze_shake', label: '🤧 Espirro Mecânico (Atchim)', category: 'emotion' },
  { name: 'peekaboo_nod', label: '🙈 Esconde-esconde (Achou!)', category: 'emotion' },
  { name: 'petting_tilt', label: '💖 Carinho no Ar (Cafuné)', category: 'emotion' },
  { name: 'pomodoro_alert', label: '⏰ Alerta Tátil Pomodoro', category: 'emotion' },
  { name: 'stop_motion', label: 'Parada de Emergência', category: 'emotion' },
];

export const ServoController: React.FC<ServoControllerProps> = ({
  currentYawDeg,
  currentPitchDeg,
  isActive,
  currentGesture,
  onMoveServo,
  onQueueGesture,
  onExecuteHeadMotion,
}) => {
  const [targetYaw, setTargetYaw] = useState(0);
  const [targetPitch, setTargetPitch] = useState(12);
  const [speed, setSpeed] = useState(500);

  // Custom keyframe designer state
  const [motionName, setMotionName] = useState('custom_scan');
  const [steps, setSteps] = useState<HeadMotionStep[]>([
    { x_deg: -20, y_deg: 5, speed: 500, hold_ms: 400 },
    { x_deg: 20, y_deg: 10, speed: 500, hold_ms: 400 },
    { x_deg: 0, y_deg: 0, speed: 400, hold_ms: 300 },
  ]);

  const handleManualMove = () => {
    // Convert degrees to BSP units: 10 units = 1 deg
    const xUnits = Math.round(targetYaw * 10);
    const yUnits = Math.round(targetPitch * 10);
    onMoveServo(xUnits, yUnits, speed);
  };

  const handleAddStep = () => {
    if (steps.length >= 6) return;
    setSteps([...steps, { x_deg: 0, y_deg: 0, speed: 500, hold_ms: 400 }]);
  };

  const handleRemoveStep = (index: number) => {
    setSteps(steps.filter((_, i) => i !== index));
  };

  const handleUpdateStep = (index: number, field: keyof HeadMotionStep, value: number) => {
    const updated = [...steps];
    updated[index] = { ...updated[index], [field]: value };
    setSteps(updated);
  };

  return (
    <div className="space-y-6">
      {/* Upper Grid: Status & Manual Controls */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
        {/* Real-time Servo Stage & Angle Visualizer */}
        <div className="bg-gray-900/90 border border-gray-800 rounded-2xl p-5 shadow-xl">
          <div className="flex items-center justify-between mb-4">
            <div className="flex items-center gap-2">
              <Compass className="w-5 h-5 text-amber-400" />
              <h3 className="font-semibold text-gray-100">Posição dos Servos (Pan-Tilt)</h3>
            </div>
            <span
              className={`text-xs px-2.5 py-0.5 rounded-full font-mono font-medium ${
                isActive
                  ? 'bg-amber-500/20 text-amber-300 border border-amber-500/30 animate-pulse'
                  : 'bg-emerald-500/10 text-emerald-400 border border-emerald-500/20'
              }`}
            >
              {isActive ? `EXECUTANDO: ${currentGesture}` : 'PARADO / AGUARDANDO'}
            </span>
          </div>

          {/* Interactive 2D Coordinate Grid */}
          <div className="relative w-full h-52 bg-gray-950 rounded-xl border border-gray-800 flex items-center justify-center overflow-hidden">
            {/* Grid axes */}
            <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
              <div className="w-full h-px bg-gray-800"></div>
              <div className="h-full w-px bg-gray-800 absolute"></div>
              {/* Range rings */}
              <div className="w-36 h-36 rounded-full border border-gray-800/80"></div>
              <div className="w-20 h-20 rounded-full border border-gray-800/80"></div>
            </div>

            {/* Current head target pointer */}
            <div
              className="absolute w-8 h-8 rounded-full bg-amber-500/20 border-2 border-amber-400 flex items-center justify-center shadow-lg shadow-amber-500/30 transition-all duration-200"
              style={{
                transform: `translate(${(currentYawDeg / 128) * 110}px, ${-((currentPitchDeg - 45) / 45) * 80}px)`,
              }}
            >
              <div className="w-2.5 h-2.5 bg-amber-400 rounded-full"></div>
            </div>

            {/* Labels */}
            <div className="absolute top-2 left-3 text-[10px] font-mono text-gray-500">
              PAN (X): {currentYawDeg.toFixed(1)}° (-128°..+128°)
            </div>
            <div className="absolute top-2 right-3 text-[10px] font-mono text-gray-500">
              TILT (Y): {currentPitchDeg.toFixed(1)}° (0°..90°)
            </div>
            <div className="absolute bottom-2 left-3 text-[9px] font-mono text-gray-600">
              UNIDADES BSP: X={Math.round(currentYawDeg * 10)} Y={Math.round(currentPitchDeg * 10)}
            </div>
          </div>

          {/* Quick Center Button */}
          <div className="mt-4 flex gap-2">
            <button
              id="servo-center-btn"
              onClick={() => onQueueGesture('center_head')}
              className="flex-1 py-2 rounded-xl bg-gray-800 hover:bg-gray-700 text-gray-200 font-medium text-xs flex items-center justify-center gap-2 border border-gray-700 transition"
            >
              <RotateCcw className="w-3.5 h-3.5 text-amber-400" /> Centralizar Cabeça (0°, 12°)
            </button>
            <button
              id="servo-stop-btn"
              onClick={() => onQueueGesture('stop_motion')}
              className="px-4 py-2 rounded-xl bg-red-500/20 hover:bg-red-500/30 text-red-300 font-medium text-xs border border-red-500/30 transition"
            >
              Parar
            </button>
          </div>
        </div>

        {/* Manual Precision Sliders */}
        <div className="bg-gray-900/90 border border-gray-800 rounded-2xl p-5 shadow-xl flex flex-col justify-between">
          <div>
            <div className="flex items-center gap-2 mb-4">
              <Sliders className="w-5 h-5 text-amber-400" />
              <h3 className="font-semibold text-gray-100">Controle Manual dos Servos</h3>
            </div>

            <div className="space-y-4">
              <div>
                <div className="flex justify-between text-xs text-gray-300 mb-1">
                  <span>Ângulo Pan / Giro Horizontal (Eixo X)</span>
                  <span className="font-mono text-amber-400">{targetYaw}°</span>
                </div>
                <input
                  id="slider-yaw"
                  type="range"
                  min="-128"
                  max="128"
                  step="1"
                  value={targetYaw}
                  onChange={(e) => setTargetYaw(Number(e.target.value))}
                  className="w-full accent-amber-500 h-2 bg-gray-800 rounded-lg cursor-pointer"
                />
                <div className="flex justify-between text-[10px] text-gray-500 mt-0.5 font-mono">
                  <span>-128° (Esquerda)</span>
                  <span>0°</span>
                  <span>+128° (Direita)</span>
                </div>
              </div>

              <div>
                <div className="flex justify-between text-xs text-gray-300 mb-1">
                  <span>Ângulo Tilt / Inclinação Vertical (Eixo Y)</span>
                  <span className="font-mono text-amber-400">{targetPitch}°</span>
                </div>
                <input
                  id="slider-pitch"
                  type="range"
                  min="0"
                  max="90"
                  step="1"
                  value={targetPitch}
                  onChange={(e) => setTargetPitch(Number(e.target.value))}
                  className="w-full accent-amber-500 h-2 bg-gray-800 rounded-lg cursor-pointer"
                />
                <div className="flex justify-between text-[10px] text-gray-500 mt-0.5 font-mono">
                  <span>0° (Baixo)</span>
                  <span>12° (Neutro)</span>
                  <span>90° (Cima)</span>
                </div>
              </div>

              <div>
                <div className="flex justify-between text-xs text-gray-300 mb-1">
                  <span>Velocidade de Movimento</span>
                  <span className="font-mono text-sky-400">{speed}</span>
                </div>
                <input
                  id="slider-speed"
                  type="range"
                  min="100"
                  max="1000"
                  step="50"
                  value={speed}
                  onChange={(e) => setSpeed(Number(e.target.value))}
                  className="w-full accent-sky-500 h-2 bg-gray-800 rounded-lg cursor-pointer"
                />
              </div>
            </div>
          </div>

          <button
            id="servo-send-move-btn"
            onClick={handleManualMove}
            className="w-full mt-4 py-2.5 rounded-xl bg-amber-500 hover:bg-amber-400 text-gray-950 font-bold text-sm flex items-center justify-center gap-2 shadow-lg shadow-amber-500/20 transition"
          >
            <Play className="w-4 h-4 fill-current" /> Executar Movimento Manual
          </button>
        </div>
      </div>

      {/* Preset Gestures Library */}
      <div className="bg-gray-900/90 border border-gray-800 rounded-2xl p-5 shadow-xl">
        <div className="flex items-center gap-2 mb-4">
          <Layers className="w-5 h-5 text-amber-400" />
          <h3 className="font-semibold text-gray-100">Biblioteca de Gestos e Varreduras Pré-programadas</h3>
        </div>

        <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 gap-2.5">
          {GESTURE_PRESETS.map((g) => (
            <button
              key={g.name}
              id={`gesture-${g.name}`}
              onClick={() => onQueueGesture(g.name)}
              className={`p-3 rounded-xl border text-left text-xs font-medium transition flex flex-col justify-between h-20 ${
                currentGesture === g.name
                  ? 'bg-amber-500/20 border-amber-400 text-amber-300 shadow-md'
                  : 'bg-gray-950/70 border-gray-800 hover:border-gray-700 text-gray-300 hover:bg-gray-800/80'
              }`}
            >
              <div className="flex items-center justify-between">
                <span className="font-mono text-[10px] text-gray-400 capitalize">
                  {g.category === 'look' ? 'Olhar' : g.category === 'search' ? 'Busca' : 'Emoção'}
                </span>
                {g.category === 'search' && (
                  <span className="w-2 h-2 rounded-full bg-cyan-400"></span>
                )}
                {g.category === 'emotion' && (
                  <span className="w-2 h-2 rounded-full bg-amber-400"></span>
                )}
              </div>
              <span className="font-medium text-gray-100 line-clamp-1">{g.label}</span>
            </button>
          ))}
        </div>
      </div>

      {/* Parametric Head Motion Keyframe Sequencer */}
      <div className="bg-gray-900/90 border border-gray-800 rounded-2xl p-5 shadow-xl">
        <div className="flex items-center justify-between mb-4">
          <div className="flex items-center gap-2">
            <CornerUpRight className="w-5 h-5 text-amber-400" />
            <h3 className="font-semibold text-gray-100">Sequenciador Paramétrico Personalizado (`head_motion`)</h3>
          </div>
          <button
            onClick={handleAddStep}
            disabled={steps.length >= 6}
            className="px-3 py-1 text-xs rounded-lg bg-gray-800 hover:bg-gray-700 text-gray-200 border border-gray-700 disabled:opacity-50"
          >
            + Adicionar Passo ({steps.length}/6)
          </button>
        </div>

        <div className="mb-4">
          <label className="block text-xs text-gray-400 mb-1">Nome do Movimento</label>
          <input
            type="text"
            value={motionName}
            onChange={(e) => setMotionName(e.target.value)}
            className="w-full px-3 py-2 bg-gray-950 border border-gray-800 rounded-xl text-sm text-gray-200 font-mono"
            placeholder="ex.: acenar_e_olhar_lados"
          />
        </div>

        <div className="space-y-3 mb-4">
          {steps.map((step, idx) => (
            <div
              key={idx}
              className="p-3 bg-gray-950 rounded-xl border border-gray-800 grid grid-cols-2 sm:grid-cols-5 gap-3 items-center"
            >
              <div>
                <span className="text-[10px] text-gray-500 font-mono">Passo #{idx + 1}</span>
                <div className="text-xs font-medium text-gray-300">Posição</div>
              </div>

              <div>
                <label className="block text-[10px] text-gray-400">Δ Pan / Yaw (°)</label>
                <input
                  type="number"
                  min="-65"
                  max="65"
                  value={step.x_deg}
                  onChange={(e) => handleUpdateStep(idx, 'x_deg', Number(e.target.value))}
                  className="w-full px-2 py-1 bg-gray-900 border border-gray-700 rounded text-xs text-amber-300 font-mono"
                />
              </div>

              <div>
                <label className="block text-[10px] text-gray-400">Δ Tilt / Pitch (°)</label>
                <input
                  type="number"
                  min="-6"
                  max="40"
                  value={step.y_deg}
                  onChange={(e) => handleUpdateStep(idx, 'y_deg', Number(e.target.value))}
                  className="w-full px-2 py-1 bg-gray-900 border border-gray-700 rounded text-xs text-amber-300 font-mono"
                />
              </div>

              <div>
                <label className="block text-[10px] text-gray-400">Pausa (ms)</label>
                <input
                  type="number"
                  min="100"
                  max="1500"
                  step="50"
                  value={step.hold_ms}
                  onChange={(e) => handleUpdateStep(idx, 'hold_ms', Number(e.target.value))}
                  className="w-full px-2 py-1 bg-gray-900 border border-gray-700 rounded text-xs text-sky-300 font-mono"
                />
              </div>

              <div className="flex justify-end">
                <button
                  onClick={() => handleRemoveStep(idx)}
                  className="text-xs text-red-400 hover:text-red-300 p-1"
                >
                  ✕ Remover
                </button>
              </div>
            </div>
          ))}
        </div>

        <button
          onClick={() => onExecuteHeadMotion(motionName, steps)}
          className="w-full py-2.5 rounded-xl bg-sky-500 hover:bg-sky-400 text-gray-950 font-bold text-sm flex items-center justify-center gap-2 shadow-lg shadow-sky-500/20 transition"
        >
          <Play className="w-4 h-4 fill-current" /> Executar Sequência Paramétrica
        </button>
      </div>
    </div>
  );
};
