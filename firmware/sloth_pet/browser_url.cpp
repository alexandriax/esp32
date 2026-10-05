#include "browser_url.h"
#include <stdio.h>
#include <string.h>
namespace sloth { namespace browser_url {
namespace {
char lower(char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;}
bool starts(const char* text,const char* prefix){while(*prefix)if(lower(*text++)!=*prefix++)return false;return true;}
bool alpha(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
bool digit(char c){return c>='0'&&c<='9';}
bool hex(char c){return digit(c)||(lower(c)>='a'&&lower(c)<='f');}
bool clean(const char* input,char out[kCapacity]){
  if(!input)return false;size_t n=strnlen(input,kCapacity);if(n>=kCapacity)return false;
  size_t start=0;while(start<n&&input[start]==' ')++start;while(n>start&&input[n-1]==' ')--n;
  for(size_t i=start;i<n;++i){const unsigned char c=input[i];if(c<33||c>126||strchr("\\\"<>^`{|}",c))return false;}
  memmove(out,input+start,n-start);out[n-start]=0;return true;
}
bool path(const char* input,char output[kCapacity]){
  char work[kCapacity];size_t n=strcspn(input,"?#");if(n>=kCapacity)return false;
  memcpy(work,input,n);work[n]=0;const char* at=work;size_t used=0;
  // RFC3986 remove_dot_segments. Percent-escaped octets remain literal bytes.
  while(*at){
    if(!strncmp(at,"../",3))at+=3;
    else if(!strncmp(at,"./",2))at+=2;
    else if(!strncmp(at,"/./",3))at+=2;
    else if(!strcmp(at,"/.")){at+=2;output[used++]='/';}
    else if(!strncmp(at,"/../",4)||!strcmp(at,"/..")){
      const bool last=at[3]==0;at+=3;
      while(used&&output[used-1]!='/')--used;if(used)--used;
      if(last)output[used++]='/';
    }else if(!strcmp(at,".")||!strcmp(at,".."))break;
    else {if(*at=='/')output[used++]=*at++;while(*at&&*at!='/')output[used++]=*at++;}
  }
  if(!used)output[used++]='/';
  if(input[n]=='?'){
    const size_t query=strcspn(input+n,"#");if(used+query>=kCapacity)return false;
    memcpy(output+used,input+n,query);used+=query;
  }
  output[used]=0;
  for(size_t i=0;i<used;++i)if(output[i]=='%'){if(i+2>=used||!hex(output[i+1])||!hex(output[i+2]))return false;i+=2;}
  return true;
}
bool format(const Parts& p,char output[kCapacity]){
  const bool standard=p.port==(p.secure?443:80);char port[8]{};
  if(!standard)snprintf(port,sizeof(port),":%u",p.port);
  const int n=snprintf(output,kCapacity,"%s://%s%s%s",p.secure?"https":"http",p.host,port,p.target);
  return n>0&&static_cast<size_t>(n)<kCapacity;
}
}
bool parse(const char* absolute,Parts& p){
  p=Parts{};char url[kCapacity];if(!clean(absolute,url)||!url[0])return false;
  const char* at=nullptr;
  if(starts(url,"https://")){p.secure=true;p.port=443;at=url+8;}
  else if(starts(url,"http://")){p.secure=false;p.port=80;at=url+7;}
  else return false;
  const char* end=at+strcspn(at,"/?#");const char* colon=nullptr;
  for(const char* s=at;s<end;++s)if(*s==':'){if(colon)return false;colon=s;}
  const size_t hostLength=static_cast<size_t>((colon?colon:end)-at);
  if(!hostLength||hostLength>=sizeof(p.host))return false;
  size_t label=0;
  for(size_t i=0;i<hostLength;++i){
    const char c=lower(at[i]);if(c=='.'){if(!label||label>63||p.host[i-1]=='-')return false;label=0;}
    else {if(!alpha(c)&&!digit(c)&&c!='-')return false;if(!label&&c=='-')return false;++label;}
    p.host[i]=c;
  }
  if(!label||label>63||p.host[hostLength-1]=='-')return false;
  if(colon){unsigned port=0;if(colon+1==end)return false;for(const char* s=colon+1;s<end;++s){if(!digit(*s))return false;port=port*10+static_cast<unsigned>(*s-'0');if(port>65535)return false;}if(!port)return false;p.port=static_cast<uint16_t>(port);}
  char target[kCapacity];
  if(*end=='/' ){if(strlen(end)>=sizeof(target))return false;strcpy(target,end);}
  else {if(strlen(end)+1>=sizeof(target))return false;target[0]='/';strcpy(target+1,end);}
  return path(target,p.target);
}
bool normalize(const char* input,char output[kCapacity]){
  char text[kCapacity];if(!clean(input,text)||!text[0]){output[0]=0;return false;}
  output[0]=0;
  char absolute[kCapacity];
  if(strstr(text,"://")){strcpy(absolute,text);}
  else {const int n=snprintf(absolute,sizeof(absolute),"https://%s",text);if(n<0||static_cast<size_t>(n)>=sizeof(absolute))return false;}
  Parts parts{};return parse(absolute,parts)&&format(parts,output);
}
bool resolve(const char* base,const char* reference,char output[kCapacity]){
  Parts p{};char ref[kCapacity];
  if(!parse(base,p)||!clean(reference,ref)){output[0]=0;return false;}
  output[0]=0;
  bool scheme=alpha(ref[0]);size_t i=1;
  while(scheme&&ref[i]&&ref[i]!=':'){if(!alpha(ref[i])&&!digit(ref[i])&&ref[i]!='+'&&ref[i]!='-'&&ref[i]!='.')scheme=false;++i;}
  if(scheme&&ref[i]==':'){Parts absolute{};return parse(ref,absolute)&&format(absolute,output);}
  if(!strncmp(ref,"//",2)){
    char url[kCapacity];const int n=snprintf(url,sizeof(url),"%s:%s",p.secure?"https":"http",ref);
    Parts absolute{};return n>0&&static_cast<size_t>(n)<sizeof(url)&&parse(url,absolute)&&format(absolute,output);
  }
  if(!ref[0]||ref[0]=='#')return format(p,output);
  char combined[kCapacity];
  if(ref[0]=='/')strcpy(combined,ref);
  else {
    size_t keep=strcspn(p.target,"?");
    if(ref[0]!='?'){while(keep&&p.target[keep-1]!='/')--keep;}
    if(keep+strlen(ref)>=sizeof(combined))return false;
    memcpy(combined,p.target,keep);strcpy(combined+keep,ref);
  }
  if(!path(combined,p.target))return false;
  return format(p,output);
}
} }
