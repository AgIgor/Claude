# Monitor Serial

Monitor serial leve para Windows, no estilo do Monitor Serial do Arduino IDE.
É um único `MonitorSerial.exe` (~60 KB) que usa o .NET Framework 4.x que já vem no Windows 10/11, sem instalação.

## Compilar

Execute `build.bat`. Ele usa o `csc.exe` do próprio Windows (`%WINDIR%\Microsoft.NET\Framework64\v4.0.30319`).

## Recursos

**Iguais ao Arduino**
- Escolha de porta e baud rate (300 a 2.000.000, ou digite qualquer valor e tecle Enter)
- Final de linha: sem final, NL, CR, NL & CR
- Autoscroll, timestamp, limpar saída, campo de envio (Enter envia)

**Adicionais**
- **Várias janelas** (botão *Nova janela* ou Ctrl+N), cada uma com sua porta. A lista marca `[em uso]` nas portas abertas em outra janela
- **Timestamp selecionável**: hora com ms, hora, data e hora, segundos desde a conexão, delta entre linhas
- Nome do dispositivo na lista de portas (ex.: `COM8 - USB-SERIAL CH340`), lista atualizada sozinha
- **Reconexão automática** quando o cabo USB é removido e recolocado
- Troca de baud, paridade e outros parâmetros sem desconectar
- Avançado: data bits, paridade, stop bits, controle de fluxo, codificação (UTF-8/ASCII/1252/Latin-1), limite de linhas, quebra de linha, remoção de cores ANSI (logs do ESP-IDF), sempre no topo, tema escuro
- DTR e RTS controláveis
- Exibição em texto ou hexadecimal
- Envio em HEX (`48 65 6C`, `0x01,0x02`, `A0B1C2`), escapes (`\n \r \t \0 \\ \xHH`), eco local colorido
- Histórico de comandos (setas para cima/baixo e lista), salvo entre sessões
- Repetir envio a cada N ms, enviar arquivo bruto
- Pausar sem perder dados, buscar (Ctrl+F / F3), salvar saída (Ctrl+S), gravar log contínuo em arquivo
- Contadores de RX/TX/linhas na barra de status
- Zoom da fonte com Ctrl + roda do mouse

## Atalhos

| Tecla | Ação |
|---|---|
| F5 | Conectar / desconectar |
| Ctrl+N | Nova janela |
| Ctrl+L | Limpar |
| Ctrl+S | Salvar saída |
| Ctrl+P | Pausar / continuar |
| Ctrl+F, F3 | Buscar / próxima ocorrência |
| ↑ / ↓ no campo de envio | Histórico |

## Linha de comando

```
MonitorSerial.exe COM3 COM4:115200
```
Abre uma janela por porta, já conectada (o baud depois de `:` é opcional).

## Configurações

Ficam em `%APPDATA%\MonitorSerial\` (`config.ini` e `historico.txt`).
Para modo portátil, crie um `config.ini` vazio ao lado do `.exe`.

## Observação

Assim como no Arduino IDE, feche o monitor (ou clique em Desconectar) antes de gravar um sketch na placa, porque a porta só pode ser aberta por um programa de cada vez.
A *reconexão automática* serve para quando o dispositivo some da lista (cabo removido, placa reiniciando com USB nativo). Depois de um upload, clique em Conectar ou tecle F5.
