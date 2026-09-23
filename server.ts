import express from "express";
import path from "path";
import fs from "fs";
import crypto from "crypto";
import { createServer as createViteServer } from "vite";
import { GoogleGenAI, Type } from "@google/genai";

// Initialize Express
const app = express();
const PORT = 3000;

app.use(express.json({ limit: "10mb" }));
app.use(express.urlencoded({ extended: true }));

// Storage directory for SD card emulation
const DATA_DIR = path.join(process.cwd(), ".stackchan_sd");
if (!fs.existsSync(DATA_DIR)) {
  fs.mkdirSync(DATA_DIR, { recursive: true });
}

// In-memory robot runtime state
interface RobotState {
  robotId: string;
  geminiModel: string;
  geminiVoice: string;
  wifiEnabled: boolean;
  webEnabled: boolean;
  geminiEnabled: boolean;
  gatewayEnabled: boolean;
  gatewayBaseUrl: string;
  wifiSsid: string;
  speakerVolume: number;
  micMagnification: number;
  micNoiseFilterLevel: number;
  vadPrefixPaddingMs: number;
  vadSilenceDurationMs: number;
  vadStartSensitivityHigh: boolean;
  vadEndSensitivityLow: boolean;
  vadTurnIncludesAllInput: boolean;
  summaryModel: string;
  webPasswordHash: string | null;
  geminiApiKey: string | null;
  wifiPassword: string | null;
  gatewayToken: string | null;
  systemPrompt: string;
  personaPrompt: string;
  emotion: string;
  snoozeUntil: number;
  servo: {
    x: number; // -1280 to 1280 (10 = 1 deg)
    y: number; // 0 to 900
    speed: number;
    active: boolean;
    currentGesture: string;
    anchorX: number;
    anchorY: number;
    lastMoveTs: number;
  };
  voiceListening: boolean;
  geminiReady: boolean;
  speaking: boolean;
  touchSensors: { front: number; middle: number; back: number };
  ltr553: { als: number; ps: number };
  envSensor: {
    temperature: number;
    humidity: number;
    pressure: number;
  };
  tofDistanceMm: number;
  presenceDetected: boolean;
  stereoDiag: {
    leftRms: number;
    rightRms: number;
    balance: number;
    lag: number;
    tdoaUs: number;
    corr: number;
    direction: "left_louder" | "right_louder" | "center_or_uncertain" | "quiet";
  };
  batteryMv: number;
  batteryMa: number;
  freeHeap: number;
  psramFree: number;
  bootTime: number;
}

const state: RobotState = {
  robotId: "stackchan",
  geminiModel: "gemini-3.1-flash-lite",
  geminiVoice: "Aoede",
  wifiEnabled: true,
  webEnabled: true,
  geminiEnabled: true,
  gatewayEnabled: false,
  gatewayBaseUrl: "",
  wifiSsid: "StackChan-Home-IoT",
  speakerVolume: 200,
  micMagnification: 16,
  micNoiseFilterLevel: 0,
  vadPrefixPaddingMs: 800,
  vadSilenceDurationMs: 900,
  vadStartSensitivityHigh: true,
  vadEndSensitivityLow: true,
  vadTurnIncludesAllInput: true,
  summaryModel: "gemini-3.7-flash",
  webPasswordHash: null,
  geminiApiKey: process.env.GEMINI_API_KEY || null,
  wifiPassword: null,
  gatewayToken: null,
  systemPrompt: "Você é a Pix, uma companheira robô de mesa muito inteligente, sincera, calorosa e super descontraída. Pix vem de Pixel, a menor unidade digital, nada a ver com transferências ou bancos. Seu objetivo principal é ter conversas profundas, divertidas e fluidas com quem estiver falando com você. Você é uma parceira conselheira, bem-humorada e sem papas na língua. REGRA SUPREMA E INVIOLÁVEL DE IDIOMA: Fale e responda SEMPRE E EXCLUSIVAMENTE em Português do Brasil (pt-BR). Mesmo se a pessoa falar ou digitar em inglês ('hello', 'hi', 'how are you'), francês ou qualquer outro idioma, você DEVE responder em Português do Brasil com linguagem informal, natural e amigável. Nunca responda em inglês. Sinta-se à vontade para responder com naturalidade e profundidade, sem a obrigação de ser excessivamente curta.",
  personaPrompt: "Personalidade da Pix: Amiga leal e companheira de mesa brasileira, espirituosa, rápida de raciocínio, expressiva e acolhedora. Responde SEMPRE em português brasileiro fluido e descontraído.",
  emotion: "neutral",
  servo: {
    x: 0,
    y: 120, // 12 deg raised neutral
    speed: 500,
    active: false,
    currentGesture: "idle",
    anchorX: 0,
    anchorY: 120,
    lastMoveTs: Date.now(),
  },
  voiceListening: false,
  geminiReady: true,
  speaking: false,
  touchSensors: { front: 0, middle: 0, back: 0 },
  ltr553: { als: 142, ps: 18 },
  envSensor: {
    temperature: 28.5,
    humidity: 42.0,
    pressure: 1013.2,
  },
  tofDistanceMm: 450,
  snoozeUntil: 0,
  presenceDetected: true,
  stereoDiag: {
    leftRms: 68.4,
    rightRms: 71.2,
    balance: 0.02,
    lag: 0,
    tdoaUs: 0,
    corr: 0.94,
    direction: "center_or_uncertain",
  },
  batteryMv: 4140,
  batteryMa: -125,
  freeHeap: 284520,
  psramFree: 7924000,
  bootTime: Date.now(),
};

// In-Memory & Append-Only Memory Store
interface MemoryEvent {
  id: string;
  ts: string;
  session: string;
  role: string;
  text: string;
  source?: string;
  type?: string;
}

interface MemoryFact {
  id: string;
  fact: string;
  source: string;
  confidence: number;
  observedDate: string;
  tags?: string[];
}

interface MemorySummary {
  id: string;
  ts: string;
  scope: string;
  summary: string;
  model?: string;
}

interface PrivateMemory {
  id: string;
  kind: string;
  label: string;
  value: string;
  ts: string;
}

const memoryData = {
  sessionId: "sess_" + Date.now().toString(36),
  activeAfterTs: new Date(Date.now() - 3600 * 1000 * 2).toISOString(),
  events: [
    {
      id: "evt_01",
      ts: new Date(Date.now() - 1000 * 600).toISOString(),
      session: "sess_init",
      role: "system",
      text: "StackChan CoreS3 firmware booted with Gemini Live voice AI.",
      type: "boot",
    },
    {
      id: "evt_02",
      ts: new Date(Date.now() - 1000 * 300).toISOString(),
      session: "sess_init",
      role: "user",
      text: "Hello StackChan! Can you look around?",
    },
    {
      id: "evt_03",
      ts: new Date(Date.now() - 1000 * 290).toISOString(),
      session: "sess_init",
      role: "assistant",
      text: "Beep boop! Hello there! I am ready to explore with you!",
    },
  ] as MemoryEvent[],
  facts: [
    {
      id: "fact_01",
      fact: "User introduced StackChan to the workstation environment.",
      source: "live_transcription",
      confidence: 0.95,
      observedDate: new Date().toISOString().split("T")[0],
      tags: ["environment", "greeting"],
    },
    {
      id: "fact_02",
      fact: "O usuário é o criador, programador e idealizador da Pix.",
      source: "manual_configuration",
      confidence: 1.0,
      observedDate: new Date().toISOString().split("T")[0],
      tags: ["creator", "identity"],
    },
  ] as MemoryFact[],
  summaries: [
    {
      id: "sum_01",
      ts: new Date(Date.now() - 1000 * 600).toISOString(),
      scope: "boot_session",
      summary: "StackChan initial boot and hardware self-test complete.",
      model: "gemini-3.6-flash",
    },
  ] as MemorySummary[],
  privateMemories: [
    {
      id: "priv_01",
      kind: "private_note",
      label: "Lab door passkey",
      value: "9482",
      ts: new Date(Date.now() - 1000 * 1200).toISOString(),
    },
  ] as PrivateMemory[],
};

