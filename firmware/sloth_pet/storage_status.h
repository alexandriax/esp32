#pragma once

#include <stdint.h>

namespace storage_status {

enum class CardState : uint8_t {
  Unchecked, Checking, Ready, NoCard, Unavailable, Error
};

// Physical flash and reserved partition capacity are not filesystem free space.
// NVS entries include metadata/overhead; they are not bytes of user content.
struct Snapshot {
  bool flashValid = false;
  uint64_t flashBytes = 0;
  bool firmwareValid = false;
  uint64_t firmwareBytes = 0;
  uint64_t firmwareCapacityBytes = 0;
  bool nvsValid = false;
  uint64_t nvsPartitionBytes = 0;
  uint64_t nvsUsedEntries = 0;
  uint64_t nvsTotalEntries = 0;
  CardState cardState = CardState::Unchecked;
  uint64_t cardCapacityBytes = 0;  // Physical card size, even if FAT is unreadable.
  // Data-cluster capacity and allocation of the first supported FAT volume.
  uint64_t cardTotalBytes = 0;
  uint64_t cardUsedBytes = 0;
  uint64_t cardFreeBytes = 0;
  bool cardSpaceKnown = false;
  bool cardSpaceCached = false;
  int32_t cardError = 0;
};

// Put an inserted card into SPI mode before the display's first transaction.
// Call after the shared SPI bus and LCD IO device exist, with LCD CS idle.
// This performs no filesystem mount, capacity scan, NVS access, or writes.
void prepareCard();

// Read-only diagnostics after board::begin() and normal NVS initialization.
// Run only on demand, on one worker at a time. The SD check shares the existing
// display SPI bus and never changes its data/clock pins, PMIC rails, or contents.
// Suspend LCD traffic for the entire probe: mixing the display DMA path with
// SD polling transactions can assert inside the C6 SPI HAL. cardReleased runs
// once on the calling worker only after filesystem and SD-device cleanup.
Snapshot onboard(); // Fast, independent of the SD probe.
// Normal checks use validated FAT metadata. Unknown free counts are reported
// as unknown, never trigger a full-card scan. recount is an explicit diagnostic.
Snapshot read(void (*cardReleased)() = nullptr, bool recount = false);

}  // namespace storage_status
