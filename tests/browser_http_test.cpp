#include "../firmware/sloth_pet/browser_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
using namespace sloth::browser_fetch;
struct Reader:detail::Reader {
  std::string data;size_t at=0,fragment=1,lastProgress=0,failAt=SIZE_MAX;
  explicit Reader(const std::string& text,size_t f=1):data(text),fragment(f){}
  int read(uint8_t* out,size_t capacity)override{if(at>=failAt)return -1;if(at==data.size())return 0;const size_t n=std::min(std::min(capacity,fragment),data.size()-at);memcpy(out,data.data()+at,n);at+=n;return static_cast<int>(n);}
  void progress(size_t n)override{assert(n>=lastProgress);lastProgress=n;}
};
Error decode(const std::string& wire,std::string* result=nullptr,size_t capacity=32769,size_t fragment=1){
  Reader reader(wire,fragment);detail::Response response;Error error=detail::headers(reader,response);if(error!=Error::None)return error;
  std::string output(capacity,'?');size_t length=0;error=detail::body(reader,response,&output[0],capacity,length);
  if(error==Error::None){assert(output[length]==0&&length==reader.lastProgress);if(result)*result=output.substr(0,length);}return error;
}
int main(){
  const std::string fixed="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: 5\r\n\r\nhello";
  for(size_t fragment:{1u,2u,3u,7u,1024u}){std::string result;assert(decode(fixed,&result,32769,fragment)==Error::None&&result=="hello");}
  std::string result;
  assert(decode("HTTP/1.0 200 OK\r\nContent-Type: application/xhtml+xml\r\n\r\n<html/>",&result)==Error::None&&result=="<html/>");
  const std::string chunked="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n2;test=x\r\nhe\r\n3\r\nllo\r\n0\r\nX-Test: value\r\n\r\n";
  for(size_t fragment:{1u,2u,5u,1024u})assert(decode(chunked,&result,32769,fragment)==Error::None&&result=="hello");
  assert(decode("HTTP/1.1 103 Early Hints\r\nLink: </style.css>\r\n\r\n"+fixed,&result)==Error::None&&result=="hello");
  assert(decode("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Encoding: gzip\r\n\r\nx")==Error::Encoding);
  assert(decode("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{}")==Error::ContentType);
  assert(decode("HTTP/1.1 200 OK\r\n\r\n<html/>")==Error::ContentType);
  assert(decode("HTTP/1.1 404 No\r\nContent-Type: text/html\r\n\r\nx")==Error::Http);
  assert(decode(fixed,nullptr,5)==Error::TooLarge);assert(decode(fixed.substr(0,fixed.size()-1))==Error::Http);
  assert(decode("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\nhello",nullptr,6)==Error::None);
  assert(decode("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\nhello!",nullptr,6)==Error::TooLarge);
  assert(decode("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n6\r\nhello!\r\n0\r\n\r\n",nullptr,6)==Error::TooLarge);
  for(const char* bad:{"Content-Length: -1","Content-Length: 184467440737095516150","Content-Length: 2\r\nContent-Length: 2","Content-Length: 1\r\nTransfer-Encoding: chunked"," folded-header","Bad Header: x","Content-Type: text/html\r\nContent-Type: text/html"}){
    const auto error=decode(std::string("HTTP/1.1 200 OK\r\n")+bad+"\r\n\r\nx");assert(error==Error::Headers);
  }
  assert(decode("HTTP/1.1 200 OK\nContent-Type: text/html\n\nx")==Error::Headers);
  assert(decode("HTTP/1.1 200 OK\r\nX: "+std::string(600,'x')+"\r\n\r\n")==Error::Headers);
  std::string many="HTTP/1.1 200 OK\r\n";for(int i=0;i<1000;++i)many+="X: value\r\n";many+="\r\n";assert(decode(many)==Error::Headers);
  for(const char* chunks:{"x\r\n","1\r\naX\n0\r\n\r\n","1\r\n","FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF\r\n"}){
    const Error error=decode(std::string("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n")+chunks);assert(error==Error::Http||error==Error::TooLarge);
  }
  Reader interrupted(fixed);interrupted.failAt=10;detail::Response response;assert(detail::headers(interrupted,response)==Error::Http);
  Reader redirect("HTTP/1.1 302 Found\r\nLocation: ../next#fragment\r\nContent-Length: 999999\r\n\r\n");assert(detail::headers(redirect,response)==Error::None&&detail::redirect(response.status)&&!strcmp(response.location,"../next#fragment"));
  assert(detail::redirect(301)&&detail::redirect(303)&&detail::redirect(307)&&detail::redirect(308)&&!detail::redirect(304));
  for(const char* type:{"text/css","image/png","image/jpeg"}){
    Reader asset(std::string("HTTP/1.1 200 OK\r\nContent-Type: ")+type+"\r\nCache-Control: public, max-age=7200\r\nContent-Length: 3\r\n\r\nabc");
    assert(detail::headers(asset,response)==Error::None&&response.maxAge==3600&&!response.noStore);
    struct Sink:detail::Writer {bool failed()const override{return true;}void write(const char*,size_t)override{}} sink;
    size_t n;assert(detail::streamBody(asset,response,sink,100,n)==Error::Storage);
  }
  for(const char* control:{"no-store, max-age=3600","private=field, max-age=3600","max-age=xyz","no-cache"}){
    Reader asset(std::string("HTTP/1.1 200 OK\r\nContent-Type: text/css\r\nCache-Control: ")+control+"\r\n\r\n");
    assert(detail::headers(asset,response)==Error::None&&response.noStore);
  }
  puts("browser_http: fragmented HTTP, bounded headers/body, strict chunks, encodings, truncation and redirect metadata passed");
}
