#include "power_mocks/Wire.h"
#include "../firmware/sloth_pet/pet_power.h"

#include <cassert>
#include <cstdio>
#include <cstring>

WireMock Wire;

void seedBattery() {
  Wire = WireMock{};
  Wire.registers[0x00] = 0x28; // Usable USB input, battery present.
  Wire.registers[0x01] = 0x30; // Charging, system on.
  Wire.registers[0x18] = 0x0A; // Gauge and charger enabled.
  Wire.registers[0x68] = 0x01;
  Wire.registers[0xA4] = 57;
}

void seedButton() {
  seedBattery();
  Wire.registers[0x22] = 0x02; // Hardware long-hold shutdown enabled.
  Wire.registers[0x27] = 0x3A; // Existing button timing must remain untouched.
  Wire.registers[0x40] = 0xA5;
  Wire.registers[0x41] = 0xF6; // Long/falling + supply IRQs, short-press disabled.
  Wire.registers[0x42] = 0x5A;
  Wire.registers[0x48] = 0xA5;
  Wire.registers[0x49] = 0xFF; // Includes stale short tap plus unrelated flags.
  Wire.registers[0x4A] = 0x5A;
}

void checkOnlyButtonWrites() {
  for (const auto& write : Wire.writes) {
    assert((write.reg == 0x41 && write.value == 0xFE) ||
           (write.reg == 0x49 && write.value == 0x08));
  }
  assert(Wire.registers[0x22] == 0x02 && Wire.registers[0x27] == 0x3A);
  assert(Wire.registers[0x18] == 0x0A && Wire.registers[0x68] == 0x01);
  assert(Wire.registers[0x40] == 0xA5 && Wire.registers[0x42] == 0x5A);
  assert(Wire.registers[0x48] == 0xA5 && Wire.registers[0x4A] == 0x5A);
  assert(Wire.timeout == 100);
}

void buttonChecks() {
  using Event = pet_power::ButtonEvent;
  seedButton();
  assert(pet_power::readButton() == Event::Unavailable); // Explicit init required.
  assert(pet_power::beginButton());
  const int beginOperations = Wire.operation;
  assert(Wire.registers[0x41] == 0xFE && Wire.registers[0x49] == 0xF7);
  assert(pet_power::readButton() == Event::None); // Stale short tap was discarded.
  Wire.registers[0x49] |= 0x08;
  assert(pet_power::readButton() == Event::Pressed);
  assert(Wire.registers[0x49] == 0xF7); // Only the handled bit was cleared.
  assert(pet_power::readButton() == Event::None);
  assert(pet_power::readButton() == Event::None); // Edge/long bits do not trigger.
  checkOnlyButtonWrites();

  // A physical hold first generates a falling edge, then a long-press event.
  // Neither, nor the later rising edge, should request a screen toggle. Leave
  // every unhandled flag for its owner instead of acknowledging the whole byte.
  for (uint8_t flags : {0x02, 0x06, 0x07, 0xF7}) {
    Wire.registers[0x49] = flags;
    const unsigned writes = Wire.dataWrites;
    assert(pet_power::readButton() == Event::None);
    assert(Wire.registers[0x49] == flags && Wire.dataWrites == writes);
  }
  Wire.registers[0x49] |= 0x08; // A later completed short tap still works.
  assert(pet_power::readButton() == Event::Pressed);
  checkOnlyButtonWrites();

  seedButton();
  Wire.registers[0x41] = 0xFE; // Short-press reporting was already enabled.
  Wire.registers[0x49] = 0xF7; // No stale short tap to acknowledge.
  assert(pet_power::beginButton());
  assert(Wire.dataWrites == 0);
  assert(pet_power::readButton() == Event::None);
  checkOnlyButtonWrites();

  // Every initialization failure leaves the API unavailable until a retry.
  for (int fail = 1; fail <= beginOperations; ++fail) {
    seedButton();
    Wire.failAt = fail;
    assert(!pet_power::beginButton());
    assert(pet_power::readButton() == Event::Unavailable);
    checkOnlyButtonWrites();
    Wire.failAt = 0;
    assert(pet_power::beginButton());
    assert(pet_power::readButton() == Event::None);
  }
  seedButton();
  Wire.ignoreIrqEnableWrites = true;
  assert(!pet_power::beginButton()); // Check enable readback, not just I2C ACK.
  assert(pet_power::readButton() == Event::Unavailable);

  // Failed reads cannot fabricate presses or consume a pending short tap.
  for (int fail = 1; fail <= 2; ++fail) {
    seedButton();
    assert(pet_power::beginButton());
    assert(pet_power::readButton() == Event::None);
    Wire.registers[0x49] |= 0x08;
    Wire.failAt = Wire.operation + fail;
    assert(pet_power::readButton() == Event::Unavailable);
    assert(Wire.registers[0x49] & 0x08);
    Wire.failAt = 0;
    assert(pet_power::readButton() == Event::Pressed);
    assert(pet_power::readButton() == Event::None);
    checkOnlyButtonWrites();
  }

  // Report the observed press despite failed acknowledgement, then retry the
  // clear without reporting that same latched event again.
  for (bool acknowledgedDespiteFailure : {false, true}) {
    seedButton();
    assert(pet_power::beginButton());
    assert(pet_power::readButton() == Event::None);
    Wire.registers[0x49] |= 0x08;
    Wire.failAt = Wire.operation + 3;
    Wire.nackAfterWrite = acknowledgedDespiteFailure;
    assert(pet_power::readButton() == Event::Pressed);
    Wire.failAt = 0;
    assert(pet_power::readButton() == Event::None);
    assert(pet_power::readButton() == Event::None);
    Wire.registers[0x49] |= 0x08;
    assert(pet_power::readButton() == Event::Pressed); // A later real press works.
    checkOnlyButtonWrites();
  }

  seedButton();
  assert(pet_power::beginButton());
  assert(pet_power::readButton() == Event::None);
  Wire.ignoreIrqClear = true;
  Wire.registers[0x49] |= 0x08;
  assert(pet_power::readButton() == Event::Pressed);
  assert(pet_power::readButton() == Event::None);
  assert(pet_power::readButton() == Event::None); // A stuck flag cannot repeat.
  checkOnlyButtonWrites();

  // If a stale short-tap clear is ACKed but the latch remains set, suppress it
  // across initialization too. Observe zero before recognizing the next tap.
  seedButton();
  Wire.ignoreIrqClear = true;
  assert(pet_power::beginButton());
  assert(pet_power::readButton() == Event::None);
  assert(pet_power::readButton() == Event::None);
  Wire.ignoreIrqClear = false;
  assert(pet_power::readButton() == Event::None);
  assert(pet_power::readButton() == Event::None);
  Wire.registers[0x49] |= 0x08;
  assert(pet_power::readButton() == Event::Pressed);
  checkOnlyButtonWrites();
}

