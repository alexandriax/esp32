#pragma once
#include "browser_fetch.h"
namespace sloth { namespace browser_fetch { namespace detail {
struct Reader {
  virtual ~Reader(){}
  virtual int read(uint8_t* output,size_t capacity)=0; // >0 bytes, 0 EOF, -1 error.
  virtual void progress(size_t received)=0;
};
struct Response {
  uint16_t status=0;bool chunked=false,hasLength=false,html=false,atom=false,encoded=false;
  bool css=false,jpeg=false,png=false,noStore=false;uint32_t maxAge=0;
  size_t length=0;char location[browser_url::kCapacity]{};
};
struct Writer { virtual ~Writer(){};virtual void write(const char*,size_t)=0;virtual bool failed()const{return false;} };
Error headers(Reader& reader,Response& response);
Error streamBody(Reader&,const Response&,Writer&,size_t limit,size_t& length);
Error body(Reader& reader,const Response& response,char* output,size_t capacity,size_t& length);
bool redirect(uint16_t status);
} } }
