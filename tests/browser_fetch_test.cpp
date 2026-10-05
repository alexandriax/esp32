#include "../firmware/sloth_pet/browser_fetch.h"
#include "../firmware/sloth_pet/browser_reader.h"
#include "../firmware/sloth_pet/browser_memory.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <Arduino.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <mbedtls/ssl.h>
#include <esp_crypto_lock.h>
#include <sys/socket.h>
using namespace sloth::browser_fetch;
namespace {
uint32_t clockMs=0,cancelAt=0;bool coreLocked=false,autoDns=true,taskFails=false,tlsFails=false,tlsWaits=false,verifyFails=false,bundleFails=false,stallRead=false,tlsCloseNotify=false;
size_t heapFree=400000;int sockets=0,tlsLive=0,bundles=0;unsigned connectCount=0;
bool cryptoHeld[3]{};unsigned cryptoPrepares=0;
time_t utc=1800000000;
void(*workerTask)(void*)=nullptr;void* workerArg=nullptr;
struct Dns {dns_found_callback callback;void* argument;};std::vector<Dns> dnsPending;
std::vector<std::string> responses;size_t inputAt=0;std::string request;
const std::string ok="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nhello";
void runWorker(){assert(workerTask);auto fn=workerTask;void* arg=workerArg;workerTask=nullptr;fn(arg);assert(!busy()&&sockets==0&&tlsLive==0&&bundles==0);assert(stackLowWaterBytes()==4096);}
void reset(){cancel();assert(!busy());clockMs=cancelAt=0;autoDns=true;taskFails=tlsFails=tlsWaits=verifyFails=bundleFails=stallRead=tlsCloseNotify=false;heapFree=400000;utc=1800000000;responses={ok};connectCount=0;inputAt=0;request.clear();}
void expect(Error e){runWorker();assert(snapshot().error==e);if(e!=Error::None)assert(snapshot().state==State::Failed||snapshot().state==State::Cancelled);}
}
uint32_t millis(){return clockMs;}
time_t browser_mock_time(time_t*){return utc;}
int xTaskCreate(void(*fn)(void*),const char*,unsigned stack,void* arg,unsigned,void*){assert(stack==16384);if(taskFails)return 0;workerTask=fn;workerArg=arg;return 1;}
void vTaskDelete(void*){}
uint32_t uxTaskGetStackHighWaterMark(void* task){assert(!task);return 4096;}
void vTaskDelay(unsigned ms){
  clockMs+=ms;if(cancelAt&&clockMs>=cancelAt){cancelAt=0;cancel();}
  if(autoDns&&!dnsPending.empty()){
    const auto pending=dnsPending;dnsPending.clear();browser_mock_lock();
    for(size_t i=0;i<pending.size();++i){ip_addr_t address{0x01020304};pending[i].callback("ignored",i+1==pending.size()?&address:nullptr,pending[i].argument);}
    browser_mock_unlock();
  }
}
size_t heap_caps_get_free_size(unsigned){return heapFree;}
size_t heap_caps_get_largest_free_block(unsigned){return heapFree;}
void browser_mock_lock(){assert(!coreLocked);coreLocked=true;}
void browser_mock_unlock(){assert(coreLocked);coreLocked=false;}
err_t dns_gethostbyname_addrtype(const char* host,ip_addr_t*,dns_found_callback cb,void* arg,int){assert(coreLocked&&host[0]);dnsPending.push_back({cb,arg});return ERR_INPROGRESS;}
int browser_mock_socket(int,int,int){++sockets;inputAt=0;return static_cast<int>(++connectCount)+20;}
int browser_mock_connect(int,const sockaddr*,socklen_t){errno=EINPROGRESS;return -1;}
int browser_mock_close(int){assert(sockets>0);--sockets;return 0;}
int browser_mock_shutdown(int,int){return 0;}
int browser_mock_fcntl(int,int,...){return 0;}
int browser_mock_select(int,fd_set*,fd_set*,fd_set*,timeval*){return 1;}
int browser_mock_getsockopt(int,int,int,void* out,socklen_t*){*static_cast<int*>(out)=0;return 0;}
int browser_mock_send(int,const void* bytes,size_t n,int){request.append(static_cast<const char*>(bytes),n);return static_cast<int>(n);}
int browser_mock_recv(int,void* out,size_t capacity,int){
  if(stallRead){errno=EAGAIN;return -1;}assert(connectCount&&connectCount<=responses.size());const auto& input=responses[connectCount-1];
  const size_t n=std::min(std::min(capacity,size_t(3)),input.size()-inputAt);memcpy(out,input.data()+inputAt,n);inputAt+=n;return static_cast<int>(n);
}
void mbedtls_ssl_init(mbedtls_ssl_context*){++tlsLive;}
void mbedtls_ssl_free(mbedtls_ssl_context*){assert(tlsLive>0);--tlsLive;}
int mbedtls_ssl_setup(mbedtls_ssl_context* s,const mbedtls_ssl_config* c){s->config=const_cast<mbedtls_ssl_config*>(c);return 0;}
int mbedtls_ssl_set_hostname(mbedtls_ssl_context* s,const char* h){assert(strlen(h)<sizeof(s->hostname));strcpy(s->hostname,h);return 0;}
void mbedtls_ssl_set_bio(mbedtls_ssl_context* s,void* p,int(*)(void*,const unsigned char*,size_t),int(*)(void*,unsigned char*,size_t),void*){s->ioFd=static_cast<int*>(p);}
int mbedtls_ssl_handshake(mbedtls_ssl_context* s){assert(s->config->auth==MBEDTLS_SSL_VERIFY_REQUIRED&&s->config->minimum==MBEDTLS_SSL_VERSION_TLS1_2&&s->config->bundle&&s->hostname[0]);return tlsFails?-99:tlsWaits?MBEDTLS_ERR_SSL_WANT_READ:0;}
uint32_t mbedtls_ssl_get_verify_result(const mbedtls_ssl_context*){return verifyFails?1:0;}
int mbedtls_ssl_write(mbedtls_ssl_context* s,const unsigned char* p,size_t n){return browser_mock_send(*s->ioFd,p,n,0);}
int mbedtls_ssl_read(mbedtls_ssl_context* s,unsigned char* p,size_t n){const int r=browser_mock_recv(*s->ioFd,p,n,0);return r<0?MBEDTLS_ERR_SSL_WANT_READ:r==0&&tlsCloseNotify?MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:r;}
int esp_crt_bundle_attach(void* p){if(bundleFails)return -1;static_cast<mbedtls_ssl_config*>(p)->bundle=true;++bundles;return 0;}
void esp_crt_bundle_detach(mbedtls_ssl_config* p){assert(p->bundle&&bundles>0);p->bundle=false;--bundles;}
void esp_crypto_sha_aes_lock_acquire(){assert(!cryptoHeld[0]);cryptoHeld[0]=true;++cryptoPrepares;}
void esp_crypto_sha_aes_lock_release(){assert(cryptoHeld[0]);cryptoHeld[0]=false;}
void esp_crypto_mpi_lock_acquire(){assert(!cryptoHeld[1]);cryptoHeld[1]=true;++cryptoPrepares;}
void esp_crypto_mpi_lock_release(){assert(cryptoHeld[1]);cryptoHeld[1]=false;}
void esp_crypto_ecc_lock_acquire(){assert(!cryptoHeld[2]);cryptoHeld[2]=true;++cryptoPrepares;}
void esp_crypto_ecc_lock_release(){assert(cryptoHeld[2]);cryptoHeld[2]=false;}
int main(){
  prepare();prepare();assert(cryptoPrepares==6 && !cryptoHeld[0] && !cryptoHeld[1] && !cryptoHeld[2]);
  assert(!busy() && !workerTask && sockets==0 && tlsLive==0 && bundles==0);
  reset();assert(stackLowWaterBytes()==0);assert(!begin("javascript:alert(1)")&&snapshot().error==Error::Url);
  taskFails=true;assert(!begin("https://a.test/")&&!busy()&&snapshot().error==Error::Memory);taskFails=false;
  assert(begin("https://a.test/start"));assert(busy()&&!begin("https://b.test/"));Result result;assert(!take(result));expect(Error::None);
  assert(snapshot().state==State::Complete&&!begin("https://b.test/"));assert(take(result)&&result.secure&&result.length==5&&!strcmp(result.bytes,"hello")&&!strcmp(result.url,"https://a.test/start"));
  assert(request.find("Host: a.test\r\n")!=std::string::npos&&request.find("Accept-Encoding: identity")!=std::string::npos);release(result);assert(!result.bytes);
  reset();utc=0;assert(begin("https://a.test/"));assert(stackLowWaterBytes()==0);expect(Error::Clock);assert(connectCount==0);
  reset();utc=0;assert(begin("http://a.test/"));expect(Error::None);assert(take(result)&&!result.secure);release(result);
  for(int which=0;which<3;++which){reset();tlsFails=which==0;verifyFails=which==1;bundleFails=which==2;assert(begin("https://a.test/"));expect(Error::Tls);assert(!take(result));}
  reset();tlsWaits=true;assert(begin("https://a.test/"));expect(Error::Timeout);assert(clockMs<9000);
  reset();autoDns=false;cancelAt=30;assert(begin("https://old.test/"));expect(Error::Cancelled);assert(!dnsPending.empty());
  // The old callback fires after a new worker starts: its generation cannot
  // access freed jobs or poison the new request's DNS result.
  reset();assert(begin("https://new.test/"));expect(Error::None);assert(take(result));release(result);assert(dnsPending.empty());
  reset();autoDns=false;assert(begin("https://a.test/"));expect(Error::Timeout);assert(clockMs==5000);autoDns=true;
  reset();tlsWaits=true;cancelAt=30;assert(begin("https://a.test/"));expect(Error::Cancelled);assert(clockMs<100);
  reset();stallRead=true;assert(begin("https://a.test/"));expect(Error::Timeout);assert(clockMs<6000);
  reset();heapFree=100;assert(begin("https://a.test/"));expect(Error::Memory);
  reset();responses={"HTTP/1.1 302 Found\r\nLocation: ../next\r\n\r\n",ok};assert(begin("https://a.test/dir/page"));expect(Error::None);assert(connectCount==2&&take(result)&&!strcmp(result.url,"https://a.test/next"));release(result);
  for(const char* location:{"http://a.test/","file:///tmp/a","javascript:alert(1)","https://user@a.test/"}){reset();responses={std::string("HTTP/1.1 302 Found\r\nLocation: ")+location+"\r\n\r\n"};assert(begin("https://a.test/"));expect(Error::Redirect);assert(connectCount==1);}
  reset();responses=std::vector<std::string>(4,"HTTP/1.1 301 Moved\r\nLocation: /again\r\n\r\n");assert(begin("https://a.test/"));expect(Error::Redirect);assert(connectCount==4);
  reset();responses={"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 32769\r\n\r\n"};assert(begin("https://a.test/"));expect(Error::TooLarge);
  reset();assert(begin("https://a.test/"));expect(Error::None);cancel();assert(snapshot().state==State::Cancelled&&!take(result));responses.push_back(ok);assert(begin("https://a.test/"));expect(Error::None);assert(take(result));release(result);
  reset();responses={"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\nhello"};assert(begin("https://a.test/"));expect(Error::Tls);assert(!take(result));
  reset();tlsCloseNotify=true;responses={"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\nhello"};assert(begin("https://a.test/"));expect(Error::None);assert(take(result)&&result.length==5);release(result);
  reset();responses={"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\nhello"};assert(begin("http://a.test/"));expect(Error::None);assert(take(result)&&result.length==5);release(result);
  // Stream a body larger than the graphical cap while retaining bounded text.
  alignas(max_align_t) unsigned char canvas[115200];assert(browser_memory_begin(canvas,sizeof(canvas)));
  std::string large="<html><script>"+std::string(40000,'x')+"</script><p><a href='/next'>Readable story</a></p></html>";
  reset();responses={"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+std::to_string(large.size())+"\r\n\r\n"+large};
  assert(begin("https://a.test/",true));expect(Error::None);assert(take(result)&&result.reader&&!result.bytes&&result.length>32768&&result.reader->links==1);release(result);assert(!browser_memory_live());
  const std::string feed="<feed><entry><content type='html'>&lt;p&gt;Body&lt;/p&gt;</content><link href='https://reddit.com/comments/abc/'/><title>Readable title</title></entry></feed>";
  reset();responses={"HTTP/1.1 200 OK\r\nContent-Type: application/atom+xml\r\nContent-Length: "+std::to_string(feed.size())+"\r\n\r\n"+feed};
  assert(begin("https://reddit.com/r/test/",true));expect(Error::None);assert(take(result)&&result.reader&&result.reader->links==1&&!strcmp(result.url,"https://reddit.com/r/test/"));assert(request.find("GET /r/test/.rss?limit=12 ")!=std::string::npos);release(result);
  reset();responses={"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 524289\r\n\r\n"};assert(begin("https://a.test/",true));expect(Error::TooLarge);assert(!browser_memory_live());
  reset();responses={"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 100\r\n\r\nshort"};assert(begin("https://a.test/",true));expect(Error::Tls);assert(!browser_memory_live());
  reset();stallRead=true;cancelAt=30;assert(begin("https://a.test/",true));expect(Error::Cancelled);assert(!browser_memory_live());assert(browser_memory_end()==canvas);
  puts("browser_fetch: actual worker with mocked DNS/socket/TLS; cancellation, stale DNS, required verification, redirects and cleanup passed");
}
