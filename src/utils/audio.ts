// StackChan Modern Warm Audio Synthesizer
// Replaces shrill high-frequency whistle sweeps with warm, cozy, harmonic chime melodies (Kalimba / Marimba / Soft Bell acoustic style)
// Adds Eilik-style grumpy/pouty cute vocalizations and shakes

class DroidAudioEngine {
  private ctx: AudioContext | null = null;
  private soundEnabled = true;
  private voices: SpeechSynthesisVoice[] = [];
  private selectedVoiceUri: string | null = null;
  private voicePitch = 1.32; // Pitch agudo e jovem, garantindo timbre feminino mesmo em vozes neutras
  private voiceRate = 1.05;

  constructor() {
    if (typeof window !== 'undefined' && 'speechSynthesis' in window) {
      this.loadVoices();
      window.speechSynthesis.onvoiceschanged = () => {
        this.loadVoices();
      };
    }
  }

  private loadVoices(): void {
    if (typeof window !== 'undefined' && 'speechSynthesis' in window) {
      this.voices = window.speechSynthesis.getVoices() || [];
    }
  }

  public getAvailableVoices(): SpeechSynthesisVoice[] {
    if (this.voices.length === 0 && typeof window !== 'undefined' && 'speechSynthesis' in window) {
      this.voices = window.speechSynthesis.getVoices() || [];
    }
    return this.voices;
  }

  public setSelectedVoiceUri(uri: string | null): void {
    this.selectedVoiceUri = uri;
  }

  public getSelectedVoiceUri(): string | null {
    return this.selectedVoiceUri;
  }

  public setVoicePitch(pitch: number): void {
    this.voicePitch = Math.max(0.5, Math.min(2.0, pitch));
  }

  public getVoicePitch(): number {
    return this.voicePitch;
  }

  public setVoiceRate(rate: number): void {
    this.voiceRate = Math.max(0.5, Math.min(2.0, rate));
  }

  public getVoiceRate(): number {
    return this.voiceRate;
  }

  private initCtx(): AudioContext {
    if (!this.ctx) {
      const AudioCtxClass = window.AudioContext || (window as any).webkitAudioContext;
      this.ctx = new AudioCtxClass();
    }
    if (this.ctx.state === 'suspended') {
      this.ctx.resume();
    }
    return this.ctx;
  }

  public setSoundEnabled(enabled: boolean) {
    this.soundEnabled = enabled;
  }

  public isSoundEnabled(): boolean {
    return this.soundEnabled;
  }

  /**
   * More realistic, organic "purr/happy" petting sound using multi-oscillator synthesis.
   */
  public playPettingSound(volume = 0.28): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;

      // 1. Dual-cycle warm robotic purr (modulated sub-bass + filtered noise)
      [0, 0.28].forEach((offset) => {
        const osc = ctx.createOscillator();
        const subOsc = ctx.createOscillator();
        const noise = ctx.createBufferSource();
        const buffer = ctx.createBuffer(1, Math.floor(ctx.sampleRate * 0.35), ctx.sampleRate);
        const data = buffer.getChannelData(0);
        for (let i = 0; i < buffer.length; i++) data[i] = (Math.random() * 2 - 1) * 0.4;
        noise.buffer = buffer;

        const noiseFilter = ctx.createBiquadFilter();
        noiseFilter.type = 'lowpass';
        noiseFilter.frequency.setValueAtTime(420, now + offset);

        const noiseGain = ctx.createGain();
        noiseGain.gain.setValueAtTime(0.001, now + offset);
        noiseGain.gain.linearRampToValueAtTime(volume * 0.12, now + offset + 0.08);
        noiseGain.gain.exponentialRampToValueAtTime(0.001, now + offset + 0.32);

        osc.type = 'sine';
        osc.frequency.setValueAtTime(105, now + offset);
        osc.frequency.linearRampToValueAtTime(125, now + offset + 0.3);

        subOsc.type = 'triangle';
        subOsc.frequency.setValueAtTime(52.5, now + offset);

        const toneGain = ctx.createGain();
        toneGain.gain.setValueAtTime(0.001, now + offset);
        toneGain.gain.linearRampToValueAtTime(volume * 0.35, now + offset + 0.06);
        toneGain.gain.exponentialRampToValueAtTime(0.001, now + offset + 0.32);

        osc.connect(toneGain);
        subOsc.connect(toneGain);
        noise.connect(noiseFilter);
        noiseFilter.connect(noiseGain);

        toneGain.connect(ctx.destination);
        noiseGain.connect(ctx.destination);

        osc.start(now + offset);
        subOsc.start(now + offset);
        noise.start(now + offset);
        osc.stop(now + offset + 0.35);
        subOsc.stop(now + offset + 0.35);
        noise.stop(now + offset + 0.35);
      });

