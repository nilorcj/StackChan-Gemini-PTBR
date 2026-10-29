# Compilação e Gravação de Firmware — StackChan Gemini PT-BR

Este guia detalha o processo de obtenção, compilação e gravação do firmware no **M5Stack CoreS3** para o projeto **StackChan Gemini PT-BR**.

---

## 🚀 Método A: Instalação do Firmware Pré-Compilado (GitHub Release)

> [!NOTE]
> **Disponibilidade e Visibilidade da Release:** A release oficial `v1.0.0` está em fase final de preparação documental e não está disponível para download antes de sua publicação formal na aba [Releases do repositório](https://github.com/nilorcj/StackChan-Gemini-PTBR/releases). Adicionalmente, enquanto o repositório permanecer em modo privado, os binários e páginas de release só serão visíveis para usuários autenticados com permissão explícita de acesso ao projeto.

### Arquivos de Flash no ESP32-S3

O microcontrolador ESP32-S3 do CoreS3 utiliza um esquema de particionamento de 16 MB. Para uma gravação completa e limpa a partir de binários pré-compilados, são necessários os seguintes componentes nos respectivos endereços de memória flash:

| Arquivo | Endereço (Offset) | Descrição |
|---|---|---|
| `bootloader.bin` | `0x0000` | Carregador de inicialização do ESP32-S3 (2º estágio) |
| `partitions.bin` | `0x8000` | Tabela de partições configurada para 16 MB |
| `boot_app0.bin` | `0xe000` | Seletor de partição de boot ativo (padrão Arduino ESP32) |
| `firmware.bin` | `0x10000` | Imagem da aplicação compilada do StackChan Gemini PT-BR |

> [!WARNING]
> **Atenção:** Em um dispositivo virgem ou gravado anteriormente com outro firmware/sistema operacional, gravar **apenas** o `firmware.bin` no endereço `0x10000` resultará em falha de inicialização, pois a placa não encontrará a tabela de partições e o bootloader compatíveis. Quando o pacote da release for disponibilizado, grave os 4 arquivos em conjunto nos offsets indicados.

### Gravação via esptool

O utilitário oficial `esptool.py` pode ser instalado via Python (`pip install esptool`).

#### Comando em Linha Única (compatível com Windows, Linux e macOS):
```bash
esptool.py --chip esp32s3 --port PORTA_SERIAL --baud 115200 --before default_reset --after hard_reset write_flash -z 0x0000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```
*(Substitua `PORTA_SERIAL` pela porta real do CoreS3: ex.: `COM3` no Windows ou `/dev/ttyACM0` no Linux).*

#### Exemplos com quebra de linha por sistema operacional:

**No Windows (PowerShell):**
```powershell
esptool.py --chip esp32s3 --port COM3 --baud 115200 --before default_reset --after hard_reset write_flash -z `
  0x0000 bootloader.bin `
  0x8000 partitions.bin `
  0xe000 boot_app0.bin `
  0x10000 firmware.bin
```

**No Windows (Prompt de Comando / CMD):**
```cmd
esptool.py --chip esp32s3 --port COM3 --baud 115200 --before default_reset --after hard_reset write_flash -z ^
  0x0000 bootloader.bin ^
  0x8000 partitions.bin ^
  0xe000 boot_app0.bin ^
  0x10000 firmware.bin
```

**No Linux / macOS (Bash / Zsh):**
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 115200 --before default_reset --after hard_reset write_flash -z \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin
```

---

## 🛠️ Método B: Compilação Manual com PlatformIO

Recomendado para desenvolvedores e usuários que desejam compilar o código-fonte a partir do zero ou customizar o comportamento.

### 1. Pré-requisitos

- Python 3.9 ou superior.
- [PlatformIO Core (CLI)](https://platformio.org/) ou extensão PlatformIO no Visual Studio Code.
- Cabo USB-C com suporte a dados conectado ao M5Stack CoreS3.
- Git.

Para instalar o PlatformIO Core via terminal:
```bash
python3 -m pip install -U platformio
```

### 2. Clonar o repositório oficial

```bash
git clone https://github.com/nilorcj/StackChan-Gemini-PTBR.git
cd StackChan-Gemini-PTBR
```

### 3. Compilar o firmware

Execute a compilação direcionada para o ambiente do CoreS3:
```bash
pio run -e m5stack-cores3
```

O PlatformIO baixará automaticamente as dependências de biblioteca e gerará os artefatos em:
```text
.pio/build/m5stack-cores3/firmware.bin
.pio/build/m5stack-cores3/bootloader.bin
.pio/build/m5stack-cores3/partitions.bin
```

### 4. Gravar no CoreS3 via USB

Conecte o CoreS3 ao computador via cabo USB-C.

**No Linux/macOS:**
```bash
pio run -e m5stack-cores3 -t upload --upload-port /dev/ttyACM0
```
*(No Linux, certifique-se de que seu usuário pertença ao grupo `dialout` ou configure regras udev apropriadas).*

**No Windows (PowerShell / CMD):**
```powershell
pio run -e m5stack-cores3 -t upload --upload-port COM3
```
*(Substitua `COM3` pela porta COM correspondente ao CoreS3).*

O PlatformIO grava automaticamente o bootloader, tabela de partições e aplicação nos offsets corretos.

### 5. Monitorar a Inicialização (Serial Monitor)

Para visualizar as mensagens de boot e diagnóstico da Pix:
```bash
pio device monitor -b 115200
```

Mensagens essenciais confirmadas no boot:
- `SD status: ok`
- `ConfigManager init: ok`
- `UnitPaHub: PCA9548AP/TCA9548A auto-detected at 0x77` (se PaHub estiver conectado)
- `WiFi: connected during boot screen! ip=192.168.X.X`
- `Gemini: ready`

---

## ⚙️ Primeira Configuração

Após o upload do firmware, certifique-se de ter inserido um cartão MicroSD formatado em **FAT32** no CoreS3. A configuração pode ser realizada por qualquer uma das duas abordagens:

### Opção 1: Configuração Guiada pela WebView (Modo SoftAP)
1. Insira o MicroSD formatado no robô e ligue-o.
2. Como não há rede cadastrada, após 15 segundos na tela de boot a Pix criará sua própria rede Wi-Fi de configuração:
   - **SSID:** `<robot_id>-setup-XXXX` (por exemplo: `stackchan-setup-XXXX` ou `pix-setup-XXXX`, onde `XXXX` são os 4 caracteres hexadecimais finais do MAC da placa);
   - **Senha:** `stackchanXXXX` (palavra `stackchan` em minúsculas seguida dos mesmos 4 caracteres em maiúsculas).
3. Conecte seu celular ou computador a essa rede e acesse:
   ```text
   http://192.168.4.1/
   ```
4. Na aba **Configuração**, preencha o SSID e Senha da sua rede Wi-Fi local (2.4 GHz), sua **Gemini API Key** e clique no botão **Salvar e reiniciar**.
5. O firmware grava as configurações no MicroSD e reinicia já conectando ao seu roteador.

### Opção 2: Preparação Manual dos Arquivos no PC
Consulte o guia [docs/SD_LAYOUT.md](SD_LAYOUT.md) para detalhes de como criar previamente os arquivos `/app/StackChan/config/runtime.json` e `/app/StackChan/secrets/` no cartão MicroSD antes de inseri-lo no CoreS3.
