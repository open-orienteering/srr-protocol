# Open questions

What is *not* known about SRR yet, roughly ordered by how much it would help
a receiver implementation. Contributions welcome — the most useful thing you
can do is run the reference receiver with `DUMP_RAW_PACKETS true`, note what
you punched with what, and open an issue with the hex dumps.

## Protocol

1. **Retransmission timing — answered for stations.** An un-ACKed punch is
   sent six times, blue/red/blue/red/blue/red at 0 / 34 / 237 / 279 / 357 /
   523 ms (protocol.md §6, SDR capture of both channels, two punches within
   ±2 ms of each other). Still open: whether SIACs use the same schedule,
   and whether the intervals are jittered when transmitters collide.

2. **Does the sequence stop immediately on ACK? — answered: yes.** With a
   receiver ACKing the first (blue) frame, the station's transmission
   counter (header byte 10) advances by exactly one per punch, so none of
   the remaining five transmissions is sent.

3. **Header bytes 8–13 — partly answered.** Byte 10 is a transmission
   counter and byte 11 a record counter (protocol.md §3), from one station.
   Still open: bytes 8–9 (`3F 03`) and 12–13 (`62 6A`), and whether SIAC
   frames use the same counters. Collect frames from different cards,
   stations and firmware versions and diff them.

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

13. **Exact ACK deadline — answered for one station.** Measured with a radio
    that schedules the ACK at a programmable time after the received sync
    word (protocol.md §6): the ACK must *start* 0.9 – 2.3 ms after the end of
    the punch; earlier and later ACKs are both ignored. Still open: whether
    SIACs have the same window, and whether it depends on frame length.
