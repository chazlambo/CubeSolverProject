#include <Wire.h>
#include <TCA9548.h>

// --------- CONFIG ----------
static const uint32_t BAUD = 115200;
static const uint32_t I2C_HZ = 100000; // drop to 100k for reliability
// ---------------------------

bool baseline[128];       // baseline presence map when all mux channels are OFF
uint8_t muxAddrs[8];      // list of muxes found at baseline
uint8_t muxCount = 0;

bool ping(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

void closeAllMuxChannelsBlind() {
  // Try to write "all channels off" (0x00) to every possible TCA9548A address
  for (uint8_t addr = 0x70; addr <= 0x77; addr++) {
    // Only attempt the write if device ACKs
    if (ping(addr)) {
      Wire.beginTransmission(addr);
      Wire.write((uint8_t)0x00);   // TCA9548A control byte: all channels disabled
      Wire.endTransmission();
    }
  }
}

const char* nameForAddr(uint8_t a) {
  // Handy names for common parts in your project; extend as needed
  switch (a) {
    case 0x36: return "AS5600 Encoder";
    case 0x10: return "VEML6040 Color";
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
  // Clear maps
  for (int i = 0; i < 128; i++) baseline[i] = false;
  muxCount = 0;

  // Force *all* muxes off first (even if we don’t yet know where they are)
  closeAllMuxChannelsBlind();

  Serial.println("Baseline scan (all mux channels OFF):");
  bool any = false;
  for (uint8_t a = 1; a < 127; a++) {
    if (ping(a)) {
      baseline[a] = true;
      printAddr(a, "  • ");
      any = true;

      // Collect mux addresses for later per-channel scanning
      if (a >= 0x70 && a <= 0x77 && muxCount < 8) {
        muxAddrs[muxCount++] = a;
      }
    }
  }
  if (!any) Serial.println("  (no devices found on the base bus)");
  Serial.println();

  // Quick recap of muxes found
  if (muxCount) {
    Serial.print("Found "); Serial.print(muxCount); Serial.println(" TCA9548A mux(es) at:");
    for (uint8_t i = 0; i < muxCount; i++) printAddr(muxAddrs[i], "  • ");
  } else {
    Serial.println("No muxes found at baseline.");
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

  Serial.print("Scanning mux 0x"); Serial.print(muxAddr, HEX); Serial.println(" by channel (excluding baseline devices):");

  for (uint8_t ch = 0; ch < 8; ch++) {
    mux.setChannelMask(0x00);      // all off
    mux.selectChannel(ch);         // enable exactly this channel
    delay(5);

    bool found = false;
    for (uint8_t a = 1; a < 127; a++) {
      if (baseline[a]) continue;   // ignore anything already seen with channels OFF (muxes, top-level devices)
      if (a == muxAddr) continue;  // extra safety (already baseline anyway)

      if (ping(a)) {
        if (!found) {
          Serial.print("  Channel "); Serial.print(ch); Serial.println(":");
          found = true;
        }
        printAddr(a);
      }
    }
    if (!found) {
      // Only print empty channels if you want the full map; comment out to keep output tighter
      // Serial.print("  Channel "); Serial.print(ch); Serial.println(": (no devices)");
    }
  }

  mux.setChannelMask(0x00); // leave all channels off
  Serial.println();
}

void setup() {
  Serial.begin(BAUD);
  delay(50);
  Wire.begin();
  Wire.setClock(I2C_HZ);

  Serial.println("\n=== Filtered I2C MUX Scanner ===");
  Serial.println("Strategy: close ALL mux channels → baseline scan → per-channel scan (excluding baseline addresses)\n");

  doBaselineScan();

  // For each mux found at baseline, scan channels and hide baseline devices
  for (uint8_t i = 0; i < muxCount; i++) {
    scanMuxChannelsFiltered(muxAddrs[i]);
  }

  Serial.println("Scan complete.");
}

void loop() {
  // idle
}
