#include "../firmware/sloth_pet/radio_explorer.h"
#include "../firmware/sloth_pet/menu_ui.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char**argv){
 if(argc<3){fprintf(stderr,"Usage: utility_preview output.ppm menu|utilities|wifi|ble|detail|ble-detail|ble-data|direction|empty|error\n");return 1;}
 const char* mode=argv[2];uint16_t pixels[240*240];sloth::MenuUi menu;menu.open();
 if(!strcmp(mode,"menu"))sloth::drawMenu(pixels,menu);
 else if(!strcmp(mode,"utilities")){menu.activate(sloth::MenuUi::kMainUtilities);sloth::drawMenu(pixels,menu);}
 else{
  sloth::RadioSnapshot data;data.kind=strstr(mode,"ble")?sloth::RadioKind::Bluetooth:sloth::RadioKind::Wifi;data.state=sloth::RadioScanState::Scanning;
  const char* names[]={"MOSS LAB","COFFEE AND QUESTIONS","NEIGHBORHOOD NETWORK","","A VERY LONG NETWORK NAME FOR QA","GARDEN LIGHTS"};
  if(strcmp(mode,"empty")&&strcmp(mode,"error"))for(unsigned i=0;i<6;++i){sloth::RadioEntry e;strncpy(e.name,names[i],32);e.address[0]=0xBE;e.address[3]=0x12;e.address[4]=0x34;e.address[5]=i;e.rssi=-43-i*7;e.channel=1+i*2;e.security=i%8;e.seenAt=1000;
   const uint8_t adv[]={2,1,6,3,3,0x0f,0x18,2,10,0xfc,5,0xff,0x4c,0,0x12,0x34};sloth::radioAdvertising(e,adv,sizeof(adv));sloth::radioObserve(data,e);}
  if(!strcmp(mode,"error"))data.state=sloth::RadioScanState::Error;
  sloth::RadioExplorerUi ui;ui.open(data.kind);ui.update(data,1000);
  if(strstr(mode,"detail")||!strcmp(mode,"direction")||!strcmp(mode,"ble-data")){ui.activate(0);ui.update(data,1000);}
  uint32_t now=1000;
  if(!strcmp(mode,"direction")){float angle=0;for(unsigned step=0;step<=250;++step){now=1000+step*20;float rate=step<100?20:-20;if(step)angle+=rate*.02f;ui.motion(0,-rate,0,0,1,0,now);if(step%10==0){data.entries[0].seenAt=now;data.entries[0].rssi=-70+angle*.4f;ui.update(data,now);}}}
  if(!strcmp(mode,"ble-data")){ui.activate(sloth::RadioExplorerUi::kMore);ui.activate(sloth::RadioExplorerUi::kMore);}
  sloth::drawRadioExplorer(pixels,ui,now);
 }
 FILE*f=fopen(argv[1],"wb");if(!f)return 2;fprintf(f,"P6\n480 480\n255\n");
 for(int y=0;y<480;++y)for(int x=0;x<480;++x){uint16_t p=pixels[(y/2)*240+x/2];unsigned char rgb[]={static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};fwrite(rgb,1,3,f);}return fclose(f)?3:0;
}
