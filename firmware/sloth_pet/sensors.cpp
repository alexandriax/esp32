#include "sensors.h"

#include <Arduino.h>
#include <Wire.h>

namespace sensors {
namespace {
// Exact board wiring and touch packet format:
// https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16
// 02_Example/Arduino-v3.3.3/08_LVGL_V8_Test/lcd_touch.cpp
constexpr uint8_t kTouchAddress = 0x5A;
constexpr int kTouchResetPin = 11;
constexpr int kTouchInterruptPin = 5;
constexpr uint16_t kDisplaySize = 480;
constexpr uint16_t kTransactionTimeoutMs = 10;

// QMI8658C datasheet register tables and board's 02_I2C_QMI8658 example:
// https://files.waveshare.com/wiki/common/QMI8658C_datasheet_rev_0.9.pdf
constexpr uint8_t kWhoAmI = 0x00;
constexpr uint8_t kCtrl1 = 0x02;
constexpr uint8_t kCtrl2 = 0x03;
constexpr uint8_t kCtrl3 = 0x04;
constexpr uint8_t kCtrl5 = 0x06;
constexpr uint8_t kCtrl7 = 0x08;
constexpr uint8_t kCtrl8 = 0x09;
constexpr uint8_t kFifoCtrl = 0x14;
constexpr uint8_t kStatus0 = 0x2E;
constexpr uint8_t kAccelerationLow = 0x35;
constexpr float kCountsPerG = 8192.0f;  // Signed 16-bit output, +/-4 g.

Capabilities available{};
uint8_t imuAddress = 0;
bool gyroEnabled = false;
bool gyroConfigDirty = false;
uint8_t gyroWarmup = 0;
uint16_t lastTouchX = 0, lastTouchY = 0;

TouchStatus releasedTouch(uint16_t& x, uint16_t& y) {
  // CST9220 finger-up packets have no reliable point. Every UI needs the last
  // valid contact position, even when the caller uses fresh outputs each poll.
  x = lastTouchX; y = lastTouchY;
  return TouchStatus::Released;
}

// This bus is shared with the PMIC. Keep sensor failures bounded without
// changing the timeout expected by the board driver. Call from the main loop.
class BusTimeout {
 public:
  BusTimeout() : previous_(Wire.getTimeOut()) {
    Wire.setTimeOut(kTransactionTimeoutMs);
  }
  ~BusTimeout() { Wire.setTimeOut(previous_); }
 private:
  uint16_t previous_;
};

bool readRegisters(uint8_t address, const uint8_t* command, size_t commandSize,
                   uint8_t* output, size_t size) {
  BusTimeout timeout;
  Wire.beginTransmission(address);
  Wire.write(command, commandSize);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(address, size, true) != size) {
    return false;
  }
  for (size_t i = 0; i < size; ++i) {
    const int value = Wire.read();
    if (value < 0) return false;
    output[i] = static_cast<uint8_t>(value);
  }
  return true;
}

bool readRegister(uint8_t address, uint8_t reg, uint8_t& value) {
  return readRegisters(address, &reg, 1, &value, 1);
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  BusTimeout timeout;
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}

bool beginAcceleration() {
  const uint8_t addresses[] = {0x6B, 0x6A};
  for (uint8_t address : addresses) {
    uint8_t id = 0;
    if (readRegister(address, kWhoAmI, id) && id == 0x05) {
      imuAddress = address;
      break;
    }
  }
  if (imuAddress == 0) return false;

  // Disable both sensors before configuring. CTRL1 bit6 enables address
  // increment; bit5 is cleared for little endian per the datasheet. CTRL2
  // bits6:4=001 select +/-4 g, bits3:0=0110 select 125 Hz (accel only).
  // Disable FIFO, filters and embedded motion detection, then enable only
  // acceleration in asynchronous mode (CTRL7 bit0; gyro and sync bits clear).
  if (!writeRegister(imuAddress, kCtrl7, 0x00) ||
      !writeRegister(imuAddress, kCtrl1, 0x40) ||
      !writeRegister(imuAddress, kCtrl2, 0x16) ||
      !writeRegister(imuAddress, kCtrl5, 0x00) ||
      !writeRegister(imuAddress, kCtrl8, 0x00) ||
      !writeRegister(imuAddress, kFifoCtrl, 0x00) ||
      !writeRegister(imuAddress, kCtrl7, 0x01)) {
    return false;
  }
  uint8_t rangeAndRate = 0, enabled = 0;
  return readRegister(imuAddress, kCtrl2, rangeAndRate) && rangeAndRate == 0x16 &&
         readRegister(imuAddress, kCtrl7, enabled) && enabled == 0x01;
}

bool beginTouch() {
  pinMode(kTouchInterruptPin, INPUT_PULLUP);
  pinMode(kTouchResetPin, OUTPUT);
  // Match the board vendor's reset sequence. No interrupt is required for
  // polling; a short interrupt pulse must not cause a touch sample to be lost.
  digitalWrite(kTouchResetPin, HIGH);
  delay(200);
  digitalWrite(kTouchResetPin, LOW);
  delay(200);
  digitalWrite(kTouchResetPin, HIGH);
  delay(200);
  BusTimeout timeout;
  Wire.beginTransmission(kTouchAddress);
  return Wire.endTransmission(true) == 0;
}

int16_t signedLittleEndian(const uint8_t* bytes) {
  return static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) |
                              (static_cast<uint16_t>(bytes[1]) << 8));
}
}  // namespace

