#include "../firmware/sloth_pet/remote_display_ui.h"
#include <stdio.h>
#include <string.h>
#include <vector>
int main(int argc,char** argv) {
  if(argc!=3)return 2;
  sloth::RemoteUi ui;
  ui.show(sloth::RemoteStatus::NoHost,true);
  if(strcmp(argv[2],"status")) {
    ui.editNetwork("My Wi-Fi Network");
    if(!strcmp(argv[2],"keyboard") || !strcmp(argv[2],"symbols")) {
      ui.activate(sloth::RemoteUi::kSsid);
      if(!strcmp(argv[2],"symbols")) { ui.touch(true,30,130);ui.touch(true,30,-600);ui.touch(false,30,130); }
    }
  }
  std::vector<uint16_t> pixels(240*240);sloth::drawRemoteDisplay(pixels.data(),ui);
  FILE* file=fopen(argv[1],"wb");if(!file)return 1;
  fprintf(file,"P6\n240 240\n255\n");
  for(uint16_t p:pixels) { const unsigned char rgb[]={static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};fwrite(rgb,1,3,file); }
  return fclose(file)?1:0;
}
