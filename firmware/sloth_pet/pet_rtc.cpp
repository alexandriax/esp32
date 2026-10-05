#include "pet_rtc.h"
#include "rtc_calendar.h"

#include <Wire.h>
#include <string.h>

namespace pet_rtc {
namespace {
// Board: PCF85063A at 0x51 on the shared SDA8/SCL7 bus. Register definitions:
// https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf sections 7.2-7.4.
// Waveshare's 03_I2C_PCF85063 example selects CAP_SEL=1 (12.5 pF).
constexpr uint8_t kAddress = 0x51;
constexpr uint8_t kControl1 = 0x00;
constexpr uint8_t kSeconds = 0x04;
constexpr uint8_t kStop = 0x20;
constexpr uint16_t kTimeoutMs = 10;

class BusTimeout {
 public:
  BusTimeout() : previous_(Wire.getTimeOut()) { Wire.setTimeOut(kTimeoutMs); }
  ~BusTimeout() { Wire.setTimeOut(previous_); }
 private:
  uint16_t previous_;
};

bool readRegisters(uint8_t reg, uint8_t* data, size_t size) {
  BusTimeout timeout;
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(kAddress, size, true) != size) return false;
  for (size_t i = 0; i < size; ++i) {
    const int value = Wire.read();
    if (value < 0) return false;
    data[i] = static_cast<uint8_t>(value);
  }
  return true;
}

bool writeRegisters(uint8_t reg, const uint8_t* data, size_t size) {
  BusTimeout timeout;
  Wire.beginTransmission(kAddress);
  const size_t registerWritten = Wire.write(reg);
  const size_t dataWritten = Wire.write(data, size);
  const uint8_t result = Wire.endTransmission(true);
  return registerWritten == 1 && dataWritten == size && result == 0;
}
}  // namespace

Status read(uint32_t& utc) {
  // One burst includes control and all seven time registers. The chip freezes
  // the time counters during this access, preventing a torn midnight rollover.
  // Our bounded transfer is well below the datasheet's one-second access limit.
  uint8_t registers[11];
  if (!readRegisters(kControl1, registers, sizeof(registers))) return Status::Unavailable;
  return calendar::decode(registers[0], registers + kSeconds, utc)
             ? Status::Valid : Status::Invalid;
}

bool set(uint32_t utc) {
  uint8_t packet[7];
  if (!calendar::encode(utc, packet)) return false;
  uint8_t previousControl;
  if (!readRegisters(kControl1, &previousControl, 1)) return false;

  // Retain correction-interrupt enable, select the board's crystal load, and
  // clear test, reset and 12-hour mode. No software reset, alarm or PMIC writes.
  const uint8_t runningControl = (previousControl & 0x04) | 0x01;
  const uint8_t stoppedControl = runningControl | kStop;
  uint8_t verifiedControl;
  if (!writeRegisters(kControl1, &stoppedControl, 1) ||
      !readRegisters(kControl1, &verifiedControl, 1) || verifiedControl != stoppedControl) {
    return false;
  }

  // A full seconds..years burst also clears seconds.OS. If the oscillator has
  // not stabilized, OS cannot clear and the readback deliberately fails.
  uint8_t verifiedPacket[7];
  if (!writeRegisters(kSeconds, packet, sizeof(packet)) ||
      !readRegisters(kSeconds, verifiedPacket, sizeof(verifiedPacket)) ||
      memcmp(packet, verifiedPacket, sizeof(packet)) != 0) return false;
  if (!writeRegisters(kControl1, &runningControl, 1)) return false;

  uint32_t verifiedUtc;
  if (read(verifiedUtc) != Status::Valid || verifiedUtc < utc || verifiedUtc - utc > 1) {
    // Best effort: keep an uncertain write visibly invalid for subsequent reads.
    writeRegisters(kControl1, &stoppedControl, 1);
    return false;
  }
  return true;
}

}  // namespace pet_rtc
