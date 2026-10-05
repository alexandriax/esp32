#include "browser_assets.h"
#include "browser_memory.h"
#include <new>
#include <string.h>
#include <stdio.h>
#include <time.h>
namespace sloth { namespace browser_assets {
namespace fs=storage_files;
struct Image { char url[512]{};char* source=nullptr;unsigned w=0,h=0;int slot=-1;bool ephemeral=false; };
namespace {
// Private, fixed-size slots bound disk use. Validate URL and exact file length
// before trusting a hit; interrupted downloads never replace a published slot.
struct Header {
  uint32_t magic=0x4d574233,expires=0,bytes=0;
  uint16_t width=0,height=0;uint8_t kind=0,reserved[3]{};
  char url[512]{};
};

void name(unsigned slot,char* out){snprintf(out,13,"C%02u.DAT",slot);}
char lower(char c){return c>='A'&&c<='Z'?c+32:c;}
bool eq(const char* p,size_t n,const char* s){if(strlen(s)!=n)return false;for(size_t i=0;i<n;++i)if(lower(p[i])!=s[i])return false;return true;}
bool ws(char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f';}
// Small HTML scanner: skips comments and raw-text elements; quoted '>' is data.
// Oversized/ambiguous attributes are ignored, never followed as URL prefixes.
struct Tag { size_t start=0,end=0;char tag[12]{},src[512]{},href[512]{},rel[64]{},media[64]{};bool endTag=false; };
bool attribute(const char* p,size_t n,char* out,size_t cap){
  size_t j=0;
  for(size_t i=0;i<n;++i){unsigned ch=static_cast<unsigned char>(p[i]);
    if(ch=='&'){
      size_t end=i+1;while(end<n&&end-i<12&&p[end]!=';')++end;
      if(end<n&&p[end]==';'){
        const char* a=p+i+1;const size_t count=end-i-1;
        if(eq(a,count,"amp"))ch='&';else if(eq(a,count,"quot"))ch='"';else if(eq(a,count,"apos"))ch='\'';
        else if(eq(a,count,"lt"))ch='<';else if(eq(a,count,"gt"))ch='>';
        else if(count>1&&a[0]=='#'){
          unsigned v=0,base=10;size_t k=1;if(k<count&&(a[k]=='x'||a[k]=='X')){base=16;++k;}
          if(k==count)return false;
          for(;k<count;++k){unsigned d=lower(a[k]);d=d>='0'&&d<='9'?d-'0':d>='a'&&d<='f'?d-'a'+10:99;if(d>=base||v>127/base)return false;v=v*base+d;}
          if(v<32||v>126)return false;ch=v;
        }else return false;
        i=end;
      } // A bare ampersand in a URL is legal HTML.
    }
    if(ch<32||ch==127||j+1>=cap)return false;out[j++]=static_cast<char>(ch);
  }
  out[j]=0;return true;
}
bool next(const char* html,size_t length,size_t& pos,Tag& t,char* raw){
  while(pos<length){
    if(html[pos]!='<'){++pos;continue;}
    if(pos+4<=length&&!memcmp(html+pos,"<!--",4)){
      pos+=4;while(pos+3<=length&&memcmp(html+pos,"-->",3))++pos;pos=pos+3<=length?pos+3:length;continue;
    }
    t=Tag{};t.start=pos++;if(pos<length&&html[pos]=='/'){t.endTag=true;++pos;}
    const size_t begin=pos;while(pos<length&&((lower(html[pos])>='a'&&lower(html[pos])<='z')||(html[pos]>='0'&&html[pos]<='9')))++pos;
    size_t n=pos-begin;if(!n||n>=sizeof(t.tag)){continue;}
    for(size_t i=0;i<n;++i)t.tag[i]=lower(html[begin+i]);
    while(pos<length){
      while(pos<length&&(ws(html[pos])||html[pos]=='/'))++pos;
      if(pos==length)break;if(html[pos]=='>'){++pos;break;}
      const size_t a=pos;while(pos<length&&!ws(html[pos])&&html[pos]!='='&&html[pos]!='>'&&html[pos]!='/')++pos;
      const size_t an=pos-a;if(!an){++pos;continue;}
      while(pos<length&&ws(html[pos]))++pos;
      if(pos==length||html[pos]!='=')continue;++pos;while(pos<length&&ws(html[pos]))++pos;
      char quote=0;if(pos<length&&(html[pos]=='\''||html[pos]=='"'))quote=html[pos++];
      const size_t v=pos;while(pos<length&&(quote?html[pos]!=quote:!ws(html[pos])&&html[pos]!='>'))++pos;
      const size_t vn=pos-v;if(quote&&pos<length)++pos;
      char* out=nullptr;size_t cap=0;
      if(eq(html+a,an,"src")){out=t.src;cap=sizeof(t.src);}else if(eq(html+a,an,"href")){out=t.href;cap=sizeof(t.href);}
      else if(eq(html+a,an,"rel")){out=t.rel;cap=sizeof(t.rel);}else if(eq(html+a,an,"media")){out=t.media;cap=sizeof(t.media);}
      if(out&&!attribute(html+v,vn,out,cap))out[0]=0;
    }
    t.end=pos;if(pos==length&&html[pos-1]!='>')return false;
    if(*raw){if(t.endTag&&!strcmp(raw,t.tag))*raw=0;continue;}
    if(!t.endTag&&(!strcmp(t.tag,"script")||!strcmp(t.tag,"style")||!strcmp(t.tag,"textarea")||!strcmp(t.tag,"title"))){strcpy(raw,t.tag);continue;}
    return true;
  }
  return false;
}
bool token(const char* list,const char* wanted){
  while(*list){while(ws(*list))++list;const char* start=list;while(*list&&!ws(*list))++list;if(eq(start,list-start,wanted))return true;}return false;
}
bool resolve(const char* base,const char* url,char* out){
  if(!*url||!browser_url::resolve(base,url,out))return false;
  return strncmp(base,"https://",8)||!strncmp(out,"https://",8);
}
struct Writer:browser_fetch::detail::Writer {
  fs::File* f;bool bad=false;explicit Writer(fs::File* file):f(file){}
  void write(const char* p,size_t n)override{if(!bad)bad=!fs::write(f,p,n);}
  bool failed()const override{return bad;}
};
}
struct Page { char base[512]{};Image images[kImages];bool pinned[kSlots]{};Stats state;Cancel cancel=nullptr;uint8_t* window=nullptr;fs::File* windowFile=nullptr;int windowImage=-1;unsigned windowFirst=0,windowRows=0; };
namespace {
fs::File* cached(Page& page,const char* url,Kind kind,Header& h,int& slot){
  uint32_t hash=2166136261u;for(const char* p=url;*p;++p)hash=(hash^static_cast<uint8_t>(*p))*16777619u;
  int candidate=-1;const time_t now=time(nullptr);
  for(unsigned j=0;j<kSlots;++j){unsigned at=(hash+j)%kSlots;if(page.pinned[at])continue;
    if(candidate<0)candidate=at;
    char path[13];name(at,path);fs::File* f=fs::open(path,fs::Mode::Read);if(!f)continue;
    Header found;const bool valid=fs::read(f,&found,sizeof(found))==sizeof(found)&&found.magic==h.magic&&
      found.kind==static_cast<uint8_t>(kind)&&!memcmp(found.url,url,strlen(url)+1)&&now>=1704067200&&
      found.expires>static_cast<uint64_t>(now)&&found.expires<=static_cast<uint64_t>(now)+3600&&
      (kind==Kind::Css?(found.bytes<=kCssBytes&&!found.width&&!found.height):
       (found.width&&found.width<=kImageSide&&found.height&&found.height<=kImageSide&&found.bytes==found.width*found.height*4u))&&
      fs::size(f)==sizeof(found)+found.bytes;
    if(valid){h=found;slot=at;return f;}fs::close(f);
  }
  slot=candidate;return nullptr;
}
fs::File* acquire(Page& page,const char* url,Kind kind,Fetch fetch,Header& h,int& slot){
  fs::File* f=cached(page,url,kind,h,slot);if(f){++page.state.hits;return f;}if(slot<0||page.cancel())return nullptr;
  fs::remove("FETCH.TMP");fs::remove("BUILD.TMP");
  f=fs::open("FETCH.TMP",fs::Mode::Create);if(!f)return nullptr;
  Download result;Writer writer(f);bool ok=fetch(url,kind,writer,kind==Kind::Css?kCssBytes:kImageBytes,result)&&!writer.bad;
  const uint32_t bytes=fs::size(f);if(!fs::close(f))ok=false;
  if(!ok||!bytes||page.cancel()){fs::remove("FETCH.TMP");return nullptr;}
  f=fs::open("FETCH.TMP",fs::Mode::Read);fs::File* out=fs::open("BUILD.TMP",fs::Mode::Create);
  h=Header{};h.kind=static_cast<uint8_t>(kind);strcpy(h.url,url);
  const time_t now=time(nullptr);if(result.maxAge&&now>=1704067200&&now<4102444800LL)h.expires=static_cast<uint32_t>(now)+result.maxAge;
  ok=f&&out&&fs::write(out,&h,sizeof(h));
  if(ok&&kind!=Kind::Css){unsigned w=0,ht=0;ok=decode(f,out,result.jpeg,w,ht,page.cancel,kind==Kind::ReaderImage);h.width=w;h.height=ht;h.bytes=w*ht*4u;}
  else if(ok){char buffer[1024];size_t n;while((n=fs::read(f,buffer,sizeof(buffer)))){if(page.cancel()||!fs::write(out,buffer,n)){ok=false;break;}h.bytes+=n;}ok=ok&&h.bytes==bytes;}
  if(ok)ok=fs::seek(out,0)&&fs::write(out,&h,sizeof(h));
  if(f&&!fs::close(f))ok=false;if(out&&!fs::close(out))ok=false;
  fs::remove("FETCH.TMP");char path[13];name(slot,path);
  if(ok&&!page.cancel())ok=fs::remove(path)&&fs::rename("BUILD.TMP",path);else ok=false;
  if(!ok){fs::remove("BUILD.TMP");return nullptr;}
  f=fs::open(path,fs::Mode::Read);if(f&&!fs::seek(f,sizeof(h)))fs::close(f);return f;
}
int lookup(void* user,const char* src,unsigned* w,unsigned* h){
  auto& page=*static_cast<Page*>(user);char url[512];if(!resolve(page.base,src,url))return -1;
  for(unsigned i=0;i<page.state.images;++i)if(!strcmp(url,page.images[i].url)||(page.images[i].source&&!strcmp(url,page.images[i].source))){*w=page.images[i].w;*h=page.images[i].h;return i;}return -1;
}
const uint8_t* row(void* user,unsigned index,unsigned y){
  auto& page=*static_cast<Page*>(user);if(index>=page.state.images)return nullptr;auto& im=page.images[index];if(y>=im.h)return nullptr;
  if(!page.window)return nullptr;
  if(page.windowImage!=static_cast<int>(index)){
    fs::close(page.windowFile);page.windowImage=-1;page.windowRows=0;
    char path[13];name(im.slot,path);page.windowFile=fs::open(path,fs::Mode::Read);
    if(!page.windowFile||fs::size(page.windowFile)!=sizeof(Header)+im.w*im.h*4u){fs::close(page.windowFile);return nullptr;}
  }
  if(page.windowImage!=static_cast<int>(index)||y<page.windowFirst||y>=page.windowFirst+page.windowRows){
    unsigned rows=4096/(im.w*4);if(rows>im.h-y)rows=im.h-y;
    const unsigned bytes=rows*im.w*4;
    if(!fs::seek(page.windowFile,sizeof(Header)+y*im.w*4)||fs::read(page.windowFile,page.window,bytes)!=bytes){fs::close(page.windowFile);page.windowImage=-1;return nullptr;}
    page.windowImage=index;page.windowFirst=y;page.windowRows=rows;
  }return page.window+(y-page.windowFirst)*im.w*4;
}
Page* create(const char* base,Cancel cancel){
 void* memory=browser_memory_malloc(sizeof(Page));if(!memory)return nullptr;
 Page* page=new(memory)Page;strcpy(page->base,base);page->cancel=cancel;page->state.sd=fs::mount();return page;
}
bool addImage(Page& page,const char* url,const char* original,Fetch fetch,Kind kind=Kind::Image){
 unsigned w,h;if(lookup(&page,original,&w,&h)>=0)return true;
 if(page.state.images==kImages)return false;
 Header head;int slot=-1;fs::File* f=acquire(page,url,kind,fetch,head,slot);if(!f)return false;
 // FatFS embeds a 4 KiB cache in every FIL on this SDK. Keep zero image
 // handles across the next TLS handshake; open just the visible one in row().
 if(!fs::close(f)){if(!head.expires){char path[13];name(slot,path);fs::remove(path);}return false;}
 auto& im=page.images[page.state.images];
 // A single read-ahead window serves every image, saving RAM during the next
 // TLS handshake and reducing the number of SD reads while dragging the page.
 if(!page.window)page.window=static_cast<uint8_t*>(browser_memory_malloc(4096));
 if(!page.window){fs::close(f);if(!head.expires){char path[13];name(slot,path);fs::remove(path);}return false;}
 if(strcmp(url,original)){
  im.source=static_cast<char*>(browser_memory_malloc(strlen(original)+1));
  if(!im.source){fs::close(f);if(!head.expires){char path[13];name(slot,path);fs::remove(path);}return false;}
  strcpy(im.source,original);
 }
 strcpy(im.url,url);im.w=head.width;im.h=head.height;im.slot=slot;im.ephemeral=!head.expires;
 page.pinned[slot]=true;++page.state.images;return true;
}

}
Page* prepare(char*& html,size_t& length,const char* base,Fetch fetch,Cancel cancel){
  Page* page=create(base,cancel);if(!page||!page->state.sd)return page;
  size_t pos=0;char raw[12]{};Tag tag;unsigned imageAttempts=0,styleAttempts=0;
  while(!cancel()&&next(html,length,pos,tag,raw)){
    if(tag.endTag)continue;
    const bool css=!strcmp(tag.tag,"link")&&token(tag.rel,"stylesheet")&&!token(tag.rel,"alternate")&&(!*tag.media||token(tag.media,"all")||token(tag.media,"screen"));
    const bool image=!strcmp(tag.tag,"img")&&*tag.src;
    if(!css&&!image)continue;
    if(css?styleAttempts++>=kStyles:imageAttempts++>=kImages){++page->state.skipped;continue;}
    char url[512];if(!resolve(base,css?tag.href:tag.src,url)){++page->state.skipped;continue;}
    if(image){
      char selected[512],resolved[512];
      if(browser_image::source(html+tag.start,tag.end-tag.start,selected)&&resolve(base,selected,resolved)){
        if(!addImage(*page,resolved,url,fetch))++page->state.skipped;
      }else ++page->state.skipped;
      continue;
    }
    Header h;int slot=-1;fs::File* f=acquire(*page,url,css?Kind::Css:Kind::Image,fetch,h,slot);
    if(!f){++page->state.skipped;continue;}
    if(css){
      char* text=static_cast<char*>(browser_memory_malloc(h.bytes+1));bool ok=text!=nullptr;size_t got=0;
      while(ok&&got<h.bytes){size_t n=h.bytes-got;if(n>1024)n=1024;ok=fs::read(f,text+got,n)==n;got+=n;}fs::close(f);
      if(ok){text[h.bytes]=0;size_t extra=0;for(size_t i=0;i<h.bytes;++i)if(text[i]=='<')extra+=3;
        const size_t added=h.bytes+extra+15,removed=tag.end-tag.start;
        const size_t size=length-removed+added;
        char* resized=size<=kHtmlBytes?static_cast<char*>(browser_memory_realloc(html,size+1)):nullptr;
        if(resized){html=resized;memmove(html+tag.start+added,html+tag.end,length-tag.end+1);
          char* out=html+tag.start;memcpy(out,"<style>",7);out+=7;
          for(size_t i=0;i<h.bytes;++i){if(text[i]=='<'){memcpy(out,"\\3c ",4);out+=4;}else *out++=text[i]?text[i]:' ';}
          memcpy(out,"</style>",8);length=size;pos=tag.start+added;++page->state.styles;
        }else ok=false;
      }
      browser_memory_free(text);if(!ok)++page->state.skipped;
      if(!h.expires){char path[13];name(slot,path);fs::remove(path);}
    }
  }
  return page;
}
Page* prepareReader(browser_reader::Document& doc,const char* base,Fetch fetch,Cancel cancel){
 if(!doc.imageCount)return nullptr;
 Page* page=create(base,cancel);if(!page||!page->state.sd)return page;
 for(unsigned i=0;i<doc.imageCount&&!cancel();++i){char url[512];const char* src=doc.link(doc.images[i].source);
  if(!src||!resolve(base,src,url)||!addImage(*page,url,url,fetch,Kind::ReaderImage))++page->state.skipped;
 }
 doc.attachImages(callbacks(page),2);return page;
}
void release(Page*& page){
  if(!page)return;
  fs::close(page->windowFile);
  for(auto& im:page->images){browser_memory_free(im.source);if(im.ephemeral&&im.slot>=0){char path[13];name(im.slot,path);fs::remove(path);}}
  if(page->state.sd){fs::remove("FETCH.TMP");fs::remove("BUILD.TMP");fs::unmount();}
  browser_memory_free(page->window);page->~Page();browser_memory_free(page);page=nullptr;
}
BrowserEngineAssets callbacks(Page* page){return {page,page?lookup:nullptr,page?row:nullptr};}
Stats stats(const Page* page){return page?page->state:Stats{};}
} }