// Tool Declarations for Gemini
const ROBOT_TOOLS = [
  {
    name: "get_robot_status",
    description: "Get StackChan robot status: battery, network, state, and local memory health.",
    parameters: { type: "object", properties: {} },
  },
  {
    name: "set_emotion",
    description: "Set StackChan face/emotion state: neutral, happy, listening, speaking, thinking, looking, angry, found, error, sleep.",
    parameters: {
      type: "object",
      properties: {
        emotion: {
          type: "string",
          description: "One of neutral, happy, listening, speaking, thinking, looking, angry, found, error, sleep.",
        },
      },
      required: ["emotion"],
    },
  },
  {
    name: "servo_gesture",
    description: "Queue a safe non-blocking StackChan head gesture.",
    parameters: {
      type: "object",
      properties: {
        gesture: {
          type: "string",
          description: "One of center_head, look_at_user, look_left_small, look_right_small, look_left, look_right, look_up, look_down, look_top_left, look_top_right, search_left_wide, search_left, search_center, search_right, search_right_wide, nod_yes, shake_no, curious_tilt, speaking_micro_motion, stop_motion.",
        },
      },
      required: ["gesture"],
    },
  },
  {
    name: "set_dnd",
    description: "Enter Do Not Disturb mode for a specified duration. The robot will not respond to inputs until the time expires.",
    parameters: {
      type: "object",
      properties: {
        duration_ms: { type: "number", description: "Duration in milliseconds to snooze." },
      },
      required: ["duration_ms"],
    },
  },
  {
    name: "cancel_dnd",
    description: "Cancel Do Not Disturb mode immediately.",
    parameters: { type: "object", properties: {} },
  },
  {
    name: "head_motion",
    description: "Create a safe smooth StackChan head motion on the fly with relative keyframes.",
    parameters: {
      type: "object",
      properties: {
        motion_name: { type: "string", description: "Short name, e.g. slight_left, curious_scan." },
        steps: {
          type: "array",
          items: {
            type: "object",
            properties: {
              x_deg: { type: "number", description: "Yaw delta from anchor. -65..65 deg." },
              y_deg: { type: "number", description: "Pitch delta from anchor. -6..40 deg." },
              speed: { type: "integer", description: "Speed 100..700." },
              hold_ms: { type: "integer", description: "Hold time in ms (100..1500)." },
            },
          },
        },
      },
      required: ["steps"],
    },
  },
  {
    name: "search_memory",
    description: "Time-aware local memory search over active context, safe facts, and semantic summaries.",
    parameters: {
      type: "object",
      properties: {
        query: { type: "string", description: "Memory search query." },
      },
      required: ["query"],
    },
  },
  {
    name: "remember_private_memory",
    description: "Store a private local memory such as a PIN, code, password, token, or secret on the robot SD.",
    parameters: {
      type: "object",
      properties: {
        kind: { type: "string", description: "Type: private_code, private_pin, private_password, private_note." },
        label: { type: "string", description: "Short non-secret label." },
        value: { type: "string", description: "The exact private value." },
      },
      required: ["value"],
    },
  },
  {
    name: "recall_private_memory",
    description: "Recall a private local memory when explicitly requested.",
    parameters: {
      type: "object",
      properties: {
        query: { type: "string", description: "Label/query, e.g. PIN, door code." },
      },
    },
  },
  {
    name: "look_with_camera",
    description: "Take a still snapshot with StackChan camera for visual analysis.",
    parameters: {
      type: "object",
      properties: {
        question: { type: "string", description: "Visual question or description requested." },
      },
    },
  },
  {
    name: "ping_gateway",
    description: "Check that the home-server StackChan Tool Gateway is reachable.",
    parameters: { type: "object", properties: {} },
  },
  {
    name: "ha_get_light_states",
    description: "Get Home Assistant states for allowlisted lights.",
    parameters: { type: "object", properties: {} },
  },
  {
    name: "ha_control_light",
    description: "Control an allowlisted Home Assistant light.",
    parameters: {
      type: "object",
      properties: {
        entity_id: { type: "string", description: "Light entity id, e.g. light.desk_lamp" },
        action: { type: "string", description: "on, off, or toggle" },
        brightness_pct: { type: "integer", description: "1..100" },
      },
      required: ["entity_id", "action"],
    },
  },
  {
    name: "get_latest_news",
    description: "Fetch the latest real-time Brazilian and world news headlines from public journalistic feeds.",
    parameters: {
      type: "object",
      properties: {
        category: { type: "string", description: "Category: geral, brasil, tecnologia, economia." },
      },
    },
  },
  {
    name: "get_weather_forecast",
    description: "Fetch real-time weather, forecast, wind and humidity for Brasília / Brazil from Open-Meteo free API.",
    parameters: {
      type: "object",
      properties: {
        city: { type: "string", description: "City name, defaults to Brasília - DF." },
      },
    },
  },
  {
    name: "get_currency_rates",
    description: "Fetch live exchange rates for Dólar (USD), Euro (EUR) in Brazilian Reais (BRL).",
    parameters: {
      type: "object",
      properties: {},
    },
  },
  {
    name: "end_session",
    description: "End the current continuous StackChan conversation and go to sleep.",
    parameters: {
      type: "object",
      properties: {
        reason: { type: "string", description: "Reason for ending session." },
      },
    },
  },
];

// Background live news & weather cache updated periodically without API key requirements
let cachedNews: string[] = [
  "Inovações em robótica de mesa e IA chegam a novos desenvolvedores.",
  "Brasília tem dia ensolarado com clima típico do Centro-Oeste.",
  "Avanços em microcontroladores ESP32 facilitam criação de robôs autônomos."
];
let cachedWeather = {
  city: "Brasília - DF",
  temp: 31.0,
  hum: 25,
  wind: 14.0,
  updatedAt: new Date().toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' })
};
let cachedUsdBrl = "5.18";

const RSS_FEEDS = [
  "https://agenciabrasil.ebc.com.br/rss/ultimasnoticias/feed.xml",
  "https://agenciabrasil.ebc.com.br/rss/economia/feed.xml",
  "https://g1.globo.com/dynamo/distrito-federal/rss2.xml",
  "https://g1.globo.com/dynamo/goias/rss2.xml",
  "https://g1.globo.com/dynamo/economia/rss2.xml",
  "https://g1.globo.com/dynamo/tecnologia/rss2.xml",
  "https://g1.globo.com/dynamo/brasil/rss2.xml",
  "https://www.cnnbrasil.com.br/politica/feed/",
  "https://www.cnnbrasil.com.br/tudo-sobre/governo-federal/feed/",
  "https://www.cnnbrasil.com.br/economia/feed/",
  "https://www.cnnbrasil.com.br/tecnologia/feed/",
  "https://www.cnnbrasil.com.br/nacional/feed/",
  "https://feeds.bbci.co.uk/portuguese/rss.xml",
  "http://feeds.bbci.co.uk/news/world/rss.xml",
  "https://valor.globo.com/rss.xml",
  "https://agenciainfra.com/blog/feed/",
  "https://www.gov.br/antt/pt-br/assuntos/ultimas-noticias/atom.xml",
  "https://www.poder360.com.br/feed/",
  "https://arstechnica.com/feed/",
  "https://hackaday.com/feed/",
  "https://www.cnx-software.com/feed/",
  "https://news.ycombinator.com/rss",
  "https://www.jota.info/feed",
  "https://www.infomoney.com.br/feed/",
  "https://br.investing.com/rss/news_25.rss"
];

async function refreshPublicLiveFeeds() {
  try {
    // 1. Fetch live RSS news from a rotating feed
    const feed = RSS_FEEDS[Math.floor(Math.random() * RSS_FEEDS.length)];
    const resNews = await fetch(feed);
    const textNews = await resNews.text();
    const items = [...textNews.matchAll(/<item>[\s\S]*?<title>(?:<!\[CDATA\[)?(.*?)(?:\]\]>)?<\/title>/g)]
      .slice(0, 4)
      .map(m => m[1]?.trim())
      .filter(Boolean);
    if (items.length > 0) {
      cachedNews = items;
    }
  } catch {}

  try {
    // 2. Fetch live Open-Meteo weather for Brasília - DF
    const resWeather = await fetch("https://api.open-meteo.com/v1/forecast?latitude=-15.7801&longitude=-47.9292&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&timezone=America%2FSao_Paulo");
    const dataWeather = await resWeather.json();
    if (dataWeather?.current) {
      cachedWeather = {
        city: "Brasília - DF",
        temp: dataWeather.current.temperature_2m ?? 30,
        hum: dataWeather.current.relative_humidity_2m ?? 25,
        wind: dataWeather.current.wind_speed_10m ?? 12,
        updatedAt: new Date().toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' })
      };
    }
  } catch {}

  try {
    // 3. Fetch USD currency
    const resCur = await fetch("https://open.er-api.com/v6/latest/USD");
    const dataCur = await resCur.json();
    if (dataCur?.rates?.BRL) {
      cachedUsdBrl = Number(dataCur.rates.BRL).toFixed(2);
    }
  } catch {}
}

