# Open questions

What is *not* known about SRR yet, roughly ordered by how much it would help
a receiver implementation. Contributions welcome — the most useful thing you
can do is run the reference receiver with `DUMP_RAW_PACKETS true`, note what
you punched with what, and open an issue with the hex dumps.

## Protocol

1. **Retransmission timing.** It is known that an un-ACKed punch is sent
   six times, alternating blue/red/blue/red/blue/red, roughly 30 ms apart.
   Not known: the exact interval, whether it is constant, whether there is
   jitter (to avoid two transmitters colliding repeatedly), and whether
   stations and SIACs use the same schedule. To measure it: run with
   `ENABLE_ACK false`, `DUMP_RAW_PACKETS true`, timestamp each frame with
   `micros()`, and punch once on each channel.

2. **Does the sequence stop immediately on ACK?** An ACK silences the
   transmitter, but it has not been checked whether an ACK on the first
   (blue) transmission suppresses all five remaining ones, or whether the
   red counterpart is still sent.

3. **Header bytes 8–13.** Six bytes in every frame that have not been
   decoded. Collect frames from different cards, stations and firmware
   versions and diff them.

4. **Byte 19 in SIAC (`0xB7`) frames.** Observed `0x07` clear, `0x1A` check,
   `0x0B` start, `0x0D` finish, `0xB2` control. Hypothesis: the low nibble is
   the punched station's SPORTident operating mode, the upper bits are
   flags. If so, where is the **control number** of a control punch? Punch a
   SIAC at beacon controls with different codes and see which byte changes.

5. **Day-of-week / AM-PM byte.** The time fields are 12 h. The TD byte must
   be somewhere (byte 23 in `0xB6` and byte 20 or 21 in `0xB7` are the
   candidates). Punch just before and after noon.

6. **Longer station frames.** When a BSF8-SRR is set to "send all unsent
   records" or "send all card contents", does it pack several records into
   one frame (up to the 41-byte `PKTLEN`) or send several frames?

7. **ACK constants.** What are `7F 15 00 0D 3F 23` and `73 60`? Does the
   transmitter validate any of them, or only the echoed Link ID? Try
   corrupting one byte at a time and see whether the transmitter retries.

8. **ACK sequence byte.** The dongle increments it per ACK. Does anything
   depend on it?

9. **Other frame types.** Only `0xB6`, `0xB7` and the ACK have been seen. Do
   the SI-GSM/LTE modems, or stations talking to each other, use others?
   Does a SIAC send anything on SIAC-ON/SIAC-OFF, or a station on
   power-up?

10. **Direction of the Link ID in the ACK.** Assumed to be "address of the
    transmitter being acknowledged". Not tested whether a wrong ID is
    ignored.

## Radio

11. **Transmit power.** The dongle's `PATABLE` value was not captured. The
    SIAC and station output power are unknown (and presumably low, given the
    8 m range).

12. **Dongle GDO/interrupt configuration.** Not recorded; not needed for
    interoperability but would show how SPORTident's firmware paces the ACK.

13. **Exact ACK deadline.** "About 1 ms" is an observation from what did and
    did not work on the ESP32, not a measurement. A logic analyser on GDO0 of
    two radios (one transmitting, one ACKing) would settle it.
