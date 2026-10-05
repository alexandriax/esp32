#include "../firmware/sloth_pet/browser_ui.h"
#include <stdio.h>
#include <string.h>
using namespace sloth;
int main(int argc,char** argv) {
  if(argc!=3){fprintf(stderr,"Usage: browser_preview output.ppm ready|loading|error|keyboard|keyboard-end|page\n");return 1;}
  BrowserUi ui;ui.show();
  if(!strcmp(argv[2],"loading")){ui.setLoading(true);ui.setStatus("CONNECTING TO SITE...");}
  else if(!strcmp(argv[2],"error"))ui.setStatus("SITE COULD NOT BE LOADED");
  else if(!strcmp(argv[2],"keyboard") || !strcmp(argv[2],"keyboard-end")){
    ui.setUrl("https://example.com/Case?search=Hello%20World");ui.activate(BrowserUi::kUrl);
    if(!strcmp(argv[2],"keyboard-end"))for(int i=0;i<94;++i)ui.next();
  } else if(!strcmp(argv[2],"page")){ui.setHasPage(true);ui.setStatus("PAGE LOADED");}
  else if(strcmp(argv[2],"ready")){fprintf(stderr,"Unknown preview state\n");return 1;}
  FILE* file=fopen(argv[1],"wb");if(!file)return 1;
  fprintf(file,"P6\n240 240\n255\n");uint16_t row[240];
  for(unsigned y=0;y<240;++y){
    drawBrowserRow(row,ui,y);
    for(unsigned x=0;x<240;++x){const unsigned c=row[x];const unsigned char rgb[]={static_cast<unsigned char>(((c>>11)&31)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};fwrite(rgb,1,3,file);}
  }
  return fclose(file)?1:0;
}
