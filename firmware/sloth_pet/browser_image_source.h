#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
namespace sloth { namespace browser_image {
constexpr unsigned kMaxImages=12;
inline bool space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f';}
inline char lower(char c){return c>='A'&&c<='Z'?c+32:c;}
inline bool equal(const char* a,size_t n,const char* b){if(n!=strlen(b))return false;for(size_t i=0;i<n;++i)if(lower(a[i])!=b[i])return false;return true;}
// Views into one complete bounded tag; quoted > and commas in URLs are data.
inline const char* attribute(const char* tag,size_t length,const char* name,size_t& size){
 const char* p=tag;const char* end=tag+length;size=0;
 if(p<end&&*p=='<')++p;while(p<end&&!space(*p)&&*p!='>')++p;
 while(p<end){while(p<end&&(space(*p)||*p=='/'))++p;if(p==end||*p=='>')break;
  const char* a=p;while(p<end&&!space(*p)&&*p!='='&&*p!='>')++p;size_t n=p-a;
  while(p<end&&space(*p))++p;if(p==end||*p!='=')continue;++p;while(p<end&&space(*p))++p;
  const char quote=p<end&&(*p=='\''||*p=='"')?*p++:0;const char* value=p;
  while(p<end&&(quote?*p!=quote:!space(*p)&&*p!='>'))++p;
  if(equal(a,n,name)){size=p-value;return value;}if(quote&&p<end)++p;
 }return nullptr;
}
inline bool decode(const char* p,size_t n,char* out,size_t cap){
 size_t at=0;out[0]=0;
 for(size_t i=0;i<n;++i){unsigned c=static_cast<unsigned char>(p[i]);
  if(c=='&'){
   size_t e=i+1;while(e<n&&e-i<12&&p[e]!=';'&&!space(p[e])&&p[e]!='&')++e;
   if(e<n&&p[e]==';'){
    const char* v=p+i+1;const size_t count=e-i-1;
    if(equal(v,count,"amp"))c='&';else if(equal(v,count,"quot"))c='"';else if(equal(v,count,"apos"))c='\'';
    else if(v[0]=='#'){unsigned base=10,k=1,num=0;if(k<count&&(v[k]=='x'||v[k]=='X')){base=16;++k;}if(k==count)return false;
     for(;k<count;++k){unsigned d=lower(v[k]);d=d>='0'&&d<='9'?d-'0':d>='a'&&d<='f'?d-'a'+10:99;if(d>=base||num>127/base)return false;num=num*base+d;}c=num;
    }else return false;i=e;
   }
  }
  if(c<32||c>126||at+1>=cap){out[0]=0;return false;}out[at++]=static_cast<char>(c);
 }out[at]=0;return at!=0;
}
inline bool eligible(const char* tag,size_t length,const char* src){
 if(!src||!*src||!strncmp(src,"data:",5))return false;
 const char* end=strpbrk(src,"?#");if(!end)end=src+strlen(src);
 for(const char* ext:{".svg",".gif",".webp",".avif"}){const size_t n=strlen(ext);if(static_cast<size_t>(end-src)>=n&&equal(end-n,n,ext))return false;}
 for(const char* dimension:{"width","height"}){size_t n=0;const char* v=attribute(tag,length,dimension,n);
  if(v&&n==1&&*v>='0'&&*v<='2')return false;
 }return true;
}
// Choose a display-sized responsive candidate, never the often enormous src
// fallback. One native pixel per image pixel; this display needs no 2x asset.
inline bool source(const char* tag,size_t length,char* out,size_t cap=512){
 size_t n=0;const char* p=attribute(tag,length,"width",n);unsigned target=320;
 if(p&&n&&n<8){char number[8]{};memcpy(number,p,n);unsigned w=static_cast<unsigned>(strtoul(number,nullptr,10));if(w&&w<target)target=w;}
 p=attribute(tag,length,"srcset",n);const char* chosen=nullptr;size_t chosenN=0;double best=0;bool below=false;
 if(p){const char* end=p+n;
  while(p<end){while(p<end&&(space(*p)||*p==','))++p;const char* u=p;while(p<end&&!space(*p))++p;size_t un=p-u;
   // Descriptor-less candidates can be followed by a comma.
   bool trailing=un&&u[un-1]==',';if(trailing)--un;
   double score=1;bool valid=un!=0;
   if(!trailing){while(p<end&&space(*p))++p;const char* d=p;while(p<end&&*p!=',')++p;size_t dn=p-d;while(dn&&space(d[dn-1]))--dn;
    if(dn){char descriptor[24]{};if(dn>=sizeof(descriptor))valid=false;else{memcpy(descriptor,d,dn);char* stop=nullptr;double value=strtod(descriptor,&stop);
      valid=value>0&&value<100000&&stop==descriptor+dn-1&&(*stop=='w'||*stop=='x');if(valid)score=*stop=='w'?value/target:value;}}
   }
   if(valid){const bool fits=score<=1;if(!chosen||(fits&&!below)||(fits==below&&(fits?score>best:score<best))){chosen=u;chosenN=un;best=score;below=fits;}}
   if(p<end&&*p==',')++p;
  }
 }
 if(chosen&&decode(chosen,chosenN,out,cap))return true;
 p=attribute(tag,length,"src",n);return p&&decode(p,n,out,cap);
}
} }
