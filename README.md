# SPORTident SRR protocol

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Documentation of the **SPORTident Short Range Radio (SRR)** air interface —
the 2.4 GHz link that SIAC cards and BSF8-SRR stations use to send punches to
radio controls — together with a working open-source receiver for an ESP32
and a TI CC2500.

SPORTident does not publish the protocol. Everything here was obtained by
observing a genuine SRR USB dongle and validating against real punches; see
[how it was done](docs/methodology.md) and the
[legal considerations](docs/legal.md).

Part of [Open Orienteering](https://github.com/open-orienteering).

## In one paragraph

SRR is TI's stock **250 kBaud MSK** CC2500 configuration on two channels:
**red = 2461.5 MHz** and **blue = 2471.5 MHz**. Frames use the CC2500 packet
engine (4-byte preamble, sync `0xD391`, length byte, CRC-16). Every payload
starts with the ASCII magic `siok`, a 32-bit sender ID, and a type byte:
`0xB6` for punches relayed by a station, `0xB7` for punches sent by a SIAC.
The receiver must **acknowledge** each frame within about a millisecond by
echoing the sender ID inside a fixed 13-byte ACK, or the transmitter
retries. Transmitters send every punch on red and then, ~30 ms later, on
blue.

## Contents

| | |
|---|---|
| [`docs/protocol.md`](docs/protocol.md) | The protocol: physical layer, frame layout, punch types, ACK, timing |
| [`docs/radio-config.md`](docs/radio-config.md) | The CC2500 register set, decoded bit by bit, and how to port it |
| [`docs/open-questions.md`](docs/open-questions.md) | What is still unknown (retransmit schedule, undecoded bytes…) and how to find out |
| [`docs/methodology.md`](docs/methodology.md) | How the protocol was reverse engineered — with photos |
| [`docs/legal.md`](docs/legal.md) | Why this is lawful, and what to be careful about |
| [`arduino/esp32-cc2500-srr-receiver/`](arduino/esp32-cc2500-srr-receiver/) | Reference receiver sketch (ESP32 + CC2500 module) |
| [`tools/decode_srr.py`](tools/decode_srr.py) | Decode captured hex payloads on a PC |

## Quick start

You need an ESP32 dev board and a CC2500 breakout module (3.3 V!).

```
ESP32   CC2500
GPIO18  SCLK
GPIO19  SO (MISO)
GPIO16  SI (MOSI)
GPIO17  CSn
GPIO25  GDO0
GPIO26  GDO2
3V3     VCC
GND     GND
```

Open `arduino/esp32-cc2500-srr-receiver/esp32-cc2500-srr-receiver.ino` in
the Arduino IDE with the `esp32` core installed, select your board, flash,
and open the serial monitor at 115200 baud. Then punch a SIAC at a beacon
control configured to send radio, or punch a BSF8-SRR station:

```
--- ESP32 + CC2500 SPORTident SRR receiver ---
[RED] Listening...
[RED] PASSIVE | Station: 42 | Card: 2448200 | CN: 42 | Time: 10:50:16.226 | RSSI: -61
[RED] CHECK | LinkID: 8506707 | Card: 8506707 | Mode: 0x1A | Time: 10:51:03.117 | RSSI: -55
```

Options at the top of the sketch: `ENABLE_ACK` (acknowledge frames — turn
off to listen passively without silencing transmitters), `ENABLE_HOPPING`
(hop red↔blue when the channel is saturated), `DUMP_RAW_PACKETS` (print
every payload as hex, useful for [answering the open questions](docs/open-questions.md)),
`STARTUP_CHANNEL`.

Or via CLI:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 arduino/esp32-cc2500-srr-receiver
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 arduino/esp32-cc2500-srr-receiver
```

## Status

The receiver has been used to receive and acknowledge real punches from
SIACs and BSF8-SRR stations. Card number, control code and time are decoded
correctly. Several header bytes and the retransmission behaviour are not
yet understood — see [open questions](docs/open-questions.md). If you have
SRR hardware, the most useful contribution is hex dumps of frames together
with what produced them.

## Contributing

Issues and pull requests are welcome, in particular:

* raw frame captures (with `DUMP_RAW_PACKETS true`) and what was punched,
* ports of the receiver to other radios (nRF52 proprietary mode, CC2510,
  SDR),
* corrections to the documentation.

Please do not contribute anything derived from SPORTident firmware or
copyrighted documentation.

## License

[MIT](LICENSE) for the code. The documentation and photos in `docs/` may be
used under the same terms.

SPORTident, SIAC and AIR+ are trademarks of SPORTident GmbH. This project is
not affiliated with or endorsed by SPORTident.
