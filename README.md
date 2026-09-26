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

## 🌐 Configuração de Wi-Fi e Acesso ao Web Viewer

Esta seção explica passo a passo como conectar a Pix à sua rede Wi-Fi, como funciona a rede de emergência quando ela não encontra o roteador e como acessar o painel de controle (Web Viewer).

### 1. Comportamento e Modos de Operação

A Pix opera em dois modos distintos de rede:

* **Modo Normal (Conectada ao Roteador Doméstico / 2.4 GHz):**
  * Quando a Pix encontra o Wi-Fi configurado, ela se conecta automaticamente durante os 15 segundos da tela de boot e recebe um endereço IP local dinâmico via DHCP (ex.: `192.168.1.105` ou `10.0.0.45`).
  * O Gemini Live, os sensores e a sincronização de data/hora/agenda tornam-se plenamente operacionais.
  * O Web Viewer fica acessível diretamente pelo endereço IP local do robô na rede da sua casa (ex.: `http://192.168.1.105/`).
* **Modo Ponto de Acesso de Configuração (Fallback / Emergência):**
  * Se o Wi-Fi configurado não for encontrado após a tela de boot (15 segundos) ou se for a primeira vez que você liga a Pix sem configurar o SD, ela entra automaticamente em modo **SoftAP**.
  * A Pix cria uma rede Wi-Fi própria temporária para que você possa se conectar via celular ou notebook e configurar a rede da sua casa.
  * Neste modo, não há conexão com a internet (o Gemini Live permanece offline até que uma rede válida seja configurada e a Pix seja reiniciada).

---

### 2. Rede de Configuração (Modo AP) — Dados de Conexão

Quando a Pix estiver no modo de configuração, conecte seu smartphone ou computador à rede Wi-Fi gerada por ela:

* **Nome da Rede (SSID):** `<robot_id>-setup-XXXX` (padrão: `stackchan-setup-XXXX` ou `pix-setup-XXXX`).
  > **O que é o `XXXX`?** São os últimos 4 caracteres hexadecimais do endereço físico (MAC) do chip ESP32-S3 da sua placa (ex.: `stackchan-setup-4190`).
* **Senha do Wi-Fi:** `stackchanXXXX`
  > **Como saber a senha correta:** A senha é sempre a palavra `stackchan` em letras minúsculas seguida **exatamente pelos mesmos 4 caracteres que aparecem no final do nome da rede**.
  > *Exemplo prático:* se o seu celular encontrar a rede `stackchan-setup-4190`, a senha dessa rede será `stackchan4190`.
* **Endereço IP do AP:** `192.168.4.1` (IP padrão fixo da Pix).
* **Porta:** `80` (porta HTTP padrão).
* **Como acessar:** Conecte o celular ou computador à rede da Pix e abra o navegador no endereço:
  ```text
  http://192.168.4.1/
  ```

---

### 3. Como Descobrir o IP da Pix na Rede Doméstica

A Pix **não utiliza mDNS nem nome de domínio** (não há suporte a `pix.local` ou `stackchan.local`); o acesso é realizado **sempre pelo endereço IP numérico**.

Para descobrir o IP da Pix quando ela estiver conectada ao seu roteador:
1. **Diretamente na tela da Pix (Dashboard de Boot):**
   * Ao ligar a Pix, a tela inicial de diagnóstico é exibida por 15 segundos.
   * No card superior esquerdo (**Wi-Fi**), quando a conexão for estabelecida, aparecerá o status verde **`OK`** e logo abaixo o **endereço IP atribuído** (ex.: `192.168.1.105`).
2. **Pelo Monitor Serial USB:**
   * Se a Pix estiver conectada ao computador via cabo USB a 115200 baud, o log de inicialização exibirá:
     ```text
     WiFi: connected during boot screen! ip=192.168.X.X dns=...
     ```
3. **Pela lista de clientes do seu roteador:**
   * Acesse o painel do seu roteador e procure pelo dispositivo ESP32-S3 (prefixo de MAC iniciado por `80:45:6B:...` ou similar).

---

### 4. Como Acessar o Web Viewer e o que ele oferece

Com a Pix conectada ao seu Wi-Fi e seu computador/smartphone no mesmo roteador:
* **Painel Principal:** `http://<IP_DA_PIX>/` (ex.: `http://192.168.1.105/`)
* **Página de Diagnóstico de Sensores:** `http://<IP_DA_PIX>/sensors`

O Web Viewer oferece controle total através de abas:
* **Status:** Telemetria em tempo real de bateria, memória livre (Heap e PSRAM), estado do Gemini Live (pronto, ouvindo, falando), chunks de áudio e status dos microfones.
* **Runtime:** Configuração do volume do alto-falante, ganho do microfone (1 a 24), filtro de ruído, modelo de IA (`geminiModel`), voz sintetizada (`geminiVoice`), fuso horário (`timezoneOffset`) e nome da rede Wi-Fi (`Wi-Fi SSID`).
* **Secrets:** Armazenamento seguro de chaves e senhas (senha do Wi-Fi, Gemini API Key e URL da agenda do Google Calendar `.ics`). Os segredos são gravados no MicroSD e **nunca são exibidos de volta pelo navegador ou API**.
* **Security:** Configuração opcional de senha para proteger o painel web via HTTP Basic Auth (usuário padrão: `stackchan`). Se nenhuma senha for cadastrada, o painel permanece aberto na rede local.
* **Gateway:** Teste e validação de ferramentas externas conectadas via Tool Gateway.
* **Prompts:** Adição de instruções de comportamento e personalidade adicionais à Pix.
* **Memory:** Visualização de diálogos recentes, resumos periódicos e busca semântica no banco de memória local.

---

### Troca rápida de Wi-Fi

Se você levou a Pix para outra casa, escritório ou trocou a senha/roteador Wi-Fi:

#### Método 1 — Pela Rede de Configuração da Pix (Sem computador / Sem retirar o MicroSD)
1. Ligue a Pix no novo local.
2. Como ela não encontrará a rede antiga, após os 15 segundos da tela de boot ela entrará em modo AP e criará a rede `stackchan-setup-XXXX`.
3. No celular ou notebook, conecte-se a essa rede Wi-Fi usando a senha `stackchanXXXX` (onde `XXXX` são os 4 dígitos finais do nome da rede).
4. Abra o navegador em `http://192.168.4.1/`.
5. Vá até a aba **Runtime**, localize o campo **Wi-Fi SSID** e digite o nome exato da nova rede Wi-Fi (compatível com 2.4 GHz).
6. Vá até a aba **Secrets**, localize o campo **Wi-Fi password** e digite a nova senha da rede. Clique em **Save secrets**.
7. Volte à aba **Runtime** e clique no botão vermelho **Save and reboot** (ou reinicie a Pix no botão lateral de energia).
8. A Pix reiniciará conectando-se ao novo roteador e exibirá o novo IP na tela de boot!

#### Método 2 — Pelo Cartão MicroSD (Offline no PC)
1. Desligue a Pix e retire o cartão MicroSD.
2. Conecte o cartão ao computador:
   * Abra o arquivo `/app/StackChan/config/runtime.json` e altere o campo `"wifi_ssid": "NomeDaNovaRede"`.
   * Abra o arquivo `/app/StackChan/secrets/wifi_password.txt` e substitua o texto pela nova senha do Wi-Fi (sem espaços ou quebras de linha adicionais).
3. Insira o MicroSD de volta na Pix e ligue o robô.

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