Capabilities begin() {
  available = {};
  imuAddress = 0;
  gyroEnabled = false;
  gyroConfigDirty = false;
  gyroWarmup = 0;
  lastTouchX = lastTouchY = 0;
  available.acceleration = beginAcceleration();
  available.touch = beginTouch();
  Serial.printf("SENSORS touch=%s acceleration=%s imu_address=0x%02X\n",
                available.touch ? "ready" : "unavailable",
                available.acceleration ? "ready" : "unavailable", imuAddress);
  return available;
}

bool readAcceleration(float& xg, float& yg, float& zg) {
  if (!available.acceleration) return false;
  uint8_t status = 0;
  // STATUS0 bit0 is accelerometer data available since the preceding read.
  if (!readRegister(imuAddress, kStatus0, status) || !(status & 0x01)) return false;
  uint8_t bytes[6];
  if (!readRegisters(imuAddress, &kAccelerationLow, 1, bytes, sizeof(bytes))) {
    return false;
  }
  xg = signedLittleEndian(bytes) / kCountsPerG;
  yg = signedLittleEndian(bytes + 2) / kCountsPerG;
  zg = signedLittleEndian(bytes + 4) / kCountsPerG;
  return true;
}

bool enableGyroscope(bool enabled) {
  if (!available.acceleration) return false;
  if (enabled == gyroEnabled && !gyroConfigDirty) return true;
  gyroConfigDirty = true;
  if (enabled) {
    // QMI8658C rev0.9 pp30-31: +/-512dps (64counts/dps), ODR117.5Hz
    // in six-axis mode; same ODR code as the existing accelerometer. Configure
    // while disabled, then restore acceleration regardless of a failed start.
    uint8_t config = 0, control = 0;
    const bool ok = writeRegister(imuAddress, kCtrl7, 0) &&
        writeRegister(imuAddress, kCtrl3, 0x56) && writeRegister(imuAddress, kCtrl7, 0x03) &&
        readRegister(imuAddress, kCtrl3, config) && config == 0x56 &&
        readRegister(imuAddress, kCtrl7, control) && control == 0x03;
    if (!ok) {
      gyroEnabled = false;
      // A failed rollback leaves configuration unknown. A later disable must
      // retry the bus write instead of treating the cached false as proof.
      if (writeRegister(imuAddress, kCtrl7, 0x01) &&
          readRegister(imuAddress, kCtrl7, control) && control == 0x01) gyroConfigDirty = false;
      return false;
    }
    gyroWarmup = 5;
  } else {
    uint8_t control = 0;
    if (!writeRegister(imuAddress, kCtrl7, 0x01) ||
        !readRegister(imuAddress, kCtrl7, control) || control != 0x01) return false;
    gyroWarmup = 0;
  }
  gyroEnabled = enabled;
  gyroConfigDirty = false;
  return true;
}

bool gyroscopeEnabled() { return gyroEnabled; }

bool readMotion(float& ax, float& ay, float& az, float& gx, float& gy, float& gz) {
  if (!available.acceleration || !gyroEnabled) return false;
  uint8_t status = 0, bytes[12];
  // STATUS0 bits0/1: fresh accel/gyro data. Consecutive output registers53..64.
  if (!readRegister(imuAddress, kStatus0, status) || (status & 3) != 3 ||
      !readRegisters(imuAddress, &kAccelerationLow, 1, bytes, sizeof(bytes))) return false;
  if (gyroWarmup) { --gyroWarmup; return false; }
  ax = signedLittleEndian(bytes) / kCountsPerG;
  ay = signedLittleEndian(bytes + 2) / kCountsPerG;
  az = signedLittleEndian(bytes + 4) / kCountsPerG;
  gx = signedLittleEndian(bytes + 6) / 64.0f;
  gy = signedLittleEndian(bytes + 8) / 64.0f;
  gz = signedLittleEndian(bytes + 10) / 64.0f;
  return true;
}

TouchStatus readTouch(uint16_t& x, uint16_t& y) {
  if (!available.touch) return TouchStatus::Unavailable;
  const uint8_t command[] = {0xD0, 0x00};
  uint8_t data[10];
  if (!readRegisters(kTouchAddress, command, sizeof(command), data, sizeof(data)) ||
      data[6] != 0xAB) {
    return TouchStatus::Error;
  }
  const uint8_t points = data[5] & 0x7F;
  if (points == 0) return releasedTouch(x, y);
  if (points > 5) return TouchStatus::Error;
  const uint8_t status = data[0] & 0x0F;
  // Release packets can retain their previous point count and coordinates.
  // The cleared status nibble denotes release; 0x06/0x07 denote contact.
  // See Espressif esp_lcd_touch_cst9220.c, cst9220_read_data().
  if (status == 0) return releasedTouch(x, y);
  if ((status >> 1) != 3) return TouchStatus::Error;

  const uint16_t rawY = (static_cast<uint16_t>(data[1]) << 4) | (data[3] >> 4);
  const uint16_t rawX = (static_cast<uint16_t>(data[2]) << 4) | (data[3] & 0x0F);
  if (rawX >= kDisplaySize || rawY >= kDisplaySize) return TouchStatus::Error;
  // Board panel axes are swapped, with display X reversed. Group the packed
  // coordinate before subtracting and use 479 so every pixel stays in bounds.
  x = lastTouchX = kDisplaySize - 1 - rawX;
  y = lastTouchY = rawY;
  return TouchStatus::Pressed;
}

}  // namespace sensors
