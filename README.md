# StackChan Gemini PT-BR

🇧🇷 [Português](README.md) | 🇺🇸 [English](README_EN.md)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange.svg)](https://platformio.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![M5Stack CoreS3](https://img.shields.io/badge/Hardware-M5Stack%20CoreS3-blue.svg)](https://m5stack.com/)
[![Gemini Live](https://img.shields.io/badge/AI-Google%20Gemini%20Live-4285F4.svg)](https://ai.google.dev/)

Firmware avançado para o robô **M5Stack CoreS3 Stack-chan** com conversação por voz em tempo real (**Google Gemini Live API**) em **Português do Brasil (PT-BR)**, suporte a periféricos modulares do ecossistema **M5Stack Units**, sistema inteligente de conversação armada por toque e gestos (*Gesture-Armed Listening*), gerenciamento profundo de energia com despertar por toque e integração com ferramentas externas via **ToolGateway assinado com HMAC-SHA256**.

* **Repositório oficial:** [https://github.com/nilorcj/StackChan-Gemini-PTBR](https://github.com/nilorcj/StackChan-Gemini-PTBR)
* **Base de hardware recomendada:** M5Stack CoreS3 + Base Stack-chan + Cartão MicroSD FAT32.

---

## 🌟 1. Apresentação do Projeto

O **StackChan Gemini PT-BR** transforma o robô de mesa M5Stack CoreS3 em um assistente físico conversacional expressivo e inteligente. Utilizando a API de voz bidirecional de baixíssima latência do **Google Gemini Live**, o robô conversa naturalmente em português brasileiro, demonstra expressões faciais procedurais, movimenta a cabeça e executa ferramentas locais e externas.

### Origem e Reconhecimento
Este projeto foi desenvolvido com base no firmware experimental inicial de [taranton/stackchan-gemini-firmware](https://github.com/taranton/stackchan-gemini-firmware), pioneiro na conexão de voz com o Gemini Live no ESP32-S3.

A partir desse ponto de partida, este repositório implementa uma evolução arquitetural completa:
- **Localização total em PT-BR** (persona, prompts de sistema, comandos de fala e interface de configuração);
- **Sistema de escuta armada (*Gesture-Armed Listening*)**, eliminando falsos disparos causados por ruídos de ambiente;
- **Integração modular com M5Stack Units** (PaHub, ToF Laser, Sensor Ambiental ENV e Feedback Háptico);
- **ToolGateway seguro via Cloudflare Worker com HMAC-SHA256** para busca web atualizada e consulta de agenda pessoal;
- **Gerenciamento de energia avançado com deep sleep automático de 15 minutos** e despertar capacitivo por toque;
- **Interface Web de Configuração (WebView) moderna, responsiva e distribuível**.

---

## 🚀 2. Principais Funcionalidades

- **Conversação Fluida em Tempo Real:** Streaming contínuo de áudio PCM 16 kHz com o modelo `models/gemini-3.8-live` em português brasileiro, com baixa latência e interrupção natural de fala (*barge-in*).
- **Escuta Consciente e Armada:** O robô nunca abre o microfone por ruídos aleatórios. A escuta é ativada exclusivamente por aceno de mão em frente ao sensor Laser ToF ou por toque na tela touchscreen.
- **Botão de Mudo Físico (Estilo Alexa):** Um clique rápido no botão de energia desliga o microfone e bloqueia conversas até ser desativado.
- **Rosto Expressivo Procedural:** Olhos e boca dinâmicos desenhados via código no display do CoreS3, com animações de piscada, estados emocionais e gaveta de status rápido (*swipe down* no topo da tela).
- **Movimentação Pan/Tilt:** Movimentação cinemática suave da cabeça por servos seriais ou PWM na base Stack-chan.
- **Ferramentas Locais da Pix:**
  - `get_sensor_readings`: Leitura de distância, temperatura, umidade e pressão de mesa;
  - `get_current_datetime`: Horário e data oficial sincronizados via NTP/RTC;
  - `get_weather` e `get_currency`: Clima de Brasília e cotação do dólar USD/BRL em cache local;
  - `get_news`: Manchetes de notícias RSS em cache local;
  - `set_speaker_volume`: Controle físico de volume;
  - `search_memory` e `remember_fact`: Memória de longo prazo persistida no microSD.
- **Ferramentas Externas via ToolGateway:**
  - `web_search`: Busca web em tempo real via API TinyFish;
  - `get_agenda`: Consulta inteligente à agenda do Google Calendar a partir de feed iCalendar privado.
- **Painel de Configuração Web Moderno:** WebView protegida na porta 80 para configuração completa de Wi-Fi, IA, ToolGateway, áudio e memórias, com gerador de chaves HMAC integrado.

---

## 🧩 3. Hardware Obrigatório, Recomendado e Opcional

> [!IMPORTANT]
> **Módulos externos NÃO são obrigatórios para a conversa básica.** O firmware foi estruturado para permitir conversação com o Gemini Live utilizando apenas o **M5Stack CoreS3**, um **cartão MicroSD** e a sua conexão Wi-Fi.

### Tabela de Componentes

| Componente | Obrigatoriedade | Funcionamento sem o componente |
|---|---|---|
| **M5Stack CoreS3** | **Obrigatório** | Controlador central do robô. Fornece o processador ESP32-S3, display touchscreen, microfones duplos, alto-falante, câmera e circuitos de alimentação. |
| **Cartão MicroSD** | **Obrigatório** *(na versão atual)* | Armazena configurações (`runtime.json`), segredos de rede/API, prompts e memória persistente. Sem o MicroSD, o firmware não inicializa a rede nem a conexão com o Gemini. Deve ser formatado em **FAT32**. |
| **Base Motorizada / Servos** | **Recomendado** | Confere expressividade física (Pan/Tilt). Se a base não estiver conectada, o firmware prossegue a inicialização após um timeout de 1,2s do expansor IO; o microcontrolador mantém a execução de voz, display e áudio. *(Essa operação como assistente estático é prevista na implementação de software, mas ainda não foi validada em testes físicos de bancada sem a base montada).* |
| **Unit PaHub** | **Opcional** | Multiplexador I2C (PCA9548AP) para a Porta A. Se ausente, o firmware segue a inicialização sem travar; portas I2C adicionais ficam inativas. *(Confirmado no código).* |
| **Unit ToF (Laser)** | **Opcional** | Mede distância e reconhece acenos de mão para armação de conversa. Se ausente, **a conversa pode ser iniciada a qualquer momento tocando na tela touchscreen**. *(Confirmado no código).* |
| **Unit ENV (III/IV)** | **Opcional** | Mede temperatura, umidade e pressão atmosférica local. Se ausente, a ferramenta `get_sensor_readings` retorna erro tratado; previsões de clima via internet continuam operacionais. *(Confirmado no código).* |
| **Unit Vibrator (U059)** | **Opcional** | Motor háptico conectado à Porta B (GPIO 9) para feedback tátil (ronronar, cliques). Se ausente, os comandos de vibração são inócuos e o robô opera normalmente. *(Confirmado no código).* |

### Cenário Mínimo de Funcionamento
O menor conjunto funcional para colocar a Pix para conversar consiste em:
1. **M5Stack CoreS3** conectado à energia USB-C;
2. **Cartão MicroSD (FAT32)** inserido no CoreS3 com a estrutura básica;
3. Rede **Wi-Fi 2.4 GHz** com acesso à internet;
4. **Chave de API do Google Gemini** (Google AI Studio).

Nesse modo compacto, sem nenhum sensor ou servo conectado, a conversa pode ser iniciada tocando na tela da Pix!

---

## 📦 4. Instalação do Firmware

Você pode instalar o firmware de duas maneiras: utilizando binários pré-compilados ou compilando o código-fonte com o PlatformIO.

### Método A: Firmware Pré-Compilado (GitHub Release)

> [!NOTE]
> **Disponibilidade e Visibilidade da Release:** A release oficial `v1.0.0` está em fase de preparação documental e não está disponível para download antes de sua publicação formal na página de [Releases do GitHub](https://github.com/nilorcj/StackChan-Gemini-PTBR/releases). Adicionalmente, enquanto o repositório permanecer em modo privado, os binários e páginas de release só serão visíveis para usuários autenticados com permissão explícita de acesso ao projeto.

O ESP32-S3 com partição de 16 MB requer 4 arquivos gravados em endereços específicos da memória flash. Gravar apenas o `firmware.bin` em uma placa virgem ou com outro firmware não é suficiente, pois a tabela de partições e o bootloader precisam coincidir:

| Arquivo | Endereço Flash | Descrição |
|---|---|---|
| `bootloader.bin` | `0x0000` | Bootloader do ESP32-S3 |
| `partitions.bin` | `0x8000` | Tabela de partições configurada para 16 MB |
| `boot_app0.bin` | `0xe000` | Seletor de boot OTA (padrão Espressif) |
| `firmware.bin` | `0x10000` | Imagem da aplicação StackChan Gemini PT-BR |

Quando os binários da release estiverem disponíveis, você poderá gravá-los usando o utilitário oficial `esptool`:

#### Comando em linha única (compatível com Windows PowerShell, CMD, Linux e macOS):
```bash
esptool.py --chip esp32s3 --port PORTA_SERIAL --baud 115200 write_flash -z 0x0000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```

#### No Windows (PowerShell):
```powershell
esptool.py --chip esp32s3 --port COM3 --baud 115200 write_flash -z `
  0x0000 bootloader.bin `
  0x8000 partitions.bin `
  0xe000 boot_app0.bin `
  0x10000 firmware.bin
```

#### No Windows (Prompt de Comando / CMD):
```cmd
esptool.py --chip esp32s3 --port COM3 --baud 115200 write_flash -z ^
  0x0000 bootloader.bin ^
  0x8000 partitions.bin ^
  0xe000 boot_app0.bin ^
  0x10000 firmware.bin
```

#### No Linux / macOS (Bash):
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 115200 write_flash -z \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin
```
*(Consulte [docs/BUILD_FLASH.md](docs/BUILD_FLASH.md) para detalhes adicionais).*

### Método B: Compilação Manual com PlatformIO
Se preferir compilar diretamente a partir do código-fonte:
1. Instale o [PlatformIO Core](https://platformio.org/) ou a extensão no VS Code.
2. Clone o repositório oficial:
   ```bash
   git clone https://github.com/nilorcj/StackChan-Gemini-PTBR.git
   cd StackChan-Gemini-PTBR
   ```
3. Conecte o CoreS3 via cabo USB-C e compile/grave com um único comando:
   ```bash
   pio run -e m5stack-cores3 -t upload
   ```
   *(O PlatformIO baixa automaticamente todas as bibliotecas e grava os offsets corretos).*

---

## 🛠️ 5. Primeira Configuração

Para que a Pix possa operar, o cartão MicroSD (formatado em FAT32) deve estar inserido no slot do CoreS3. Você pode realizar a configuração inicial por qualquer uma das duas abordagens abaixo:

### Opção 1: Configuração Inicial Guiada pela WebView (Modo SoftAP)
Esta é a abordagem mais prática, pois dispensa a criação manual de arquivos no computador:
1. Insira o cartão MicroSD formatado em FAT32 no CoreS3 (pode estar vazio);
2. Ligue o CoreS3. Como não há dados de Wi-Fi gravados, após 15 segundos na tela de boot o robô iniciará automaticamente o ponto de acesso Wi-Fi de configuração:
   - **SSID:** `<robot_id>-setup-XXXX` (por exemplo: `stackchan-setup-XXXX` ou `pix-setup-XXXX`, onde `XXXX` são os 4 caracteres hexadecimais finais do MAC da placa);
   - **Senha:** `stackchanXXXX`;
3. Conecte seu smartphone ou computador à rede da Pix e abra no navegador:
   ```text
   http://192.168.4.1/
   ```
4. Na aba **Configuração**, informe o SSID e a Senha da sua rede Wi-Fi (2.4 GHz) no cartão **1. Wi-Fi** e a sua chave no cartão **2. Gemini Live**;
5. Clique no botão **Salvar e reiniciar**. O firmware criará as pastas e gravará os arquivos correspondentes no MicroSD automaticamente, reiniciando o robô já conectado à sua rede local!

### Opção 2: Preparação Manual dos Arquivos no MicroSD (no Computador)
Se preferir preparar o cartão diretamente no PC antes de ligar o robô:
1. Conecte o cartão MicroSD ao computador;
2. Crie a pasta `/app/StackChan/config/` e o arquivo `/app/StackChan/config/runtime.json`:
   ```json
   {
     "robot_id": "pix",
     "wifi_enabled": true,
     "wifi_ssid": "NOME_DO_SEU_WIFI",
     "web_enabled": true,
     "gemini_enabled": true,
     "gemini_model": "models/gemini-3.8-live",
     "gemini_voice": "Puck",
     "gateway_enabled": false
   }
   ```
3. Crie a pasta `/app/StackChan/secrets/` e os arquivos de texto simples (sem quebras de linha adicionais):
   - `/app/StackChan/secrets/wifi_password.txt` contendo a senha do Wi-Fi;
   - `/app/StackChan/secrets/gemini_api_key.txt` contendo a chave da Gemini API.
4. Insira o cartão no CoreS3 e ligue o dispositivo.

*(Consulte a referência completa da estrutura do cartão em [docs/SD_LAYOUT.md](docs/SD_LAYOUT.md)).*

---

## 🌐 6. Configuração de Wi-Fi e Acesso à WebView

### Conexão Normal vs. Ponto de Acesso de Emergência (SoftAP)
- **Modo Normal:** Ao ligar, o CoreS3 exibe o Dashboard de Boot por 15 segundos enquanto se conecta ao seu Wi-Fi doméstico. Conectado, ele exibe seu endereço IP local (ex.: `192.168.1.105`).
- **Modo SoftAP (Fallback):** Se o Wi-Fi configurado não for encontrado em 15 segundos ou se o cartão não tiver credenciais, a Pix cria automaticamente sua própria rede Wi-Fi para configuração:
  - **Nome da Rede (SSID):** `<robot_id>-setup-XXXX` (por exemplo: `stackchan-setup-XXXX` ou `pix-setup-XXXX`, onde `XXXX` são os 4 dígitos finais do MAC da placa);
  - **Senha do Wi-Fi:** `stackchanXXXX` (a palavra `stackchan` em minúsculas seguida dos mesmos 4 dígitos em maiúsculas);
  - **Endereço IP:** `http://192.168.4.1/`.

> [!NOTE]
> **Compatibilidade Wi-Fi:** O ESP32-S3 requer redes **2.4 GHz** com autenticação **WPA/WPA2-Personal (PSK)**. Redes corporativas WPA2/WPA3-Enterprise (802.1X com usuário de domínio, como eduroam) **não são suportadas**.

### Acessando a Interface Web (WebView)
Com o robô e o computador/smartphone na mesma rede local:
- Acesse pelo navegador: `http://<IP_DO_ROBO>/` (ex.: `http://192.168.1.105/`).
- O acesso é realizado sempre pelo endereço IP numérico (não utiliza mDNS / `.local`).

### Estrutura da WebView Moderna
A interface Web é dividida em três abas principais:
1. **Status:** Telemetria em tempo real (bateria, tensão, RSSI, status da conexão Gemini Live, turnos de fala, memória livre Heap/PSRAM, sensores e notícias em cache);
2. **Configuração:** Painel moderno com cartões modulares:
   - **1. Wi-Fi:** Habilitação, SSID e senha segura com botão de revelação;
   - **2. Gemini Live:** Habilitação, seleção de modelo, voz e chave de API protegida;
   - **3. ToolGateway:** Habilitação, URL HTTPS do Worker e chave secreta HMAC-SHA256 (com botão "Gerar nova chave" e botão "Copiar");
   - **4. Energia e repouso:** Apresentação gráfica dos ciclos validados de sleep e deep sleep;
   - **5. Áudio & Fala:** Volume e calibração fina de ganho do microfone e VAD;
   - **6. Segurança Web & Sistema:** Fuso horário, identificador do robô e senha de proteção Web;
   - **7. Prompts Adicionais:** Overlays de sistema e persona para ajuste de comportamento.
   - **Ações:** Botões "Salvar configurações" e "Salvar e reiniciar" (campos de segredo deixados em branco preservam os valores gravados).
3. **Memória:** Inspeção dos diálogos recentes, resumos periódicos e busca no banco de fatos gravados no MicroSD.

---

## 🤖 7. Gemini Live e Configuração da API

- **Obtenção da Chave:** Obtenha uma chave de API no [Google AI Studio](https://aistudio.google.com/) (conforme a disponibilidade de plano, cotas e termos vigentes da Google).
- **Modelo Padrão Validado:** O firmware utiliza nativamente o modelo `models/gemini-3.8-live` com voz `Puck`, testado e validado em hardware com áudio estável e baixa latência.
- **Funcionamento com Free Tier:** O setup de conexão do firmware foi ajustado para remover o parâmetro legado de Google Search Grounding nativo. No ambiente testado, essa modificação permitiu que chaves configuradas na camada de avaliação gratuita (*Free Tier*) conectassem com sucesso (`connected=true`, `ready=true`). *(Ressalta-se que políticas de cotas, limites por minuto e níveis de acesso gratuito são estabelecidos exclusivamente pelo Google e podem ser alterados pelo provedor sem aviso prévio).*
- **Início da Conversa:**
  - **Por Toque na Tela:** Toque em qualquer parte do display para armar a conversa;
  - **Por Aceno:** Faça um gesto rápido de mão a cerca de 15 a 30 cm do sensor Laser ToF.
- **Independência do Worker:** O Gemini Live opera de forma autônoma para diálogos de voz gerais sem exigir nenhum gateway externo. O ToolGateway é necessário apenas caso se deseje busca web e consulta de compromissos na agenda.

---

## ☁️ 8. ToolGateway, Cloudflare Worker, TinyFish e Google Calendar

Para dotar a Pix de informações externas em tempo real, o firmware utiliza uma arquitetura baseada em um **Cloudflare Worker** próprio que atua como **ToolGateway**:

```text
Pix (CoreS3) ──[POST /v2/call + HMAC-SHA256]──> Cloudflare Worker ──┬──> TinyFish API (web_search)
                                                                    └──> Google Calendar ICS (get_agenda)
```

### Autenticação Criptográfica HMAC-SHA256
Todas as chamadas para o Worker no endpoint `POST /v2/call` são protegidas por:
- `X-Timestamp`: Unix timestamp em segundos (janela de tolerância: ±120s);
- `X-Nonce`: 32 caracteres hexadecimais aleatórios;
- `X-Signature`: Assinatura HMAC-SHA256 calculada sobre o método, path, timestamp, nonce e payload JSON.

### Como Configurar seu Próprio Worker:
1. Navegue até o diretório `cloudflare-worker/` deste repositório;
2. Siga as instruções do [cloudflare-worker/README.md](cloudflare-worker/README.md) para implantar o Worker na Cloudflare (utilizando o plano gratuito ou padrão do serviço, respeitando os limites e cotas da plataforma);
3. Configure os três segredos no Worker:
   - `HMAC_SHARED_KEY`: chave de 64 caracteres hexadecimais;
   - `TINYFISH_API_KEY`: sua chave de API para o serviço de busca [TinyFish](https://tinyfish.ai/) (sujeita aos termos e planos de uso do provedor);
   - `CALENDAR_ICS_URL`: a URL privada (`https://...`) do feed iCalendar do seu Google Agenda.
4. Na WebView da Pix (aba **Configuração** > cartão **3. ToolGateway**):
   - Ative a opção **Habilitar ToolGateway**;
   - Cole a URL do seu Worker no campo **URL Base do ToolGateway** (ex.: `https://pix-tool-gateway.seu-usuario.workers.dev`);
   - Preencha ou gere a **Chave Secreta HMAC-SHA256** idêntica à configurada no Worker;
   - Clique em **Salvar configurações**.

---

## 🔒 9. Arquitetura, Segurança e Memória

- **Separação Local vs. Remoto:** Operações críticas de voz, processamento de sensores e movimentação são executadas localmente no microcontrolador. Apenas consultas que necessitam de processamento de texto pesado (como expansão de regras `RRULE` de agendas grandes ou scraping web) são delegadas ao Worker.
- **Segredos Isolados e Fora do Git:** Chaves de API, senhas de Wi-Fi e URLs de agenda nunca são gravadas no código-fonte. Ficam restritas ao cartão MicroSD da Pix ou às variáveis protegidas do Cloudflare Worker.
- **Isolamento de Rede:** A WebView local da Pix foi projetada para operação em rede local (LAN). Nunca exponha as portas da Pix diretamente para a internet aberta sem autenticação e VPN/firewall.
- **Memória Local:** A Pix armazena dados de diálogos e resumos na pasta `/app/StackChan/memory/` do MicroSD. Caso você queira transferir o robô ou o cartão para outra pessoa, lembre-se de limpar os arquivos dessa pasta.

---

## ⚡ 10. Gerenciamento de Energia e Sensores

O CoreS3 possui um sistema integrado de gerenciamento de energia de dois estágios, validado em hardware:

1. **Repouso Visual (~2 minutos sem interação):**
   - A Pix fecha os olhos e entra em animação de sono leve (`sleep`);
   - O áudio e a conexão de rede permanecem disponíveis;
   - O robô acorda imediatamente ao detectar toque ou presença no ToF.
2. **Deep Sleep Automático (15 minutos sem interação humana real):**
   - Se permanecer 15 minutos contínuos sem comandos de voz ou toques, a Pix entra em **Deep Sleep profundo** para preservar a bateria e os componentes;
   - Desliga a alimentação dos servos motores (corta a linha de 5 V);
   - Desconecta intencionalmente a sessão do Gemini Live;
   - Desliga os LEDs, amplificador de som e rádio Wi-Fi;
   - Coloca a CPU ESP32-S3 em sono profundo com:
     ```cpp
     M5.Power.deepSleep(M5.Power.sleep_no_timer, true);
     ```
3. **Despertar por Toque na Tela:**
   - Basta encostar o dedo na tela touchscreen do CoreS3;
   - O controlador de toque aciona o pino de interrupção (`ESP_SLEEP_WAKEUP_EXT1`), acordando o robô imediatamente;
   - A Pix executa a inicialização rápida, reconecta ao Wi-Fi e ao Gemini Live e volta a interagir normalmente.

---

## 💻 11. Compilação pelo Código-Fonte

Para compilar, auditar ou customizar o firmware do robô:

```bash
# 1. Instalar PlatformIO
pip install -U platformio

# 2. Clonar o repositório
git clone https://github.com/nilorcj/StackChan-Gemini-PTBR.git
cd StackChan-Gemini-PTBR

# 3. Compilar
pio run -e m5stack-cores3

# 4. Gravar no CoreS3 via USB
pio run -e m5stack-cores3 -t upload

# 5. Monitorar logs de inicialização
pio device monitor -b 115200
```

---

## ⚠️ 12. Limitações Conhecidas

- **Redes Wi-Fi:** Suporte restrito a redes de **2.4 GHz** com segurança **WPA-PSK / WPA2-PSK**. Redes 5 GHz e redes autenticadas com credenciais corporativas (WPA2-Enterprise 802.1X) não são suportadas pelo hardware.
- **Resolução de Nomes Local (mDNS):** O acesso à interface Web deve ser feito pelo endereço IP da Pix (`http://192.168.x.x/`); o firmware não responde por nomes de domínio locais (ex.: `pix.local`).
- **Cartão MicroSD Requerido:** O firmware requer um cartão MicroSD formatado em FAT32 inserido para inicializar as credenciais e conexões de rede.
- **Natureza do Projeto:** Este é um projeto de código aberto voltado a desenvolvedores, estudantes e entusiastas de robótica e inteligência artificial.

---

## 📚 13. Documentação Complementar, Créditos e Licença

### Documentação Adicional
- [Compilação e Gravação Detalhada](docs/BUILD_FLASH.md)
- [Estrutura do Cartão MicroSD](docs/SD_LAYOUT.md)
- [Matriz de Recursos do Firmware](docs/FEATURES.md)
- [Guia do Cloudflare Worker](cloudflare-worker/README.md)
- [Segurança e Configuração Web](docs/WEB_SECURITY.md)
- [Referência da API HTTP](docs/API.md)
- [Arquitetura de Memória Local](docs/MEMORY_INTERNALS.md)

### Créditos
- Projeto original de referência: [taranton/stackchan-gemini-firmware](https://github.com/taranton/stackchan-gemini-firmware);
- Stack-chan original por [shakao / robo-stackchan](https://github.com/stack-chan/stack-chan);
- Desenvolvido e mantido por [@nilorcj](https://github.com/nilorcj).

### Licença
Distribuído sob a licença **MIT**. Consulte o arquivo [LICENSE](LICENSE) para obter todos os termos e condições.
