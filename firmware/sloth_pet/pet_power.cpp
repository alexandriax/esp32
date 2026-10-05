#include "pet_power.h"

#include <Wire.h>

namespace pet_power {
namespace {
// AXP2101 datasheet v1.4 sections 6.7.3.5, 6.13.2.1/.2/.14/.65/.88.
// Bundled primary source:
// https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16/blob/
// 294543798f1a44e2f2c4d2976522323f2beee11d/01_Arduino_Libraries/
// XPowersLib/datasheet/AXP2101_Datasheet_V1.4_en.pdf
// Registers also verified against the bundled XPowersAXP2101.tpp methods
// isVbusGood(), isBatteryConnect(), isCharging(), and getBatteryPercent().
constexpr uint8_t kAddress = 0x34;
constexpr uint8_t kStatus1 = 0x00;          // bit5 VBUS good, bit3 battery present.
constexpr uint8_t kGaugeControl = 0x18;     // bit3 fuel-gauge module enable.
constexpr uint8_t kDetectionControl = 0x68; // bit0 battery detection enable.
constexpr uint8_t kBatteryPercent = 0xA4;   // Entire byte is percentage, no flag bits.
constexpr uint16_t kTimeoutMs = 10;
// Datasheet sections 6.5.4.1/.3, 6.12.1, 6.13.2.41/.44 (pp. 19, 20, 42, 44):
// PWRON short, long, falling and rising events are separate. Short press is
// bit3 in both IRQ Enable 1 and IRQ Status 1 (RW1C); also verified against
// XPowers isPekeyShortPressIrq()/XPOWERS_AXP2101_PKEY_SHORT_IRQ. Enabled events
// latch even when polled without the physical IRQ pin. Leave the edge/long IRQs
// untouched, as well as hardware shutdown/timing registers 0x22 and 0x27.
constexpr uint8_t kButtonIrqEnable = 0x41;
constexpr uint8_t kButtonIrqStatus = 0x49;
constexpr uint8_t kButtonShortPress = 0x08;
bool buttonReady = false;
bool buttonReported = false;

class BusTimeout {
 public:
  BusTimeout() : previous_(Wire.getTimeOut()) { Wire.setTimeOut(kTimeoutMs); }
  ~BusTimeout() { Wire.setTimeOut(previous_); }
 private:
  uint16_t previous_;
};

bool readRegisters(uint8_t reg, uint8_t* output, size_t size) {
  BusTimeout timeout;
  Wire.beginTransmission(kAddress);
  Wire.write(reg);  // Register pointer only: never writes PMIC register data.
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(kAddress, size, true) != size) return false;
  for (size_t i = 0; i < size; ++i) {
    const int value = Wire.read();
    if (value < 0) return false;
    output[i] = static_cast<uint8_t>(value);
  }
  return true;
}

bool writeRegister(uint8_t reg, uint8_t value) {
  BusTimeout timeout;
  Wire.beginTransmission(kAddress);
  const size_t registerWritten = Wire.write(reg);
  const size_t valueWritten = Wire.write(value);
  const uint8_t result = Wire.endTransmission(true);
  return registerWritten == 1 && valueWritten == 1 && result == 0;
}
}  // namespace

Status read() {
  Status result;
  uint8_t status[2], detection;
  if (!readRegisters(kStatus1, status, sizeof(status)) ||
      !readRegisters(kDetectionControl, &detection, 1)) return result;
  const uint8_t direction = (status[1] >> 5) & 0x03;
  // Reject impossible status values instead of displaying a plausible battery
  // from corrupt/all-ones data. These bits are reserved zero in the datasheet.
  if ((status[0] & 0xC0) || (status[1] & 0x80) || direction == 3 ||
      (detection & 0xFE)) return result;

  result.available = true;
  result.externalPower = (status[0] & 0x20) != 0;
  // With detection disabled the PMIC assumes a battery is present, even if it
  // is physically absent. Do not expose that assumption as a measured battery.
  result.batteryPresent = (detection & 0x01) && (status[0] & 0x08);
  result.charging = result.batteryPresent && result.externalPower && direction == 1;
  if (!result.batteryPresent) return result;

  uint8_t gaugeControl, percent;
  if (readRegisters(kGaugeControl, &gaugeControl, 1) &&
      !(gaugeControl & 0xF0) && (gaugeControl & 0x08) &&
      readRegisters(kBatteryPercent, &percent, 1) && percent <= 100) {
    result.percent = percent;
  }
  return result;
}

bool beginButton() {
  buttonReady = false;
  buttonReported = false;
  uint8_t enabled;
  if (!readRegisters(kButtonIrqEnable, &enabled, 1)) return false;
  const uint8_t wanted = enabled | kButtonShortPress;
  if (wanted != enabled && !writeRegister(kButtonIrqEnable, wanted)) return false;
  uint8_t verified;
  if (!readRegisters(kButtonIrqEnable, &verified, 1) || verified != wanted) return false;

  uint8_t pending;
  if (!readRegisters(kButtonIrqStatus, &pending, 1)) return false;
  if (pending & kButtonShortPress) {
    // Discard the power-on/stale tap. The W1C write must contain only our bit:
    // echoing the whole status byte would consume unrelated PMIC interrupts.
    buttonReported = true;
    if (!writeRegister(kButtonIrqStatus, kButtonShortPress)) return false;
  }
  buttonReady = true;
  return true;
}

ButtonEvent readButton() {
  if (!buttonReady) return ButtonEvent::Unavailable;
  uint8_t pending;
  if (!readRegisters(kButtonIrqStatus, &pending, 1)) return ButtonEvent::Unavailable;
  if (!(pending & kButtonShortPress)) {
    buttonReported = false;
    return ButtonEvent::None;
  }
  const bool firstObservation = !buttonReported;
  buttonReported = true;
  const bool acknowledged = writeRegister(kButtonIrqStatus, kButtonShortPress);
  // Deliver a known press immediately, even when acknowledgement needs a later
  // retry. A failed/stuck clear must not keep producing phantom new presses.
  if (firstObservation) return ButtonEvent::Pressed;
  return acknowledged ? ButtonEvent::None : ButtonEvent::Unavailable;
}

}  // namespace pet_power
