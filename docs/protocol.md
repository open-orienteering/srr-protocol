# SPORTident SRR — protocol description

This is a description of the SPORTident Short Range Radio (SRR) link as
observed with a genuine SRR USB dongle and re-implemented on an ESP32 with a
TI CC2500. Everything here was derived by observation; nothing comes from
SPORTident documentation or firmware. Where something is a guess it is
marked as such. See [open-questions.md](open-questions.md) for what is still
unknown and [methodology.md](methodology.md) for how the information was
obtained.

Confidence legend used below:

| Mark | Meaning |
|------|---------|
| ✅ | Verified: used by the reference receiver and confirmed against real punches |
| 🟡 | Observed but only partially understood, or based on a small number of samples |
| ❓ | Hypothesis |

## 1. What SRR is

SRR is the 2.4 GHz radio used by SPORTident to get punch records off the
course in near real time. Transmitters are the **SIAC** (active card) and the
**BSF8-SRR** station; receivers are the **SRR USB dongle**, the SI-GSM/LTE
modems, and now this project. SPORTident's own public description
([docs.sportident.com](https://docs.sportident.com/user-guide/short-range-radio))
gives the range (6–8 m), the existence of two channels called *red* and
*blue*, that transmitters use both channels while a dongle listens on one,
and that a SIAC only transmits when it is active, has punched an AIR+ station
and that station has asked for a radio transmission. Nothing about the air
interface is published. The rest of this document fills that gap.

## 2. Physical layer ✅

The dongle contains an unmodified TI **CC2500** transceiver, so the physical
layer is exactly what that chip does with the register set captured on its
SPI bus ([radio-config.md](radio-config.md)). In plain terms:

| Parameter | Value | Derived from |
|-----------|-------|--------------|
| Band | 2.4 GHz ISM | — |
| Base frequency | 2 424.9999 MHz | `FREQ = 0x5D44EC`, f_xosc = 26 MHz |
| Channel spacing | 249.94 kHz | `MDMCFG1/0` |
| **Red channel** | **2 461.49 MHz** | `CHANNR = 0x92` (146) |
| **Blue channel** | **2 471.49 MHz** | `CHANNR = 0xBA` (186) |
| Modulation | MSK | `MDMCFG2 = 0x73` |
| Data rate | 249.94 kbit/s (nominal 250 kBaud) | `MDMCFG4/3 = 0x?D / 0x3B` |
| Symbol time | 4.0 µs | |
| RX filter bandwidth (dongle) | 541.7 kHz | `MDMCFG4 = 0x2D` |
| IF | 177.7 kHz | `FSCTRL1 = 0x07` |
| Preamble | 4 bytes (`0xAA…`) | `MDMCFG1` |
| Sync word | `0xD391`, 30 of 32 bits must match | `SYNC1/0`, `MDMCFG2` |
| Manchester / FEC / whitening | none | `MDMCFG2`, `MDMCFG1`, `PKTCTRL0` |
| Length mode | Variable; first byte after sync is payload length | `PKTCTRL0 = 0x05` |
| CRC | CC2500 hardware CRC-16 (polynomial x¹⁶+x¹⁵+x²+1 = `0x8005`, init `0xFFFF`) over length + payload | `PKTCTRL0` |
| Max payload length | 41 bytes | `PKTLEN = 0x29` |
| Address filtering | none | `PKTCTRL1` |

These are TI SmartRF Studio's stock "250 kBaud, MSK" settings with a
customised base frequency, so an SRR frame on the air looks like this:

```
| preamble 4 B | sync D3 91 | LEN | payload (LEN bytes) | CRC16 (2 B) |
```

An earlier iteration of this work described the link as 38.4 kbit/s. That
was wrong; the register values give 250 kBaud unambiguously.

The red and blue channels sit 10 MHz apart, and both coincide with Wi-Fi
channel centres (channel 11 at 2 462 MHz and channel 13 at 2 472 MHz). Nearby
Wi-Fi traffic is therefore the main source of the "saturated channel"
condition that the reference receiver hops away from.

### Which channel do I listen on?

Transmitters send every punch on **both** channels, one after the other (see
§6). A single receiver on either channel therefore sees every punch, as long
as that channel is not jammed. The reference receiver hops to the other
channel when its current channel is saturated with noise. A fixed
installation is better served by two receivers, one per channel — which is
also SPORTident's own recommendation for dongles.

## 3. Frame payload — common header ✅

All SRR payloads start with the same 15-byte header. Offsets are into the
payload, i.e. byte 0 is the first byte after the length byte.

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0–3 | 4 | Magic | ASCII `siok` (`73 69 6F 6B`) — present in every frame |
| 4–7 | 4 | Link ID (big-endian) | Identity of the **sender**: station number for station frames, card number for SIAC frames. This value is echoed in the ACK. |
| 8–13 | 6 | ❓ unknown | Not decoded. Candidates: firmware/protocol version, record counters, flags. |
| 14 | 1 | Frame type | `0xB6` = station-originated punch, `0xB7` = SIAC-originated punch. Other values have not been observed, but the ACK sent by receivers is a different frame (see §5). |

## 4. Punch frames

### 4.1 Type `0xB6` — punch relayed by a BSF8-SRR station ✅

Sent by a station in SRR mode when a card punches it (classic contact punch
or, for non-SIAC cards, AIR+ read). SPORTident calls this "passive" from the
card's point of view. Observed payload length is 27 bytes or more.

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0–14 | 15 | Header | Link ID = **station number** |
| 15–18 | 4 | ❓ unknown | |
| 19 | 1 | CN | Control code of the station ✅ |
| 20–22 | 3 | Card number (big-endian) | 24 bits — enough for all card ranges up to 16 777 215 ✅ |
| 23 | 1 | 🟡 probably TD | SPORTident "date" byte: day of week and 12 h half-day bit. Not decoded. |
| 24–25 | 2 | Time, seconds (big-endian) | Seconds since 12:00 or 00:00 (12 h format, see §7) ✅ |
| 26 | 1 | Time, sub-seconds | 1/256 s ✅ |
| 27… | | ❓ | Possibly further records when the station is configured to "send all unsent records" |

### 4.2 Type `0xB7` — punch sent by a SIAC ✅

Sent by the SIAC itself after an AIR+ (contactless) punch at a beacon
station, when that station is configured to request radio transmission.
Observed payload length is 25 bytes or more.

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0–14 | 15 | Header | Link ID = **card number** |
| 15–18 | 4 | Card number (big-endian) | Same value as the Link ID ✅ |
| 19 | 1 | Station mode / code byte | See below 🟡 |
| 20–21 | 2 | ❓ unknown | One of these is probably the TD byte |
| 22–23 | 2 | Time, seconds (big-endian) | 12 h format ✅ |
| 24 | 1 | Time, sub-seconds | 1/256 s ✅ |

Values seen in byte 19 and the punch they accompanied:

| Value | Punch |
|-------|-------|
| `0x07` | Clear |
| `0x1A` | Check |
| `0x0B` | Start |
| `0x0D` | Finish |
| `0xB2` | Control |

❓ The low nibbles line up suspiciously well with SPORTident station
*operating mode* codes (`0x02` control, `0x07` clear, `0x0A` check, `0x0B`/
`0x0D` are among the start/finish variants), which suggests this byte is the
mode byte of the station that was punched, with flags in the upper bits.
For a control punch the actual control number (CN) is therefore probably
somewhere else in the frame — possibly bytes 20–21 or in the undecoded
header bytes 8–13. The reference receiver currently prints this byte raw.

## 5. Acknowledgement frame ✅

Transmitters expect an ACK. Without one they retransmit (§6), and the
station/SIAC keeps the record marked as unsent. A receiver that ACKs
correctly silences the transmitter after one frame, which is also how you
know your ACK is right.

The ACK is a 13-byte payload (14 bytes on air including the length byte):

```
offset  0   1   2   3   4   5   6   7   8   9   10  11  12
        [ Link ID (echoed) ] 7F  15  00  0D  3F  23  SEQ 73  60
```

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0–3 | 4 | Link ID | Bytes 4–7 of the frame being acknowledged, verbatim |
| 4–9 | 6 | `7F 15 00 0D 3F 23` | Constant in every ACK observed from the dongle. Meaning unknown ❓ |
| 10 | 1 | Sequence | Incremented by the receiver for each ACK sent. Any value is accepted; the counter is kept only to mirror the dongle's behaviour 🟡 |
| 11–12 | 2 | `73 60` | Constant. `0x73` is also the first byte of `siok`, which may or may not be a coincidence ❓ |

There is no `siok` magic in the ACK and the transmitter does not seem to
check anything except (presumably) the echoed Link ID.

Air time of the ACK is about 0.7 ms (4 + 2 + 1 + 13 + 2 bytes at 250 kBaud),
which is why the ACK deadline in §6 is tight.

## 6. Timing and retransmission behaviour 🟡

These numbers come from watching transmitters with the reference receiver,
not from any specification, and should be treated as approximate.

* **ACK window ≈ 1 ms.** After the last bit of a punch frame the transmitter
  waits roughly a millisecond for the ACK, then gives up and moves on. In
  practice the receiver has to have the ACK in the CC2500 TX FIFO and
  strobed within a few hundred microseconds of the end-of-packet signal.
  Parsing, printing, or anything else has to wait until after the ACK.
* **Six transmissions, alternating channels.** ✅ Without an ACK, a punch
  is transmitted six times, alternating between the channels: blue, red,
  blue, red, blue, red — three attempts per channel. An ACK stops the
  sequence. Consecutive transmissions are roughly 30 ms apart; the exact
  spacing, and whether it is constant across the six, has not been
  measured precisely (see [open-questions.md](open-questions.md)).
  Consequences for a receiver: a single-channel receiver gets up to three
  chances per punch, and the whole burst is over in well under a second.
* **Unsent queue.** Records that were never ACKed are kept. SPORTident's
  station configuration offers "send last record" / "send all unsent
  records" / "send all card contents", so at least stations retry old
  records at the next punch when configured to. Whether a SIAC retries an
  old record later on its own has not been observed.
* **Station vs SIAC collision.** SPORTident notes that when an SI-Card is
  punched directly and a SIAC punches contactlessly at the same station at
  the same time, the station and the SIAC transmit simultaneously and one of
  them may be lost. This is consistent with there being no carrier sense on
  the transmit side (the dongle's `MCSM1` has CCA disabled).

## 7. SPORTident time format ✅

The time fields are the classic SPORTident representation:

* 16-bit **seconds** since the start of the current 12-hour half-day
  (0–43 199).
* 8-bit **sub-seconds** in 1/256 s.
* A separate "TD" byte holds the day of week and the AM/PM bit. Its
  position in the SRR frames is not confirmed (§4), so the reference
  receiver prints 12 h times.

`hh:mm:ss.mmm = seconds/3600 : (seconds%3600)/60 : seconds%60 . subsec*1000/256`

## 8. Receiver behaviour (as implemented)

The reference receiver in [`arduino/esp32-cc2500-srr-receiver`](../arduino/esp32-cc2500-srr-receiver/)
does the following; anything ported to another radio should do the same.

1. Configure the radio with the register set in [radio-config.md](radio-config.md),
   sit in RX on one channel.
2. On sync word (GDO0 high) wait for end of packet (GDO0 low), read the RX
   FIFO, check the CRC_OK bit appended to the packet.
3. Immediately build and transmit the ACK, echoing payload bytes 4–7.
4. Return to RX (the CC2500 does this by itself with `MCSM1 = 0x3F`).
5. Only then parse and report the frame.
6. Optionally: measure the fraction of time carrier sense is asserted and
   hop to the other channel if the current one is saturated.

## 9. Relation to the dongle's serial output

The SRR dongle presents itself to the PC as a CP2102N USB-serial device and
emits records in SPORTident's serial protocol ("basic" or "AIR+" format,
selectable in Config+). That serial protocol is *not* the subject of this
repository — it is a different layer, and it is what event software
normally consumes. What this repository documents is the radio link the
dongle listens to.
