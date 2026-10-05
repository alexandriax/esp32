#include "../firmware/sloth_pet/screen_rotation.h"
#include <cassert>
#include <cstdio>
int main() {
  for(unsigned size=240;size<=480;size+=240)for(unsigned turns=0;turns<4;++turns)
    for(unsigned sy=0;sy<size;++sy)for(unsigned sx=0;sx<size;++sx) {
      unsigned x=sx,y=sy;for(unsigned t=0;t<turns;++t){const unsigned old=x;x=size-1-y;y=old;}
      assert(sloth::rotatedSource(x,y,size,turns)==sy*size+sx);
      assert(sloth::unrotateTouch(x,y,size,turns)&&x==sx&&y==sy);
    }
  unsigned x=480,y=0;assert(!sloth::unrotateTouch(x,y,480,1)&&x==480&&y==0);
  float fx=2,fy=3;sloth::unrotateVector(fx,fy,1);assert(fx==3&&fy==-2);
  sloth::unrotateVector(fx,fy,3);assert(fx==2&&fy==3);
  puts("Screen rotation: all panel pixels, inverse touch, bounds and tilt passed");
}
