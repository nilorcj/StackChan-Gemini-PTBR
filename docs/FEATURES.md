# Matriz de Recursos — StackChan Gemini PT-BR

Este documento descreve a divisão de responsabilidades, arquitetura de ferramentas e matriz de funcionalidades do firmware **StackChan Gemini PT-BR**.

---

## 🤖 1. Funcionalidades Nativas da Pix (Sem Gateway)

Estas funcionalidades rodam no microcontrolador ESP32-S3 do CoreS3, dependendo de Wi-Fi e de uma chave de API válida para o Google Gemini Live:

- **Conversação de voz em tempo real (Gemini Live API):**
  - Conexão direta via WebSocket seguro (WSS) com o Google Gemini Live.
  - Streaming bidirecional de áudio PCM 16 kHz com VAD (*Voice Activity Detection*) ajustável.
  - Modelo padrão validado em hardware: `models/gemini-3.8-live`.
- **Sistema de Escuta Armada (*Gesture-Armed Listening*):**
  - Modo padrão `DISARMED`: o microfone fica fechado para prevenir falsos disparos por ruídos de TV ou conversas paralelas.
  - Armação por aceno de mão em frente ao sensor Laser ToF (`HAND_WAVE` / `FAST_GESTURE`) ou por toque na tela touchscreen (`TOUCH`).
  - Desarmamento automático após 15 segundos contínuos de silêncio/inatividade.
  - Botão de Mudo Físico (clique rápido no botão de energia, estilo Alexa/smart speaker).
- **Display e Expressões Faciais:**
  - Rosto procedural com olhos animados, piscadas e boca responsiva sincronizada com o status de fala.
  - Renderização otimizada com *dirty rects* para economizar ciclos de CPU para a criptografia de rede.
  - Gaveta de status rápido (*quick status drawer*) acionada por deslizamento do topo para baixo na tela sensível ao toque.
- **Movimentação Pan/Tilt e Expressividade:**
  - Controle cinemático suave dos servos da base Stack-chan (Yaw e Pitch).
  - Gestos expressivos programados (`nod`, `nod_yes`, `shake_head`, `curious_tilt`, `look_around`, etc.).
  - *Nota de suporte sem base:* Na ausência da base motorizada, o firmware detecta o timeout do expansor IO (1,2s) e segue a execução normalmente com rosto, áudio e voz ativos. Essa tolerância de inicialização é prevista na implementação de software, embora os ensaios físicos principais tenham sido executados com a base montada.
- **Ferramentas Locais da Pix (*Local Function Calling*):**
  - Leitura dos sensores físicos de mesa (`get_sensor_readings` via PaHub/ToF/ENV).
  - Consulta à telemetria interna do robô (`get_robot_status`).
  - Consulta de data e hora oficial sincronizada com fuso horário local (`get_current_datetime`).
  - Leitura de clima de Brasília em cache local (`get_weather`).
  - Leitura de cotação do dólar USD/BRL em cache local (`get_currency`).
  - Leitura de manchetes de notícias RSS em cache local (`get_news`).
  - Atualização dos feeds de notícias e clima sob demanda (`refresh_news_feeds`).
  - Controle de volume físico dos alto-falantes (`set_speaker_volume`).
  - Disparo de gestos e expressões faciais pela IA (`servo_gesture`, `set_emotion`).
- **Memória Local Persistente no MicroSD:**
  - Registro de fatos aprendidos sobre o usuário (`remember_fact`, `search_memory`).
  - Memórias privadas de contexto protegido (`remember_private_memory`, `recall_private_memory`).
  - Histórico de eventos e diálogos diários estruturados em formato JSONL.
- **Gerenciamento de Energia:**
  - Repouso visual/comportamental após aproximadamente 2 minutos de inatividade.
  - Desligamento para **deep sleep automático** após 15 minutos sem interação humana real.
  - Despertar do deep sleep por toque na tela (ESP32 EXT1 / AW9523 INTN).
- **Interface Web e Diagnóstico:**
  - WebView responsiva (abas Status, Configuração e Memória) na porta 80.
  - Fallback automático para ponto de acesso Wi-Fi próprio (`<robot_id>-setup-XXXX`, por exemplo `stackchan-setup-XXXX` ou `pix-setup-XXXX`) quando a rede configurada não estiver ao alcance.

---

## ☁️ 2. Funcionalidades Externas via ToolGateway (Cloudflare Worker)

Quando o ToolGateway está habilitado (`gateway_enabled=true`), as ferramentas que exigem acesso dinâmico à internet externa ou processamento pesado são delegadas ao Cloudflare Worker via `POST /v2/call` assinado com HMAC-SHA256:

- **Busca Web em Tempo Real (`web_search`):**
  - Executada pelo Worker através da API **TinyFish**.
  - Devolve resultados estruturados (título, link, data, site e resumo) para enriquecer a resposta do Gemini Live sem Grounding nativo.
- **Agenda Pessoal do Google Calendar (`get_agenda` / `get_daily_agenda`):**
  - Processada pelo Worker a partir do feed privado iCalendar (`CALENDAR_ICS_URL`).
  - Trata eventos únicos, recorrentes (`RRULE`), cancelamentos e exceções no fuso `America/Sao_Paulo`.
  - Suporta os períodos `today`, `tomorrow` e `upcoming`.

---

## 📋 3. Dependências do Cartão MicroSD

O microcontrolador CoreS3 depende obrigatoriamente do cartão MicroSD para carregar e persistir:

- Credenciais da rede Wi-Fi (`runtime.json` e `wifi_password.txt`).
- Chave de API do Gemini Live (`gemini_api_key.txt`).
- Chave secreta de autenticação do ToolGateway (`gateway_hmac_key.txt`).
- Senha de acesso à interface Web (`web_password_sha256.txt`).
- Prompts personalizados de persona e sistema (`persona.txt` e `system.txt`).
- Banco de dados de memórias e diários (`memory/`).

---

## ⚠️ 4. Limitações Conhecidas

- **Rede Wi-Fi:** O ESP32-S3 suporta exclusivamente redes **2.4 GHz** com autenticação **WPA/WPA2-Personal (PSK)**. Redes corporativas WPA2/WPA3-Enterprise (802.1X, eduroam) não são suportadas.
- **Resolução de Nomes (mDNS):** Acesso à WebView é feito exclusivamente por endereço IP numérico (`http://IP_DO_ROBO/`); não há suporte a nomes de domínio locais (ex.: `pix.local`).
- **Reinicialização após Configurar Rede:** Ao alterar as credenciais de Wi-Fi pela interface Web, é necessário reiniciar o robô (pelo botão "Salvar e reiniciar" da WebView ou pelo botão de energia) para que a nova rede seja conectada.
- **Obrigatoriedade do MicroSD:** Sem um cartão MicroSD formatado em FAT32 inserido, o firmware não carrega credenciais, não inicia Wi-Fi e não conecta ao Gemini Live.
- **Natureza do Projeto:** Projeto de código aberto voltado para desenvolvedores e entusiastas de robótica (não é um produto de consumo fechado).
