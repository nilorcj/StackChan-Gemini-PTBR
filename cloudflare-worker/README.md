# Pix ToolGateway — Cloudflare Worker

Este diretório contém a implementação sanitizada e reprodutível do **Cloudflare Worker** utilizado como **ToolGateway** pela Pix (M5Stack CoreS3).

---

## 🎯 Papel do Worker

O Worker atua como um gateway seguro e leve entre a Pix e serviços externos que exigem processamento adicional ou acesso autenticado a APIs de terceiros.

A Pix realiza chamadas de ferramentas (*tool calling*) via HTTPS diretamente para este Worker utilizando autenticação criptográfica **HMAC-SHA256**. O Worker valida a integridade, o timestamp e a autenticidade da requisição antes de delegar a execução às ferramentas suportadas.

---

## 🔌 Endpoints Expostos

| Método | Endpoint | Autenticação | Descrição |
|---|---|---|---|
| `POST` | `/v2/call` | **HMAC-SHA256 Obrigatório** | Endpoint produtivo para execução de ferramentas (`web_search`, `get_agenda`, etc.). |
| `POST` | `/v2/echo` | **HMAC-SHA256 Obrigatório** | Endpoint de diagnóstico e validação do fluxo de assinatura HMAC. |
| Outros | `/*` | Nenhuma | Retorna HTTP 404 (`{"ok": false, "error": "not_found"}`). |

### Headers de Autenticação HMAC no `/v2/call`

Toda requisição ao `/v2/call` deve enviar:
- `X-Timestamp`: timestamp UNIX atual em segundos (formato de 10 dígitos). Janela de tolerância máxima: ±120 segundos.
- `X-Nonce`: string hexadecimal única de 32 caracteres (16 bytes aleatórios).
- `X-Signature`: assinatura HMAC-SHA256 (64 caracteres hexadecimais em minúsculas).
- `Content-Type`: `application/json`.
- Tamanho máximo do payload: 16 KB.

O cabeçalho `Authorization` legado é rejeitado com HTTP 400 (`authorization_not_allowed`).

---

## 🛠️ Ferramentas Suportadas

### 1. `web_search`
- **Provedor:** TinyFish API (`https://api.search.tinyfish.ai`)
- **Parâmetros:** `{"query": "..."}`
- Executa busca na web em tempo real e retorna os resultados formatados para o modelo Gemini.

### 2. `get_agenda` / `get_daily_agenda`
- **Provedor:** Google Calendar via URL privada de feed iCalendar (ICS)
- **Fuso Horário:** `America/Sao_Paulo`
- **Parâmetros aceitos:** `period` (`today`, `tomorrow`, `this_week`, `date`) e `limit`.
- Faz o parsing do feed ICS, processa regras de recorrência (`RRULE`, `EXDATE`, `RECURRENCE-ID`), expande eventos recorrentes e retorna apenas os compromissos relevantes do período solicitado.

> **Nota:** As ferramentas de Clima (`get_weather`) e Cotações (`get_currency`) são atendidas diretamente pelo firmware da Pix através de seus caches locais, desonerando o Worker.

---

## 🔐 Variáveis de Ambiente / Secrets Necessários

O Worker espera que os segredos sejam injetados via variáveis de ambiente da Cloudflare (Secrets):

| Nome da Variável | Tipo | Descrição |
|---|---|---|
| `HMAC_SHARED_KEY` | Secret | Chave secreta compartilhada para assinatura e validação HMAC-SHA256. |
| `TINYFISH_API_KEY` | Secret | Chave de API para o serviço TinyFish de busca na web. |
| `CALENDAR_ICS_URL` | Secret | URL privada e secreta do feed iCalendar (`.ics`) do Google Agenda. |

> [!IMPORTANT]
> **Sincronização de Chaves:** A chave configurada em `HMAC_SHARED_KEY` no Cloudflare Worker **deve ser exatamente a mesma** cadastrada na Pix através da aba **Configuração** da sua interface Web (WebView). Caso contrário, as requisições serão rejeitadas com HTTP 401 (`unauthorized`).

---

## 🚀 Como Fazer o Deploy

### Opção 1: Via Cloudflare Dashboard (Sem CLI)

1. Acesse o [Cloudflare Dashboard](https://dash.cloudflare.com/) e vá em **Workers & Pages**.
2. Clique em **Create application** > **Create Worker**.
3. Dê um nome ao Worker (ex.: `pix-tool-gateway`) e clique em **Deploy**.
4. Clique em **Edit code**, copie todo o conteúdo de `worker.js` e cole no editor.
5. Clique em **Deploy** no canto superior direito.
6. Vá na aba **Settings** > **Variables and Secrets**:
   - Adicione `HMAC_SHARED_KEY` (tipo **Secret**).
   - Adicione `TINYFISH_API_KEY` (tipo **Secret**).
   - Adicione `CALENDAR_ICS_URL` (tipo **Secret**).
7. Salve as variáveis.

### Opção 2: Via Wrangler CLI

1. Instale o Wrangler e faça login:
   ```bash
   npx wrangler login
   ```
2. Copie o arquivo de exemplo de configuração:
   ```bash
   cp wrangler.toml.example wrangler.toml
   ```
3. Defina os secrets no ambiente Cloudflare:
   ```bash
   npx wrangler secret put HMAC_SHARED_KEY
   npx wrangler secret put TINYFISH_API_KEY
   npx wrangler secret put CALENDAR_ICS_URL
   ```
4. Execute o deploy:
   ```bash
   npx wrangler deploy
   ```
