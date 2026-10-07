# CC2500 register configuration

The SRR dongle programs its CC2500 with the register values below. They were
captured on the dongle's SPI bus (MSP430F2370 → CC2500) with a logic
analyser; see [methodology.md](methodology.md). The one deliberate deviation
in the reference receiver is `MDMCFG4` (RX filter bandwidth), explained at
the end.

All formulas use the CC2500's 26 MHz crystal (`f_xosc`). Register semantics
follow the [CC2500 datasheet](https://www.ti.com/lit/ds/symlink/cc2500.pdf).

## Register table

| Register | Addr | Dongle | Receiver | Decoded |
|----------|------|--------|----------|---------|
| `IOCFG2` | 0x00 | — | 0x0E | GDO2 = carrier sense (RSSI above threshold). Used by the receiver for noise measurement; the dongle's GDO configuration was not relevant to the protocol and is not reproduced. |
| `IOCFG0` | 0x02 | — | 0x06 | GDO0 asserts on sync word, de-asserts at end of packet (or on CRC-fail flush). |
| `SYNC1` | 0x04 | 0xD3 | 0xD3 | Sync word high byte |
| `SYNC0` | 0x05 | 0x91 | 0x91 | Sync word low byte → `0xD391` (TI default) |
| `PKTLEN` | 0x06 | 0x29 | 0x29 | Max payload 41 bytes (variable-length mode) |
| `PKTCTRL1` | 0x07 | 0x0C | 0x0C | `APPEND_STATUS=1` (RSSI, LQI/CRC_OK appended to RX FIFO), `CRC_AUTOFLUSH=1`, no address check |
| `PKTCTRL0` | 0x08 | 0x05 | 0x05 | Normal FIFO mode, `CRC_EN=1`, `LENGTH_CONFIG=01` (variable, first byte = length), no whitening |
| `CHANNR` | 0x0A | 0x92 / 0xBA | 0x92 / 0xBA | Channel 146 (red) / 186 (blue) |
| `FSCTRL1` | 0x0B | 0x07 | 0x07 | IF = f_xosc / 2¹⁰ · 7 = 177.7 kHz |
| `FSCTRL0` | 0x0C | 0x00 | 0x00 | No frequency offset |
| `FREQ2` | 0x0D | 0x5D | 0x5D | `FREQ[23:0] = 0x5D44EC = 6 112 492` |
| `FREQ1` | 0x0E | 0x44 | 0x44 | f_carrier = f_xosc / 2¹⁶ · FREQ = **2 424.9999 MHz** |
| `FREQ0` | 0x0F | 0xEC | 0xEC | |
| `MDMCFG4` | 0x10 | **0x2D** | **0x0D** | `CHANBW_E:CHANBW_M` = 0:2 → 541.7 kHz (dongle) / 0:0 → 812.5 kHz (receiver). `DRATE_E = 13` in both. |
| `MDMCFG3` | 0x11 | 0x3B | 0x3B | `DRATE_M = 59` → R = (256+59)·2¹³ / 2²⁸ · f_xosc = **249.94 kbit/s** |
| `MDMCFG2` | 0x12 | 0x73 | 0x73 | `DEM_DCFILT_OFF=0` (DC filter on), `MOD_FORMAT=111` (**MSK**), `MANCHESTER_EN=0`, `SYNC_MODE=011` (30/32 sync bits) |
| `MDMCFG1` | 0x13 | 0x23 | 0x23 | `FEC_EN=0`, `NUM_PREAMBLE=010` (4 bytes), `CHANSPC_E=3` |
| `MDMCFG0` | 0x14 | 0x3B | 0x3B | `CHANSPC_M=59` → Δf = f_xosc / 2¹⁸ · (256+59) · 2³ = **249.9 kHz** |
| `DEVIATN` | 0x15 | 0x01 | 0x01 | For MSK this is not a deviation: `DEVIATION_M` sets the fraction of a symbol period used for the phase transition. MSK deviation is fixed at R/4 ≈ 62.5 kHz. |
| `MCSM1` | 0x17 | 0x3F | 0x3F | `CCA_MODE=11` (always clear — no listen-before-talk), `RXOFF_MODE=11` (stay in RX), `TXOFF_MODE=11` (go to RX after TX) |
| `MCSM0` | 0x18 | 0x18 | 0x18 | `FS_AUTOCAL=01` (calibrate on IDLE→RX/TX), `PO_TIMEOUT=10` |
| `FOCCFG` | 0x19 | 0x1D | 0x1D | Frequency offset compensation: gain 4K before sync, K after, limit ±BW/4 |
| `BSCFG` | 0x1A | 0x1C | 0x1C | Bit synchronisation loop gains, no data-rate offset saturation |
| `AGCCTRL2` | 0x1B | 0xC7 | 0xC7 | Max LNA+LNA2 gain, target amplitude 42 dB |
| `AGCCTRL1` | 0x1C | 0x00 | 0x00 | Carrier sense: absolute threshold at `MAGN_TARGET`, relative disabled |
| `AGCCTRL0` | 0x1D | 0xB0 | 0xB0 | Medium hysteresis, 32-sample wait, 8-sample filter |
| `FREND1` | 0x21 | 0xB6 | 0xB6 | RX front-end currents |
| `FREND0` | 0x22 | 0x10 | 0x10 | `PA_POWER=0` (uses `PATABLE[0]`) |
| `FSCAL3` | 0x23 | 0xEA | 0xEA | Synthesizer calibration constants |
| `FSCAL2` | 0x24 | 0x0A | 0x0A | |
| `FSCAL1` | 0x25 | 0x00 | 0x00 | |
| `FSCAL0` | 0x26 | 0x11 | 0x11 | |

Registers not listed (`FSTEST`, `TEST2..0`, `PATABLE`, `IOCFG1`, `FIFOTHR`,
`ADDR`, `WORCTRL` etc.) are left at their reset values in the reference
receiver. The dongle's `PATABLE` (output power) was not recorded; the
receiver uses the reset default, which is plenty for the ACK at a few
metres.

## Where these values come from

Apart from the base frequency, this is byte-for-byte the "250 kBaud, MSK"
preset that TI's SmartRF Studio generates for the CC2500 (`MDMCFG4=0x2D,
MDMCFG3=0x3B, MDMCFG2=0x73, MDMCFG1=0x23, MDMCFG0=0x3B, DEVIATN=0x01,
FOCCFG=0x1D, BSCFG=0x1C, AGCCTRL2=0xC7, AGCCTRL1=0x00, AGCCTRL0=0xB0,
FREND1=0xB6, FREND0=0x10, FSCAL3=0xEA, FSCAL2=0x0A, FSCAL1=0x00,
FSCAL0=0x11`). SPORTident evidently took the vendor preset and only picked
the base frequency and channel numbers. Knowing this is useful in two ways:

* It gives confidence that the SPI capture was decoded correctly — a random
  decoding error would not land on a well-known preset.
* It means any radio that can be configured for "250 kBaud MSK, 4-byte
  preamble, 16-bit sync `0xD391`, variable length, CRC-16" can talk SRR,
  including other TI chips (CC1101 family cannot — wrong band — but CC2510,
  CC2511 and CC2500 can) and, with more effort, nRF52-class SoCs in
  proprietary radio mode or an SDR. Confirmed on air (protocol.md §2): the
  TI CC2340R5 with its stock `msk_250_kbps` PHY receives punches and gets
  its ACKs accepted; the only radio-specific details are the symbol map
  (`1` = lower tone) and the ACK timing (§6).

## The bandwidth change (`MDMCFG4`)

The cheap CC2500 breakout modules used for the receiver have poor crystals
that drift. With the dongle's 541 kHz filter, reception was unreliable.
Opening the filter to 812.5 kHz (`CHANBW_M = 0`) fixed it. The cost is a
few dB of sensitivity (more noise in the filter) which at SRR's intended
6–8 m range does not matter.

If you use a module with a decent 26 MHz crystal (±10 ppm), `0x2D` should
work and give a bit more range. Alternatively, measure the offset with
`FREQEST` and correct it via `FSCTRL0`.

## Verifying your own configuration

The quickest check that the radio is configured correctly is the ACK:

1. Hold a SIAC (switched on with a CHECK or SIAC-ON station) near a beacon
   control set to send radio, or punch a BSF8-SRR station.
2. Without a working ACK you will see the same punch several times as the
   transmitter retries.
3. With a working ACK you see it exactly once per channel (or once, if you
   hop).
