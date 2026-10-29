# Pix ToolGateway — Cloudflare Worker (StackChan Gemini PT-BR)

Este diretório contém a implementação sanitizada e reprodutível do **Cloudflare Worker** utilizado como **ToolGateway** pelo robô **StackChan Gemini PT-BR** (M5Stack CoreS3).

---

## 🎯 Papel do Worker

O Worker atua como um gateway seguro e leve entre o robô e serviços externos que exigem processamento adicional ou acesso autenticado a APIs de terceiros.

A Pix realiza chamadas de ferramentas (*tool calling*) via HTTPS diretamente para este Worker utilizando autenticação criptográfica **HMAC-SHA256**. O Worker valida a integridade, o timestamp e a autenticidade da requisição antes de delegar a execução às ferramentas suportadas.

---

## 🔌 Endpoints Expostos

| Método | Endpoint | Autenticação | Descrição |
|---|---|---|---|
| `POST` | `/v2/call` | **HMAC-SHA256 Obrigatório** | Endpoint produtivo para execução de ferramentas (`web_search`, `get_agenda`). |
| `POST` | `/v2/echo` | **HMAC-SHA256 Obrigatório** | Endpoint de diagnóstico e validação do fluxo de assinatura HMAC. |
| Outros | `/*` | Nenhuma | Retorna HTTP 404 (`{"ok": false, "error": "not_found"}`). |

### Headers de Autenticação HMAC no `/v2/call`

Toda requisição ao `/v2/call` deve enviar:
- `X-Timestamp`: timestamp UNIX atual em segundos (formato de 10 dígitos). Janela de tolerância máxima: ±120 segundos.
- `X-Nonce`: string hexadecimal única de 32 caracteres (16 bytes aleatórios).
- `X-Signature`: assinatura HMAC-SHA256 (64 caracteres hexadecimais em minúsculas).
- `Content-Type`: `application/json`.
- Tamanho máximo do payload: 16 KB.

O cabeçalho `Authorization` legado é expressamente rejeitado com HTTP 400 (`authorization_not_allowed`).

---

## 🛠️ Ferramentas Suportadas

### 1. `web_search`
- **Provedor:** TinyFish API (`https://api.search.tinyfish.ai`)
- **Parâmetros:** `{"query": "..."}`
- Executa busca na web em tempo real e retorna até 5 resultados formatados (título, URL, snippet, data e site) para o modelo Gemini.

### 2. `get_agenda` / `get_daily_agenda`
- **Provedor:** Google Calendar via URL privada de feed iCalendar (ICS)
- **Fuso Horário:** `America/Sao_Paulo`
- **Parâmetros aceitos:**
  - `period`: `today` (ou `hoje`), `tomorrow` (ou `amanha`/`amanhã`), e `upcoming` (padrão).
  - `limit`: quantidade máxima de eventos a retornar (inteiro entre 1 e 20; padrão: 20).
- **Comportamento dos períodos:**
  - `today`: filtra eventos que ocorrem hoje no fuso de São Paulo.
  - `tomorrow`: filtra eventos que ocorrem amanhã no fuso de São Paulo.
  - `upcoming`: se o período for omitido ou qualquer outro valor for passado, o Worker assume `upcoming` por padrão, retornando os eventos de hoje, de amanhã e os próximos compromissos identificados por dia.
- Faz o parsing do feed ICS, processa regras de recorrência (`RRULE`, `EXDATE`, `RECURRENCE-ID`), expande eventos recorrentes e retorna apenas os compromissos relevantes do período solicitado, otimizado para calendários extensos.

> **Nota:** As ferramentas de Clima (`get_weather`) e Cotações (`get_currency`) são atendidas diretamente pelo firmware da Pix através de seus caches locais, desonerando o Worker.

---

## 🔐 Variáveis de Ambiente / Secrets Necessários

O Worker espera que os segredos sejam injetados via variáveis de ambiente secretas da Cloudflare (Secrets):

| Nome da Variável | Tipo | Descrição |
|---|---|---|
| `HMAC_SHARED_KEY` | Secret | Chave secreta compartilhada para assinatura e validação HMAC-SHA256 (64 caracteres hexadecimais). |
| `TINYFISH_API_KEY` | Secret | Chave de API para o serviço TinyFish de busca na web. |
| `CALENDAR_ICS_URL` | Secret | URL privada e secreta (`https://...`) do feed iCalendar (`.ics`) do Google Agenda. |

> [!IMPORTANT]
> **Sincronização de Chaves:** A chave configurada em `HMAC_SHARED_KEY` no Cloudflare Worker **deve ser exatamente a mesma** cadastrada na Pix através da aba **Configuração** da sua interface Web (WebView) no cartão **3. ToolGateway**. Caso contrário, as requisições serão rejeitadas com HTTP 401 (`unauthorized`).

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
5. Copie a URL gerada (ex.: `https://pix-tool-gateway.<seu-subdominio>.workers.dev`) e configure-a na interface Web da Pix no campo **URL Base do ToolGateway**.
