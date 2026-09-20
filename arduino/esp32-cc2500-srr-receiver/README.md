# esp32-cc2500-srr-receiver

Reference SRR receiver: ESP32 + TI CC2500 module. Receives, acknowledges and
prints SPORTident SRR punches. No libraries beyond the ESP32 core's `SPI.h`.

Wiring, options and expected output are in the [top-level README](../../README.md);
the protocol is in [`docs/protocol.md`](../../docs/protocol.md).

## Structure

| Section | What it does |
|---------|--------------|
| `CONFIGURATION` | Feature switches, pins, channel constants |
| `CC2500 REGISTER MAP` | Register/strobe addresses used |
| `RADIO CONFIGURATION` | `RADIO_CONFIG[]` — the register set captured from the dongle, one comment per register |
| `SRR PACKET LAYOUT` | Named offsets into the payload; ACK constants |
| `SPI / CC2500 PRIMITIVES` | `cc2500Strobe/WriteReg/ReadReg`, channel switching |
| `ACK` | `sendAck()` — must run before anything else after a frame |
| `DECODER` | `decodePacket()`, SPORTident time formatting |
| `PACKET RECEPTION` | `handlePacketRx()` — the RX → CRC check → ACK → decode sequence |
| `NOISE POLLING / CHANNEL HOPPING` | Carrier-sense duty cycle measurement, red↔blue hopping |

## Design notes

* **Polling, not interrupts.** The ACK deadline is roughly 1 ms after the end
  of the incoming frame. Interrupt handlers on the same core disturbed SPI
  timing enough to miss it, so `loop()` polls GDO0 and blocks while a frame is
  being received and acknowledged.
* **ACK before decode.** `handlePacketRx()` sends the ACK straight after the
  CRC_OK check; parsing and `Serial` output come afterwards.
* **No heap use in the hot path.** Output uses `Serial.printf` with stack
  buffers; there is no `String`.
* **Wider RX filter than the dongle.** `MDMCFG4 = 0x0D` (812 kHz) instead of
  `0x2D` (541 kHz) to tolerate the crystal drift of cheap modules. See
  [`docs/radio-config.md`](../../docs/radio-config.md).

## Pin choice

The pins (18/19/16/17 and 25/26) are not the ESP32's default VSPI set. They
are simply what the board used during development had on its two 4-pin
connectors. Change the `*_PIN` defines freely; `SPI.begin()` is called with
explicit pins.
