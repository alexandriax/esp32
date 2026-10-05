#include "game_records.h"
#include "pet_record.h"
#include <stdio.h>
#include <string.h>

namespace sloth {
namespace {
bool validKind(GameKind game) { return static_cast<unsigned>(game) < kGameCount; }
bool validScore(GameKind game, uint32_t score, uint16_t detail) {
  return validKind(game) && (game != GameKind::Pong || (score == 7 && detail < 7));
}
bool better(GameKind game, const GameScore& a, const GameScore& b) {
  if (a.score != b.score) return a.score > b.score;
  return game == GameKind::Pong ? a.detail < b.detail : a.detail > b.detail;
}
bool validName(const char* name) {
  size_t n = 0;
  for (; n <= kScoreNameLength && name[n]; ++n) {
    const char c = name[n];
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '\'')) return false;
  }
  return n > 0 && n <= kScoreNameLength && name[0] != ' ' && name[n - 1] != ' ';
}
bool validRecords(const GameRecords& records) {
  for (unsigned g = 0; g < kGameCount; ++g) {
    const auto& table = records.tables[g];
    if (table.count > kScoreCount) return false;
    for (unsigned i = 0; i < table.count; ++i) {
      const auto& entry = table.entries[i];
      if (!validName(entry.name) || !validScore(static_cast<GameKind>(g), entry.score, entry.detail) ||
          (i && better(static_cast<GameKind>(g), entry, table.entries[i - 1]))) return false;
    }
  }
  return true;
}
}

const char* gameTitle(GameKind game) {
  const char* const titles[kGameCount] = {"MOSS PONG", "3-TOED TETRIS", "LEAF SWEEP"};
  return validKind(game) ? titles[static_cast<unsigned>(game)] : "GAME";
}

bool qualifies(const GameRecords& records, GameKind game, uint32_t score, uint16_t detail) {
  if (!validScore(game, score, detail)) return false;
  const auto& table = records.tables[static_cast<unsigned>(game)];
  if (table.count > kScoreCount) return false;
  GameScore candidate; candidate.score = score; candidate.detail = detail;
  return table.count < kScoreCount || better(game, candidate, table.entries[kScoreCount - 1]);
}

bool addGameScore(GameRecords& records, GameKind game, uint32_t score, uint16_t detail, const char* name) {
  if (!name || !qualifies(records, game, score, detail)) return false;
  GameScore candidate;
  candidate.score = score; candidate.detail = detail;
  while (*name == ' ') ++name;
  size_t length = strlen(name);
  while (length && name[length - 1] == ' ') --length;
  if (!length || length > kScoreNameLength) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = name[i]; candidate.name[i] = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
  }
  if (!validName(candidate.name)) return false;
  auto& table = records.tables[static_cast<unsigned>(game)];
  unsigned position = 0;
  while (position < table.count && !better(game, candidate, table.entries[position])) ++position;
  if (table.count < kScoreCount) ++table.count;
  for (unsigned i = table.count - 1; i > position; --i) table.entries[i] = table.entries[i - 1];
  table.entries[position] = candidate;
  return true;
}

void formatGameScore(GameKind game, const GameScore& score, char* out, size_t size) {
  if (!out || !size) return;
  if (game == GameKind::Pong) snprintf(out, size, "%lu-%u", static_cast<unsigned long>(score.score), score.detail);
  else snprintf(out, size, "%lu", static_cast<unsigned long>(score.score));
}

bool encodeGameRecords(const GameRecords& records, uint8_t* out, size_t size) {
  if (!out || size != kGameRecordSize || !validRecords(records)) return false;
  memset(out, 0, size);
  memcpy(out, "GAME", 4); out[4] = 2;
  for (unsigned g = 0; g < kGameCount; ++g) {
    out[5] |= (records.music[g] ? 1u : 0u) << (g * 2);
    out[5] |= (records.effects[g] ? 2u : 0u) << (g * 2);
    out[6 + g] = records.tables[g].count;
    for (unsigned i = 0; i < records.tables[g].count; ++i) {
      uint8_t* p = out + 12 + (g * kScoreCount + i) * 17;
      const auto& entry = records.tables[g].entries[i];
      memcpy(p, entry.name, strlen(entry.name));
      record_detail::write32(p + 11, entry.score);
      record_detail::write16(p + 15, entry.detail);
    }
  }
  record_detail::write32(out + size - 4, record_detail::checksum(out, size - 4));
  return true;
}

bool decodeGameRecords(const uint8_t* data, size_t size, GameRecords& records) {
  if (!data || size < 5) return false;
  const bool legacy = data[4] == 1;
  if ((!legacy && data[4] != 2) || size != (legacy ? kLegacyGameRecordSize : kGameRecordSize) ||
      memcmp(data, "GAME", 4) || data[5] > (legacy ? 15 : 63) ||
      record_detail::read32(data + size - 4) != record_detail::checksum(data, size - 4)) return false;
  if (!legacy && (data[9] || data[10] || data[11])) return false;
  const unsigned games = legacy ? 2 : kGameCount;
  const size_t header = legacy ? 8 : 12;
  GameRecords result;
  for (unsigned g = 0; g < games; ++g) {
    if (data[6 + g] > kScoreCount) return false;
    result.music[g] = data[5] & (1u << (g * 2));
    result.effects[g] = data[5] & (2u << (g * 2));
    result.tables[g].count = data[6 + g];
    for (unsigned i = 0; i < kScoreCount; ++i) {
      const uint8_t* p = data + header + (g * kScoreCount + i) * 17;
      if (i >= result.tables[g].count) {
        for (unsigned b = 0; b < 17; ++b) if (p[b]) return false;
        continue;
      }
      auto& entry = result.tables[g].entries[i];
      memcpy(entry.name, p, 11);
      if (entry.name[10] != 0) return false;
      const size_t length = strlen(entry.name);
      for (size_t b = length; b < 11; ++b) if (p[b]) return false;
      entry.score = record_detail::read32(p + 11); entry.detail = record_detail::read16(p + 15);
    }
  }
  if (!validRecords(result)) return false;
  records = result;
  return true;
}
}  // namespace sloth
