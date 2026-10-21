# M5Stack Stack-chan PT-BR Units (Gemini Live)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange.svg)](https://platformio.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![M5Stack CoreS3](https://img.shields.io/badge/Hardware-M5Stack%20CoreS3-blue.svg)](https://m5stack.com/)
[![Gemini Live](https://img.shields.io/badge/AI-Google%20Gemini%20Live-4285F4.svg)](https://ai.google.dev/)

Firmware avançado para o robô **M5Stack CoreS3 Stack-chan** com conversação por voz em tempo real (**Google Gemini Live API**) em **Português do Brasil (PT-BR)**, integração com o ecossistema de **M5Stack Units** via multiplexador I2C **PaHub**, sistema inteligente de conversação armada por gestos e toque (**Gesture-Armed Listening**) e integração com ferramentas externas via **ToolGateway assinado com HMAC-SHA256**.

---

## 🌟 Origem e Reconhecimento

Este projeto foi desenvolvido com base no firmware experimental inicial de [taranton/stackchan-gemini-firmware](https://github.com/taranton/stackchan-gemini-firmware), que estabeleceu a conexão inicial de voz com Gemini Live no ESP32-S3.

A partir dessa base, este repositório traz uma reformulação arquitetural profunda: suporte a periféricos modulares externos (Units M5Stack), eliminação de falsos disparos de voz com detecção de gestos ToF, localização completa em PT-BR, cache local de feeds e integração externa enxuta via ToolGateway no ESP32-S3.

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

## 🔌 ToolGateway, Busca Web e Agenda

A arquitetura atual separa claramente as **ferramentas locais** (que rodam diretamente na Pix sem bloqueio de rede durante a fala) das **ferramentas externas** (que são delegadas ao ToolGateway remoto em Cloudflare Worker).

* **Local na Pix:**
  * Sensores físicos (`get_sensor_readings`) e telemetria do robô (`get_robot_status`);
  * Data e hora local oficial sincronizada via NTP/RTC (`get_current_datetime`);
  * Clima de Brasília via cache local (`get_weather`);
  * Cotação USD/BRL via cache local (`get_currency`);
  * Manchetes de notícias via cache RSS local (`get_news`); a atualização explícita dos feeds (`refresh_news_feeds`) acessa a internet para renovar esse cache;
  * Memória persistente e privada no microSD (`search_memory`, `remember_fact`, `remember_private_memory`, `recall_private_memory`);
  * Comandos físicos e de sessão (`servo_gesture`, `head_motion`, `set_emotion`, `set_speaker_volume`, `look_with_camera`, `end_session`).
* **Externo via ToolGateway:**
  * Busca web atualizada (`web_search`);
  * Agenda do Google Calendar (`get_daily_agenda` → `get_agenda`).

**Fluxo de busca web:**
```text
Pix → ToolGatewayClient → POST /v2/call → Cloudflare Worker → TinyFish → resposta para o Gemini Live
```

**Fluxo da agenda:**
```text
Pix → get_daily_agenda → GeminiToolBridge → mapeamento interno para get_agenda → POST /v2/call → Cloudflare Worker → Google Calendar ICS → resposta estruturada para o Gemini
```
> **Nota de compatibilidade:** O modelo Gemini Live continua vendo e chamando a função declarada `get_daily_agenda`, mas o firmware (`GeminiToolBridge`) roteia internamente a chamada para a tool `get_agenda` no ToolGateway.

### Autenticação do ToolGateway (HMAC-SHA256)

O endpoint produtivo utilizado pelo firmware é:
```text
POST /v2/call
```

Cada requisição enviada pela Pix é assinada com **HMAC-SHA256** sobre uma string canônica composta por:
* método HTTP (`POST`);
* path (`/v2/call`);
* timestamp Unix atual em segundos;
* nonce aleatório de 32 caracteres hexadecimais;
* body JSON exato da requisição.

A requisição inclui os cabeçalhos HTTP de autenticação:
* `X-Timestamp`
* `X-Nonce`
* `X-Signature`

A chave compartilhada fica armazenada no cartão microSD da Pix em:
```text
/app/StackChan/secrets/gateway_hmac_key.txt
```
* Esse arquivo deve conter exatamente uma chave de **64 caracteres hexadecimais**.
* O mesmo segredo deve estar configurado no ambiente do Cloudflare Worker como `HMAC_SHARED_KEY`.
* **Importante sobre segurança de transporte:** O HMAC autentica a requisição e protege sua integridade. O timestamp e o nonce ajudam a mitigar requisições antigas ou repetidas, mas o HMAC por si só não criptografa o conteúdo transportado quando o gateway é acessado por HTTP nem elimina completamente ataques de replay dentro da janela de validade aceita pelo servidor.
* **Legado:** O arquivo `/app/StackChan/secrets/gateway_token.txt` (Bearer token) é legado e **não é usado** pelo caminho produtivo atual de `POST /v2/call`.

### Busca Web Atualizada

* O Gemini Live pode acionar a ferramenta `web_search` quando precisa consultar informações externas atualizadas ou quando solicitado pelo usuário.
* O firmware envia a consulta para o ToolGateway via `POST /v2/call`.
* O Cloudflare Worker executa a pesquisa utilizando a API **TinyFish** e devolve resultados estruturados contendo **título, URL, data, site e snippet** (atualmente até **5 resultados** por consulta).
* Esse fluxo via ToolGateway substitui a dependência do Google Search Grounding como caminho produtivo principal de `web_search` na Pix (o Grounding nativo foi removido do setup da Live API, operando a busca exclusivamente via gateway).

### Compatibilidade com Gemini Free Tier

* **Remoção do Grounding nativo:** O Google Search Grounding nativo foi removido do setup da Gemini Live. Anteriormente, ele era enviado em toda sessão no array `setup.tools` como `{"googleSearch": {}}`.
* **Causa e resolução da conexão Free Tier:** Na configuração testada, a presença do Google Search Grounding impedia a conexão com a chave Free Tier; sua remoção resolveu o problema. Mantendo a mesma chave Free Tier instalada no microSD e removendo apenas o Grounding nativo, a Pix passou a conectar normalmente (`connected=true`, `ready=true`).
* **Validação de voz e áudio:** A conversa por voz e a fluidez do áudio foram validadas na prática após a mudança.
* **Busca web preservada via ToolGateway:** Isso **não** remove a busca web da Pix: a ferramenta `web_search` continua operando pelo caminho `Pix → ToolGateway → /v2/call → Cloudflare Worker → TinyFish`, tornando o Google Search Grounding nativo dispensável na arquitetura atual.
* **Independência de busca:** Essa separação permite utilizar a Gemini Live sem depender do Grounding nativo do Google para pesquisas externas.

### Google Calendar via ToolGateway

* No fluxo produtivo atual, a agenda **não é mais baixada nem processada diretamente pelo ESP32-S3 da Pix**, evitando consumo excessivo de RAM/CPU e conexões TLS pesadas durante a sessão de voz.
* O Cloudflare Worker acessa o feed privado do Google Calendar através do segredo de ambiente `CALENDAR_ICS_URL`.
* O firmware **não precisa** receber a URL privada do calendário (`calendar_url.txt`) no microSD para esse fluxo.
* O Cloudflare Worker suporta os períodos `today`, `tomorrow` e `upcoming` no fuso `America/Sao_Paulo`. Como a função atualmente declarada ao Gemini é `get_daily_agenda` sem parâmetros e o firmware a converte em `get_agenda`, sem `period` explícito o Worker utiliza `upcoming` como padrão — retornando compromissos de hoje, amanhã e próximos eventos identificados por dia.
* O parser ICS do Worker trata:
  * eventos únicos;
  * eventos de dia inteiro (`VALUE=DATE`);
  * recorrências `DAILY`, `WEEKLY`, `MONTHLY` e `YEARLY`;
  * modificadores `COUNT`, `UNTIL`, `INTERVAL`, `BYDAY` e `BYMONTHDAY`;
  * exceções e cancelamentos: `EXDATE`, `RECURRENCE-ID` e `STATUS:CANCELLED`.
* O processamento foi otimizado especificamente para calendários grandes, filtrando blocos relevantes sem expandir todo o histórico desnecessariamente — validado com um calendário ICS real de aproximadamente **2 MB** e milhares de `VEVENT`s dentro do orçamento de CPU do Cloudflare Worker.

### Configuração Distribuível e Cloudflare Worker Próprio

O firmware da Pix foi desenhado para ser totalmente distribuível e independente:

* **Arquitetura de conexão:**
  * `Pix → Google Gemini Live API` (WebSocket de voz direta, baixa latência);
  * `Pix → ToolGateway → Cloudflare Worker próprio` (`POST /v2/call` assinado com HMAC-SHA256) `→ TinyFish / Google Calendar / serviços externos`.
* **Worker individual:** Não existe um Worker público compartilhado embutido no firmware. Cada usuário que compilar ou instalar a Pix deve criar e hospedar seu próprio Cloudflare Worker (ou gateway compatível).
* **Configuração amigável pela WebView:** A URL do Worker é configurada diretamente pela interface web da Pix, sem necessidade de recompilar código ou alterar manualmente arquivos de configuração no microSD.
* **Segredos isolados:** Os segredos do Worker nunca devem ser commitados no GitHub nem gravados em texto claro em repositórios.

#### Segredos Necessários no Cloudflare Worker

Na implantação de referência utilizada e validada neste projeto, o seu Worker requer três variáveis de ambiente secretas:

1. `HMAC_SHARED_KEY`: Chave secreta compartilhada para autenticar e verificar a integridade de todas as chamadas vindas da Pix via HMAC-SHA256.
2. `TINYFISH_API_KEY`: Chave de API utilizada pelo Worker para executar a busca web externa (`web_search`).
3. `CALENDAR_ICS_URL`: URL privada do feed iCalendar (`.ics`) do seu Google Calendar. **Atenção:** Essa URL fica exclusivamente no Worker e **NÃO** deve ser informada nem configurada na Pix.

#### Chave HMAC (Autenticação Pix ↔ Worker)

* A autenticação entre a Pix e o seu ToolGateway utiliza assinaturas criptográficas **HMAC-SHA256** acompanhadas de cabeçalhos de *timestamp* e *nonce* (para proteção contra repetição).
* Cada instalação deve possuir sua própria chave HMAC exclusiva, formada por **32 bytes aleatórios** e representada como uma string de **64 caracteres hexadecimais em lowercase**.
* **Onde a chave reside:**
  * **Na Pix:** salva em `/app/StackChan/secrets/gateway_hmac_key.txt` (armazenada de forma protegida no cartão microSD);
  * **No Worker:** configurada como o secret `HMAC_SHARED_KEY`.
* **Fluxo recomendado via WebView:**
  1. Acesse o painel web da Pix e vá até a aba **Configuração**;
  2. No cartão **3. ToolGateway**, clique em **Gerar nova chave** (gerada localmente no seu navegador via `window.crypto.getRandomValues()`);
  3. Clique no botão **Copiar**;
  4. Cadastre esse mesmo valor no secret `HMAC_SHARED_KEY` do seu Cloudflare Worker (via dashboard da Cloudflare ou `wrangler secret put HMAC_SHARED_KEY`);
  5. Retorne à WebView da Pix e clique em **Salvar configurações**;
  6. Uma vez salva, a chave é armazenada de forma segura e nunca mais é exibida ou devolvida pela API HTTP da Pix. Campos de segredo deixados em branco preservam os valores existentes.
* **Observação de segurança:** O HMAC-SHA256 autentica a requisição, garante a integridade dos parâmetros enviados e previne reutilização não autorizada, mas não substitui o uso de HTTPS/TLS na comunicação de rede. Segredos nunca devem ser expostos em logs públicos.

#### Compatibilidade e Campos Legados

Nas versões anteriores do projeto, a Pix tentava baixar a agenda diretamente e utilizava tokens estáticos:
* `/app/StackChan/secrets/calendar_url.txt` (Legado)
* `/app/StackChan/secrets/gateway_token.txt` (Legado)

Esses arquivos podem continuar sendo reconhecidos internamente pelo firmware para compatibilidade retroativa, mas **não fazem mais parte do fluxo produtivo atual** e foram removidos da interface de configuração. O caminho produtivo exclusivo é:
`gateway_base_url` (configurado na WebView) + `gateway_hmac_key.txt` (HMAC-SHA256).

### Arquitetura Resumida

| Recurso | Onde executa | Dependência externa |
| :--- | :--- | :--- |
| **Sensores** (`get_sensor_readings`) | Pix | nenhuma |
| **Clima** (`get_weather`) | cache local na Pix | atualização de feed já existente |
| **USD/BRL** (`get_currency`) | cache local na Pix | atualização de feed já existente |
| **Notícias** (`get_news`) | Pix / cache RSS | feeds RSS |
| **Busca web** (`web_search`) | Cloudflare ToolGateway | TinyFish |
| **Agenda** (`get_daily_agenda` → `get_agenda`) | Cloudflare ToolGateway | Google Calendar ICS |
| **Gemini Live** | Pix + Google | Gemini Live API |
| **Memória** (`search_memory`, `remember_*`, `recall_*`) | Pix / microSD | nenhuma para memória local |

### Baseline Validado

* **Baseline funcional do firmware validado:** `63deec4f4d24414424babb8b0e9ed18c2c097e31`
* **Commits principais desta evolução:**
  * `63deec4` — `chore(gemini): set 3.8 live as default model`
  * `56efcb4` — `feat(web): modernize distributable configuration interface`
  * `4aad207` — `fix(power): set deep sleep timeout to 15 minutes`
  * `4ec1920` — `feat(power): add automatic deep sleep with touch wake`
  * `f1f10c7` — `fix(gemini): remove native Google Search grounding from Live setup`
  * `383d6e1` — `feat(tools): route web search and agenda through ToolGateway`
  * `fa6e21b` — `feat(gateway): add HMAC-SHA256 signing and migrate callTool to /v2/call`
  * `80d55d5` — `feat(tools): use local weather and currency caches`

---

## ⚡ Gerenciamento de Energia e Sleep

### Deep Sleep automático

Comportamento final validado no CoreS3:

- após ~2 minutos sem interação:
  a Pix entra no sleep visual/comportamental atual;

- após 15 minutos sem interação real:
  a Pix entra automaticamente em deep sleep;

- presença contínua do ToF NÃO impede o deep sleep;
  o timer profundo usa atividade real separada;

- antes do deep sleep:
  - servos são parados;
  - torque e alimentação dos servos são desligados;
  - Gemini é desconectado intencionalmente;
  - haptic é parado;
  - mic/speaker são encerrados;
  - LEDs são apagados;
  - alimentação externa de 5 V é cortada;
  - Wi-Fi é desligado;

- deep sleep usa:
  `M5.Power.deepSleep(M5.Power.sleep_no_timer, true)`

- wake:
  toque na tela;
  CoreS3 GPIO21 / EXT1 / AW9523 INTN;

- após o toque:
  ocorre boot normal;
  Wi-Fi reconecta;
  HTTP retorna;
  Gemini Live reconecta e fica ready.

*Fluxo validado em hardware.*

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
Crie a estrutura de diretórios no cartão SD (veja detalhes adicionais em [docs/SD_LAYOUT.md](docs/SD_LAYOUT.md)):
```text
/app/StackChan/
├── config/
│   ├── runtime.json
│   └── gateway.json
├── prompts/
│   ├── system.txt
│   └── persona.txt
└── secrets/
    ├── gemini_api_key.txt
    ├── wifi_password.txt
    └── gateway_hmac_key.txt
```

* `gateway_hmac_key.txt` (chave hexadecimal de 64 caracteres) **só é necessário se `gateway_enabled=true`**.
* `gateway_token.txt` não é requisito da arquitetura atual (`/v2/call` usa assinatura HMAC-SHA256).
* `calendar_url.txt` no microSD não é requisito para a agenda atual via Cloudflare ToolGateway (a URL ICS fica configurada como segredo `CALENDAR_ICS_URL` no Worker).

#### Configuração do Gateway (`gateway.json` ou `runtime.json`)
Para habilitar o ToolGateway, configure os campos abaixo (em `/app/StackChan/config/gateway.json` ou diretamente em `runtime.json`):
```json
{
  "gateway_enabled": true,
  "gateway_base_url": "http://SEU_WORKER_OU_GATEWAY",
  "robot_id": "pix"
}
```
* `gateway_base_url` aponta para o endereço do Worker/gateway configurado pelo usuário (sem barra final necessária);
* O firmware acrescenta automaticamente o sufixo `/v2/call` ao invocar ferramentas externas;
* O relógio do ESP32 precisa estar sincronizado via NTP/RTC (`time(nullptr)` válido) para gerar o `X-Timestamp` aceito na validação HMAC.

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

### 4. Como Acessar o Painel Web (WebView Moderna)

Com a Pix conectada ao seu Wi-Fi e seu computador/smartphone no mesmo roteador:
* **Painel Principal:** `http://<IP_DA_PIX>/` (ex.: `http://192.168.0.234/`)
* **Página de Diagnóstico de Sensores:** `http://<IP_DA_PIX>/sensors`

A interface web foi completamente reformulada com um tema moderno, limpo e responsivo:
* **Status:** Telemetria em tempo real de bateria, memória livre (Heap e PSRAM), estado do Gemini Live (pronto, ouvindo, falando), sensores de presença/ToF e ambiente, além de notícias em cache.
* **Configuração:** Painel organizado em cartões limpos com switches modernos:
  * **1. Wi-Fi:** Ativação, SSID e senha segura (com botão de visualização temporária).
  * **2. Gemini Live:** Ativação, modelo de IA configurável em tempo de execução, voz sintetizada e Gemini API Key protegida.
  * **3. ToolGateway:** Ativação, URL do Cloudflare Worker próprio e gerenciamento de chave HMAC-SHA256 (com gerador criptográfico no navegador e botão de cópia rápida).
  * **4. Energia e repouso:** Apresentação gráfica informativa dos tempos validados no CoreS3 (~2 min sleep visual → 15 min deep sleep → wake por toque na tela).
  * **5. Áudio avançado:** Volume principal e seção técnica recolhida (`<details>`) para calibração de ganho, filtro de ruído e parâmetros de VAD sem poluição visual.
  * **6. Segurança Web & Sistema:** Fuso horário oficial, Robot ID e proteção opcional via HTTP Basic Auth (`stackchan`).
  * **7. Prompts:** Overlays de sistema e personalidade adicionais.
  * **Ações Globais:** Botões destacados de "Salvar configurações" e "Salvar e reiniciar", preservando valores existentes quando os campos secretos forem deixados em branco.
* **Memória:** Visualização de diálogos recentes, resumos periódicos e busca semântica no banco de memória local persistente no microSD.

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