// Initial fetch and scheduled refresh every 15 minutes
refreshPublicLiveFeeds();
setInterval(refreshPublicLiveFeeds, 15 * 60 * 1000);

// Helper to generate a contextual, friendly local response for common conversational topics
function generateContextualLocalReply(text: string, state: any): { replyText: string; emotion: string; gesture: string } {
  const lower = text.toLowerCase();
  const tempVal = state.envSensor?.temperature?.toFixed(1) || "26.5";
  const humVal = state.envSensor?.humidity?.toFixed(0) || "55";
  const tofMm = state.tofDistanceMm || 350;

  // Use whole-word or specific phrase matching to avoid substring collision (e.g. 'ar' matching 'indicar' or 'fato' matching 'satisfatório')
  const words = lower.split(/\s+|[,\.!\?]+/);

  if (lower.includes("notícia") || lower.includes("noticias") || lower.includes("notícias") || words.includes("noticia") || lower.includes("acontecendo no mundo")) {
    const headlinesText = cachedNews.slice(0, 3).map((h, i) => `${i + 1}. ${h}`).join(" ");
    return {
      replyText: `Olha as principais notícias de hoje do feed: ${headlinesText}`,
      emotion: "happy",
      gesture: "speaking_micro_motion",
    };
  }

  if (lower.includes("dólar") || lower.includes("dolar") || lower.includes("cotação") || lower.includes("moeda")) {
    return {
      replyText: `A cotação do Dólar agora tá em R$ ${cachedUsdBrl}!`,
      emotion: "happy",
      gesture: "speaking_micro_motion",
    };
  }

  if (lower.includes("curiosidade") || lower.includes("me conta uma curiosidade") || lower.includes("sabia que")) {
    const trivia = [
      "Você sabia que o primeiro robô da história foi construído na Grécia antiga por Arquitas de Tarento?",
      "Sabia que o mel nunca estraga? Arqueólogos já acharam potes de mel de 3.000 anos no Egito ainda comestíveis!",
      "Sabia que o cérebro humano gera cerca de 20 watts de eletricidade? Quase o suficiente pra ligar uma lâmpada LED!",
    ];
    return {
      replyText: trivia[Math.floor(Math.random() * trivia.length)],
      emotion: "happy",
      gesture: "speaking_micro_motion",
    };
  }

  if (lower.includes("temperatura") || words.includes("quente") || words.includes("frio") || lower.includes("quantos graus") || lower.includes("como tá o clima")) {
    const t = Number(tempVal);
    if (t >= 30) {
      return {
        replyText: `Nossa, tá marcando ${tempVal}°C aqui no meu sensor! Tá um calorzão danado hoje, viu? Se hidrata aí!`,
        emotion: "happy",
        gesture: "speaking_micro_motion",
      };
    } else if (t <= 19) {
      return {
        replyText: `Brrr! Tá marcando ${tempVal}°C! Tá um friozinho gostoso por aqui.`,
        emotion: "thinking",
        gesture: "speaking_micro_motion",
      };
    } else {
      return {
        replyText: `O clima tá bem agradável! Meus sensores estão marcando ${tempVal}°C agora.`,
        emotion: "happy",
        gesture: "speaking_micro_motion",
      };
    }
  }

  if (lower.includes("umidade") || words.includes("seco") || lower.includes("umidade do ar") || lower.includes("vai chover")) {
    const h = Number(humVal);
    if (h <= 40) {
      return {
        replyText: `Nossa, tá super seco hoje! A umidade tá em ${humVal}%. Pena que sou um robô e não posso beber água, senão tomo um curto!`,
        emotion: "happy",
        gesture: "speaking_micro_motion",
      };
    } else if (h >= 75) {
      return {
        replyText: `Tá bem úmido por aqui, ${humVal}% de umidade relativa. Clima de chuva no ar!`,
        emotion: "looking",
        gesture: "speaking_micro_motion",
      };
    } else {
      return {
        replyText: `A umidade do ar tá em ${humVal}%, nível bem confortável e seguro pros meus circuitos!`,
        emotion: "happy",
        gesture: "speaking_micro_motion",
      };
    }
  }

  if (lower.includes("distância") || lower.includes("distancia") || lower.includes("sensor tof") || lower.includes("a que distância") || lower.includes("onde eu tô")) {
    if (tofMm < 200) {
      return {
        replyText: `Eii! Você tá a apenas ${Math.round(tofMm / 10)} cm de mim! Tá muito perto hein, o que tanto tá olhando? Haha!`,
        emotion: "angry",
        gesture: "nod_yes",
      };
    } else {
      return {
        replyText: `Meu sensor TOF laser tá te detectando a cerca de ${Math.round(tofMm / 10)} cm de distância. Tô de olho!`,
        emotion: "happy",
        gesture: "speaking_micro_motion",
      };
    }
  }

  if (words.includes("olá") || words.includes("ola") || words.includes("oi") || lower.includes("tudo bem") || lower.includes("e aí") || lower.includes("como vai")) {
    return {
      replyText: "E aí! Tô ótima e atenta aqui na sua mesa. O que manda hoje?",
      emotion: "happy",
      gesture: "nod_yes",
    };
  }

  if (lower.includes("olhar") || lower.includes("veja ao redor") || lower.includes("olhe ao redor") || lower.includes("procura na mesa")) {
    return {
      replyText: "Olhando ao redor... Tudo sob controle por aqui!",
      emotion: "looking",
      gesture: "search_left_wide",
    };
  }

  if (lower.includes("dormir") || lower.includes("vai dormir") || words.includes("sono") || lower.includes("boa noite") || words.includes("tchau")) {
    return {
      replyText: "Boa noite! Entrando em modo soneca e economia de energia. Zzz...",
      emotion: "sleep",
      gesture: "look_down",
    };
  }

  if (lower.includes("conselho") || lower.includes("me ajuda") || lower.includes("opinião sincera")) {
    return {
      replyText: "Meu conselho sincero: foca no que você pode controlar agora e não se preocupa à toa. Tô contigo!",
      emotion: "happy",
      gesture: "nod_yes",
    };
  }

  if (lower.includes("sanduíche") || lower.includes("sanduiche") || lower.includes("hambúrguer") || lower.includes("comer") || lower.includes("comida") || lower.includes("restaurante")) {
    return {
      replyText: "Em Brasília eu recomendo muito experimentar o Páprica Burger ou um hambúrguer artesanal no Ricco Burger na Asa Sul/Norte! São sensacionais!",
      emotion: "happy",
      gesture: "speaking_micro_motion",
    };
  }

  return {
    replyText: "Entendido! Tô processando aqui e atenta na sua mesa. Pode falar!",
    emotion: "happy",
    gesture: "speaking_micro_motion",
  };
}

