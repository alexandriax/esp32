#include "Arduino.h"
#include "Wire.h"
#include "sensors.h"
#include <cassert>
#include <cstdio>
#include <cstring>
unsigned elapsedDelay=0;
SerialMock Serial;
WireMock Wire;
void packet(uint16_t rx, uint16_t ry, uint8_t status=6, uint8_t count=1) {
  memset(Wire.touch,0,sizeof(Wire.touch));
  Wire.touch[0]=status; Wire.touch[1]=ry>>4; Wire.touch[2]=rx>>4;
  Wire.touch[3]=((ry&15)<<4)|(rx&15); Wire.touch[5]=count; Wire.touch[6]=0xAB;
}
int main() {
  Wire.present[0x6A]=true; Wire.present[0x5A]=true; // Exercise low-address fallback.
  Wire.registers[0x6A][0]=5;
  auto cap=sensors::begin();
  assert(cap.touch && cap.acceleration && elapsedDelay==600 && Wire.timeout==100);
  assert(Wire.registers[0x6A][2]==0x40 && Wire.registers[0x6A][3]==0x16 && Wire.registers[0x6A][8]==1);
  float x=9,y=8,z=7;
  assert(!sensors::readAcceleration(x,y,z) && x==9 && y==8 && z==7);
  Wire.registers[0x6A][0x2E]=1;
  Wire.registers[0x6A][0x35]=0; Wire.registers[0x6A][0x36]=0x20; // +1g.
  Wire.registers[0x6A][0x37]=0; Wire.registers[0x6A][0x38]=0xE0; // -1g.
  Wire.registers[0x6A][0x39]=0; Wire.registers[0x6A][0x3A]=0x10; // +0.5g.
  assert(sensors::readAcceleration(x,y,z) && x==1 && y==-1 && z==0.5f);
  Wire.shortRead=true;
  assert(!sensors::readAcceleration(x,y,z) && x==1 && y==-1 && z==0.5f);
  assert(!sensors::gyroscopeEnabled());
  assert(sensors::enableGyroscope(true) && sensors::gyroscopeEnabled());
  assert(Wire.registers[0x6A][3]==0x16 && Wire.registers[0x6A][4]==0x56 && Wire.registers[0x6A][8]==3);
  float gx=90,gy=80,gz=70;
  assert(!sensors::readMotion(x,y,z,gx,gy,gz)); // Accel-only ready flag is insufficient.
  Wire.registers[0x6A][0x2E]=3;
  Wire.registers[0x6A][0x3B]=0x40; Wire.registers[0x6A][0x3C]=0; // +1dps.
  Wire.registers[0x6A][0x3D]=0x80; Wire.registers[0x6A][0x3E]=0xFD; // -10dps.
  Wire.registers[0x6A][0x3F]=0x80; Wire.registers[0x6A][0x40]=0; // +2dps.
  for (int warmup=0;warmup<5;++warmup) {
    assert(!sensors::readMotion(x,y,z,gx,gy,gz));
    assert(gx==90 && gy==80 && gz==70);
  }
  assert(sensors::readMotion(x,y,z,gx,gy,gz));
  assert(x==1 && y==-1 && z==0.5f && gx==1 && gy==-10 && gz==2);
  Wire.shortRead=true;
  assert(!sensors::readMotion(x,y,z,gx,gy,gz) && gx==1 && gy==-10 && gz==2);
  Wire.shortRead=true; // A failed disable verification must be retried.
  assert(!sensors::enableGyroscope(false));
  assert(sensors::enableGyroscope(false) && !sensors::gyroscopeEnabled());
  assert(Wire.registers[0x6A][8]==1 && Wire.registers[0x6A][3]==0x16);
  assert(!sensors::readMotion(x,y,z,gx,gy,gz));
  Wire.shortRead=true;
  Wire.rejectedRegister=8; Wire.rejectedValue=1; // Both verification and rollback fail.
  assert(!sensors::enableGyroscope(true) && !sensors::gyroscopeEnabled());
  assert(Wire.registers[0x6A][8]==3);
  Wire.rejectedRegister=-1; Wire.rejectedValue=-1;
  assert(sensors::enableGyroscope(false)); // Cached false cannot skip recovery.
  assert(Wire.registers[0x6A][8]==1);
  uint16_t tx=111,ty=222;
  packet(0,0);
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Pressed && tx==479 && ty==0);
  packet(479,479,7);
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Pressed && tx==0 && ty==479);
  tx=ty=0; // Real callers use fresh zeroed outputs on every poll.
  packet(123,234,0); // Release retains count but has unrelated coordinates.
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Released && tx==0 && ty==479);
  tx=ty=0;
  packet(0,0,6,0);
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Released && tx==0 && ty==479);
  packet(480,234);
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Error && tx==0 && ty==479);
  packet(123,480);
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Error);
  packet(123,234); Wire.touch[6]=0;
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Error);
  packet(123,234); Wire.shortRead=true;
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Error && tx==0 && ty==479);
  assert(Wire.timeout==100);
  Wire.present[0x6A]=false; Wire.present[0x5A]=false;
  cap=sensors::begin();
  assert(!cap.touch && !cap.acceleration);
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Unavailable);
  assert(!sensors::readAcceleration(x,y,z));
  assert(Wire.timeout==100);
  Wire.present[0x5A]=true;cap=sensors::begin();assert(cap.touch);
  tx=111;ty=222;packet(0,0,0,0);
  assert(sensors::readTouch(tx,ty)==sensors::TouchStatus::Released && tx==0 && ty==0);
  puts("sensor mock checks passed: release position survives fresh outputs and resets on initialization");
}
