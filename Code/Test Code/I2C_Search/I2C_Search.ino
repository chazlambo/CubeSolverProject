#include <Wire.h>
#include <TCA9548.h>

// --------- CONFIG ----------
static const uint32_t BAUD = 115200;
static const uint32_t I2C_HZ = 100000; // drop to 100k for reliability
// ---------------------------

bool baseline[128];       // baseline presence map on Wire
uint8_t muxAddrs[8];      // list of muxes found on Wire
uint8_t muxCount = 0;

bool pingWire(TwoWire& bus, uint8_t addr) {
  bus.beginTransmission(addr);
  return (bus.endTransmission() == 0);
}


void closeAllMuxChannelsBlind() {
  // Use Wire (I2C0). Wire1 has no muxes.
  for (uint8_t addr = 0x70; addr <= 0x77; addr++) {
    if (pingWire(Wire, addr)) {
      Wire.beginTransmission(addr);
      Wire.write((uint8_t)0x00);   // disable all channels
      Wire.endTransmission();
    }
  }
}

const char* nameForAddr(uint8_t a) {
  switch (a) {
    case 0x36: return "AS5600 Encoder";
    case 0x10: return "VEML6040 Color";
    case 0x49: return "Seesaw Encoder Wheel";
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77: return "TCA9548A MUX";
    default:   return "";
  }
}

void printAddr(uint8_t a, const char* prefix = "  ➜ ") {
  Serial.print(prefix);
  Serial.print("0x");
  if (a < 16) Serial.print('0');
  Serial.print(a, HEX);
  const char* n = nameForAddr(a);
  if (n[0]) { Serial.print("  ("); Serial.print(n); Serial.print(")"); }
  Serial.println();
}

void doBaselineScan() {
  for (int i = 0; i < 128; i++) baseline[i] = false;
  muxCount = 0;

  closeAllMuxChannelsBlind();

  Serial.println("Baseline scan on Wire (I2C0):");
  bool any = false;
  for (uint8_t a = 1; a < 127; a++) {
    if (pingWire(Wire, a)) {
      baseline[a] = true;
      printAddr(a, "  • ");
      any = true;

      if (a >= 0x70 && a <= 0x77 && muxCount < 8)
        muxAddrs[muxCount++] = a;
    }
  }
  if (!any) Serial.println("  (no devices found on Wire)");
  Serial.println();

  if (muxCount) {
    Serial.print("Found "); Serial.print(muxCount); Serial.println(" TCA9548A mux(es):");
    for (uint8_t i = 0; i < muxCount; i++) printAddr(muxAddrs[i], "  • ");
  } else {
    Serial.println("No muxes found on Wire.");
  }
  Serial.println();
}

void scanMuxChannelsFiltered(uint8_t muxAddr) {
  TCA9548 mux(muxAddr);
  if (!mux.begin()) {
    Serial.print("Could not initialize mux at 0x");
    Serial.println(muxAddr, HEX);
    return;
  }

  Serial.print("Scanning mux 0x"); Serial.print(muxAddr, HEX); Serial.println(" (channels 0–7):");

  for (uint8_t ch = 0; ch < 8; ch++) {
    mux.setChannelMask(0x00);
    mux.selectChannel(ch);
    delay(5);

    bool found = false;
    for (uint8_t a = 1; a < 127; a++) {
      if (baseline[a]) continue;
      if (a == muxAddr) continue;

      if (pingWire(Wire, a)) {
        if (!found) {
          Serial.print("  Channel "); Serial.print(ch); Serial.println(":");
          found = true;
        }
        printAddr(a);
      }
    }
  }

  mux.setChannelMask(0x00);
  Serial.println();
}

void scanWire1() {
  Serial.println("Top-level scan on Wire1:");

  bool any = false;
  for (uint8_t a = 1; a < 127; a++) {
    if (pingWire(Wire1, a)) {
      printAddr(a, "  • ");
      any = true;
    }
  }

  if (!any) Serial.println("  (no devices found on Wire1)");
  Serial.println();
}

// --------------------------------------------------------------------

void setup() {
  Serial.begin(BAUD);
  delay(50);

  Wire.begin();      // main bus
  Wire.setClock(I2C_HZ);

  Wire1.begin();
  Wire1.setClock(I2C_HZ);

  Serial.println("\n=== Filtered I2C MUX Scanner ===");

  // Normal scan on Wire + mux-discovery
  doBaselineScan();

  // Per-mux channel scan
  for (uint8_t i = 0; i < muxCount; i++) {
    scanMuxChannelsFiltered(muxAddrs[i]);
  }

  // NEW: scan the second bus
  scanWire1();

  Serial.println("Scan complete.");
}

void loop() {}
