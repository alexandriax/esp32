#include "../firmware/sloth_pet/browser_url.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
using namespace sloth::browser_url;
void normal(const char* input,const char* expected){char out[kCapacity];assert(normalize(input,out));if(strcmp(out,expected)){fprintf(stderr,"normalize %s -> %s, expected %s\n",input,out,expected);assert(false);}}
void resolved(const char* ref,const char* expected){char out[kCapacity];assert(resolve("https://a.test/b/c/d;p?q",ref,out));if(strcmp(out,expected)){fprintf(stderr,"resolve %s -> %s, expected %s\n",ref,out,expected);assert(false);}}
int main(){
  normal("EXAMPLE.com","https://example.com/");normal(" https://Example.com:443/a/./b/../c?x=1#section ","https://example.com/a/c?x=1");
  normal("http://192.0.2.1:8080?query","http://192.0.2.1:8080/?query");normal("http://a.test:80","http://a.test/");normal("https://a.test/a//b/%2E%2E/c","https://a.test/a//b/%2E%2E/c");
  resolved("g","https://a.test/b/c/g");resolved("./g","https://a.test/b/c/g");resolved("g/","https://a.test/b/c/g/");resolved("/g","https://a.test/g");resolved("//g.test","https://g.test/");resolved("?y","https://a.test/b/c/d;p?y");resolved("g?y","https://a.test/b/c/g?y");resolved("#s","https://a.test/b/c/d;p?q");resolved("","https://a.test/b/c/d;p?q");resolved(".","https://a.test/b/c/");resolved("..","https://a.test/b/");resolved("../g","https://a.test/b/g");resolved("../../g","https://a.test/g");resolved("../../../g","https://a.test/g");resolved("/./g","https://a.test/g");resolved("/../g","https://a.test/g");resolved("g/./h","https://a.test/b/c/g/h");resolved("g/../h","https://a.test/b/c/h");resolved("g;x=1/../y","https://a.test/b/c/y");resolved("g?y/../x","https://a.test/b/c/g?y/../x");resolved("HTTP://OTHER.test/a","http://other.test/a");
  const char* invalid[]={"","https://","https:///x","ftp://example.com","javascript:alert(1)","file:///x","data:text/html,x","https://user:pass@example.com/","https://example.com\\@evil.test/","https://example.com/\r\nHost:evil.test","https://example.com/a b","https://[::1]/","https://-bad.test/","https://bad-.test/","https://bad..test/","https://bad.test:0/","https://bad.test:65536/","https://bad.test:+80/","https://bad.test:80:90/","https://bad.test/%zz","https://bad.test/%a","https://bad.test/%","https://exämple.test/","https://a.test/<script>"};
  char out[kCapacity];for(const char* input:invalid){if(normalize(input,out)){fprintf(stderr,"accepted invalid %s\n",input);assert(false);}}
  for(const char* input:{"javascript:alert(1)","data:text/html,x","file:///tmp/x","mailto:a@b.test","https://a.test/\n"})assert(!resolve("https://a.test/",input,out));
  std::string longUrl="https://a.test/"+std::string(600,'a');assert(!normalize(longUrl.c_str(),out));
  Parts parts{};assert(parse("https://a.test:444/a?b#c",parts)&&parts.secure&&parts.port==444&&!strcmp(parts.host,"a.test")&&!strcmp(parts.target,"/a?b"));
  strcpy(out,"https://A.test/a/b");assert(normalize(out,out)&&!strcmp(out,"https://a.test/a/b"));
  assert(resolve(out,"../c",out)&&!strcmp(out,"https://a.test/c"));
  strcpy(out,"next");assert(resolve("https://a.test/",out,out)&&!strcmp(out,"https://a.test/next"));
  puts("browser_url: HTTP(S) normalization, RFC relative references, bounds and unsafe URL rejection passed");
}