int main() {
  seedBattery();
  Wire.timeout = 73; // Restoration must retain an arbitrary caller setting.
  uint8_t before[256];
  std::memcpy(before, Wire.registers, sizeof(before));
  auto status = pet_power::read();
  assert(status.available && status.batteryPresent && status.externalPower && status.charging);
  assert(status.percent == 57 && Wire.observedTimeout == 10 && Wire.timeout == 73);
  assert(Wire.dataWrites == 0 && std::memcmp(before, Wire.registers, sizeof(before)) == 0);
  const int readOperations = Wire.operation;

  // Missing PMIC and transport failures must not become fake 0% readings.
  seedBattery();
  Wire.present = false;
  status = pet_power::read();
  assert(!status.available && status.percent == -1 && Wire.timeout == 100);
  for (int failure = 1; failure <= readOperations; ++failure) {
    seedBattery();
    Wire.failAt = failure;
    status = pet_power::read();
    assert(status.percent == -1 && Wire.timeout == 100 && Wire.dataWrites == 0);
    if (failure <= 4) assert(!status.available);
    else assert(status.available && status.batteryPresent && status.externalPower && status.charging);
  }

  seedBattery();
  Wire.registers[0x00] = 0x08;
  Wire.registers[0x01] = 0x50; // Battery-only discharging.
  status = pet_power::read();
  assert(status.available && status.batteryPresent && !status.externalPower && !status.charging);
  assert(status.percent == 57);

  seedBattery();
  Wire.registers[0x00] = 0x20; // USB with no battery; stale gauge data is ignored.
  status = pet_power::read();
  assert(status.available && !status.batteryPresent && status.externalPower && !status.charging);
  assert(status.percent == -1);

  seedBattery();
  Wire.registers[0x68] = 0; // Disabled detection fabricates hardware presence.
  status = pet_power::read();
  assert(status.available && !status.batteryPresent && status.externalPower && !status.charging);
  assert(status.percent == -1);

  seedBattery();
  Wire.registers[0x18] &= ~0x08; // Do not trust cached percentage with gauge off.
  status = pet_power::read();
  assert(status.available && status.batteryPresent && status.charging && status.percent == -1);

  seedBattery();
  Wire.registers[0x01] = 0x14; // Charge-complete status, actual direction standby.
  status = pet_power::read();
  assert(status.available && status.externalPower && !status.charging && status.percent == 57);

  // Zero and 100 are real percentages. Other full-byte values are never masked
  // into range or replaced with estimates from battery voltage.
  const uint8_t percentages[] = {0, 100, 101, 128, 255};
  for (uint8_t percent : percentages) {
    seedBattery();
    Wire.registers[0xA4] = percent;
    status = pet_power::read();
    assert(status.available && status.percent == (percent <= 100 ? percent : -1));
  }

  seedBattery();
  Wire.registers[0x00] = 0xFF;
  assert(!pet_power::read().available);
  seedBattery();
  Wire.registers[0x01] = 0x70; // Reserved current direction.
  assert(!pet_power::read().available);
  buttonChecks();
  std::puts("Battery status read-only/failure checks passed");
  std::puts("PWR short-tap/long-hold exclusion and IRQ preservation/failure checks passed");
}
