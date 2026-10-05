#include "../firmware/sloth_pet/game_records.h"
#include "../firmware/sloth_pet/pet_record.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace sloth;
namespace {
void seal(uint8_t* data, size_t size = kGameRecordSize) { record_detail::write32(data + size - 4, record_detail::checksum(data, size - 4)); }
void reject(const uint8_t* data, size_t size) {
  GameRecords output;
  output.music[0] = true;
  assert(addGameScore(output, GameKind::Tetris, 12345, 99, "KEEP"));
  uint8_t before[kGameRecordSize], after[kGameRecordSize];
  assert(encodeGameRecords(output, before, sizeof(before)));
  assert(!decodeGameRecords(data, size, output));
  assert(encodeGameRecords(output, after, sizeof(after)));
  assert(!memcmp(before, after, sizeof(before)));
}
void ranking() {
  GameRecords records;
  assert(!records.music[0] && !records.music[1] && !records.music[2]);
  assert(records.effects[0] && records.effects[1] && records.effects[2]);
  assert(!qualifies(records, static_cast<GameKind>(255), 7, 0));
  assert(!qualifies(records, GameKind::Pong, 6, 0));
  assert(!qualifies(records, GameKind::Pong, 8, 1));
  assert(!qualifies(records, GameKind::Pong, 7, 7));
  assert(qualifies(records, GameKind::Pong, 7, 6));
  assert(addGameScore(records, GameKind::Pong, 7, 6, "slow"));
  assert(addGameScore(records, GameKind::Pong, 7, 0, " ace "));
  assert(addGameScore(records, GameKind::Pong, 7, 0, "second"));
  assert(!strcmp(records.tables[0].entries[0].name, "ACE"));
  assert(!strcmp(records.tables[0].entries[1].name, "SECOND"));
  assert(!strcmp(records.tables[0].entries[2].name, "SLOW"));
  for (unsigned i = 0; i < 7; ++i) assert(addGameScore(records, GameKind::Pong, 7, 3, "MID"));
  assert(records.tables[0].count == 10);
  assert(!qualifies(records, GameKind::Pong, 7, 6));
  assert(!addGameScore(records, GameKind::Pong, 7, 6, "TOO LATE"));
  assert(addGameScore(records, GameKind::Pong, 7, 2, "THIRD"));
  assert(!strcmp(records.tables[0].entries[2].name, "THIRD"));
  assert(records.tables[0].entries[9].detail == 3);
  assert(!qualifies(records, GameKind::Pong, 7, 3));
  for (unsigned i = 0; i < 10; ++i)
    assert(addGameScore(records, GameKind::Tetris, i * 100, i, "MOSS"));
  assert(records.tables[1].count == 10);
  for (unsigned i = 0; i < 10; ++i) assert(records.tables[1].entries[i].score == (9-i)*100);
  assert(!qualifies(records, GameKind::Tetris, 0, 0));
  assert(addGameScore(records, GameKind::Tetris, 0, 1, "LINES"));
  assert(records.tables[1].entries[9].detail == 1);
  assert(addGameScore(records, GameKind::Tetris, UINT32_MAX, UINT16_MAX, "TOP"));
  assert(records.tables[1].entries[0].score == UINT32_MAX);
  assert(records.tables[0].count == 10 && records.tables[0].entries[0].detail == 0);
  for (unsigned i = 0; i < 10; ++i)
    assert(addGameScore(records, GameKind::LeafSweep, i * 20, i * 2, "LEAF"));
  assert(!qualifies(records, GameKind::LeafSweep, 0, 0));
  assert(addGameScore(records, GameKind::LeafSweep, 180, 19, "MORE LEAF"));
  assert(!strcmp(records.tables[2].entries[0].name,"MORE LEAF"));
  assert(addGameScore(records, GameKind::LeafSweep, 180, 19, "TIED"));
  assert(!strcmp(records.tables[2].entries[1].name,"TIED"));
  assert(addGameScore(records, GameKind::LeafSweep, UINT32_MAX, UINT16_MAX, "LEAF MAX"));
  assert(records.tables[0].entries[0].detail == 0 && records.tables[1].entries[0].score == UINT32_MAX);
  uint8_t full[kGameRecordSize], roundTrip[kGameRecordSize];
  GameRecords loaded;
  assert(encodeGameRecords(records,full,sizeof(full)));
  assert(decodeGameRecords(full,sizeof(full),loaded));
  assert(loaded.tables[0].count==10 && loaded.tables[1].count==10 && loaded.tables[2].count==10);
  assert(encodeGameRecords(loaded,roundTrip,sizeof(roundTrip)));
  assert(!memcmp(full,roundTrip,sizeof(full)));

  GameRecords names;
  const char* invalid[] = {nullptr, "", "    ", "ABCDEFGHIJK", "MOSS!", "MOSS\n", "M\xc3\xb6SS"};
  for (const char* name : invalid) {
    assert(!addGameScore(names, GameKind::Tetris, 50, 2, name));
    assert(names.tables[1].count == 0);
  }
  assert(addGameScore(names, GameKind::Tetris, 50, 2, "  o'leaf-7  "));
  assert(!strcmp(names.tables[1].entries[0].name, "O'LEAF-7"));
  assert(addGameScore(names, GameKind::Tetris, 50, 2, "0123456789"));
  assert(!strcmp(names.tables[1].entries[1].name, "0123456789"));
  char score[16], tiny[3] = {};
  GameScore s; s.score = 7; s.detail = 2;
  formatGameScore(GameKind::Pong, s, score, sizeof(score)); assert(!strcmp(score,"7-2"));
  s.score = UINT32_MAX;
  formatGameScore(GameKind::Tetris, s, score, sizeof(score)); assert(!strcmp(score,"4294967295"));
  formatGameScore(GameKind::LeafSweep, s, score, sizeof(score)); assert(!strcmp(score,"4294967295"));
  assert(!strcmp(gameTitle(GameKind::LeafSweep), "LEAF SWEEP"));
  formatGameScore(GameKind::Tetris, s, tiny, sizeof(tiny)); assert(!strcmp(tiny,"42"));
  formatGameScore(GameKind::Tetris, s, nullptr, 20);
  formatGameScore(GameKind::Tetris, s, tiny, 0); assert(!strcmp(tiny,"42"));
}
void persistence() {
  GameRecords records, decoded;
  assert(addGameScore(records, GameKind::Pong, 7, 2, "MOSS"));
  assert(addGameScore(records, GameKind::Tetris, 0x12345678, 0x1234, "FERN"));
  uint8_t data[kGameRecordSize], copy[kGameRecordSize], again[kGameRecordSize];
  for (unsigned bits = 0; bits < 64; ++bits) {
    for (unsigned g = 0; g < kGameCount; ++g) {
      records.music[g] = bits & (1u << (g*2));
      records.effects[g] = bits & (2u << (g*2));
    }
    assert(encodeGameRecords(records, data, sizeof(data)));
    assert(data[4] == 2 && data[5] == bits && data[6] == 1 && data[7] == 1 && data[8] == 0);
    assert(!data[9] && !data[10] && !data[11]);
    assert(decodeGameRecords(data, sizeof(data), decoded));
    assert(encodeGameRecords(decoded, again, sizeof(again)));
    assert(!memcmp(data, again, sizeof(data)));
  }
  const size_t tetris = 12 + 10*17;
  assert(data[tetris+11] == 0x78 && data[tetris+14] == 0x12);
  assert(data[tetris+15] == 0x34 && data[tetris+16] == 0x12);
  reject(nullptr, sizeof(data));
  for (size_t size = 0; size < sizeof(data); ++size) reject(data, size);
  reject(data, sizeof(data)+1);
  for (size_t i = 0; i < sizeof(data); ++i) {
    memcpy(copy, data, sizeof(copy)); copy[i] ^= 1; reject(copy, sizeof(copy));
  }
  // Recompute CRC for structurally invalid and noncanonical records. CRC alone
  // must not authorize bad lengths, enum bits, score ordering or hidden strings.
  const size_t offsets[] = {0,4,5,6,7,8,9,10,11,12,12+4,12+10,12+11,12+15,12+17,tetris};
  const uint8_t values[] = {'X',3,64,11,11,11,1,1,1,'m',' ',1,6,7,1,'!'};
  for (unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
    memcpy(copy,data,sizeof(copy)); copy[offsets[i]]=values[i]; seal(copy); reject(copy,sizeof(copy));
  }
  memcpy(copy,data,sizeof(copy)); copy[12+5]='X'; seal(copy); reject(copy,sizeof(copy));
  // Deterministic authenticated mutations exercise deeper parsing. Accepted
  // records must re-encode byte-identically; rejected ones preserve the output.
  uint32_t random = 0x50a67b19;
  for (unsigned trial=0;trial<2000;++trial) {
    memcpy(copy,data,sizeof(copy));
    random=random*1664525u+1013904223u;
    const size_t offset=random%(sizeof(copy)-4);
    random=random*1664525u+1013904223u;
    copy[offset]=static_cast<uint8_t>(random>>24); seal(copy);
    GameRecords candidate;
    if(decodeGameRecords(copy,sizeof(copy),candidate)) {
      assert(encodeGameRecords(candidate,again,sizeof(again)));
      assert(!memcmp(copy,again,sizeof(copy)));
    } else reject(copy,sizeof(copy));
  }
  GameRecords sorted;
  assert(addGameScore(sorted,GameKind::Pong,7,0,"FIRST"));
  assert(addGameScore(sorted,GameKind::Pong,7,6,"LAST"));
  assert(encodeGameRecords(sorted,copy,sizeof(copy)));
  copy[12+15]=6; copy[12+17+15]=0; seal(copy); reject(copy,sizeof(copy));
  sorted.tables[0].entries[0].detail=7;
  memset(copy,0xa5,sizeof(copy));
  assert(!encodeGameRecords(sorted,copy,sizeof(copy)));
  for (uint8_t byte : copy) assert(byte==0xa5);
  assert(!encodeGameRecords(records,nullptr,sizeof(data)));
  assert(!encodeGameRecords(records,copy,sizeof(copy)-1));
  assert(!encodeGameRecords(records,copy,sizeof(copy)+1));
  GameRecords empty;
  assert(encodeGameRecords(empty,copy,sizeof(copy)));
  assert(decodeGameRecords(copy,sizeof(copy),decoded));
  assert(!decoded.tables[0].count && !decoded.tables[1].count);
}

void legacyMigration() {
  static_assert(kLegacyGameRecordSize == 352 && kGameRecordSize == 526, "Stable save layouts");
  uint8_t legacy[kLegacyGameRecordSize] = {}, corrupt[kLegacyGameRecordSize];
  // Independent construction of the released v1 layout, with both boards full.
  // This deliberately does not derive the fixture from the new v2 encoder.
  memcpy(legacy,"GAME",4); legacy[4]=1; legacy[6]=legacy[7]=10;
  for(unsigned g=0;g<2;++g) for(unsigned i=0;i<10;++i) {
    uint8_t* p=legacy+8+(g*10+i)*17;
    snprintf(reinterpret_cast<char*>(p),11,g?"TETRIS %u":"PONG %u",i);
    record_detail::write32(p+11,g?5000-i*333:7);
    record_detail::write16(p+15,g?50-i:i*6/9);
  }
  for(unsigned flags=0;flags<16;++flags) {
    legacy[5]=flags; seal(legacy,sizeof(legacy));
    GameRecords migrated;
    assert(addGameScore(migrated,GameKind::LeafSweep,42,3,"REPLACE"));
    migrated.music[2]=true; migrated.effects[2]=false;
    assert(decodeGameRecords(legacy,sizeof(legacy),migrated));
    assert(!migrated.music[2] && migrated.effects[2] && migrated.tables[2].count==0);
    for(unsigned g=0;g<2;++g) {
      assert(migrated.music[g]==!!(flags&(1u<<(g*2))));
      assert(migrated.effects[g]==!!(flags&(2u<<(g*2))));
      assert(migrated.tables[g].count==10);
      for(unsigned i=0;i<10;++i) {
        const uint8_t* p=legacy+8+(g*10+i)*17;
        const auto& entry=migrated.tables[g].entries[i];
        assert(!strcmp(entry.name,reinterpret_cast<const char*>(p)));
        assert(entry.score==record_detail::read32(p+11));
        assert(entry.detail==record_detail::read16(p+15));
      }
    }
    uint8_t saved[kGameRecordSize], again[kGameRecordSize];
    assert(addGameScore(migrated,GameKind::LeafSweep,1250,40,"NEW LEAF"));
    assert(encodeGameRecords(migrated,saved,sizeof(saved)));
    assert(saved[4]==2 && saved[5]==(flags|32));
    GameRecords reloaded;
    assert(decodeGameRecords(saved,sizeof(saved),reloaded));
    assert(reloaded.tables[2].count==1 && reloaded.tables[2].entries[0].score==1250);
    assert(encodeGameRecords(reloaded,again,sizeof(again)) && !memcmp(saved,again,sizeof(saved)));
  }
  for(size_t size=0;size<sizeof(legacy);++size) reject(legacy,size);
  reject(legacy,sizeof(legacy)+1);
  for(size_t i=0;i<sizeof(legacy);++i) {
    memcpy(corrupt,legacy,sizeof(corrupt)); corrupt[i]^=1; reject(corrupt,sizeof(corrupt));
  }
  const size_t offsets[]={4,5,6,7,8,18,23,8+17+15};
  const uint8_t values[]={2,16,11,11,'p',1,7,7};
  for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
    memcpy(corrupt,legacy,sizeof(corrupt)); corrupt[offsets[i]]=values[i];
    seal(corrupt,sizeof(corrupt)); reject(corrupt,sizeof(corrupt));
  }
  // Empty v1 saves migrate too, but nonzero unused slots remain forbidden.
  memset(legacy,0,sizeof(legacy)); memcpy(legacy,"GAME",4);legacy[4]=1;legacy[5]=10;
  seal(legacy,sizeof(legacy)); GameRecords empty;
  assert(decodeGameRecords(legacy,sizeof(legacy),empty));
  for(unsigned g=0;g<kGameCount;++g) assert(!empty.music[g] && empty.effects[g] && !empty.tables[g].count);
  legacy[8]=1; seal(legacy,sizeof(legacy)); reject(legacy,sizeof(legacy));
}
}
int main() {
  ranking(); persistence(); legacyMigration();
  puts("Game records passed: three independent ten-score tables, stable ties, all 64 audio preferences, canonical v2, complete v1 migration, CRC and structural rejection preserve output");
}
