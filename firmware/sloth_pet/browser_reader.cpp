#include "browser_reader.h"
#include "browser_url.h"
#include "browser_memory.h"
#include "keyboard.h"
#include <new>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
namespace sloth { namespace browser_reader {
namespace {
char lower(char c){return c>='A'&&c<='Z'?c-'A'+'a':c;}
bool same(const char* a,const char* b){while(*a&&*b)if(lower(*a++)!=lower(*b++))return false;return *a==*b;}
bool space(char c){return c==' '||c=='\n'||c=='\r'||c=='\t'||c=='\f';}
void copy(char* out,size_t size,const char* in){if(!size)return;size_t n=0;while(n+1<size&&in[n]){out[n]=in[n];++n;}out[n]=0;}
bool reddit(const char* host){return same(host,"reddit.com")||same(host,"www.reddit.com")||same(host,"old.reddit.com")||same(host,"www.old.reddit.com");}
unsigned entity(const char* s){
 if(s[0]=='#'){char* end=nullptr;const bool hex=s[1]=='x'||s[1]=='X';const unsigned long n=strtoul(s+(hex?2:1),&end,hex?16:10);return end && !*end && n && n<=0x10ffff?static_cast<unsigned>(n):'?';}
 struct Named {const char* name;unsigned value;};
 static const Named names[]={{"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",' '},{"ndash",'-'},{"mdash",'-'},{"lsquo",'\''},{"rsquo",'\''},{"ldquo",'"'},{"rdquo",'"'},{"hellip",0x2026},{"copy",0xa9},{"bull",'*'}};
 for(const auto& n:names)if(!strcmp(n.name,s))return n.value;return '?';
}
char ascii(unsigned n){if(n==0xa0)return ' ';if(n==0x2018||n==0x2019)return '\'';if(n==0x201c||n==0x201d)return '"';if(n==0x2013||n==0x2014)return '-';return n>=32&&n<127?static_cast<char>(n):'?';}
void attribute(const char* tag,const char* wanted,char* out,size_t capacity){
 out[0]=0;const char* p=tag;while(*p&&!space(*p))++p;
 while(*p){while(space(*p)||*p=='/')++p;if(!*p)break;
  char name[32]{};unsigned n=0;while(*p&&!space(*p)&&*p!='='){if(n+1<sizeof(name))name[n++]=lower(*p);++p;}
  while(space(*p))++p;if(*p!='=')continue;++p;while(space(*p))++p;
  const char quote=(*p=='\''||*p=='"')?*p++:0;const char* start=p;
  while(*p&&(quote?*p!=quote:!space(*p)))++p;
  if(same(name,wanted)){
   size_t at=0;for(const char* v=start;v<p&&at+1<capacity;++v){
    if(*v=='&'){const char* end=v+1;while(end<p&&end-v<24&&*end!=';')++end;if(end<p&&*end==';'){char e[24]{};memcpy(e,v+1,end-v-1);out[at++]=ascii(entity(e));v=end;continue;}}
    out[at++]=*v;
   }out[at]=0;if(p-start>=static_cast<ptrdiff_t>(capacity))out[0]=0;return;
  }if(quote&&*p)++p;
 }
}
}
struct Parser::State {
 Document& d;bool atom,entry=false;unsigned capture=0;char logical[512]{};
 char title[256]{},content[4096]{},href[512]{},id[64]{},lastId[64]{};
 unsigned titleAt=0,contentAt=0,idAt=0;bool entryClipped=false;
 uint16_t currentLink=0;unsigned skip=0,utf=0,code=0;bool pendingSpace=false;
 char word[kColumns]{};unsigned wordAt=0;uint16_t wordLink=0;
 struct Tokenizer {
  State& s;bool xml,inTag=false,inEntity=false,comment=false,cdata=false;char quote=0;
  char tag[3072]{},ent[32]{},raw[16]{};unsigned at=0,entAt=0,rawAt=0,tail=0;bool overflow=false;
  Tokenizer(State& state,bool isXml):s(state),xml(isXml){}
  void emit(unsigned c,bool decoded=false){s.character(c,xml,decoded);}
  void setRaw(const char* name){strcpy(raw,same(name,"script")?"</script":"</style");rawAt=0;}
  void push(char c){
   if(raw[0]){const char v=lower(c);
    if(!raw[rawAt]){if(v=='>'){raw[0]=0;rawAt=0;return;}if(space(v))return;rawAt=v=='<'?1:0;return;}
    if(v==raw[rawAt])++rawAt;else rawAt=v=='<'?1:0;return;
   }
   if(comment){tail=((tail<<8)|static_cast<unsigned char>(c))&0xffffff;if(tail==0x2d2d3e){comment=false;tail=0;}return;}
   if(cdata){if(c==']'){++tail;return;}if(c=='>'&&tail>=2){for(unsigned i=2;i<tail;++i)emit(']');cdata=false;tail=0;return;}while(tail){emit(']');--tail;}emit(static_cast<unsigned char>(c));return;}
   if(inTag){
    if(c=='>'&&!quote){inTag=false;tag[at]=0;if(!overflow)s.tag(tag,xml,*this);at=0;overflow=false;return;}
    if(c==quote)quote=0;else if(!quote&&(c=='\''||c=='"'))quote=c;
    if(at+1<sizeof(tag))tag[at++]=c;else overflow=true;
    if(at==3&&!memcmp(tag,"!--",3)){inTag=false;comment=true;at=tail=0;}
    if(at==8&&!memcmp(tag,"![CDATA[",8)){inTag=false;cdata=true;at=tail=0;}
    return;
   }
   if(inEntity){
    if(c==';'){ent[entAt]=0;emit(entity(ent),true);inEntity=false;entAt=0;return;}
    if(entAt+1<sizeof(ent)&&!space(c)&&c!='<'&&c!='&'){ent[entAt++]=c;return;}
    emit('&');for(unsigned i=0;i<entAt;++i)emit(static_cast<unsigned char>(ent[i]));inEntity=false;entAt=0;
   }
   if(c=='<'){inTag=true;at=0;quote=0;return;}if(c=='&'){inEntity=true;entAt=0;return;}emit(static_cast<unsigned char>(c));
  }
  void reset(){inTag=inEntity=comment=cdata=overflow=false;quote=0;at=entAt=rawAt=tail=0;raw[0]=0;}
  void finish(){if(inEntity){emit('&');for(unsigned i=0;i<entAt;++i)emit(ent[i]);inEntity=false;} }
 } outer,inner;
 State(Document& doc,bool isAtom,const char* url):d(doc),atom(isAtom),outer(*this,true),inner(*this,false){copy(logical,sizeof(logical),url);}
 uint16_t link(const char* url){
  if(!url[0]||url[0]=='#'||strchr(url,'\n')||strchr(url,'\r'))return 0;
  const char* colon=strchr(url,':');const char* slash=strchr(url,'/');if(colon&&(!slash||colon<slash)&&strncmp(url,"https://",8)&&strncmp(url,"http://",7))return 0;
  for(unsigned i=0;i<d.links;++i)if(!strcmp(d.urls+d.offsets[i],url))return i+1;
  const size_t n=strlen(url)+1;if(n>512||d.links==kLinks||n>kUrlBytes-d.urlBytes){d.clipped=true;return 0;}
  if(!d.ensureUrls(d.urlBytes+n)){d.clipped=true;return 0;}
  d.offsets[d.links]=d.urlBytes;memcpy(d.urls+d.urlBytes,url,n);d.urlBytes+=n;return ++d.links;
 }
 void newline(){if(d.lines[d.count-1].length){if(d.count==kLines||!d.ensureLines(d.count+1))d.clipped=true;else ++d.count;}pendingSpace=false;}
 void put(char c,uint16_t target){
  Line* line=&d.lines[d.count-1];if(line->length==kColumns){newline();line=&d.lines[d.count-1];if(line->length==kColumns)return;}
  if(target&&line->spans==4&&(line->span[3].link!=target||line->span[3].last!=line->length)){
    newline();line=&d.lines[d.count-1];if(line->spans==4){d.clipped=true;return;}
  }
  const unsigned col=line->length;line->text[line->length++]=c;
  if(target){if(line->spans&&line->span[line->spans-1].link==target&&line->span[line->spans-1].last==col)line->span[line->spans-1].last=col+1;
   else if(line->spans<4)line->span[line->spans++]={target,static_cast<uint8_t>(col),static_cast<uint8_t>(col+1)};else d.clipped=true;}
 }
 void flush(){
  if(!wordAt)return;Line& line=d.lines[d.count-1];
  if(line.length&&line.length+wordAt+(pendingSpace?1:0)>kColumns)newline();
  if(pendingSpace&&d.lines[d.count-1].length)put(' ',0);
  for(unsigned i=0;i<wordAt;++i)put(word[i],wordLink);wordAt=0;pendingSpace=false;
 }
 void text(unsigned value){
  if(value<256){const uint8_t c=value;
   if(utf){if((c&0xc0)==0x80){code=(code<<6)|(c&63);if(--utf)return;value=code;}else{utf=0;value=c;}}
   else if(c>=0xc2&&c<=0xf4){utf=c<0xe0?1:c<0xf0?2:3;code=c&((1u<<(6-utf))-1);return;}
   else if(c>=128)value='?';
  }
  if(value=='\n'||value=='\r'||value=='\t'||value==' '||value==0xa0){flush();pendingSpace=true;return;}
  if(value<32)return;
  if(wordAt==sizeof(word)||wordLink!=currentLink)flush();wordLink=currentLink;
  word[wordAt++]=ascii(value);
 }
 void breakLine(){flush();newline();}
 void utf8(char* out,unsigned& used,unsigned capacity,unsigned c,bool decoded){
  char bytes[4];unsigned n=0;
  if(!decoded||c<128)bytes[n++]=c;else if(c<2048){bytes[n++]=0xc0|(c>>6);bytes[n++]=0x80|(c&63);}else if(c<65536){bytes[n++]=0xe0|(c>>12);bytes[n++]=0x80|((c>>6)&63);bytes[n++]=0x80|(c&63);}else{bytes[n++]=0xf0|(c>>18);bytes[n++]=0x80|((c>>12)&63);bytes[n++]=0x80|((c>>6)&63);bytes[n++]=0x80|(c&63);}
  if(used+n>=capacity){entryClipped=true;return;}memcpy(out+used,bytes,n);used+=n;out[used]=0;
 }
 void character(unsigned c,bool xml,bool decoded){
  if(xml){if(!entry)return;if(capture==1)utf8(title,titleAt,sizeof(title),c,decoded);else if(capture==2)utf8(content,contentAt,sizeof(content),c,decoded);else if(capture==3)utf8(id,idAt,sizeof(id),c,decoded);}
  else if(!skip){if(decoded){utf=0;if(c==0x2026){text('.');text('.');text('.');return;}text(static_cast<unsigned char>(ascii(c)));}else text(c);}
 }
 void emitEntry(){
  breakLine();currentLink=link(href);for(unsigned i=0;title[i];++i)text(static_cast<uint8_t>(title[i]));breakLine();currentLink=0;
  inner.reset();skip=utf=0;for(unsigned i=0;content[i];++i)inner.push(content[i]);inner.finish();breakLine();
  if(entryClipped){const char* note="[Post excerpt shortened]";for(;*note;++note)text(*note);breakLine();}
  if(!strncmp(id,"t3_",3))copy(lastId,sizeof(lastId),id);
  currentLink=0;skip=0;utf=0;
 }
 void tag(char* tag,bool xml,Tokenizer& tokenizer){
  const bool closing=*tag=='/';const char* p=tag+(closing?1:0);char name[32]{};unsigned n=0;
  while(*p&&!space(*p)&&*p!='/'&&n+1<sizeof(name))name[n++]=lower(*p++);
  if(xml){
   if(same(name,"entry")){if(closing){emitEntry();entry=false;capture=0;}else{entry=true;capture=0;titleAt=contentAt=idAt=0;title[0]=content[0]=href[0]=id[0]=0;entryClipped=false;}return;}
   if(!entry)return;
   if(closing){if(same(name,"title")||same(name,"content")||same(name,"id"))capture=0;return;}
   if(same(name,"title"))capture=1;else if(same(name,"content"))capture=2;else if(same(name,"id"))capture=3;
   else if(same(name,"link"))attribute(tag,"href",href,sizeof(href));return;
  }
  if(!closing&&(same(name,"script")||same(name,"style"))){tokenizer.setRaw(name);return;}
  if(same(name,"head")||same(name,"svg")||same(name,"template")){if(closing){if(skip)--skip;}else ++skip;return;}
  if(skip)return;
  if(same(name,"a")){flush();if(closing)currentLink=0;else{char url[512];attribute(tag,"href",url,sizeof(url));currentLink=link(url);}return;}
  if(same(name,"img")&&!closing){
   if(d.imageCount==browser_image::kMaxImages)return;
   char src[512];if(!browser_image::source(tag,strlen(tag),src)||!browser_image::eligible(tag,strlen(tag),src))return;
   const uint16_t source=link(src);if(!source)return;
   breakLine();if(d.count==kLines&&d.lines[d.count-1].length)return;
   auto& image=d.images[d.imageCount++];image.line=d.count-1;image.source=source;image.link=currentLink;
   char alt[96];attribute(tag,"alt",alt,sizeof(alt));
   const char* label="[image";while(*label)text(*label++);
   if(*alt){text(':');text(' ');for(unsigned i=0;alt[i]&&i<24;++i)text(static_cast<uint8_t>(alt[i]));}
   text(']');breakLine();return;
  }
  if(same(name,"p")||same(name,"br")||same(name,"div")||same(name,"li")||same(name,"tr")||same(name,"hr")||same(name,"h1")||same(name,"h2")||same(name,"h3")||same(name,"article")||same(name,"section")||same(name,"pre")){breakLine();return;}
  if(same(name,"td")||same(name,"th")){flush();pendingSpace=true;}
 }
 void finish(){
  if(atom){outer.finish();if(entry){emitEntry();entry=false;d.clipped=true;}
   if(lastId[0]&&!strstr(logical,"/comments/")){
    char more[512];copy(more,sizeof(more),logical);char* query=strchr(more,'?');if(query)*query=0;char* feed=strstr(more,".rss");if(feed)*feed=0;
    const size_t n=strlen(more);if(n+strlen(lastId)+8<sizeof(more)){snprintf(more+n,sizeof(more)-n,"?after=%s",lastId);breakLine();currentLink=link(more);const char* text="MORE POSTS >";for(;*text;++text)this->text(*text);currentLink=0;}
   }
  }else inner.finish();
  flush();if(d.count>1&&!d.lines[d.count-1].length)--d.count;
  if(d.count==1&&!d.lines[0].length){const char* note="No readable text. This page may require JavaScript or sign-in.";for(;*note;++note)text(*note);flush();}
 }
};
Parser::Parser(Document& d,bool atom,const char* logical){void* p=d.lines?browser_memory_malloc(sizeof(State)):nullptr;state_=p?new(p)State(d,atom,logical):nullptr;if(!state_)d.clipped=true;}
Parser::~Parser(){if(state_){state_->~State();browser_memory_free(state_);}}
void Parser::feed(const char* bytes,size_t size){if(state_)for(size_t i=0;i<size;++i)(state_->atom?state_->outer:state_->inner).push(bytes[i]);}
void Parser::finish(){if(state_)state_->finish();}
namespace {
unsigned imageRows(const Image& im){const unsigned rows=im.logicalHeight()+6;return im.asset>=0&&rows>kRowHeight?rows:kRowHeight;}
// Map expanded image blocks back to fixed text rows without reflow or a DOM.
const Image* atRow(const Document& doc,unsigned& y){
 for(unsigned i=0;i<doc.imageCount;++i){const auto& im=doc.images[i];const unsigned start=im.line*kRowHeight,rows=imageRows(im);
  if(y<start)break;if(y<start+rows){if(im.asset>=0){y-=start;return &im;}break;}
  y-=rows-kRowHeight;
 }return nullptr;
}
}
Document::Document(){ensureLines(1);}
Document::~Document(){browser_memory_free(lines);browser_memory_free(urls);}
bool Document::ensureLines(unsigned needed){
 if(needed<=lineCapacity)return true;if(needed>kLines)return false;
 const unsigned capacity=(needed+15)/16*16;
 Line* grown=static_cast<Line*>(browser_memory_realloc(lines,capacity*sizeof(Line)));if(!grown)return false;
 for(unsigned i=lineCapacity;i<capacity;++i)new(grown+i)Line{};
 lines=grown;lineCapacity=capacity;return true;
}
bool Document::ensureUrls(unsigned needed){
 if(needed<=urlCapacity)return true;if(needed>kUrlBytes)return false;
 const unsigned capacity=(needed+1023)/1024*1024;
 char* grown=static_cast<char*>(browser_memory_realloc(urls,capacity));if(!grown)return false;
 urls=grown;urlCapacity=capacity;return true;
}
unsigned Document::height() const {unsigned rows=count*kRowHeight;for(unsigned i=0;i<imageCount;++i)rows+=imageRows(images[i])-kRowHeight;return rows*2;}
void Document::attachImages(const BrowserEngineAssets& assets,unsigned pixelScale){
 for(unsigned i=0;i<imageCount;++i){auto& im=images[i];im.asset=-1;unsigned w=0,h=0;
  const int id=assets.lookup?assets.lookup(assets.user,link(im.source),&w,&h):-1;
  const unsigned bound=pixelScale==2?160:320;
  if(id>=0&&w&&w<=bound&&h&&h<=bound){im.asset=id;im.width=w;im.height=h;im.pixelScale=pixelScale==2?2:1;}
 }
}
int Document::linkAt(unsigned x,unsigned y) const {
 if(!lines||x<BROWSER_PAGE_MARGIN*2||x>=(240-BROWSER_PAGE_MARGIN)*2)return 0;
 unsigned row=y/2;const Image* im=atRow(*this,row);
 if(im){const unsigned w=im->logicalWidth(),left=(240-w)/2;return x/2>=left&&x/2<left+w&&row>=3&&row<3+im->logicalHeight()?im->link:0;}
 if(row/kRowHeight>=count)return 0;x-=BROWSER_PAGE_MARGIN*2;const Line& line=lines[row/kRowHeight];
 for(unsigned i=0;i<line.spans;++i)if(x/12>=line.span[i].first&&x/12<line.span[i].last)return line.span[i].link;return 0;
}
void drawRow(uint16_t* row,const Document& doc,unsigned y,const BrowserEngineAssets* assets){
 if(!row)return;for(unsigned x=0;x<240;++x)row[x]=0xffff;
 const Image* im=atRow(doc,y);
 if(im){
  const unsigned w=im->logicalWidth(),h=im->logicalHeight(),left=(240-w)/2;
  if(y<3||y>=h+3||!assets||!assets->row)return;
  const uint8_t* pixels=assets->row(assets->user,im->asset,(y-3)*im->height/h);if(!pixels)return;
  // Neutral matte keeps both white and dark transparent logos legible.
  for(unsigned x=0;x<w;++x){const uint8_t* p=pixels+(x*im->width/w)*4;const unsigned alpha=p[3];
   const unsigned r=(p[0]*alpha+112*(255-alpha))/255,g=(p[1]*alpha+112*(255-alpha))/255,b=(p[2]*alpha+112*(255-alpha))/255;
   row[left+x]=((r&248)<<8)|((g&252)<<3)|(b>>3);
  }return;
 }
 if(!doc.lines||y/kRowHeight>=doc.count)return;const Line& line=doc.lines[y/kRowHeight];
 for(unsigned x=0;x<line.length;++x){uint16_t color=0x1082;bool linked=false;for(unsigned i=0;i<line.spans;++i)if(x>=line.span[i].first&&x<line.span[i].last){color=0x0255;linked=true;break;}
  drawKeyboardGlyphRow(row,y%kRowHeight,BROWSER_PAGE_MARGIN+x*6,0,line.text[x],color);if(linked&&y%kRowHeight==7)for(unsigned pixel=0;pixel<5;++pixel)row[BROWSER_PAGE_MARGIN+x*6+pixel]=color;
 }
}
bool requestUrl(const char* logical,char* output,size_t capacity){
 browser_url::Parts p{};if(!browser_url::parse(logical,p)||!output||!capacity)return false;
 if(!reddit(p.host)||p.port!=(p.secure?443:80)){if(strlen(logical)>=capacity)return false;copy(output,capacity,logical);return true;}
 char target[512];copy(target,sizeof(target),p.target);char* query=strchr(target,'?');char parameters[256]{};
 if(query){if(strlen(query+1)>=sizeof(parameters))return false;char* part=query+1;size_t used=0;
  while(*part){char* end=strchr(part,'&');size_t len=end?static_cast<size_t>(end-part):strlen(part);
   if(strncmp(part,"limit=",6)&&len){if(used)parameters[used++]='&';memcpy(parameters+used,part,len);used+=len;parameters[used]=0;}
   if(!end)break;part=end+1;
  }*query=0;}
 size_t n=strlen(target);if(n>=4&&!strcmp(target+n-4,".rss")){}else{while(n&&target[n-1]=='/')target[--n]=0;if(n+6>=sizeof(target))return false;strcpy(target+n,"/.rss");}
 const int size=snprintf(output,capacity,"https://www.reddit.com%s?%s%slimit=12",target,parameters,parameters[0]?"&":"");return size>0&&static_cast<size_t>(size)<capacity;
}
} }
