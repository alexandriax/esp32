#include "../firmware/sloth_pet/jpeg_display.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cstring>
#include <vector>
using Bytes = std::vector<uint8_t>;
static uint32_t hash(const uint16_t* p) {
  uint32_t c=2166136261u;
  for (unsigned i=0; i<240*240; ++i) {
    c=(c^(p[i]&255))*16777619u;
    c=(c^(p[i]>>8))*16777619u;
  }
  return c;
}
static uint32_t randomWord(uint32_t& state) {
  state^=state<<13; state^=state>>17; state^=state<<5; return state;
}
int main() {
  std::ifstream f("tests/fixtures/jpeg240-texture.jpg", std::ios::binary);
  const Bytes valid((std::istreambuf_iterator<char>(f)), {});
  assert(valid.size()==6636);
  const size_t workspaceBytes=sloth::jpeg_display::workspaceBytes();
  assert(workspaceBytes>=15360 && workspaceBytes<32768);
  assert(sloth::jpeg_display::workspaceAlignment()<=alignof(uint64_t));
  std::vector<uint64_t> scratch((workspaceBytes+7)/8+2,UINT64_C(0xa5a5a5a5a5a5a5a5));
  auto* workspace=reinterpret_cast<uint8_t*>(scratch.data()+1);
  const auto scratchGuards=[&]() {
    assert(scratch.front()==UINT64_C(0xa5a5a5a5a5a5a5a5));
    const uint8_t* end=reinterpret_cast<const uint8_t*>(scratch.data()+scratch.size());
    assert(std::all_of(static_cast<const uint8_t*>(workspace)+workspaceBytes,end,[](uint8_t b){return b==0xa5;}));
  };
  struct Frame { uint64_t before; uint16_t pixels[240*240]; uint64_t after; } frame{};
  frame.before=UINT64_C(0xdec0de1122334455); frame.after=~frame.before;
  const auto decode=[&](const Bytes& b) {
    const bool result=sloth::jpeg_display::decode(b.data(),b.size(),frame.pixels,240*240,
                                               workspace,workspaceBytes);
    assert(frame.before==UINT64_C(0xdec0de1122334455) && frame.after==~frame.before);
    scratchGuards();
    return result;
  };
  assert(decode(valid));
  assert(hash(frame.pixels)==UINT32_C(0x02aa0842)); // Pinned decoder RGB565 reference.
  assert(!sloth::jpeg_display::decode(valid.data(),valid.size(),nullptr,240*240,workspace,workspaceBytes));
  assert(!sloth::jpeg_display::decode(valid.data(),valid.size(),frame.pixels,240*240-1,workspace,workspaceBytes));
  assert(!sloth::jpeg_display::decode(nullptr,valid.size(),frame.pixels,240*240,workspace,workspaceBytes));
  // Workspace errors are rejected before touching either output or scratch.
  const uint32_t initialHash=hash(frame.pixels);
  const Bytes scratchBefore(workspace,workspace+workspaceBytes);
  const auto rejectWorkspace=[&](void* data,size_t size) {
    assert(!sloth::jpeg_display::decode(valid.data(),valid.size(),frame.pixels,240*240,data,size));
    assert(hash(frame.pixels)==initialHash);
    assert(std::equal(scratchBefore.begin(),scratchBefore.end(),workspace));
    scratchGuards();
  };
  rejectWorkspace(nullptr,workspaceBytes);
  rejectWorkspace(workspace,workspaceBytes-1);
  rejectWorkspace(workspace+1,workspaceBytes);
  rejectWorkspace(workspace,SIZE_MAX);
  rejectWorkspace(frame.pixels,sizeof(frame.pixels));
  rejectWorkspace(reinterpret_cast<uint8_t*>(&frame)+sizeof(frame.before)-2,workspaceBytes);
  rejectWorkspace(const_cast<uint8_t*>(valid.data()),workspaceBytes);
  assert(!sloth::jpeg_display::decode(valid.data(),valid.size(),frame.pixels,SIZE_MAX,workspace,workspaceBytes));
  assert(!sloth::jpeg_display::decode(valid.data(),valid.size(),
      reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(frame.pixels)+1),240*240,workspace,workspaceBytes));
  assert(hash(frame.pixels)==initialHash);
  // Overlap may begin before input/output, not only exactly at their address.
  std::vector<uint64_t> combined((workspaceBytes+valid.size()+16+7)/8);
  auto* combinedBytes=reinterpret_cast<uint8_t*>(combined.data());
  std::memcpy(combinedBytes+workspaceBytes-8,valid.data(),valid.size());
  assert(!sloth::jpeg_display::decode(combinedBytes+workspaceBytes-8,valid.size(),
      frame.pixels,240*240,combinedBytes,workspaceBytes));
  // Exact adjacency is safe and no prior decoder object/state is required.
  std::memcpy(combinedBytes+workspaceBytes,valid.data(),valid.size());
  assert(sloth::jpeg_display::decode(combinedBytes+workspaceBytes,valid.size(),
      frame.pixels,240*240,combinedBytes,workspaceBytes));
  assert(hash(frame.pixels)==initialHash);
  for (size_t n=0; n<valid.size(); ++n) {
    Bytes truncated(valid.begin(),valid.begin()+n);
    assert(!decode(truncated)); // Missing EOI, or incomplete header/scan.
  }
  // Wrong dimension/progressive frames/metadata/invalid markers are rejected
  // before pixel output, even though packet CRC could have been valid.
  size_t sof=0,sos=0;
  for(size_t at=2;at+4<valid.size();) {
    const unsigned marker=valid[at+1], length=valid[at+2]*256u+valid[at+3];
    if(marker==0xc0) sof=at;
    if(marker==0xda) {sos=at;break;}
    at+=length+2;
  }
  assert(sof && sos);
  for (size_t field : {sof+5,sof+7,sof+9,sof+11}) {
    Bytes bad=valid; bad[field]^=1; assert(!decode(bad));
  }
  Bytes bad=valid; bad[sof+1]=0xc2; assert(!decode(bad));
  bad=valid; bad[3]=0xe1; assert(!decode(bad));
  bad=valid; bad[4]=0xff; bad[5]=0xff; assert(!decode(bad));
  bad=valid; bad.push_back(0); assert(!decode(bad));
  // Valid headers but a deliberately empty scan cannot produce a complete frame.
  bad.assign(valid.begin(),valid.begin()+sos+14); bad.push_back(0xff); bad.push_back(0xd9);
  assert(!decode(bad));
  // A panel transfer may completely replace the previous decoder bytes.
  // Alternate success, malformed entropy, and arbitrary DMA-like overwrites.
  for(unsigned cycle=0;cycle<12;++cycle) {
    std::memset(workspace,static_cast<int>(cycle*23),workspaceBytes);
    assert(decode(valid) && hash(frame.pixels)==initialHash);
    std::memset(workspace,0x3c,workspaceBytes);
    assert(!decode(bad));
    std::memset(workspace,0x96,workspaceBytes);
  }
  uint32_t random=UINT32_C(0x491215aa);
  for(unsigned trial=0;trial<1500;++trial) {
    bad=valid;
    const unsigned changes=1+randomWord(random)%8;
    for(unsigned n=0;n<changes;++n) bad[randomWord(random)%bad.size()]^=static_cast<uint8_t>(1u<<(randomWord(random)%8));
    (void)decode(bad); // Arbitrary mutations may form another valid image; must remain bounded.
  }
  assert(decode(valid) && hash(frame.pixels)==UINT32_C(0x02aa0842));
  puts("JPEG display: shared workspace lifetime/alignment/alias guards, reference pixels, dimensions, metadata restrictions, truncation, malformed-input bounds and recovery passed");
}