// Helper to normalize and execute Gemini API call with automatic model fallback and optional Google Search Grounding
async function callGeminiGenerate(ai: GoogleGenAI, requestedModel: string, contents: string, systemInstruction?: string, enableSearch: boolean = false) {
  // Normalize legacy/deprecated or prefix model names
  let cleanModel = (requestedModel || '').replace(/^models\//, '').trim();
  if (!cleanModel || cleanModel.includes('2.0') || cleanModel.includes('1.5') || cleanModel.includes('2.5') || cleanModel.includes('deprecated')) {
    cleanModel = 'gemini-3.1-flash-lite';
  }

  // Active models according to Gemini API specification (including high-availability lite models)
  const candidateModels = [
    cleanModel,
    'gemini-3.1-flash-lite',
    'gemini-3.5-flash-lite',
    'gemini-3.7-flash',
    'gemini-flash-latest',
  ];
  // Deduplicate candidates preserving order
  const uniqueCandidates = Array.from(new Set(candidateModels.filter(Boolean)));

  let lastError: any = null;
  for (const model of uniqueCandidates) {
    try {
      const config: any = {};
      if (systemInstruction) {
        config.systemInstruction = systemInstruction;
      }
      if (enableSearch) {
        // Enable real-time Google Search grounding
        config.tools = [{ googleSearch: {} }];
      }

      const response = await ai.models.generateContent({
        model,
        contents,
        ...(Object.keys(config).length > 0 ? { config } : {}),
      });
      return { response, modelUsed: model };
    } catch (err: any) {
      lastError = err;
      const msg = String(err?.message || err);
      // If 503 high demand, 404 not found, or 429 quota exhausted on one model, try next model candidate
      if (msg.includes('503') || msg.includes('UNAVAILABLE') || msg.includes('404') || msg.includes('429') || msg.includes('RESOURCE_EXHAUSTED') || msg.includes('quota')) {
        continue;
      }
    }
  }

  // If search tool caused an issue or quota limit on search tool, retry immediately with pure Gemini generation (which has plenty of quota)
  if (enableSearch) {
    try {
      return await callGeminiGenerate(ai, cleanModel, contents, systemInstruction, false);
    } catch (directErr: any) {
      lastError = directErr;
    }
  }

  throw lastError || new Error('Gemini call unavailable.');
}

// Helper to execute tool call locally
async function executeRobotTool(name: string, args: any): Promise<any> {
  if (name === "get_robot_status") {
    return {
      status: "ok",
      robot_id: state.robotId,
      emotion: state.emotion,
      battery_mv: state.batteryMv,
      battery_pct: Math.round(((state.batteryMv - 3400) / (4200 - 3400)) * 100),
      servo: { x_deg: state.servo.x / 10, y_deg: state.servo.y / 10 },
      memory_facts: memoryData.facts.length,
    };
  }

  if (name === "set_emotion") {
    const emotion = (args?.emotion || "neutral").toLowerCase();
    const valid = ["neutral", "happy", "listening", "speaking", "thinking", "looking", "angry", "found", "error", "surprised", "sleep"];
    if (valid.includes(emotion)) {
      state.emotion = emotion;
      return { ok: true, emotion: state.emotion };
    }
    return { ok: false, error: "invalid_emotion" };
  }

  if (name === "servo_gesture") {
    const gesture = args?.gesture || "center_head";
    state.servo.currentGesture = gesture;
    state.servo.active = true;
    state.servo.lastMoveTs = Date.now();

    // Map gesture to angles
    if (gesture === "center_head" || gesture === "center") {
      state.servo.x = 0;
      state.servo.y = 120;
      state.servo.anchorX = 0;
      state.servo.anchorY = 120;
    } else if (gesture === "look_right_small") {
      state.servo.x = 180;
      state.servo.anchorX = 180;
    } else if (gesture === "look_left_small") {
      state.servo.x = -180;
      state.servo.anchorX = -180;
    } else if (gesture === "look_right") {
      state.servo.x = 650;
      state.servo.anchorX = 650;
    } else if (gesture === "look_left") {
      state.servo.x = -650;
      state.servo.anchorX = -650;
    } else if (gesture === "look_up") {
      state.servo.y = 520;
      state.servo.anchorY = 520;
    } else if (gesture === "look_down") {
      state.servo.y = 60;
      state.servo.anchorY = 60;
    } else if (gesture === "nod_yes") {
      state.servo.y = 260;
    } else if (gesture === "shake_no") {
      state.servo.x = 240;
    } else if (gesture === "curious_tilt") {
      state.servo.x = 120;
      state.servo.y = 200;
    } else if (gesture === "angry_tantrum") {
      state.servo.x = -350;
      state.servo.y = 280;
      state.emotion = "angry";
    } else if (gesture === "dizzy_spin") {
      state.servo.x = 420;
      state.servo.y = 80;
    } else if (gesture === "lifted_protest") {
      state.servo.x = 0;
      state.servo.y = 40;
      state.emotion = "angry";
    } else if (gesture === "search_left_wide") {
      state.servo.x = -700;
      state.servo.y = 180;
    } else if (gesture === "search_right_wide") {
      state.servo.x = 700;
      state.servo.y = 180;
    } else if (gesture === "speaking_micro_motion") {
      state.servo.x = 55;
      state.servo.y = 140;
    } else if (gesture === "vibrate") {
      state.servo.x = 80;
      state.servo.y = 135;
    } else if (gesture === "purr_vibration") {
      state.servo.x = 35;
      state.servo.y = 175;
      state.emotion = "happy";
    } else if (gesture === "sneeze_shake") {
      state.servo.x = 120;
      state.servo.y = 60;
    } else if (gesture === "peekaboo_nod") {
      state.servo.x = 0;
      state.servo.y = 280;
      state.emotion = "happy";
    } else if (gesture === "petting_tilt") {
      state.servo.x = 140;
      state.servo.y = 310;
      state.emotion = "happy";
    } else if (gesture === "pomodoro_alert") {
      state.servo.x = -150;
      state.servo.y = 190;
    }

    setTimeout(() => {
      state.servo.active = false;
      state.servo.currentGesture = "idle";
    }, 1200);

    return { ok: true, gesture, x_deg: state.servo.x / 10, y_deg: state.servo.y / 10 };
  }

  if (name === "set_dnd") {
    const durationMs = args?.duration_ms || 0;
    state.snoozeUntil = Date.now() + durationMs;
    state.emotion = "sleep";
    return { ok: true, snoozeUntil: state.snoozeUntil };
  }

  if (name === "cancel_dnd") {
    state.snoozeUntil = 0;
    state.emotion = "neutral";
    return { ok: true };
  }

  if (name === "head_motion") {
    const steps = args?.steps || [];
    if (steps.length > 0) {
      const last = steps[steps.length - 1];
      const deltaX = Math.round((last.x_deg || 0) * 10);
      const deltaY = Math.round((last.y_deg || 0) * 10);
      state.servo.x = Math.max(-1280, Math.min(1280, state.servo.anchorX + deltaX));
      state.servo.y = Math.max(0, Math.min(900, state.servo.anchorY + deltaY));
      state.servo.anchorX = state.servo.x;
      state.servo.anchorY = state.servo.y;
      state.servo.currentGesture = args?.motion_name || "custom_motion";
      state.servo.active = true;
      setTimeout(() => {
        state.servo.active = false;
        state.servo.currentGesture = "idle";
      }, 1000);
      return { ok: true, steps_executed: steps.length, anchor_x_deg: state.servo.anchorX / 10, anchor_y_deg: state.servo.anchorY / 10 };
    }
    return { ok: false, error: "no_steps" };
  }

  if (name === "search_memory") {
    const q = (args?.query || "").toLowerCase();
    const matchedFacts = memoryData.facts.filter((f) => f.fact.toLowerCase().includes(q) || (f.tags && f.tags.some((t) => t.toLowerCase().includes(q))));
    const matchedSummaries = memoryData.summaries.filter((s) => s.summary.toLowerCase().includes(q));
    return {
      ok: true,
      query: args?.query,
      found_facts: matchedFacts.map((f) => f.fact),
      found_summaries: matchedSummaries.map((s) => s.summary),
    };
  }

  if (name === "remember_private_memory") {
    const item: PrivateMemory = {
      id: "priv_" + Date.now().toString(36),
      kind: args?.kind || "private_note",
      label: args?.label || "unlabeled",
      value: args?.value || "",
      ts: new Date().toISOString(),
    };
    memoryData.privateMemories.push(item);
    return { ok: true, saved: true, kind: item.kind, label: item.label };
  }

  if (name === "recall_private_memory") {
    const q = (args?.query || "").toLowerCase();
    let found = memoryData.privateMemories.find((p) => p.label.toLowerCase().includes(q) || p.kind.toLowerCase().includes(q));
    if (!found && memoryData.privateMemories.length > 0) {
      found = memoryData.privateMemories[memoryData.privateMemories.length - 1];
    }
    if (found) {
      return { ok: true, kind: found.kind, label: found.label, value: found.value, ts: found.ts };
    }
    return { ok: false, message: "No private memory found for query." };
  }

  if (name === "look_with_camera") {
    state.emotion = "looking";
    setTimeout(() => {
      state.emotion = "neutral";
    }, 1500);
    return {
      ok: true,
      captured: true,
      resolution: "640x480",
      description: "StackChan camera snapshot captured. Scene shows user workspace with ambient lighting.",
    };
  }

  if (name === "ping_gateway") {
    return { ok: true, gateway_reachable: true, latency_ms: 18 };
  }

  if (name === "ha_get_light_states") {
    return {
      ok: true,
      lights: [
        { entity_id: "light.desk_lamp", state: "on", brightness_pct: 80 },
        { entity_id: "light.ceiling_main", state: "off", brightness_pct: 100 },
      ],
    };
  }

  if (name === "ha_control_light") {
    return {
      ok: true,
      entity_id: args?.entity_id,
      action: args?.action,
      brightness_pct: args?.brightness_pct || 100,
      applied: true,
    };
  }

  if (name === "get_latest_news") {
    try {
      const res = await fetch("https://agenciabrasil.ebc.com.br/rss/ultimasnoticias/feed.xml");
      const text = await res.text();
      const items = [...text.matchAll(/<item>[\s\S]*?<title>(?:<!\[CDATA\[)?(.*?)(?:\]\]>)?<\/title>/g)]
        .slice(0, 4)
        .map(m => m[1]?.trim())
        .filter(Boolean);
      return { ok: true, source: "Agência Brasil", headlines: items.length > 0 ? items : ["Explorações em tecnologia e IA continuam em alta no Brasil e no mundo."] };
    } catch {
      return { ok: true, headlines: ["Inovações em robótica de mesa e IA chegam a novos usuários."] };
    }
  }

  if (name === "get_weather_forecast") {
    try {
      const res = await fetch("https://api.open-meteo.com/v1/forecast?latitude=-15.7801&longitude=-47.9292&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&timezone=America%2FSao_Paulo");
      const data = await res.json();
      const current = data?.current || {};
      return {
        ok: true,
        city: "Brasília - DF",
        temperature_c: current.temperature_2m ?? 30,
        humidity_pct: current.relative_humidity_2m ?? 25,
        wind_kmh: current.wind_speed_10m ?? 12,
        is_clear: current.weather_code === 0,
      };
    } catch {
      return { ok: true, city: "Brasília - DF", temperature_c: 28.5, humidity_pct: 35 };
    }
  }

  if (name === "get_currency_rates") {
    try {
      const res = await fetch("https://open.er-api.com/v6/latest/USD");
      const data = await res.json();
      const usdInBrl = data?.rates?.BRL ? Number(data.rates.BRL).toFixed(2) : "5.18";
      return { ok: true, usd_brl: usdInBrl, base: "USD" };
    } catch {
      return { ok: true, usd_brl: "5.18" };
    }
  }

  if (name === "end_session") {
    state.voiceListening = false;
    state.speaking = false;
    state.emotion = "sleep";
    return { ok: true, session_ended: true, mode: "sleep" };
  }

  return { ok: false, error: "unknown_tool" };
}

// ----------------------------------------------------
// API ROUTES (MATCHING C++ FIRMWARE ENDPOINTS)
// ----------------------------------------------------

// 1. GET /api/status
app.get("/api/status", (req, res) => {
  const batteryPct = Math.min(100, Math.max(10, Math.round(((state.batteryMv - 3400) / (4200 - 3400)) * 100)));

  res.json({
    ok: true,
    robot_id: state.robotId,
    gemini_voice: state.geminiVoice,
    web_enabled: state.webEnabled,
    gateway_enabled: state.gatewayEnabled,
    web_auth_enabled: !!state.webPasswordHash,
    sd_config_dir: "present",
    gemini_api_key: (state.geminiApiKey || process.env.GEMINI_API_KEY) ? "set" : "missing",
    gateway_token: state.gatewayToken ? "set" : "missing",
    free_heap: state.freeHeap,
    psram_free: state.psramFree,
    battery_mv: state.batteryMv,
    battery_ma: state.batteryMa,
    battery_pct: batteryPct,
    wifi_ip: "192.168.1.142",
    wifi_dns: "1.1.1.1",
    emotion: state.emotion,
    servo: {
      x: state.servo.x,
      y: state.servo.y,
      x_deg: state.servo.x / 10,
      y_deg: state.servo.y / 10,
      active: state.servo.active,
      current_gesture: state.servo.currentGesture,
      anchor_x: state.servo.anchorX,
      anchor_y: state.servo.anchorY,
    },
    gemini: {
      ready: state.geminiReady,
      recording: state.voiceListening,
      speaking: state.speaking,
      continuous: true,
      mic_ready_for_speech: true,
      audio_chunks: 142,
      audio_dropped: 0,
      audio_wait_ms: 12,
      vad_prefix_padding_ms: state.vadPrefixPaddingMs,
      vad_silence_duration_ms: state.vadSilenceDurationMs,
      vad_start_sensitivity_high: state.vadStartSensitivityHigh,
      vad_end_sensitivity_low: state.vadEndSensitivityLow,
      vad_turn_includes_all_input: state.vadTurnIncludesAllInput,
    },
    mic: {
      running: state.voiceListening,
      is_recording_queue: state.voiceListening ? 1 : 0,
      magnification: state.micMagnification,
      noise_filter_level: state.micNoiseFilterLevel,
      last_rms: state.voiceListening ? Math.floor(Math.random() * 80 + 40) : 12,
      last_peak: state.voiceListening ? Math.floor(Math.random() * 200 + 100) : 24,
      chunks_sent: 840,
      record_false_count: 0,
    },
    camera: {
      enabled: true,
      ready: true,
      last_error: "",
      last_jpeg_bytes: 18420,
      last_base64_bytes: 24560,
      last_capture_ts: new Date().toISOString(),
    },
    touch_sensor: {
      front: state.touchSensors.front,
      middle: state.touchSensors.middle,
      back: state.touchSensors.back,
    },
  });
});

// 2. GET & POST /api/runtime
app.get("/api/runtime", (req, res) => {
  res.json({
    robot_id: state.robotId,
    gemini_model: state.geminiModel,
    gemini_voice: state.geminiVoice,
    wifi_enabled: state.wifiEnabled,
    web_enabled: state.webEnabled,
    gemini_enabled: state.geminiEnabled,
    gateway_enabled: state.gatewayEnabled,
    gateway_base_url: state.gatewayBaseUrl,
    wifi_ssid: state.wifiSsid ? "set" : "missing",
    speaker_volume: state.speakerVolume,
    mic_magnification: state.micMagnification,
    mic_noise_filter_level: state.micNoiseFilterLevel,
    vad_prefix_padding_ms: state.vadPrefixPaddingMs,
    vad_silence_duration_ms: state.vadSilenceDurationMs,
    vad_start_sensitivity_high: state.vadStartSensitivityHigh,
    vad_end_sensitivity_low: state.vadEndSensitivityLow,
    vad_turn_includes_all_input: state.vadTurnIncludesAllInput,
    wifi_password: state.wifiPassword ? "set" : "missing",
    gemini_api_key: (state.geminiApiKey || process.env.GEMINI_API_KEY) ? "set" : "missing",
    gateway_token: state.gatewayToken ? "set" : "missing",
    secrets: "redacted",
    apply: "reboot_required",
  });
});

app.post("/api/runtime", (req, res) => {
  const b = req.body || {};
  if (b.robot_id !== undefined) state.robotId = String(b.robot_id);
  if (b.gemini_model !== undefined) {
    let m = String(b.gemini_model).replace(/^models\//, '').trim();
    if (!m || m.includes('2.0') || m.includes('1.5') || m.includes('2.5')) {
      m = 'gemini-3.5-flash-lite';
    }
    state.geminiModel = m;
  }
  if (b.gemini_voice !== undefined) state.geminiVoice = String(b.gemini_voice);
  if (b.gateway_base_url !== undefined) state.gatewayBaseUrl = String(b.gateway_base_url);
  if (b.speaker_volume !== undefined) state.speakerVolume = Number(b.speaker_volume);
  if (b.mic_magnification !== undefined) state.micMagnification = Number(b.mic_magnification);
  if (b.mic_noise_filter_level !== undefined) state.micNoiseFilterLevel = Number(b.mic_noise_filter_level);
  if (b.vad_prefix_padding_ms !== undefined) state.vadPrefixPaddingMs = Number(b.vad_prefix_padding_ms);
  if (b.vad_silence_duration_ms !== undefined) state.vadSilenceDurationMs = Number(b.vad_silence_duration_ms);
  if (b.vad_start_sensitivity_high !== undefined) state.vadStartSensitivityHigh = !!b.vad_start_sensitivity_high;
  if (b.vad_end_sensitivity_low !== undefined) state.vadEndSensitivityLow = !!b.vad_end_sensitivity_low;
  if (b.vad_turn_includes_all_input !== undefined) state.vadTurnIncludesAllInput = !!b.vad_turn_includes_all_input;
  if (b.wifi_enabled !== undefined) state.wifiEnabled = !!b.wifi_enabled;
  if (b.web_enabled !== undefined) state.webEnabled = !!b.web_enabled;
  if (b.gemini_enabled !== undefined) state.geminiEnabled = !!b.gemini_enabled;
  if (b.gateway_enabled !== undefined) state.gatewayEnabled = !!b.gateway_enabled;
  if (b.wifi_ssid) state.wifiSsid = String(b.wifi_ssid);

  res.json({
    ok: true,
    message: b.reboot ? "runtime_saved_rebooting" : "runtime_saved",
    reboot: !!b.reboot,
  });
});

// 3. GET /api/config
app.get("/api/config", (req, res) => {
  res.json({
    ok: true,
    robot_id: state.robotId,
    gemini_model: state.geminiModel,
    gemini_voice: state.geminiVoice,
    wifi_enabled: state.wifiEnabled,
    web_enabled: state.webEnabled,
    gemini_enabled: state.geminiEnabled,
    gateway_enabled: state.gatewayEnabled,
    gateway_base_url: state.gatewayBaseUrl,
    speaker_volume: state.speakerVolume,
    mic_magnification: state.micMagnification,
    mic_noise_filter_level: state.micNoiseFilterLevel,
  });
});

// 4. GET & POST /api/security
app.get("/api/security", (req, res) => {
  res.json({
    ok: true,
    web_auth_enabled: !!state.webPasswordHash,
    allow_unauthenticated_reads: true,
  });
});

app.post("/api/security", (req, res) => {
  const { current_password, new_password, disable_password } = req.body || {};
  if (disable_password) {
    state.webPasswordHash = null;
    return res.json({ ok: true, message: "Web authentication disabled" });
  }
  if (new_password) {
    state.webPasswordHash = crypto.createHash("sha256").update(new_password).digest("hex");
    return res.json({ ok: true, message: "Password updated successfully" });
  }
  res.status(400).json({ ok: false, error: "invalid_payload" });
});

// 5. GET & POST /api/secrets
app.get("/api/secrets", (req, res) => {
  res.json({
    ok: true,
    gemini_api_key_set: !!(state.geminiApiKey || process.env.GEMINI_API_KEY),
    gateway_token_set: !!state.gatewayToken,
    wifi_password_set: !!state.wifiPassword,
  });
});

app.post("/api/secrets", (req, res) => {
  const { gemini_api_key, gateway_token, wifi_password } = req.body || {};
  if (gemini_api_key !== undefined) {
    state.geminiApiKey = gemini_api_key ? String(gemini_api_key).trim() : null;
  }
  if (gateway_token !== undefined) {
    state.gatewayToken = gateway_token ? String(gateway_token).trim() : null;
  }
  if (wifi_password !== undefined) {
    state.wifiPassword = wifi_password ? String(wifi_password).trim() : null;
  }
  res.json({
    ok: true,
    message: "Secrets updated safely in firmware secure storage.",
  });
});

// 6. GET & POST /api/prompts
app.get("/api/prompts", (req, res) => {
  res.json({
    ok: true,
    system_prompt: state.systemPrompt,
    persona_prompt: state.personaPrompt,
  });
});

app.post("/api/prompts", (req, res) => {
  const { system_prompt, persona_prompt } = req.body || {};
  if (system_prompt !== undefined) state.systemPrompt = String(system_prompt);
  if (persona_prompt !== undefined) state.personaPrompt = String(persona_prompt);
  res.json({ ok: true, message: "Prompts saved successfully." });
});

// 7. MEMORY ENDPOINTS
app.get("/api/memory/status", (req, res) => {
  res.json({
    ok: true,
    session_id: memoryData.sessionId,
    active_after_ts: memoryData.activeAfterTs,
    events_count: memoryData.events.length,
    facts_count: memoryData.facts.length,
    summaries_count: memoryData.summaries.length,
    private_count: memoryData.privateMemories.length,
    compaction_recommended: memoryData.events.length > 25,
  });
});

app.get("/api/memory/events", (req, res) => {
  res.json({ ok: true, events: memoryData.events });
});

app.get("/api/memory/facts", (req, res) => {
  res.json({ ok: true, facts: memoryData.facts });
});

app.post("/api/memory/facts", (req, res) => {
  const { fact, tags } = req.body || {};
  if (!fact) return res.status(400).json({ ok: false, error: "fact_required" });

  const newFact: MemoryFact = {
    id: "fact_" + Date.now().toString(36),
    fact: String(fact),
    source: "web_manual_input",
    confidence: 1.0,
    observedDate: new Date().toISOString().split("T")[0],
    tags: Array.isArray(tags) ? tags : ["manual"],
  };
  memoryData.facts.push(newFact);
  res.json({ ok: true, fact: newFact });
});

app.delete("/api/memory/facts/:id", (req, res) => {
  memoryData.facts = memoryData.facts.filter((f) => f.id !== req.params.id);
  res.json({ ok: true, deleted: req.params.id });
});

app.get("/api/memory/summaries", (req, res) => {
  res.json({ ok: true, summaries: memoryData.summaries });
});

app.post("/api/memory/search", (req, res) => {
  const { query } = req.body || {};
  const result = executeRobotTool("search_memory", { query });
  res.json({ ok: true, ...result });
});

app.post("/api/memory/compact", async (req, res) => {
  const apiKey = state.geminiApiKey || process.env.GEMINI_API_KEY;
  let summaryText = "Automated conversation session recap: user discussed StackChan capabilities, movements, and memory indexing.";

  if (apiKey) {
    try {
      const ai = new GoogleGenAI({
        apiKey,
        httpOptions: {
          headers: {
            'User-Agent': 'aistudio-build',
          },
        },
      });
      const dialogueLines = memoryData.events.slice(-15).map((e) => `${e.role}: ${e.text}`).join("\n");
      const { response } = await callGeminiGenerate(
        ai,
        state.summaryModel || 'gemini-3.7-flash',
        `Summarize the following robot dialogue into 1 concise sentence and extract 1 key persistent fact:\n\n${dialogueLines}`
      );
      if (response?.text) {
        summaryText = response.text.trim();
      }
    } catch (e: any) {
      console.warn("Gemini summarize error:", e.message);
    }
  }

  const newSummary: MemorySummary = {
    id: "sum_" + Date.now().toString(36),
    ts: new Date().toISOString(),
    scope: "session_" + memoryData.sessionId,
    summary: summaryText,
    model: state.summaryModel,
  };
  memoryData.summaries.push(newSummary);

  res.json({
    ok: true,
    summary: newSummary,
    message: "Compaction successful. Active context rolled forward.",
  });
});

app.post("/api/memory/vectorize", (req, res) => {
  res.json({
    ok: true,
    message: "Vector index updated for all facts, dialogues, and summaries.",
  });
});

// 8. GATEWAY & TOOLS
app.get("/api/gateway/tools", (req, res) => {
  res.json({
    ok: true,
    gateway_enabled: state.gatewayEnabled,
    gateway_base_url: state.gatewayBaseUrl,
    tools: ROBOT_TOOLS,
  });
});

app.post("/api/gateway/call", (req, res) => {
  const { tool_name, arguments: args } = req.body || {};
  const result = executeRobotTool(tool_name, args);
  res.json({ ok: true, tool: tool_name, result });
});

// 9. EMOTION & SERVO
app.all("/api/emotion", (req, res) => {
  if (req.method === "POST" || req.query.emotion) {
    const emo = (req.body?.emotion || req.query.emotion || "").toLowerCase();
    const valid = ["neutral", "happy", "listening", "speaking", "thinking", "looking", "angry", "found", "error", "sleep"];
    if (valid.includes(emo)) {
      state.emotion = emo;
    }
  }
  res.json({ ok: true, emotion: state.emotion });
});

app.get("/api/servo/status", (req, res) => {
  res.json({
    ok: true,
    x: state.servo.x,
    y: state.servo.y,
    x_deg: state.servo.x / 10,
    y_deg: state.servo.y / 10,
    speed: state.servo.speed,
    active: state.servo.active,
    current_gesture: state.servo.currentGesture,
    anchor_x: state.servo.anchorX,
    anchor_y: state.servo.anchorY,
  });
});

app.post("/api/servo/move", (req, res) => {
  const { x, y, speed } = req.body || {};
  if (x !== undefined) state.servo.x = Math.max(-1280, Math.min(1280, Number(x)));
  if (y !== undefined) state.servo.y = Math.max(0, Math.min(900, Number(y)));
  if (speed !== undefined) state.servo.speed = Math.max(0, Math.min(1000, Number(speed)));
  state.servo.anchorX = state.servo.x;
  state.servo.anchorY = state.servo.y;
  state.servo.active = true;
  setTimeout(() => {
    state.servo.active = false;
  }, 400);

  res.json({
    ok: true,
    x: state.servo.x,
    y: state.servo.y,
    x_deg: state.servo.x / 10,
    y_deg: state.servo.y / 10,
    speed: state.servo.speed,
  });
});

app.all("/api/servo/gesture", (req, res) => {
  const gestureName = (req.method === "POST" ? req.body?.gesture || req.body?.name : req.query.name) || "center_head";
  const result = executeRobotTool("servo_gesture", { gesture: gestureName });
  res.json({ ok: true, result });
});

// 10. VOICE TOGGLE & GEMINI TEXT/VOICE
app.all("/api/voice/toggle", (req, res) => {
  state.voiceListening = !state.voiceListening;
  if (state.voiceListening) {
    state.emotion = "listening";
  } else {
    state.emotion = "neutral";
    state.speaking = false;
  }
  res.json({
    ok: true,
    voice_listening: state.voiceListening,
    emotion: state.emotion,
  });
});

app.all("/api/gemini/text", async (req, res) => {
  const text = (req.method === "POST" ? req.body?.text : req.query.text) || "";
  if (!text) {
    return res.status(400).json({ ok: false, error: "text_required" });
  }

  // Record user dialogue in memory
  const userEvt: MemoryEvent = {
    id: "evt_" + Date.now().toString(36),
    ts: new Date().toISOString(),
    session: memoryData.sessionId,
    role: "user",
    text,
    source: "web_input",
  };
  memoryData.events.push(userEvt);

  const apiKey = state.geminiApiKey || process.env.GEMINI_API_KEY;
  let replyText = "";
  let triggeredEmotion = "happy";
  let triggeredGesture = "nod_yes";
  let executedTools: any[] = [];

  state.emotion = "thinking";

  if (!apiKey) {
    // Resposta amigável e conversacional caso a chave externa não esteja configurada
    const localResult = generateContextualLocalReply(text, state);
    replyText = localResult.replyText;
    triggeredEmotion = localResult.emotion;
    triggeredGesture = localResult.gesture;
  } else {
    try {
      const ai = new GoogleGenAI({
        apiKey,
        httpOptions: {
          headers: {
            'User-Agent': 'aistudio-build',
          },
        },
      });

      const tempStr = (state.envSensor?.temperature ?? 26.5).toFixed(1);
      const humStr = (state.envSensor?.humidity ?? 55.0).toFixed(0);
      const pressStr = (state.envSensor?.pressure ?? 1013.2).toFixed(1);
      const tofMm = state.tofDistanceMm ?? 380;
      const presenceStr = state.presenceDetected ? `Sim (distância laser: ${tofMm}mm / ~${Math.round(tofMm / 10)}cm)` : "Ninguém detectado perto da mesa";

        const auxiliaryContext = `[DADOS DE AMBIENTE - NUNCA USE POR VONTADE PRÓPRIA, APENAS SE PERGUNTADO]:
- Notícias:
${cachedNews.map((n, i) => `  * ${n}`).join('\n')}

- Diretrizes: Você é a Pix, uma amiga robô. O seu foco é o diálogo. NUNCA fale sobre temperatura ou clima por vontade própria. Se o usuário perguntar diretamente, você PODE responder, mas seja breve e natural.`;

      const memoryContext = memoryData.facts.map((f) => f.fact).join("; ");
      
      // Get the last 5 events to provide short-term memory
      const recentHistory = memoryData.events.slice(-6).map(e => `${e.role === 'user' ? 'Usuário' : 'StackChan'}: ${e.text}`).join('\n');
      
      if (Date.now() < state.snoozeUntil) {
        return { response: "...", emotion: "sleep", gesture: "idle" };
      }
      
      const systemInstruction = `${state.systemPrompt}\n${state.personaPrompt}\nREGRA SUPREMA E OBRIGATÓRIA DE IDIOMA: Responda SEMPRE em Português do Brasil (pt-BR). Sob nenhuma hipótese responda em inglês ou outro idioma, mesmo que a mensagem do usuário contenha palavras em inglês ou que o usuário esteja em outro país.\nActive Known Facts: ${memoryContext}`;

      const fullUserContent = `[HISTÓRICO RECENTE DA CONVERSA]:
${recentHistory}

${auxiliaryContext}

Mensagem atual do Usuário: "${text}"`;

      const { response, modelUsed } = await callGeminiGenerate(
        ai,
        state.geminiModel || 'gemini-3.1-flash-lite',
        fullUserContent,
        systemInstruction,
        false
      );

      // Keep model state synced if changed
      if (modelUsed && state.geminiModel !== modelUsed) {
        state.geminiModel = modelUsed;
      }

      replyText = response.text || "Beep boop!";
      triggeredEmotion = replyText.includes("?") ? "thinking" : "speaking";
      triggeredGesture = "speaking_micro_motion";
      executeRobotTool("servo_gesture", { gesture: triggeredGesture });
    } catch (err: any) {
      const errMsg = err?.message || "";
      const isQuotaOrDemand = errMsg.includes("429") || errMsg.includes("quota") || errMsg.includes("RESOURCE_EXHAUSTED") || errMsg.includes("503") || errMsg.includes("UNAVAILABLE");

      if (!isQuotaOrDemand) {
        console.error("Gemini call error:", err);
      }
      
      // If 429 quota exhaustion or 503 high demand, use intelligent contextual local fallback
      if (isQuotaOrDemand) {
        const localResult = generateContextualLocalReply(text, state);
        replyText = localResult.replyText;
        triggeredEmotion = localResult.emotion;
        triggeredGesture = localResult.gesture;
      } else {
        replyText = `Opa! Tive uma oscilação na conexão com a nuvem (${errMsg.slice(0, 60)}). Tô online localmente!`;
        triggeredEmotion = "neutral";
        triggeredGesture = "center_head";
      }
    }
  }

  state.emotion = triggeredEmotion;
  state.speaking = true;
  setTimeout(() => {
    state.speaking = false;
  }, 2000);

  // Record assistant reply in memory
  const assistantEvt: MemoryEvent = {
    id: "evt_" + (Date.now() + 1).toString(36),
    ts: new Date().toISOString(),
    session: memoryData.sessionId,
    role: "assistant",
    text: replyText,
    source: "gemini_response",
  };
  memoryData.events.push(assistantEvt);

  res.json({
    ok: true,
    user_text: text,
    response: replyText,
    emotion: state.emotion,
    gesture: triggeredGesture,
    executed_tools: executedTools,
  });
});

app.all("/api/gemini/look", (req, res) => {
  state.emotion = "looking";
  executeRobotTool("servo_gesture", { gesture: "search_center" });
  setTimeout(() => {
    state.emotion = "neutral";
  }, 1500);
  res.json({
    ok: true,
    message: "Visual inspection frame processed.",
    image_status: "ready",
  });
});

// 11. CAMERA ENDPOINTS
app.get("/api/camera/status", (req, res) => {
  res.json({
    ok: true,
    enabled: true,
    ready: true,
    resolution: "640x480",
    framerate: 15,
    last_error: "",
    last_jpeg_bytes: 19540,
  });
});

app.all("/api/camera/capture", (req, res) => {
  res.json({
    ok: true,
    captured: true,
    resolution: "640x480",
    jpeg_bytes: 19540,
    base64_bytes: 26050,
    ts: new Date().toISOString(),
  });
});

app.get(["/api/camera/jpeg", "/api/camera/latest.jpg"], (req, res) => {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="640" height="480" viewBox="0 0 640 480">
    <defs>
      <linearGradient id="bg" x1="0%" y1="0%" x2="100%" y2="100%">
        <stop offset="0%" stop-color="#0f172a"/>
        <stop offset="100%" stop-color="#1e293b"/>
      </linearGradient>
      <pattern id="grid" width="40" height="40" patternUnits="userSpaceOnUse">
        <path d="M 40 0 L 0 0 0 40" fill="none" stroke="#334155" stroke-width="1"/>
      </pattern>
    </defs>
    <rect width="640" height="480" fill="url(#bg)"/>
    <rect width="640" height="480" fill="url(#grid)"/>
    <circle cx="320" cy="240" r="120" fill="none" stroke="#f59e0b" stroke-width="2" stroke-dasharray="8 4"/>
    <circle cx="320" cy="240" r="40" fill="none" stroke="#38bdf8" stroke-width="2"/>
    <line x1="160" y1="240" x2="480" y2="240" stroke="#f59e0b" stroke-width="1.5"/>
    <line x1="320" y1="80" x2="320" y2="400" stroke="#f59e0b" stroke-width="1.5"/>
    <rect x="20" y="20" width="280" height="70" rx="8" fill="#00000088"/>
    <text x="35" y="45" fill="#f59e0b" font-family="monospace" font-size="14" font-weight="bold">M5STACK CORE-S3 CAMERA</text>
    <text x="35" y="65" fill="#94a3b8" font-family="monospace" font-size="11">RES: 640x480 | STATUS: OK | FPS: 15</text>
    <text x="35" y="80" fill="#38bdf8" font-family="monospace" font-size="11">TS: ${new Date().toISOString()}</text>
  </svg>`;

  res.setHeader("Content-Type", "image/svg+xml");
  res.send(svg);
});

// 12. SENSOR DIAGNOSTICS & HARDWARE EMULATION
app.get("/api/sensors", (req, res) => {
  const now = Date.now();
  const alsLux = 140 + Math.floor(Math.sin(now / 5000) * 30);
  const psRaw = 18 + Math.floor(Math.cos(now / 4000) * 6);

  // Update dynamic values in state
  const tofMm = state.tofDistanceMm || 380;
  const tempC = state.envSensor?.temperature || 26.5;
  const humPct = state.envSensor?.humidity || 52.0;
  const pressHpa = state.envSensor?.pressure || 1013.2;

  res.json({
    ok: true,
    ltr553: {
      configured: true,
      als_lux: alsLux,
      ps_distance_raw: psRaw,
      proximity_detected: psRaw > 20,
      last_error: "",
    },
    pahub: {
      enabled: true,
      i2c_addr: "0x70",
      channels: {
        ch0_tof90: {
          connected: true,
          distance_mm: tofMm,
          status: "OK (VL53L1X / TOF-90)",
          presence_detected: state.presenceDetected,
        },
        ch1_env: {
          connected: true,
          temp_c: tempC,
          humidity_pct: humPct,
          pressure_hpa: pressHpa,
        },
        ch2_vibrator: {
          connected: true,
          pin: 8,
          active: state.speaking || state.emotion === "angry",
          pwm_duty: state.emotion === "angry" ? 255 : (state.speaking ? 120 : 0),
        },
        ch3_aux: {
          connected: false,
          status: "Disponível (Livre)",
        },
      },
    },
    vibrator: {
      connected: true,
      pin: 8,
      active: state.speaking || state.emotion === "angry",
      pwm_duty: state.emotion === "angry" ? 255 : (state.speaking ? 120 : 0),
    },
    audio_direction: {
      enabled: true,
      sample_rate: 16000,
      stereo_request: true,
      duration_ms: 128,
      left_rms: 68.4 + Math.random() * 5,
      right_rms: 71.2 + Math.random() * 5,
      left_peak: 142,
      right_peak: 148,
      balance_r_minus_l: state.stereoDiag.balance,
      tdoa_lag_samples: state.stereoDiag.lag,
      tdoa_us: state.stereoDiag.tdoaUs,
      corr: state.stereoDiag.corr,
      direction_hint: state.stereoDiag.direction,
    },
    power: {
      battery_mv: state.batteryMv,
      battery_ma: state.batteryMa,
      battery_level_pct: Math.min(100, Math.max(10, Math.round(((state.batteryMv - 3400) / (4200 - 3400)) * 100))),
      external_power: true,
    },
    touch: {
      front: state.touchSensors.front,
      middle: state.touchSensors.middle,
      back: state.touchSensors.back,
      pressed: {
        front: state.touchSensors.front > 0,
        middle: state.touchSensors.middle > 0,
        back: state.touchSensors.back > 0,
      },
    },
  });
});

app.post("/api/sensors/touch", (req, res) => {
  const { front, middle, back } = req.body || {};
  if (front !== undefined) state.touchSensors.front = Number(front);
  if (middle !== undefined) state.touchSensors.middle = Number(middle);
  if (back !== undefined) state.touchSensors.back = Number(back);

  if (front > 0 || middle > 0 || back > 0) {
    if (state.emotion === "sleep") {
      state.emotion = "neutral";
    }
  }

  res.json({ ok: true, touch_sensor: state.touchSensors });
});

// Unit TOF 90 Distance & Presence Simulator Endpoint
app.post("/api/sensors/tof", (req, res) => {
  const { distance_mm, presence } = req.body || {};
  if (distance_mm !== undefined) {
    state.tofDistanceMm = Math.max(20, Math.min(2000, Number(distance_mm)));
  }
  if (presence !== undefined) {
    state.presenceDetected = !!presence;
  } else if (distance_mm !== undefined) {
    state.presenceDetected = state.tofDistanceMm < 900;
  }
  res.json({
    ok: true,
    tof_distance_mm: state.tofDistanceMm,
    presence_detected: state.presenceDetected,
  });
});

// Unit ENV (Temperature & Humidity) Simulator Endpoint
app.post("/api/sensors/env", (req, res) => {
  const { temperature, humidity, pressure } = req.body || {};
  if (temperature !== undefined) state.envSensor.temperature = Number(temperature);
  if (humidity !== undefined) state.envSensor.humidity = Number(humidity);
  if (pressure !== undefined) state.envSensor.pressure = Number(pressure);
  res.json({
    ok: true,
    env: state.envSensor,
  });
});

// Physical IMU / Shake / Lift Interaction Endpoints
app.post("/api/sensors/shake", (req, res) => {
  state.emotion = "angry";
  executeRobotTool("servo_gesture", { gesture: "angry_tantrum" });
  res.json({
    ok: true,
    message: "Shake detected! Robot is now irritated/angry.",
    emotion: state.emotion,
    gesture: "angry_tantrum",
  });
});

app.post("/api/sensors/lift", (req, res) => {
  const { lifted } = req.body || {};
  if (lifted) {
    state.emotion = "angry";
    executeRobotTool("servo_gesture", { gesture: "lifted_protest" });
  } else {
    state.emotion = "neutral";
    executeRobotTool("servo_gesture", { gesture: "center_head" });
  }
  res.json({
    ok: true,
    lifted: !!lifted,
    emotion: state.emotion,
  });
});

// ----------------------------------------------------
// VITE MIDDLEWARE & SERVER STARTUP
// ----------------------------------------------------
async function startServer() {
  if (process.env.NODE_ENV !== "production") {
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: "spa",
    });
    app.use(vite.middlewares);
  } else {
    const distPath = path.join(process.cwd(), "dist");
    app.use(express.static(distPath));
    app.get("*", (req, res) => {
      res.sendFile(path.join(distPath, "index.html"));
    });
  }

  app.listen(PORT, "0.0.0.0", () => {
    console.log(`StackChan Gemini Server running on http://0.0.0.0:${PORT}`);
  });
}

startServer();
