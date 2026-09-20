/*
 * ESP32 + CC2500 SPORTident SRR receiver
 * ======================================
 *
 * Receives (and acknowledges) SPORTident Short Range Radio (SRR) punch
 * packets sent by SIAC cards and BSF8-SRR stations, and prints them on the
 * serial port.
 *
 * Protocol documentation lives in ../../docs/ — start with protocol.md.
 *
 * Hardware
 * --------
 *   MCU:   ESP32 (any dev board; uses VSPI on non-default pins, see below)
 *   Radio: TI CC2500 module (the cheap 2.4 GHz "CC2500 breakout" boards)
 *
 *   ESP32   CC2500
 *   GPIO18  SCLK
 *   GPIO19  SO  (MISO)
 *   GPIO16  SI  (MOSI)
 *   GPIO17  CSn
 *   GPIO25  GDO0   (sync word detected / end of packet)
 *   GPIO26  GDO2   (carrier sense)
 *   3V3     VCC    (the CC2500 is a 3.3 V part — do NOT feed it 5 V)
 *   GND     GND
 *
 * Key points (details in docs/protocol.md and docs/radio-config.md)
 * -----------------------------------------------------------------
 *  1. ACK deadline. A SIAC/station only waits about a millisecond for an
 *     ACK before retransmitting. The ACK is therefore sent straight after
 *     the CRC check, before any parsing or printing.
 *  2. No interrupts on the radio path. ISRs on the same core disturbed the
 *     SPI timing enough to miss the ACK window, so everything is polled.
 *  3. Wide RX filter. Cheap CC2500 modules drift; 812 kHz filter bandwidth
 *     (MDMCFG4 = 0x0D) instead of the dongle's 541 kHz (0x2D) made
 *     reception reliable at the cost of some sensitivity.
 *  4. Channel hopping. Without an ACK a punch is sent up to six times,
 *     alternating blue/red/blue/red/blue/red ~30 ms apart, so a receiver on
 *     either channel gets three chances. If the current channel is saturated
 *     with noise the receiver hops to the other one. For a fixed
 *     installation, use two receivers instead.
 */

#include <SPI.h>

// =============================================================================
//   CONFIGURATION
// =============================================================================
#define ENABLE_ACK        true    // Acknowledge packets so transmitters stop retrying
#define ENABLE_HOPPING    true    // Hop red<->blue when the channel is saturated
#define DUMP_RAW_PACKETS  false   // Also print every payload as hex (research aid)
#define STARTUP_CHANNEL   CHAN_RED

#define NOISE_LIMIT_MS    200     // Hop if carrier sense is high > 200 ms per second
#define CHECK_INTERVAL_MS 1000    // Evaluate noise every second
#define HOP_COOLDOWN_MS   2000    // Minimum time between hops

// --- Pins ---
#define SCK_PIN   18
#define MISO_PIN  19
#define MOSI_PIN  16
#define CS_PIN    17
#define GDO0_PIN  25
#define GDO2_PIN  26

// --- SRR channels (CHANNR values; base 2425 MHz, 250 kHz spacing) ---
#define CHAN_RED  0x92            // 2461.5 MHz
#define CHAN_BLUE 0xBA            // 2471.5 MHz

// =============================================================================
//   CC2500 REGISTER MAP (subset)
// =============================================================================
// Header byte bits
#define CC2500_WRITE_BURST  0x40
#define CC2500_READ_SINGLE  0x80
#define CC2500_READ_BURST   0xC0