      // 2. Affectionate melodic trills of gratitude (ascending cheerful kalimba tones)
      this.playWarmNote(587.33, now + 0.12, 0.22, volume * 0.45, 'triangle'); // D5
      this.playWarmNote(739.99, now + 0.26, 0.24, volume * 0.55, 'sine');     // F#5
      this.playWarmNote(880.00, now + 0.42, 0.30, volume * 0.65, 'sine');     // A5
      this.playWarmNote(1174.66, now + 0.58, 0.35, volume * 0.70, 'triangle'); // D6 (happy sparkle note)
    } catch (err) {
      console.warn('Petting sound error:', err);
    }
  }

  /**
   * Plays a single warm, pleasant bell/kalimba note with soft harmonics and smooth low-pass filtering.
   */
  private playWarmNote(
    freq: number,
    startTime: number,
    duration = 0.28,
    volume = 0.18,
    type: OscillatorType = 'sine'
  ): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = startTime;

      // Primary warm oscillator
      const osc1 = ctx.createOscillator();
      osc1.type = type;
      osc1.frequency.setValueAtTime(freq, now);

      // Subtle harmonic overtone for a rich, cute, round acoustic timbre
      const osc2 = ctx.createOscillator();
      osc2.type = 'sine';
      osc2.frequency.setValueAtTime(freq * 2, now);

      // Soft Low-pass Filter to cut off any harsh piercing high frequencies
      const filter = ctx.createBiquadFilter();
      filter.type = 'lowpass';
      filter.frequency.setValueAtTime(2400, now);
      filter.frequency.exponentialRampToValueAtTime(800, now + duration);

      // Smooth Gain Envelope (soft gentle attack, warm natural decay)
      const gain1 = ctx.createGain();
      gain1.gain.setValueAtTime(0.0001, now);
      gain1.gain.linearRampToValueAtTime(volume, now + 0.015);
      gain1.gain.exponentialRampToValueAtTime(0.0001, now + duration);

      const gain2 = ctx.createGain();
      gain2.gain.setValueAtTime(0.0001, now);
      gain2.gain.linearRampToValueAtTime(volume * 0.25, now + 0.012);
      gain2.gain.exponentialRampToValueAtTime(0.0001, now + duration * 0.7);

      osc1.connect(gain1);
      osc2.connect(gain2);

      gain1.connect(filter);
      gain2.connect(filter);
      filter.connect(ctx.destination);

      osc1.start(now);
      osc2.start(now);
      osc1.stop(now + duration + 0.05);
      osc2.stop(now + duration + 0.05);
    } catch (err) {
      console.warn('Warm note synthesis error:', err);
    }
  }

  /**
   * Boot / Startup Chime: Warm, friendly ascending 3-note crystal chime (C5 -> E5 -> G5)
   */
  public playBootWhistle(volume = 0.22): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.02;

      // C5 (523.25 Hz) -> E5 (659.25 Hz) -> G5 (783.99 Hz) -> C6 (1046.50 Hz)
      const notes = [
        { f: 523.25, dur: 0.22, delay: 0.0 },
        { f: 659.25, dur: 0.24, delay: 0.11 },
        { f: 783.99, dur: 0.28, delay: 0.22 },
        { f: 1046.5, dur: 0.38, delay: 0.34 },
      ];

      notes.forEach((n) => {
        this.playWarmNote(n.f, now + n.delay, n.dur, volume, 'sine');
      });
    } catch (err) {
      console.warn('Boot sound error:', err);
    }
  }

  /**
   * Wake / Attention Chirp: Cute, cheerful 2-note water droplet chime (E5 -> A5)
   */
  public playWakeChirp(volume = 0.2): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;

      // E5 (659.25 Hz) -> A5 (880.00 Hz)
      this.playWarmNote(659.25, now, 0.18, volume * 0.9, 'sine');
      this.playWarmNote(880.0, now + 0.09, 0.25, volume, 'sine');
    } catch (err) {
      console.warn('Wake sound error:', err);
    }
  }

  /**
   * Short, subtle wooden tap / droplet feedback for UI clicks and small servo moves.
   */
  public playBeep(freq = 580, duration = 0.06, type: OscillatorType = 'sine'): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime;
      // Soft gentle low-frequency click/bubble
      const clampedFreq = Math.min(Math.max(freq * 0.7, 340), 750);
      this.playWarmNote(clampedFreq, now, duration + 0.05, 0.12, type);
    } catch (err) {
      console.warn('Beep error:', err);
    }
  }

  /**
   * Gentle descending lullaby sigh when falling asleep (G4 -> E4 -> C4)
   */
  public playSleepChime(volume = 0.16): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;

      // G4 (392.00 Hz) -> E4 (329.63 Hz) -> C4 (261.63 Hz)
      this.playWarmNote(392.0, now, 0.28, volume, 'sine');
      this.playWarmNote(329.63, now + 0.16, 0.32, volume * 0.9, 'sine');
      this.playWarmNote(261.63, now + 0.34, 0.45, volume * 0.75, 'sine');
    } catch (err) {
      console.warn('Sleep chime error:', err);
    }
  }

  /**
   * Playful spark of joy for happy emotion
   */
  public playHappyChime(volume = 0.2): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;

      // C5 -> G5 -> C6 happy sparkle
      this.playWarmNote(523.25, now, 0.15, volume * 0.8, 'sine');
      this.playWarmNote(783.99, now + 0.08, 0.16, volume * 0.9, 'sine');
      this.playWarmNote(1046.5, now + 0.16, 0.26, volume, 'sine');
    } catch (err) {
      console.warn('Happy chime error:', err);
    }
  }

  /**
   * Eilik-style Grumpy / Pouty Droid Groan: Cute robot protest sounds when shaken or lifted!
   * Rapid descending and wobbling pitch (grunhido fofo de robô brava)
   */
  public playGrumpyGrumble(volume = 0.24): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;

      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      const filter = ctx.createBiquadFilter();

      osc.type = 'sawtooth';
      filter.type = 'lowpass';
      filter.frequency.setValueAtTime(1100, now);
      filter.frequency.linearRampToValueAtTime(320, now + 0.45);

      // Pitch wobble / pout: 480Hz -> 380Hz -> 290Hz -> 210Hz
      osc.frequency.setValueAtTime(460, now);
      osc.frequency.linearRampToValueAtTime(360, now + 0.1);
      osc.frequency.linearRampToValueAtTime(420, now + 0.18);
      osc.frequency.linearRampToValueAtTime(220, now + 0.42);

      gain.gain.setValueAtTime(0.001, now);
      gain.gain.linearRampToValueAtTime(volume, now + 0.03);
      gain.gain.linearRampToValueAtTime(volume * 0.8, now + 0.25);
      gain.gain.exponentialRampToValueAtTime(0.0001, now + 0.46);

      osc.connect(filter);
      filter.connect(gain);
      gain.connect(ctx.destination);

      osc.start(now);
      osc.stop(now + 0.48);

      // Follow-up cute short huff
      setTimeout(() => {
        if (!this.soundEnabled) return;
        const now2 = ctx.currentTime + 0.01;
        const osc2 = ctx.createOscillator();
        const gain2 = ctx.createGain();
        osc2.type = 'triangle';
        osc2.frequency.setValueAtTime(320, now2);
        osc2.frequency.exponentialRampToValueAtTime(180, now2 + 0.22);
        gain2.gain.setValueAtTime(0.001, now2);
        gain2.gain.linearRampToValueAtTime(volume * 0.7, now2 + 0.02);
        gain2.gain.exponentialRampToValueAtTime(0.0001, now2 + 0.22);
        osc2.connect(gain2);
        gain2.connect(ctx.destination);
        osc2.start(now2);
        osc2.stop(now2 + 0.24);
      }, 260);
    } catch (err) {
      console.warn('Grumpy groan sound error:', err);
    }
  }

  /**
   * Lift / Dizzy wobble sound when lifted into the air
   */
  public playLiftedSound(volume = 0.22): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();

      osc.type = 'sine';
      // Pitch goes up like "whoaa!"
      osc.frequency.setValueAtTime(280, now);
      osc.frequency.exponentialRampToValueAtTime(740, now + 0.28);
      osc.frequency.linearRampToValueAtTime(560, now + 0.4);

      gain.gain.setValueAtTime(0.001, now);
      gain.gain.linearRampToValueAtTime(volume, now + 0.04);
      gain.gain.exponentialRampToValueAtTime(0.0001, now + 0.45);

      osc.connect(gain);
      gain.connect(ctx.destination);

      osc.start(now);
      osc.stop(now + 0.48);
    } catch (err) {
      console.warn('Lifted sound error:', err);
    }
  }

  /**
   * Robot Sneeze ("Atchim!"): Inhaling buildup, sharp explosive pop/sneeze tone, and descending relief.
   */
  public playSneezeSound(volume = 0.28): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;

      // 1. Inhale windup ("Ah-ah...")
      const inhaleOsc = ctx.createOscillator();
      const inhaleGain = ctx.createGain();
      inhaleOsc.type = 'triangle';
      inhaleOsc.frequency.setValueAtTime(320, now);
      inhaleOsc.frequency.linearRampToValueAtTime(680, now + 0.22);
      inhaleGain.gain.setValueAtTime(0.001, now);
      inhaleGain.gain.linearRampToValueAtTime(volume * 0.4, now + 0.18);
      inhaleGain.gain.exponentialRampToValueAtTime(0.001, now + 0.24);
      inhaleOsc.connect(inhaleGain);
      inhaleGain.connect(ctx.destination);
      inhaleOsc.start(now);
      inhaleOsc.stop(now + 0.25);

      // 2. Sneeze blast ("...TCHIM!")
      const sneezeTime = now + 0.28;
      const blastOsc = ctx.createOscillator();
      const blastNoise = ctx.createBufferSource();
      const buffer = ctx.createBuffer(1, Math.floor(ctx.sampleRate * 0.3), ctx.sampleRate);
      const data = buffer.getChannelData(0);
      for (let i = 0; i < buffer.length; i++) data[i] = (Math.random() * 2 - 1) * 0.5;
      blastNoise.buffer = buffer;

      const noiseFilter = ctx.createBiquadFilter();
      noiseFilter.type = 'bandpass';
      noiseFilter.frequency.setValueAtTime(1400, sneezeTime);
      noiseFilter.Q.setValueAtTime(2.5, sneezeTime);

      const blastGain = ctx.createGain();
      blastGain.gain.setValueAtTime(0.001, sneezeTime);
      blastGain.gain.linearRampToValueAtTime(volume * 0.9, sneezeTime + 0.02);
      blastGain.gain.exponentialRampToValueAtTime(0.001, sneezeTime + 0.28);

      blastOsc.type = 'sawtooth';
      blastOsc.frequency.setValueAtTime(950, sneezeTime);
      blastOsc.frequency.exponentialRampToValueAtTime(240, sneezeTime + 0.26);

      blastOsc.connect(blastGain);
      blastNoise.connect(noiseFilter);
      noiseFilter.connect(blastGain);
      blastGain.connect(ctx.destination);

      blastOsc.start(sneezeTime);
      blastNoise.start(sneezeTime);
      blastOsc.stop(sneezeTime + 0.3);
      blastNoise.stop(sneezeTime + 0.3);
    } catch (err) {
      console.warn('Sneeze sound error:', err);
    }
  }

  /**
   * Deep, affectionate warm purring synthesis (continuous gentle hum with harmonic trills)
   */
  public playPurrSound(volume = 0.25): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;

      // Modulated double pulse
      [0, 0.25, 0.5].forEach((offset) => {
        const osc = ctx.createOscillator();
        const lfo = ctx.createOscillator();
        const lfoGain = ctx.createGain();
        const gain = ctx.createGain();

        osc.type = 'triangle';
        osc.frequency.setValueAtTime(95, now + offset);

        lfo.frequency.setValueAtTime(16, now + offset); // 16Hz vibration flutter
        lfoGain.gain.setValueAtTime(25, now + offset);
        lfo.connect(osc.frequency);

        gain.gain.setValueAtTime(0.001, now + offset);
        gain.gain.linearRampToValueAtTime(volume * 0.5, now + offset + 0.05);
        gain.gain.exponentialRampToValueAtTime(0.001, now + offset + 0.22);

        osc.connect(gain);
        gain.connect(ctx.destination);

        lfo.start(now + offset);
        osc.start(now + offset);
        lfo.stop(now + offset + 0.24);
        osc.stop(now + offset + 0.24);
      });

      // Melodic purr ending note
      this.playWarmNote(880.0, now + 0.35, 0.22, volume * 0.6, 'sine');
      this.playWarmNote(1174.66, now + 0.52, 0.28, volume * 0.7, 'triangle');
    } catch (err) {
      console.warn('Purr sound error:', err);
    }
  }

  /**
   * Peek-a-boo playful giggle with ascending chime sparkles
   */
  public playPeekabooGiggle(volume = 0.24): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;

      // Ascending happy double chirp
      this.playWarmNote(659.25, now, 0.12, volume * 0.75, 'triangle'); // E5
      this.playWarmNote(880.0, now + 0.08, 0.14, volume * 0.85, 'sine'); // A5
      this.playWarmNote(1046.5, now + 0.16, 0.16, volume * 0.9, 'triangle'); // C6
      this.playWarmNote(1318.51, now + 0.26, 0.28, volume, 'sine'); // E6
    } catch (err) {
      console.warn('Peekaboo giggle sound error:', err);
    }
  }

  /**
   * Tactile motor vibration hum synthesizer (sub-bass low rumble simulation)
   */
  public playVibrationHum(durationMs = 350, volume = 0.2): void {
    if (!this.soundEnabled) return;
    try {
      const ctx = this.initCtx();
      const now = ctx.currentTime + 0.01;
      const durSec = durationMs / 1000;

      const osc = ctx.createOscillator();
      const osc2 = ctx.createOscillator();
      const gain = ctx.createGain();

      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(65, now); // 65Hz motor fundamental

      osc2.type = 'sine';
      osc2.frequency.setValueAtTime(130, now);

      const filter = ctx.createBiquadFilter();
      filter.type = 'lowpass';
      filter.frequency.setValueAtTime(220, now);

      gain.gain.setValueAtTime(0.001, now);
      gain.gain.linearRampToValueAtTime(volume * 0.45, now + 0.03);
      gain.gain.setValueAtTime(volume * 0.45, now + durSec - 0.04);
      gain.gain.exponentialRampToValueAtTime(0.001, now + durSec);

      osc.connect(gain);
      osc2.connect(gain);
      gain.connect(filter);
      filter.connect(ctx.destination);

      osc.start(now);
      osc2.start(now);
      osc.stop(now + durSec + 0.02);
      osc2.stop(now + durSec + 0.02);
    } catch (err) {
      console.warn('Vibration hum error:', err);
    }
  }

  // Speech synthesis helper for TTS voice playback
  public speakText(text: string, voiceName = 'Aoede', onEnd?: () => void): void {
    if (typeof window === 'undefined' || !('speechSynthesis' in window)) {
      if (onEnd) onEnd();
      return;
    }

    try {
      window.speechSynthesis.cancel();
      const utterance = new SpeechSynthesisUtterance(text);

      // Pix is a Brazilian companion: always default to pt-BR
      utterance.lang = 'pt-BR';
      utterance.rate = this.voiceRate;
      utterance.pitch = this.voicePitch; // Default 1.32+ ensuring youthful female tone

      const allVoices = this.getAvailableVoices();
      if (allVoices.length > 0) {
        // 1. If user explicitly selected a voice by URI
        if (this.selectedVoiceUri) {
          const matched = allVoices.find((v) => v.voiceURI === this.selectedVoiceUri);
          if (matched) {
            utterance.voice = matched;
          }
        }

        // 2. If no voice is assigned yet, search for verified Portuguese voices
        if (!utterance.voice) {
          const maleKeywords = [
            'daniel',
            'jorge',
            'ricardo',
            'felipe',
            'antonio',
            'antônio',
            'david',
            'mark',
            'guy',
            'george',
            'stefan',
            'diego',
            'helio',
            'hélio',
            'gabriel',
            'mateus',
            'matheus',
            'lucas',
            'pedro',
            'tiago',
            'thiago',
            'male',
            'homem',
            'masculino',
            'masculina',
          ];

          const femaleKeywords = [
            'francisca',
            'thalita',
            'luciana',
            'helena',
            'yara',
            'maria',
            'brenda',
            'giovanna',
            'leila',
            'leticia',
            'letícia',
            'camila',
            'vitoria',
            'vitória',
            'joana',
            'ines',
            'inês',
            'raquel',
            'fernanda',
            'polly',
            'female',
            'mulher',
            'feminina',
            'feminino',
          ];

          // Strict priority for Portuguese voices (pt-BR first, then any pt)
          const ptBrVoices = allVoices.filter(
            (v) => v.lang === 'pt-BR' || v.lang === 'pt_BR' || v.lang.toLowerCase().includes('brazil') || v.lang.toLowerCase().includes('brasil')
          );
          const anyPtVoices = allVoices.filter((v) => v.lang.toLowerCase().startsWith('pt'));
          const ptVoices = ptBrVoices.length > 0 ? ptBrVoices : anyPtVoices;

          if (ptVoices.length > 0) {
            // Priority 1: Brazilian female voice
            const femalePt = ptVoices.find(
              (v) =>
                femaleKeywords.some((fk) => v.name.toLowerCase().includes(fk)) &&
                !maleKeywords.some((mk) => v.name.toLowerCase().includes(mk))
            );

            // Priority 2: Non-male Portuguese voice
            const nonMalePt = ptVoices.find(
              (v) => !maleKeywords.some((mk) => v.name.toLowerCase().includes(mk))
            );

            // Priority 3: Any Portuguese voice
            const chosenPtVoice = femalePt || nonMalePt || ptVoices[0];
            if (chosenPtVoice) {
              utterance.voice = chosenPtVoice;
              const isProbablyMale = maleKeywords.some((mk) => chosenPtVoice.name.toLowerCase().includes(mk));
              if (isProbablyMale) {
                utterance.pitch = 1.45;
                utterance.rate = 1.08;
              }
            }
          } else {
            // No Portuguese voice detected in device list - keep lang as pt-BR so browser attempts online synthesis
            const fallbackFemale = allVoices.find(
              (v) =>
                femaleKeywords.some((fk) => v.name.toLowerCase().includes(fk)) &&
                !maleKeywords.some((mk) => v.name.toLowerCase().includes(mk))
            );
            if (fallbackFemale) {
              utterance.voice = fallbackFemale;
              utterance.pitch = 1.35;
            }
          }
        }
      }

      utterance.onend = () => {
        if (onEnd) onEnd();
      };
      utterance.onerror = (e) => {
        console.warn('TTS utterance error:', e);
        if (onEnd) onEnd();
      };

      window.speechSynthesis.speak(utterance);
    } catch (err) {
      console.warn('Speech synthesis error:', err);
      if (onEnd) onEnd();
    }
  }

  public testVoice(customText?: string): void {
    const sampleText =
      customText ||
      'Oi! Sou a StackChan, sua companheira de mesa! Pode contar comigo pra trocar uma ideia ou rir um pouco.';
    this.speakText(sampleText);
  }
}

export const droidAudio = new DroidAudioEngine();
