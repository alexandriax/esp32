#include "browser_http.h"
#include <string.h>
namespace sloth { namespace browser_fetch { namespace detail {
namespace {
constexpr size_t kHeaderLimit=6144;
char lower(char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;}
bool equal(const char* a,const char* b){while(*a&&*b)if(lower(*a++)!=*b++)return false;return *a==*b;}
bool token(char c){return (c>='0'&&c<='9')||(lower(c)>='a'&&lower(c)<='z')||strchr("!#$%&'*+-.^_`|~",c);}
Error line(Reader& reader,char* out,size_t capacity,size_t& budget){
  size_t used=0;bool cr=false;
  for(;;){uint8_t byte;const int n=reader.read(&byte,1);if(n!=1)return Error::Http;
    if(!budget--)return Error::Headers;
    if(cr){if(byte!='\n')return Error::Headers;out[used]=0;return Error::None;}
    if(byte=='\r'){cr=true;continue;}
    if(byte=='\n'||byte==0||byte==127||(byte<32&&byte!='\t'))return Error::Headers;
    if(used+1>=capacity)return Error::Headers;out[used++]=static_cast<char>(byte);
  }
}
char* value(char* line){
  char* colon=strchr(line,':');if(!colon||colon==line)return nullptr;
  for(char* at=line;at<colon;++at)if(!token(*at))return nullptr;
  *colon++=0;while(*colon==' '||*colon=='\t')++colon;
  size_t n=strlen(colon);while(n&&(colon[n-1]==' '||colon[n-1]=='\t'))colon[--n]=0;
  return colon;
}
bool number(const char* text,size_t& result){
  result=0;if(!*text)return false;
  for(;*text;++text){if(*text<'0'||*text>'9')return false;const unsigned digit=*text-'0';if(result>(SIZE_MAX-digit)/10)return false;result=result*10+digit;}
  return true;
}
Error exact(Reader& reader,Writer& out,size_t n,size_t& length){
  char buffer[1024];while(n){const size_t wanted=n<sizeof(buffer)?n:sizeof(buffer);const int count=reader.read(reinterpret_cast<uint8_t*>(buffer),wanted);if(count<=0||static_cast<size_t>(count)>wanted)return Error::Http;out.write(buffer,count);if(out.failed())return Error::Storage;length+=count;n-=count;reader.progress(length);}
  return Error::None;
}
}
bool redirect(uint16_t status){return status==301||status==302||status==303||status==307||status==308;}
Error headers(Reader& reader,Response& response){
  size_t budget=kHeaderLimit;char text[512];
  for(unsigned interim=0;interim<3;++interim){
    response=Response{};bool typeSeen=false,encodingSeen=false,transferSeen=false,locationSeen=false;
    Error error=line(reader,text,sizeof(text),budget);if(error!=Error::None)return error;
    if(strncmp(text,"HTTP/1.0 ",9)&&strncmp(text,"HTTP/1.1 ",9))return Error::Http;
    if(strlen(text)<12||text[9]<'1'||text[9]>'5'||text[10]<'0'||text[10]>'9'||text[11]<'0'||text[11]>'9'||(text[12]&&text[12]!=' '))return Error::Http;
    response.status=(text[9]-'0')*100+(text[10]-'0')*10+text[11]-'0';
    for(;;){
      error=line(reader,text,sizeof(text),budget);if(error!=Error::None)return error;if(!text[0])break;
      char* content=value(text);if(!content)return Error::Headers;
      if(equal(text,"content-length")){if(response.hasLength||!number(content,response.length))return Error::Headers;response.hasLength=true;}
      else if(equal(text,"transfer-encoding")){if(transferSeen||!equal(content,"chunked"))return Error::Encoding;response.chunked=true;transferSeen=true;}
      else if(equal(text,"content-encoding")){if(encodingSeen)return Error::Headers;response.encoded=*content&&!equal(content,"identity");encodingSeen=true;}
      else if(equal(text,"content-type")){
        if(typeSeen)return Error::Headers;typeSeen=true;char* semi=strchr(content,';');if(semi)*semi=0;
        size_t n=strlen(content);while(n&&(content[n-1]==' '||content[n-1]=='\t'))content[--n]=0;
        response.html=equal(content,"text/html")||equal(content,"application/xhtml+xml");response.atom=equal(content,"application/atom+xml");
        response.css=equal(content,"text/css");response.jpeg=equal(content,"image/jpeg");response.png=equal(content,"image/png");
      }else if(equal(text,"cache-control")){
        // Only explicit freshness is reusable; conservative for private/no-cache.
        for(char* part=content;part && *part;){
          char* next=strchr(part,',');if(next)*next++=0;
          while(*part==' ' || *part=='\t')++part;
          size_t n=strlen(part);while(n && (part[n-1]==' ' || part[n-1]=='\t'))part[--n]=0;
          if(equal(part,"no-store") || equal(part,"no-cache") || equal(part,"private"))response.noStore=true;
          char* eq=strchr(part,'=');if(eq){*eq++=0;size_t seconds=0;
            if(equal(part,"max-age")){if(number(eq,seconds))response.maxAge=seconds>3600?3600:static_cast<uint32_t>(seconds);else response.noStore=true;}
            if(equal(part,"private") || equal(part,"no-cache"))response.noStore=true;
          }
          part=next;
        }
      }else if(equal(text,"set-cookie") || equal(text,"vary")){response.noStore=true;
      }else if(equal(text,"location")){
        if(locationSeen||strlen(content)>=sizeof(response.location))return Error::Headers;locationSeen=true;strcpy(response.location,content);
      }
    }
    if(response.hasLength&&response.chunked)return Error::Headers;
    if(response.status==100||response.status==102||response.status==103)continue;
    return Error::None;
  }
  return Error::Http;
}
Error streamBody(Reader& reader,const Response& response,Writer& output,size_t limit,size_t& length){
  length=0;
  if(response.status!=200)return Error::Http;
  if(!response.html&&!response.atom&&!response.css&&!response.jpeg&&!response.png)return Error::ContentType;
  if(response.encoded)return Error::Encoding;
  Error error=Error::None;
  if(response.chunked){
    size_t budget=64*1024;char text[128];
    for(;;){
      error=line(reader,text,sizeof(text),budget);if(error!=Error::None)return error;
      size_t chunk=0,digits=0;const char* at=text;
      for(;*at&&*at!=';';++at){
        const char c=lower(*at);unsigned digit;
        if(c>='0'&&c<='9')digit=c-'0';else if(c>='a'&&c<='f')digit=c-'a'+10;else return Error::Http;
        if(chunk>(SIZE_MAX-digit)/16)return Error::TooLarge;chunk=chunk*16+digit;++digits;
      }
      if(!digits)return Error::Http;
      if(!chunk){
        for(;;){error=line(reader,text,sizeof(text),budget);if(error!=Error::None)return error;if(!text[0])break;if(!value(text))return Error::Headers;}
        break;
      }
      if(chunk>limit-length)return Error::TooLarge;
      error=exact(reader,output,chunk,length);if(error!=Error::None)return error;
      uint8_t crlf[2];for(unsigned i=0;i<2;++i)if(reader.read(crlf+i,1)!=1)return Error::Http;
      if(crlf[0]!='\r'||crlf[1]!='\n')return Error::Http;
    }
  }else if(response.hasLength){
    if(response.length>limit)return Error::TooLarge;
    error=exact(reader,output,response.length,length);if(error!=Error::None)return error;
  }else {
    for(;;){
      if(length==limit){uint8_t extra;const int n=reader.read(&extra,1);if(n>0)return Error::TooLarge;if(n<0)return Error::Http;break;}
      const size_t left=limit-length;
      char buffer[1024];const size_t wanted=left<sizeof(buffer)?left:sizeof(buffer);
      const int count=reader.read(reinterpret_cast<uint8_t*>(buffer),wanted);
      if(count<0||static_cast<size_t>(count)>wanted)return Error::Http;if(!count)break;output.write(buffer,count);if(output.failed())return Error::Storage;length+=count;reader.progress(length);
    }
  }
  return Error::None;
}
Error body(Reader& reader,const Response& response,char* output,size_t capacity,size_t& length){
  length=0;if(!output||!capacity)return Error::Memory;output[0]=0;
  if(!response.html)return Error::ContentType;
  struct Buffer:Writer {char* data;size_t used=0;explicit Buffer(char* p):data(p){};void write(const char* p,size_t n)override{memcpy(data+used,p,n);used+=n;}} sink(output);
  const Error error=streamBody(reader,response,sink,capacity-1,length);output[length]=0;return error;
}
} } }