// Configuration registers
#define CC2500_IOCFG2    0x00  // GDO2 output pin configuration
#define CC2500_IOCFG0    0x02  // GDO0 output pin configuration
#define CC2500_SYNC1     0x04  // Sync word, high byte
#define CC2500_SYNC0     0x05  // Sync word, low byte
#define CC2500_PKTLEN    0x06  // Packet length
#define CC2500_PKTCTRL1  0x07  // Packet automation control
#define CC2500_PKTCTRL0  0x08  // Packet automation control
#define CC2500_CHANNR    0x0A  // Channel number
#define CC2500_FSCTRL1   0x0B  // Frequency synthesizer control (IF)
#define CC2500_FSCTRL0   0x0C  // Frequency synthesizer control (offset)
#define CC2500_FREQ2     0x0D  // Frequency control word, high byte
#define CC2500_FREQ1     0x0E  // Frequency control word, middle byte
#define CC2500_FREQ0     0x0F  // Frequency control word, low byte
#define CC2500_MDMCFG4   0x10  // Modem config: RX filter BW, data rate exponent
#define CC2500_MDMCFG3   0x11  // Modem config: data rate mantissa
#define CC2500_MDMCFG2   0x12  // Modem config: modulation, sync mode
#define CC2500_MDMCFG1   0x13  // Modem config: FEC, preamble, channel spacing exp
#define CC2500_MDMCFG0   0x14  // Modem config: channel spacing mantissa
#define CC2500_DEVIATN   0x15  // Modem deviation / MSK phase transition
#define CC2500_MCSM1     0x17  // Main radio control state machine config
#define CC2500_MCSM0     0x18  // Main radio control state machine config
#define CC2500_FOCCFG    0x19  // Frequency offset compensation config
#define CC2500_BSCFG     0x1A  // Bit synchronization config
#define CC2500_AGCCTRL2  0x1B  // AGC control
#define CC2500_AGCCTRL1  0x1C  // AGC control
#define CC2500_AGCCTRL0  0x1D  // AGC control
#define CC2500_FREND1    0x21  // Front end RX configuration
#define CC2500_FREND0    0x22  // Front end TX configuration
#define CC2500_FSCAL3    0x23  // Frequency synthesizer calibration
#define CC2500_FSCAL2    0x24  // Frequency synthesizer calibration
#define CC2500_FSCAL1    0x25  // Frequency synthesizer calibration
#define CC2500_FSCAL0    0x26  // Frequency synthesizer calibration

// Command strobes
#define CC2500_SRES      0x30  // Reset chip
#define CC2500_SRX       0x34  // Enable RX
#define CC2500_STX       0x35  // Enable TX
#define CC2500_SIDLE     0x36  // Exit RX/TX, turn off frequency synthesizer
#define CC2500_SFRX      0x3A  // Flush the RX FIFO
#define CC2500_SFTX      0x3B  // Flush the TX FIFO

// Status registers (read with the burst bit set)
#define CC2500_MARCSTATE 0x35  // Main radio control state
#define CC2500_RXBYTES   0x3B  // Overflow flag + number of bytes in RX FIFO

// FIFO access
#define CC2500_FIFO      0x3F  // TX FIFO (write) / RX FIFO (read)

// MARCSTATE values while transmitting
#define MARCSTATE_TX          0x13
#define MARCSTATE_TX_END      0x14
#define MARCSTATE_RXTX_SWITCH 0x15

// =============================================================================
//   RADIO CONFIGURATION
// =============================================================================
// These values were recovered by sniffing the SPI bus of a genuine SPORTident
// SRR USB dongle (MSP430F2370 -> CC2500). Apart from MDMCFG4 they are exactly
// what the dongle programs. They match TI SmartRF Studio's "250 kBaud, MSK"
// preset. See docs/radio-config.md for the bit-by-bit breakdown.
struct RegVal { uint8_t addr; uint8_t val; };

static const RegVal RADIO_CONFIG[] = {
  // GDO pins
  { CC2500_IOCFG0,   0x06 }, // GDO0: high on sync word, low at end of packet
  { CC2500_IOCFG2,   0x0E }, // GDO2: carrier sense (RSSI above threshold)

  // Frequency synthesizer: 2425 MHz base, 250 kHz channel spacing
  { CC2500_FSCTRL1,  0x07 }, // IF = 26 MHz/1024 * 7 = 177.7 kHz
  { CC2500_FSCTRL0,  0x00 }, // No frequency offset
  { CC2500_FREQ2,    0x5D }, // FREQ = 0x5D44EC -> 26 MHz/2^16 * 6112492
  { CC2500_FREQ1,    0x44 }, //      = 2424.9999 MHz
  { CC2500_FREQ0,    0xEC },

  // Modem
  { CC2500_MDMCFG4,  0x0D }, // CHANBW_E=0, CHANBW_M=0 -> 812.5 kHz RX filter
                             // (dongle uses 0x2D = 541 kHz; wider copes with
                             // module oscillator drift). DRATE_E = 13.
  { CC2500_MDMCFG3,  0x3B }, // DRATE_M = 59 -> (256+59)*2^13/2^28 * 26 MHz
                             //              = 249.94 kbps (i.e. 250 kBaud MSK)
  { CC2500_MDMCFG2,  0x73 }, // DC filter on, MSK, no Manchester, 30/32 sync bits
  { CC2500_MDMCFG1,  0x23 }, // No FEC, 4 preamble bytes, CHANSPC_E = 3
  { CC2500_MDMCFG0,  0x3B }, // CHANSPC_M = 59 -> 26 MHz/2^18 * 315 * 8 = 249.9 kHz
  { CC2500_DEVIATN,  0x01 }, // For MSK this sets the phase-transition fraction,
                             // not a frequency deviation (MSK dev = rate/4)

  // Packet engine
  { CC2500_PKTCTRL0, 0x05 }, // Normal FIFO mode, CRC on, variable length (first byte)
  { CC2500_PKTCTRL1, 0x0C }, // Append RSSI+LQI/CRC_OK to RX data, auto-flush on CRC fail
  { CC2500_PKTLEN,   0x29 }, // Max packet length 41

  // State machine
  { CC2500_MCSM1,    0x3F }, // CCA always, stay in RX after RX, go to RX after TX
  { CC2500_MCSM0,    0x18 }, // Auto-calibrate when going IDLE -> RX/TX

  // Offset compensation / bit sync / AGC / front end / calibration
  { CC2500_FOCCFG,   0x1D },
  { CC2500_BSCFG,    0x1C },
  { CC2500_AGCCTRL2, 0xC7 },
  { CC2500_AGCCTRL1, 0x00 },
  { CC2500_AGCCTRL0, 0xB0 },
  { CC2500_FREND1,   0xB6 },
  { CC2500_FREND0,   0x10 },
  { CC2500_FSCAL3,   0xEA },
  { CC2500_FSCAL2,   0x0A },
  { CC2500_FSCAL1,   0x00 },
  { CC2500_FSCAL0,   0x11 },

  // Sync word 0xD391 (TI default)
  { CC2500_SYNC1,    0xD3 },
  { CC2500_SYNC0,    0x91 },
};

