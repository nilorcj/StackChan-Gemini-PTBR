# M5Stack Stack-chan PT-BR Units (Gemini Live)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange.svg)](https://platformio.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![M5Stack CoreS3](https://img.shields.io/badge/Hardware-M5Stack%20CoreS3-blue.svg)](https://m5stack.com/)
[![Gemini Live](https://img.shields.io/badge/AI-Google%20Gemini%20Live-4285F4.svg)](https://ai.google.dev/)

Firmware avançado para o robô **M5Stack CoreS3 Stack-chan** com conversação por voz em tempo real (**Google Gemini Live API**) em **Português do Brasil (PT-BR)**, integração com o ecossistema de **M5Stack Units** via multiplexador I2C **PaHub**, e sistema inteligente de conversação armada por gestos e toque (**Gesture-Armed Listening**).

---

## 🌟 Origem e Reconhecimento

Este projeto foi desenvolvido com base no firmware experimental inicial de [taranton/stackchan-gemini-firmware](https://github.com/taranton/stackchan-gemini-firmware), que estabeleceu a conexão inicial de voz com Gemini Live no ESP32-S3.

A partir dessa base, este repositório traz uma reformulação arquitetural profunda: suporte a periféricos modulares externos (Units M5Stack), eliminação de falsos disparos de voz com detecção de gestos ToF, localização completa em PT-BR e extensivas otimizações de performance no ESP32-S3.

---

## 🚀 O que há de novo nesta versão

### 1. Sistema de Conversação Armada por Gesto e Toque (*Gesture-Armed Listening*)
Em assistentes de voz convencionais, ruídos de fundo (como televisores, rádios ou conversas paralelas) causam interrupções indesejadas e custos desnecessários de API. Este firmware introduz uma máquina de estados segura:
* **Modo DISARMED (Padrão):** O microfone permanece fechado. Nenhum dado de áudio ou PCM é transmitido ao Gemini.
* **Presença Passiva Inteligente:** A presença de pessoas detectada pelo sensor ToF ou PIR acorda o robô e ativa expressões faciais no display, mas **nunca** abre o microfone espontaneamente.
* **Armação Consciente:** A escuta é armada exclusivamente por:
  * **Aceno de mão (`FAST_GESTURE` / `HAND_WAVE`):** Gesto rápido e intencional em frente ao sensor Laser ToF.
  * **Toque na Tela (`TOUCH`):** Toque na tela capacitiva do CoreS3.
* **Conversação Contínua:** Uma vez armada, a conversa flui naturalmente turno a turno sem exigir novos gestos a cada fala.
* **Desarmamento por Timeout:** Se houver 15 segundos contínuos de silêncio/inatividade em modo de escuta, o robô encerra o turno e retorna automaticamente ao estado `DISARMED`.

### 2. Suporte ao Ecossistema M5Stack Units via PaHub (I2C Multiplexer)
Expansão da Porta A I2C do CoreS3 através do **Unit PaHub** (PCA9548AP / TCA9548A) para permitir múltiplos módulos simultâneos:
* **Canal 0 — Unit ToF (VL53L0X / VL53L1X):** Medição de distância a laser de alta precisão, responsável pelo rastreamento de presença (entrada a <850mm, saída a >1000mm) e detecção de gestos cinemáticos (`HAND_WAVE`) com filtro anti-outliers.
* **Canal 1 — Unit ENV (SHT30 / QMP6988):** Monitoramento ambiental em tempo real (temperatura, umidade relativa e pressão atmosférica).
* **Feedback Tátil (Unit Vibrator):** Motor háptico configurável com múltiplos padrões emocionais (`Purr`, `CatPurr`, `Alert`, `Angry`, `Click`, `Shake`, etc.).

### 3. Otimizações de Performance e Estabilidade do ESP32-S3
* **Audio I2S DMA Coalescing & Ring Buffers:** Transmissão de pacotes PCM 16 kHz otimizada em buffers de anel para evitar gargalos de rede, ruídos de buffer underrun e estalos.
* **Renderização Facial com Dirty Flags:** O display redesenha apenas as áreas dos olhos e boca que sofreram alteração geométrica, liberando dezenas de milissegundos de CPU para a criptografia TLS/WebSocket do Gemini Live.
* **Suavização Cinemática dos Servos (Pan/Tilt):** Movimentação com interpolação suave que elimina ruídos de alta frequência (jitter) e evita quedas de tensão na alimentação do CoreS3.

### 4. Localização Nativa em Português do Brasil (PT-BR)
* Prompts de sistema, persona do Stack-chan e ferramentas ajustados nativamente para expressividade, clareza e sotaque brasileiro ao utilizar as vozes de baixa latência do Gemini Live.

---

## 🛠️ Hardware Necessário

* **Controlador:** [M5Stack CoreS3](https://docs.m5stack.com/en/core/CoreS3) (ESP32-S3).
* **Base Stack-chan:** Base motorizada com servos seriais (ex.: SCS0009 / Feetech ou servos PWM compatíveis).
* **Armazenamento:** Cartão MicroSD (formatado em FAT32).
* **Módulos I2C / Porta A:**
  * [M5Stack Unit PaHub](https://docs.m5stack.com/en/unit/pahub) (Multiplexador I2C).
  * [M5Stack Unit ToF / ToF4M](https://docs.m5stack.com/en/unit/tof) (Conectado ao Canal 0 do PaHub).
  * *(Opcional)* [M5Stack Unit ENV III/IV](https://docs.m5stack.com/en/unit/envIII) (Conectado ao Canal 1 do PaHub).
* **Módulos Porta B:**
  * *(Opcional)* [M5Stack Unit Vibrator](https://docs.m5stack.com/en/unit/vibrator) (Conectado à Porta B, GPIO 9).

---

## 📦 Como Instalar e Rodar

### 1. Clonar o repositório
```bash
git clone https://github.com/nilorcj/M5-Stackchan-PT-BR-Units.git
cd M5-Stackchan-PT-BR-Units
```

### 2. Configurar o Cartão MicroSD
Crie a estrutura de diretórios no cartão SD (veja detalhes em [docs/SD_LAYOUT.md](docs/SD_LAYOUT.md)):
```text
/app/StackChan/
├── config/
│   └── runtime.json              # Copie de examples/runtime.example.json
├── prompts/
│   ├── system.txt                # Prompt de comportamento do assistente
│   └── persona.txt               # Personalidade e tom de voz
└── secrets/
    ├── gemini_api_key.txt        # Sua chave da Google Gemini API
    └── wifi_password.txt         # Senha da sua rede Wi-Fi (opcional se configurado no JSON)
```

### 3. Compilar e Gravar o Firmware
Certifique-se de ter o [PlatformIO Core](https://platformio.org/) instalado:
```bash
# Compilar o firmware
pio run -e m5stack-cores3

# Gravar no M5Stack CoreS3 via USB
pio run -e m5stack-cores3 -t upload

# Acompanhar o log serial
pio device monitor -b 115200
```

---

## 🌐 Painel Web e Acesso Inicial

Se o Wi-Fi ainda não estiver configurado no SD, o robô criará automaticamente uma rede de configuração:
* **SSID:** `<robot_id>-setup-XXXX`
* **Senha:** `stackchanXXXX`
* **Endereço do Painel:** `http://192.168.4.1/`

Após conectar à sua rede Wi-Fi local, acesse diretamente o endereço IP atribuído ao Stack-chan para:
* Visualizar diagnósticos dos sensores (ToF, ENV, I2C PaHub).
* Testar movimentos dos servos e feedback tátil.
* Acompanhar transcrições de voz e memória de eventos.
* Configurar senhas e credenciais com segurança HTTP Basic Auth.

---

## 📚 Documentação Adicional

* [Instruções de Compilação e Gravação](docs/BUILD_FLASH.md)
* [Estrutura do Cartão SD](docs/SD_LAYOUT.md)
* [Segurança e Configuração Web](docs/WEB_SECURITY.md)
* [Endpoints e Referência da API HTTP](docs/API.md)
* [Matriz de Recursos do Firmware](docs/FEATURES.md)
* [Exemplos de Prompts PT-BR](docs/PROMPTS.md)
* [Arquitetura de Memória Interna](docs/MEMORY_INTERNALS.md)

---

## 📄 Licença

Este projeto é distribuído sob a licença **MIT**. Consulte o arquivo [LICENSE](LICENSE) para obter mais informações.
