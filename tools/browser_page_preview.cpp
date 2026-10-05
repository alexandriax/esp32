// Host-only screenshot of the production LHP engine composited with real UI.
// Usage: browser-page-preview input.html output.ppm [url] [scroll-y]
#include "browser_engine.h"
#include "browser_ui.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
extern "C" size_t moss_probe_network_attempts;
namespace {
std::vector<uint8_t> image(480*480*3,255);
unsigned nextRow=0,frames=0;
bool rendered(){
  for(unsigned n=0;n<20000;++n){
    const BrowserEngineState state=browser_engine_service();
    if(state==BROWSER_ENGINE_READY)return true;
    if(state==BROWSER_ENGINE_FAILED)return false;
  }
  return false;
}
void chrome(const char* url){
  sloth::BrowserUi ui;ui.show(url);ui.activate(sloth::BrowserUi::kMode);ui.setHasPage(true);
  ui.setStatus(browser_engine_clipped()?"LONG PAGE CLIPPED - SCROLL TO READ":
      !std::strncmp(url,"https://",8)?"HTTPS - SCROLL / TAP A LINK":"HTTP (UNENCRYPTED) - TAP A LINK");
  uint16_t row[240];
  for(unsigned y=0;y<240;++y){
    if(!sloth::browserChromeRow(y))continue;
    sloth::drawBrowserRow(row,ui,y);
    for(unsigned x=0;x<240;++x){
      const uint16_t color=row[x];
      const uint8_t red=static_cast<uint8_t>(((color>>11)&31)*255/31);
      const uint8_t green=static_cast<uint8_t>(((color>>5)&63)*255/63);
      const uint8_t blue=static_cast<uint8_t>((color&31)*255/31);
      for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){
        const size_t at=((2*y+dy)*480+2*x+dx)*3;
        image[at]=red;image[at+1]=green;image[at+2]=blue;
      }
    }
  }
}
}
extern "C" int browser_panel_begin(){nextRow=0;return 1;}
extern "C" int browser_panel_line(unsigned y,const uint8_t* rgb,size_t bytes){
  if(y!=nextRow||y>=BROWSER_VIEW_HEIGHT||bytes!=480*3)return 0;
  std::memcpy(&image[(y+BROWSER_PAGE_TOP*2)*480*3],rgb,bytes);++nextRow;return 1;
}
extern "C" int browser_panel_finish(){if(nextRow!=BROWSER_VIEW_HEIGHT)return 0;++frames;return 1;}
int main(int argc,char** argv){
  if(argc<3||argc>5){std::fprintf(stderr,"usage: %s input.html output.ppm [url] [scroll-y]\n",argv[0]);return 2;}
  unsigned scroll=0;
  if(argc==5){char* end=nullptr;errno=0;const unsigned long n=std::strtoul(argv[4],&end,10);if(errno||!end||*end||n>8192){std::fprintf(stderr,"invalid scroll offset\n");return 2;}scroll=static_cast<unsigned>(n);}
  FILE* file=std::fopen(argv[1],"rb");if(!file){std::perror(argv[1]);return 1;}
  char input[32769];const size_t bytes=std::fread(input,1,sizeof(input),file);
  const bool readError=std::ferror(file);std::fclose(file);
  if(readError||!bytes||bytes>32768){std::fprintf(stderr,"input must contain 1..32768 HTML bytes\n");return 1;}
  const std::string html(input,bytes);
  if(browser_engine_start(html.data(),html.size(),128*1024)!=0||!rendered()){
    std::fprintf(stderr,"production engine failed: error=%d\n",browser_engine_error());browser_engine_stop();return 1;
  }
  if(scroll&&(browser_engine_scroll(static_cast<int>(scroll))!=0||!rendered())){
    std::fprintf(stderr,"production engine scroll failed\n");browser_engine_stop();return 1;
  }
  const unsigned height=browser_engine_content_height(),offset=browser_engine_scroll_y();
  const size_t peak=browser_engine_peak_bytes();
  chrome(argc>=4?argv[3]:"https://example.com/");
  browser_engine_stop();
  if(moss_probe_network_attempts||browser_engine_live_bytes()){
    std::fprintf(stderr,"network attempt or retained engine allocation\n");return 1;
  }
  file=std::fopen(argv[2],"wb");if(!file){std::perror(argv[2]);return 1;}
  const bool written=std::fprintf(file,"P6\n480 480\n255\n")>0&&std::fwrite(image.data(),1,image.size(),file)==image.size();
  const int closed=std::fclose(file);
  if(!written||closed){std::fprintf(stderr,"output write failed\n");return 1;}
  std::printf("real production engine+UI: %zu HTML bytes, %u px content, scroll %u, %u frames, %zu peak engine bytes, 0 network attempts -> %s\n",bytes,height,offset,frames,peak,argv[2]);
}