// =============================================================================
//   SRR PACKET LAYOUT (offsets into the payload, after the length byte)
// =============================================================================
// See docs/protocol.md. Only the fields that are understood are named here.
#define SRR_HDR_LEN        4     // "siok"
#define SRR_OFF_LINK_ID    4     // [4..7]  32-bit BE: station ID (0xB6) or card ID (0xB7)
#define SRR_OFF_TYPE       14    // packet type
#define SRR_TYPE_PASSIVE   0xB6  // sent by a BSF8-SRR station
#define SRR_TYPE_ACTIVE    0xB7  // sent by a SIAC

// 0xB6 (station) body
#define SRR_B6_OFF_CN      19
#define SRR_B6_OFF_CARD    20    // [20..22] 24-bit BE
#define SRR_B6_OFF_TIME    24    // [24..25] seconds (12 h), [26] 1/256 s

// 0xB7 (SIAC) body
#define SRR_B7_OFF_CARD    15    // [15..18] 32-bit BE
#define SRR_B7_OFF_MODE    19    // station mode / code byte
#define SRR_B7_OFF_TIME    22    // [22..23] seconds (12 h), [24] 1/256 s

// ACK frame: 4-byte echoed link ID, 6 constant bytes, sequence counter, 2 constant bytes
static const uint8_t ACK_MAGIC_MID[]  = { 0x7F, 0x15, 0x00, 0x0D, 0x3F, 0x23 };
static const uint8_t ACK_MAGIC_TAIL[] = { 0x73, 0x60 };
#define ACK_LEN 13

// =============================================================================
//   STATE
// =============================================================================
static uint8_t  ackSequence    = 1;
static uint8_t  currentChannel = STARTUP_CHANNEL;

static uint32_t lastPollTime     = 0;
static uint32_t noiseAccumulator = 0;
static uint32_t lastCheckTime    = 0;
static uint32_t lastHopTime      = 0;

// =============================================================================
//   SPI / CC2500 PRIMITIVES
// =============================================================================
static inline void csLow() {
  digitalWrite(CS_PIN, LOW);
  while (digitalRead(MISO_PIN) == HIGH) {}  // CC2500 pulls SO low when ready
}
static inline void csHigh() { digitalWrite(CS_PIN, HIGH); }

static void cc2500Strobe(uint8_t cmd) {
  csLow();
  SPI.transfer(cmd);
  csHigh();
}

static void cc2500WriteReg(uint8_t addr, uint8_t value) {
  csLow();
  SPI.transfer(addr);
  SPI.transfer(value);
  csHigh();
}

// Status registers (0x30..0x3D) must be read with the burst bit set,
// otherwise the address is interpreted as a command strobe.
static uint8_t cc2500ReadReg(uint8_t addr) {
  uint8_t header = addr | ((addr >= 0x30) ? CC2500_READ_BURST : CC2500_READ_SINGLE);
  csLow();
  SPI.transfer(header);
  uint8_t val = SPI.transfer(0x00);
  csHigh();
  return val;
}

