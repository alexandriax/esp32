#include "browser_assets.h"
#include "browser_memory.h"
#include <libwebsockets.h>
#include <string.h>
#include <stddef.h>
#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
namespace sloth { namespace browser_assets {
namespace {
union Allocation { max_align_t align;size_t size; };
size_t live=0,limit=64*1024;bool oom=false;
void* allocate(void* p,size_t n,const char*){
  auto* old=p?static_cast<Allocation*>(p)-1:nullptr;size_t before=old?old->size:0;
  if(!n){live-=before;browser_memory_free(old);return nullptr;}
  if(n>limit || live-before>limit-n){oom=true;return nullptr;}
  auto* next=static_cast<Allocation*>(browser_memory_realloc(old,sizeof(Allocation)+n));if(!next){oom=true;return nullptr;}
  next->size=n;live=live-before+n;return next+1;
}
void quiet(int,const char*){}
uint32_t big(const uint8_t* p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
void storeBig(uint8_t* p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
uint32_t crc(const uint8_t* p,size_t n){uint32_t v=~0u;while(n--){v^=*p++;for(unsigned b=0;b<8;++b)v=(v>>1)^(0xedb88320u&-(v&1));}return ~v;}
// Read frame dimensions before LWS reduces progressive JPEG to a DC preview.
// This both bounds the real source and lets us present that preview at a useful
// size. Segment lengths are checked; large EXIF/ICC data is skipped on disk.
bool jpegDimensions(storage_files::File* input,unsigned& width,unsigned& height,Cancel cancel){
 namespace fs=storage_files;const uint32_t size=fs::size(input);uint32_t pos=2;
 for(unsigned segments=0;segments<256&&pos<size-4&&!cancel();++segments){
  uint8_t b[10];if(!fs::seek(input,pos)||fs::read(input,b,4)!=4||b[0]!=255)return false;
  if(b[1]==255){++pos;continue;}const unsigned n=(unsigned(b[2])<<8)|b[3];
  if(n<2||n>size-pos-2)return false;
  if(b[1]==0xc0||b[1]==0xc2){if(n<8||fs::read(input,b+4,6)!=6||b[4]!=8)return false;
   height=(unsigned(b[5])<<8)|b[6];width=(unsigned(b[7])<<8)|b[8];return width&&width<=1280&&height&&height<=2048;
  }
  if(b[1]==0xda||b[1]==0xd9)return false;pos+=n+2;
 }return false;
}
struct Palette {
 uint8_t rgba[256][4];unsigned count=0,depth=0,width=0;uint32_t data=0;
};
bool indexedHeader(storage_files::File* input,uint8_t* header,Palette& pal,Cancel cancel){
 namespace fs=storage_files;
 pal.width=big(header+16);pal.depth=header[24];
 if(pal.depth!=1&&pal.depth!=2&&pal.depth!=4&&pal.depth!=8)return false;
 if(crc(header+12,17)!=big(header+29))return false;
 for(auto& color:pal.rgba){color[0]=color[1]=color[2]=0;color[3]=255;}
 uint32_t pos=33;bool transparent=false;
 // Metadata cannot consume an unbounded scan or seek budget. Palette and alpha
 // chunks are small and validated before feeding any compressed pixels.
 for(unsigned chunks=0;chunks<128&&pos<=65536&&!cancel();++chunks){
  uint8_t chunk[8],bytes[772];
  if(!fs::seek(input,pos)||fs::read(input,chunk,8)!=8)return false;
  const uint32_t n=big(chunk),size=fs::size(input);
  if(pos>size||size-pos<12||n>size-pos-12)return false;
  if(!memcmp(chunk+4,"IDAT",4)){
   if(!pal.count)return false;pal.data=pos;
   // Expose packed index bytes as gray8 to uPNG, then expand the palette while
   // producing the scaled RGBA row. The original file is never modified.
   storeBig(header+16,(pal.width*pal.depth+7)/8);header[24]=8;header[25]=0;
   storeBig(header+29,crc(header+12,17));return true;
  }
  if(!memcmp(chunk+4,"PLTE",4)||!memcmp(chunk+4,"tRNS",4)){
   if(n>768||fs::read(input,bytes,n+4)!=n+4)return false;
   uint8_t checked[772];memcpy(checked,chunk+4,4);memcpy(checked+4,bytes,n);
   if(crc(checked,n+4)!=big(bytes+n))return false;
   if(chunk[4]=='P'){
    if(pal.count||!n||n%3||n/3>(1u<<pal.depth))return false;
    pal.count=n/3;for(unsigned j=0;j<pal.count;++j)memcpy(pal.rgba[j],bytes+j*3,3);
   }else{
    if(!pal.count||transparent||!n||n>pal.count)return false;
    transparent=true;for(unsigned j=0;j<n;++j)pal.rgba[j][3]=bytes[j];
   }
  }else if(!(chunk[4]&32))return false;
  pos+=n+12;
 }
 return false;
}

}
bool decode(storage_files::File* input,storage_files::File* output,bool jpeg,unsigned& width,unsigned& height,Cancel cancel,bool readerResolution){
  namespace fs=storage_files;
  width=height=0;if(live||!input||!output)return false;
  const uint32_t size=fs::size(input);uint8_t check[33];
  if(size<33||size>kImageBytes||fs::read(input,check,sizeof(check))!=sizeof(check))return false;
  Palette palette{};bool indexed=false;unsigned jpegWidth=0,jpegHeight=0;
  if(jpeg){if(check[0]!=255||check[1]!=216||!jpegDimensions(input,jpegWidth,jpegHeight,cancel))return false;
    if(!fs::seek(input,size-2)||fs::read(input,check,2)!=2||check[0]!=255||check[1]!=217)return false;
  }else{
    static const uint8_t signature[]={137,80,78,71,13,10,26,10};
    // Noninterlaced gray/RGB/RGBA8 and indexed PNG (1/2/4/8-bit).
    if(memcmp(check,signature,8)||big(check+8)!=13||memcmp(check+12,"IHDR",4)||
       !big(check+16)||big(check+16)>1280||!big(check+20)||big(check+20)>2048||
       (check[24]!=8&&check[25]!=3)||(check[25]!=0&&check[25]!=2&&check[25]!=3&&check[25]!=4&&check[25]!=6)||check[26]||check[27]||check[28])return false;
    indexed=check[25]==3;if(indexed&&!indexedHeader(input,check,palette,cancel))return false;
    static const uint8_t end[]={0,0,0,0,'I','E','N','D',174,66,96,130};
    uint8_t tail[12];if(!fs::seek(input,size-12)||fs::read(input,tail,12)!=12||memcmp(tail,end,12))return false;
  }
  if(!fs::seek(input,indexed?palette.data:0))return false;
  oom=false;limit=jpeg?96*1024:64*1024;lws_set_allocator(allocate);lws_set_log_level(LLL_ERR|LLL_WARN,quiet);
  lws_jpeg_t* j=jpeg?lws_jpeg_new():nullptr;lws_upng_t* p=jpeg?nullptr:lws_upng_new();
  uint8_t buffer[1024],scaled[kImageSide*4];const uint8_t* in=indexed?check:buffer;size_t left=indexed?sizeof(check):0;
  unsigned sw=0,sh=0,components=0,sourceRow=0,outRow=0;bool metadata=true,ok=false;
  bool needInput=true;
  if(j||p)for(unsigned calls=0;calls<32768&&!cancel()&&!oom;++calls){
    if(needInput&&!left){left=fs::read(input,buffer,sizeof(buffer));in=buffer;if(!left)break;}
    const uint8_t* pixels=nullptr;const size_t before=left;
    const auto r=jpeg?lws_jpeg_emit_next_line(j,&pixels,&in,&left,metadata):lws_upng_emit_next_line(p,&pixels,&in,&left,metadata);
    if(r&LWS_SRET_FATAL)break;
    if(metadata){
      sw=jpeg?lws_jpeg_get_width(j):lws_upng_get_width(p);sh=jpeg?lws_jpeg_get_height(j):lws_upng_get_height(p);
      if(sw&&sh&&(r&LWS_SRET_AWAIT_RETRY)){
        if(indexed)sw=palette.width;
        if(sw>1280||sh>2048)break;
        components=jpeg?lws_jpeg_get_components(j):lws_upng_get_components(p);
        if(!components||components>4||(jpeg&&components!=1&&components!=3))break;
        const unsigned displayW=jpeg?jpegWidth:sw,displayH=jpeg?jpegHeight:sh,side=displayW>displayH?displayW:displayH;
        width=side>kImageSide?displayW*kImageSide/side:displayW;height=side>kImageSide?displayH*kImageSide/side:displayH;
        // The reader emits logical pixels at panel scale 2. Caching those
        // exact samples avoids reading four native pixels to display just one.
        if(readerResolution){width=(width+1)/2;height=(height+1)/2;}
        if(!width)width=1;if(!height)height=1;metadata=false;needInput=false;continue;
      }
    }
    if(pixels&&!metadata){
      if(sourceRow>=sh)break;
      while(outRow<height && sourceRow==(outRow*sh)/height){
        for(unsigned x=0;x<width;++x){const unsigned sx=x*sw/width;uint8_t* dst=scaled+x*4;
          if(indexed){const unsigned bit=sx*palette.depth,index=(pixels[bit/8]>>(8-palette.depth-bit%8))&((1u<<palette.depth)-1);
            if(index>=palette.count){oom=true;break;}memcpy(dst,palette.rgba[index],4);continue;}
          const uint8_t* src=pixels+sx*components;
          dst[0]=src[0];dst[1]=components>=3?src[1]:src[0];dst[2]=components>=3?src[2]:src[0];dst[3]=components==4?src[3]:components==2?src[1]:255;
        }
        if(oom||!fs::write(output,scaled,width*4)){oom=true;break;}++outRow;
      }
      ++sourceRow;if(sourceRow==sh){ok=outRow==height;break;}
#if defined(ESP_PLATFORM)
      if(!(sourceRow%16))vTaskDelay(1);
#endif
    }
    needInput=(r&LWS_SRET_WANT_INPUT)!=0;
    if(!pixels&&before==left&&!needInput&&!(r&LWS_SRET_AWAIT_RETRY))break;
  }
  if(j)lws_jpeg_free(&j);if(p)lws_upng_free(&p);
  if(!ok||oom||live||cancel()){width=height=0;return false;}return true;
}
} }
