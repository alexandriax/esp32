#include "../firmware/sloth_pet/wifi_networks_ui.h"
#include <stdio.h>
#include <string.h>
#include <vector>
int main(int argc,char** argv) {
  if(argc!=3){fprintf(stderr,"Usage: %s output.ppm list|bottom|password|network|forget|waiting|connecting|connected|failed|empty\n",argv[0]);return 2;}
  using namespace sloth;
  wifi_networks::Snapshot s={};s.state=wifi_networks::State::Ready;s.hasSaved=true;
  strcpy(s.savedSsid,"My Home WiFi");s.count=7;
  const char* names[]={"My Home WiFi","Coffee Shop","Guest Network","Office Enterprise","Workshop_2G","Cafe Upstairs","Hidden Garden"};
  for(unsigned i=0;i<s.count;++i){strcpy(s.networks[i].ssid,names[i]);s.networks[i].rssi=-33-static_cast<int>(i)*6;s.networks[i].secured=i!=2;s.networks[i].supported=i!=3;}
  WifiNetworksUi ui;ui.show(s);
  if(!strcmp(argv[2],"bottom")){ui.touch(true,30,90);ui.touch(true,30,-200);ui.touch(false,30,100);}
  else if(!strcmp(argv[2],"password") || !strcmp(argv[2],"network")) {
    ui.activate(1);
    if(!strcmp(argv[2],"password"))ui.activate(WifiNetworkEditor::kPassword);
  } else if(!strcmp(argv[2],"forget"))ui.activate(WifiNetworksUi::kForget);
  else if(!strcmp(argv[2],"waiting"))ui.setWaiting(true);
  else if(!strcmp(argv[2],"connecting")){s.state=wifi_networks::State::Joining;strcpy(s.currentSsid,s.savedSsid);ui.update(s);}
  else if(!strcmp(argv[2],"connected")){s.state=wifi_networks::State::Connected;strcpy(s.currentSsid,s.savedSsid);strcpy(s.address,"192.168.1.42");ui.update(s);}
  else if(!strcmp(argv[2],"failed")){s.state=wifi_networks::State::Failed;s.error=wifi_networks::Error::Join;ui.update(s);}
  else if(!strcmp(argv[2],"empty")){s={};ui.show(s);}
  else if(strcmp(argv[2],"list"))return 2;
  std::vector<uint16_t> pixels(240*240);drawWifiNetworks(pixels.data(),ui);
  FILE* out=fopen(argv[1],"wb");if(!out)return 1;
  if(fprintf(out,"P6\n240 240\n255\n")<0){fclose(out);return 1;}
  for(uint16_t p:pixels){const unsigned char rgb[]={static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};if(fwrite(rgb,1,3,out)!=3){fclose(out);return 1;}}
  return fclose(out)?1:0;
}