static const char* channelName() {
  return (currentChannel == CHAN_RED) ? "RED" : "BLUE";
}

static void setChannel(uint8_t ch) {
  currentChannel = ch;
  cc2500WriteReg(CC2500_CHANNR, ch);
  cc2500Strobe(CC2500_SRX);
  Serial.printf("[%s] Listening...\n", channelName());
}

static void toggleChannel() {
  setChannel(currentChannel == CHAN_RED ? CHAN_BLUE : CHAN_RED);
  lastHopTime = millis();
}

static void restartRx() {
  cc2500Strobe(CC2500_SIDLE);
  cc2500Strobe(CC2500_SFRX);
  cc2500Strobe(CC2500_SRX);
}

// =============================================================================
//   ACK
// =============================================================================
// Must complete within roughly a millisecond of the end of the received
// packet, so this only does raw register access — no printing.
static void sendAck(const uint8_t* linkId) {
  cc2500Strobe(CC2500_SIDLE);
  cc2500Strobe(CC2500_SFTX);

  csLow();
  SPI.transfer(CC2500_FIFO | CC2500_WRITE_BURST);
  SPI.transfer(ACK_LEN);
  for (int i = 0; i < 4; i++) SPI.transfer(linkId[i]);
  for (size_t i = 0; i < sizeof(ACK_MAGIC_MID); i++) SPI.transfer(ACK_MAGIC_MID[i]);
  SPI.transfer(ackSequence++);
  for (size_t i = 0; i < sizeof(ACK_MAGIC_TAIL); i++) SPI.transfer(ACK_MAGIC_TAIL[i]);
  csHigh();

  cc2500Strobe(CC2500_STX);

  // Wait for TX to finish (MCSM1 then returns the radio to RX by itself)
  uint32_t t0 = micros();
  for (;;) {
    uint8_t state = cc2500ReadReg(CC2500_MARCSTATE) & 0x1F;
    if (state != MARCSTATE_TX && state != MARCSTATE_TX_END && state != MARCSTATE_RXTX_SWITCH) break;
    if (micros() - t0 > 5000) { cc2500Strobe(CC2500_SIDLE); break; }
  }
}

// =============================================================================
//   DECODER
// =============================================================================
static uint32_t be24(const uint8_t* p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }
static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | be24(p + 1); }

// SPORTident time: 16-bit seconds in a 12 h half-day + 1/256 s sub-seconds.
static void formatSiTime(const uint8_t* p, char* out, size_t outLen) {
  uint16_t seconds = ((uint16_t)p[0] << 8) | p[1];
  uint16_t ms      = (uint16_t)p[2] * 1000 / 256;
  snprintf(out, outLen, "%02u:%02u:%02u.%03u",
           seconds / 3600, (seconds % 3600) / 60, seconds % 60, ms);
}

static const char* activeModeName(uint8_t mode) {
  switch (mode) {
    case 0x07: return "CLEAR";
    case 0x1A: return "CHECK";
    case 0x0B: return "START";
    case 0x0D: return "FINISH";
    case 0xB2: return "CONTROL";
    default:   return "SIAC";
  }
}

static int rssiToDbm(uint8_t raw) {
  // CC2500 datasheet: RSSI_dBm = RSSI_dec/2 - 74, with RSSI_dec as signed 8-bit
  return (int)((int8_t)raw) / 2 - 74;
}

#if DUMP_RAW_PACKETS
static void dumpHex(const uint8_t* data, uint8_t len) {
  Serial.printf("[%s] RAW %2u:", channelName(), len);
  for (uint8_t i = 0; i < len; i++) Serial.printf(" %02X", data[i]);
  Serial.println();
}
#endif

