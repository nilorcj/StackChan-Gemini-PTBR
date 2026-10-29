# Estrutura do Cartão MicroSD — StackChan Gemini PT-BR

O firmware armazena seu estado mutável de tempo de execução, configurações, segredos e banco de memória local no cartão MicroSD sob o diretório raiz `/app/StackChan/`.

> [!IMPORTANT]
> **Formatação do Cartão:** O cartão MicroSD deve ser formatado obrigatoriamente no sistema de arquivos **FAT32** (ou FAT16) com tamanho de cluster padrão. Cartões formatados em exFAT ou NTFS não são reconhecidos pelo CoreS3.

---

## 📁 Árvore de Diretórios

```text
/app/StackChan/
  config/
    runtime.json                  # Configurações gerais (Wi-Fi, áudio, VAD, opções)
    gateway.json                  # Configuração do ToolGateway (opcional, pode ficar em runtime.json)
    summary.json                  # Resumo do estado do sistema
  prompts/
    system.txt                    # Prompt base do sistema
    persona.txt                   # Prompt de persona / comportamento em PT-BR
  secrets/
    gemini_api_key.txt            # Chave de API da Google Gemini Live
    wifi_password.txt             # Senha da rede Wi-Fi configurada
    gateway_hmac_key.txt          # Chave secreta HMAC-SHA256 (64 hex) para o ToolGateway
    web_password_sha256.txt       # Hash SHA-256 da senha de proteção da WebView
    meta.jsonl                    # Metadados de auditoria de segredos
    gateway_token.txt             # LEGADO: não utilizado pelo fluxo POST /v2/call
    calendar_url.txt              # LEGADO: não utilizado pelo ToolGateway atual
  memory/
    events/YYYY-MM-DD.jsonl       # Registro temporal de eventos do robô
    dialogues/YYYY-MM-DD.jsonl    # Histórico de diálogos locais
    summaries.jsonl               # Resumos periódicos consolidados
    memories.jsonl                # Fatos aprendidos e memórias persistentes
    profile.json                  # Perfil do usuário e preferências
  camera/
    latest.jpg                    # Última captura de imagem da câmera do CoreS3
```

O firmware cria os diretórios faltantes automaticamente no primeiro boot após montar o cartão.

---

## 🛠️ Opções de Primeira Configuração

Para a configuração inicial com o cartão MicroSD inserido, você pode optar por duas abordagens:

### Opção 1: Configuração Guiada pela WebView (Modo SoftAP)
Basta inserir o cartão MicroSD formatado no robô e ligá-lo. Como não haverá rede configurada, o robô iniciará automaticamente o ponto de acesso Wi-Fi próprio:
- **SSID:** `<robot_id>-setup-XXXX` (por exemplo: `stackchan-setup-XXXX` ou `pix-setup-XXXX`, onde `XXXX` são os 4 caracteres hexadecimais finais do MAC);
- **Senha:** `stackchanXXXX`;
- **Endereço:** `http://192.168.4.1/`.

Ao preencher as informações de rede e chave na aba **Configuração** e clicar em **Salvar e reiniciar**, o firmware grava automaticamente os arquivos no MicroSD.

### Opção 2: Preparação Manual dos Arquivos (no PC)
Caso prefira criar os arquivos no computador antes de inserir o cartão no robô, crie a estrutura mínima abaixo:

```text
/app/StackChan/config/runtime.json
/app/StackChan/secrets/wifi_password.txt
```

#### Exemplo de `runtime.json`:

```json
{
  "robot_id": "pix",
  "wifi_enabled": true,
  "wifi_ssid": "NOME_DA_SUA_REDE_2_4GHZ",
  "web_enabled": true,
  "gemini_enabled": true,
  "gemini_model": "models/gemini-3.8-live",
  "gemini_voice": "Puck",
  "gateway_enabled": false
}
```

#### Exemplo de `wifi_password.txt`:

```text
SUA_SENHA_WIFI_AQUI
```
*(Grave o arquivo em texto simples, sem espaços antes/depois e sem quebras de linha adicionais).*

---

## 🤖 Configuração do Gemini Live

Para habilitar a conversação por voz com a Google Gemini Live API:

1. Adicione sua chave de API em:
   ```text
   /app/StackChan/secrets/gemini_api_key.txt
   ```
2. Certifique-se de que `"gemini_enabled": true` está configurado em `runtime.json` (ou ative pela WebView na aba **Configuração** > cartão **2. Gemini Live**).
3. O modelo padrão é `models/gemini-3.8-live` e a voz padrão é `Puck`.

Você também pode personalizar os prompts de comportamento em:
```text
/app/StackChan/prompts/system.txt
/app/StackChan/prompts/persona.txt
```

---

## 🌐 Configuração do ToolGateway (Busca Web e Agenda)

A integração com o **ToolGateway** (executado em Cloudflare Worker) é opcional e adiciona busca web atualizada (`web_search`) e consulta à agenda do Google Calendar (`get_agenda`):

```json
{
  "gateway_enabled": true,
  "gateway_base_url": "https://pix-tool-gateway.seu-subdominio.workers.dev",
  "robot_id": "pix"
}
```

### Autenticação HMAC-SHA256 (`POST /v2/call`)

- O firmware comunica-se com o gateway via `POST /v2/call`.
- A autenticação é realizada com assinatura **HMAC-SHA256** enviada nos cabeçalhos `X-Timestamp`, `X-Nonce` e `X-Signature`.
- A chave secreta compartilhada deve ser salva no microSD em:
  ```text
  /app/StackChan/secrets/gateway_hmac_key.txt
  ```
- O arquivo deve conter uma sequência exata de **64 caracteres hexadecimais em minúsculas**.
- Essa chave pode ser gerada com um clique na WebView (aba **Configuração** > cartão **3. ToolGateway** > botão **Gerar nova chave**).
- O mesmo valor deve ser configurado como o secret `HMAC_SHARED_KEY` no Cloudflare Worker.

---

## ⚠️ Arquivos Legados (Não Utilizados no Fluxo Atual)

Nas versões preliminares da arquitetura:
- `/app/StackChan/secrets/gateway_token.txt`: antigo token estático Bearer; **não é utilizado** pelo caminho produtivo `/v2/call` com HMAC.
- `/app/StackChan/secrets/calendar_url.txt`: antiga URL de calendário lida localmente pelo CoreS3; **não é utilizada**, pois a URL privada do feed ICS é mantida exclusivamente no Cloudflare Worker como `CALENDAR_ICS_URL`.

---

## 🔒 Política de Segurança e Privacidade

- **Nunca versione segredos:** Os arquivos sob `/app/StackChan/secrets/` e os arquivos de histórico em `/app/StackChan/memory/` contêm dados sensíveis e pessoais (chaves de API, senhas e histórico de conversa). Nunca faça commit desses arquivos no Git.
- **Redação na Web/API:** A interface Web e a API HTTP da Pix nunca retornam os segredos em texto claro; eles são reportados como `set` (configurado) ou `missing` (ausente).
- **Campos em branco na WebView:** Ao salvar configurações pela WebView, deixar os campos de senha/chave em branco preserva o valor já gravado no microSD, sem sobrescrevê-lo.