static void decodePacket(const uint8_t* d, uint8_t len, uint8_t rssiRaw) {
  int rssi = rssiToDbm(rssiRaw);

  if (len <= SRR_OFF_TYPE || memcmp(d, "siok", SRR_HDR_LEN) != 0) {
    Serial.printf("[%s] NON-SRR PACKET (len %u, RSSI %d)\n", channelName(), len, rssi);
    return;
  }

  uint8_t  type   = d[SRR_OFF_TYPE];
  uint32_t linkId = be32(d + SRR_OFF_LINK_ID);
  char timeStr[16];

  if (type == SRR_TYPE_PASSIVE && len >= SRR_B6_OFF_TIME + 3) {
    formatSiTime(d + SRR_B6_OFF_TIME, timeStr, sizeof(timeStr));
    Serial.printf("[%s] PASSIVE | Station: %lu | Card: %lu | CN: %u | Time: %s | RSSI: %d\n",
                  channelName(), (unsigned long)linkId,
                  (unsigned long)be24(d + SRR_B6_OFF_CARD), d[SRR_B6_OFF_CN], timeStr, rssi);
  } else if (type == SRR_TYPE_ACTIVE && len >= SRR_B7_OFF_TIME + 3) {
    formatSiTime(d + SRR_B7_OFF_TIME, timeStr, sizeof(timeStr));
    uint8_t mode = d[SRR_B7_OFF_MODE];
    Serial.printf("[%s] %s | LinkID: %lu | Card: %lu | Mode: 0x%02X | Time: %s | RSSI: %d\n",
                  channelName(), activeModeName(mode), (unsigned long)linkId,
                  (unsigned long)be32(d + SRR_B7_OFF_CARD), mode, timeStr, rssi);
  } else {
    Serial.printf("[%s] UNKNOWN/SHORT TYPE 0x%02X | len %u | LinkID: %lu | RSSI: %d\n",
                  channelName(), type, len, (unsigned long)linkId, rssi);
  }
}

// =============================================================================
//   PACKET RECEPTION
// =============================================================================
static void handlePacketRx() {
  // GDO0 went high on sync word; wait for it to drop at end of packet.
  uint32_t t0 = millis();
  while (digitalRead(GDO0_PIN) == HIGH) {
    if (millis() - t0 > 50) return;  // stuck; bail out
  }

  uint8_t rxBytes = cc2500ReadReg(CC2500_RXBYTES) & 0x7F;
  bool acked = false;

  if (rxBytes > 0) {
    uint8_t payload[64];
    csLow();
    SPI.transfer(CC2500_FIFO | CC2500_READ_BURST);
    uint8_t len = SPI.transfer(0x00);
    if (len > sizeof(payload)) len = sizeof(payload);
    for (uint8_t i = 0; i < len; i++) payload[i] = SPI.transfer(0x00);
    uint8_t rssi = SPI.transfer(0x00);
    uint8_t lqi  = SPI.transfer(0x00);   // bit 7 = CRC_OK
    csHigh();

    if (lqi & 0x80) {
#if ENABLE_ACK
      // ACK first, decode later — the transmitter is already counting down.
      if (len >= SRR_OFF_LINK_ID + 4) {
        sendAck(&payload[SRR_OFF_LINK_ID]);
        acked = true;
      }
#endif
#if DUMP_RAW_PACKETS
      dumpHex(payload, len);
#endif
      decodePacket(payload, len, rssi);
    }
  }

  // If we did not go through TX (which returns to RX by itself), restart RX
  // explicitly to clear any leftover FIFO state.
  if (!acked) restartRx();
}

// =============================================================================
//   NOISE POLLING / CHANNEL HOPPING
// =============================================================================
static void pollNoiseLevel() {
  uint32_t now = millis();
  uint32_t elapsed = now - lastPollTime;
  lastPollTime = now;

  if (digitalRead(GDO2_PIN) == HIGH) noiseAccumulator += elapsed;

  if (now - lastCheckTime > CHECK_INTERVAL_MS) {
    if (noiseAccumulator > NOISE_LIMIT_MS && now - lastHopTime > HOP_COOLDOWN_MS) {
      Serial.printf("[%s] SATURATED (%lu ms) -> HOPPING\n", channelName(), (unsigned long)noiseAccumulator);
      toggleChannel();
    }
    noiseAccumulator = 0;
    lastCheckTime = now;
  }
}

// =============================================================================
//   SETUP / LOOP
// =============================================================================
void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("\n--- ESP32 + CC2500 SPORTident SRR receiver ---");

  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  pinMode(GDO0_PIN, INPUT);
  pinMode(GDO2_PIN, INPUT);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);

  cc2500Strobe(CC2500_SRES);
  delay(100);

  for (size_t i = 0; i < sizeof(RADIO_CONFIG) / sizeof(RADIO_CONFIG[0]); i++) {
    cc2500WriteReg(RADIO_CONFIG[i].addr, RADIO_CONFIG[i].val);
  }

  setChannel(STARTUP_CHANNEL);
  lastPollTime = millis();
}

void loop() {
  if (digitalRead(GDO0_PIN) == HIGH) {
    // A packet is coming in. Block here so the ACK goes out in time.
    handlePacketRx();
    lastPollTime = millis();  // don't count RX time as noise
  } else if (ENABLE_HOPPING) {
    pollNoiseLevel();
  }
}
