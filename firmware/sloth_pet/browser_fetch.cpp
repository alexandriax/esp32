#include "browser_fetch.h"
#include "browser_assets.h"
#include "browser_http.h"
#include "browser_reader.h"
#include "browser_memory.h"
#include <mbedtls/platform.h>
#include <Arduino.h>
#include <atomic>
#include <errno.h>
#include <fcntl.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <esp_heap_caps.h>
#include <esp_crt_bundle.h>
#include <esp_crypto_lock.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

namespace sloth { namespace browser_fetch {
namespace {
constexpr uint32_t kTotalMs=60000,kDnsMs=5000,kConnectMs=4000,kTlsMs=8000,kIdleMs=5000;
constexpr size_t kHeapReserve=16*1024;
std::atomic<bool> running(false),cancelled(false);
std::atomic<uint32_t> workerStackLowWater(0);
uint32_t assetStarted=0;bool preparingAssets=false;
portMUX_TYPE stateLock=portMUX_INITIALIZER_UNLOCKED;
Snapshot info{};
struct Job { Result result;bool textMode=false;char requestUrl[512]{};bool feed=false;detail::Writer* sink=nullptr;browser_assets::Kind kind=browser_assets::Kind::Css;size_t limit=0;browser_assets::Download download; };
Job* current=nullptr;
// lwIP owns DNS queries until their callbacks finish. This process-lifetime
// slot never points into worker memory. Its generation invalidates late replies
// after cancellation, timeout or a later request. All fields use the core lock.
struct DnsSlot { uintptr_t generation=0;bool done=false,ok=false;ip_addr_t address{}; } dnsSlot;

void progress(size_t bytes,uint16_t status=0){
  portENTER_CRITICAL(&stateLock);
  info.received=static_cast<uint32_t>(bytes);if(status)info.httpStatus=status;
  portEXIT_CRITICAL(&stateLock);
}
Error interrupted(uint32_t started){
  if(cancelled.load())return Error::Cancelled;
  return (static_cast<uint32_t>(millis()-started)>=kTotalMs || (preparingAssets && static_cast<uint32_t>(millis()-assetStarted)>=60000))?Error::Timeout:Error::None;
}
void pause(){vTaskDelay(pdMS_TO_TICKS(10));}
void dnsDone(const char*,const ip_addr_t* address,void* argument){
  // lwIP invokes this callback on its TCP/IP thread with the core lock held.
  if(reinterpret_cast<uintptr_t>(argument)!=dnsSlot.generation)return;
  dnsSlot.ok=address&&IP_IS_V4(address);
  if(dnsSlot.ok)dnsSlot.address=*address;
  dnsSlot.done=true;
}
Error resolveHost(const char* host,ip_addr_t& address,uint32_t totalStarted){
  LOCK_TCPIP_CORE();
  if(++dnsSlot.generation==0)++dnsSlot.generation;
  const uintptr_t generation=dnsSlot.generation;
  dnsSlot.done=dnsSlot.ok=false;
  const err_t result=dns_gethostbyname_addrtype(host,&dnsSlot.address,dnsDone,
      reinterpret_cast<void*>(generation),LWIP_DNS_ADDRTYPE_IPV4);
  if(result==ERR_OK){dnsSlot.done=true;dnsSlot.ok=IP_IS_V4(&dnsSlot.address);}
  else if(result!=ERR_INPROGRESS)dnsSlot.done=true;
  UNLOCK_TCPIP_CORE();
  const uint32_t started=millis();Error error=Error::None;
  for(;;){
    error=interrupted(totalStarted);if(error!=Error::None)break;
    LOCK_TCPIP_CORE();const bool done=dnsSlot.done,ok=dnsSlot.ok;
    if(done&&ok)address=dnsSlot.address;
    UNLOCK_TCPIP_CORE();
    if(done){error=ok?Error::None:Error::Dns;break;}
    if(static_cast<uint32_t>(millis()-started)>=kDnsMs){error=Error::Timeout;break;}
    pause();
  }
  LOCK_TCPIP_CORE();++dnsSlot.generation;UNLOCK_TCPIP_CORE();
  return error;
}
struct Socket {
  int fd=-1;
  ~Socket(){if(fd>=0){shutdown(fd,SHUT_RDWR);close(fd);}}
  Error connectTo(const ip_addr_t& address,uint16_t port,uint32_t totalStarted){
    fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(fd<0)return Error::Connect;
    const int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)return Error::Connect;
    sockaddr_in peer{};peer.sin_family=AF_INET;peer.sin_port=htons(port);peer.sin_addr.s_addr=ip4_addr_get_u32(ip_2_ip4(&address));
    const int result=::connect(fd,reinterpret_cast<sockaddr*>(&peer),sizeof(peer));
    if(result==0)return Error::None;
    if(errno!=EINPROGRESS)return Error::Connect;
    const uint32_t started=millis();
    for(;;){
      const Error interruptedError=interrupted(totalStarted);if(interruptedError!=Error::None)return interruptedError;
      if(static_cast<uint32_t>(millis()-started)>=kConnectMs)return Error::Timeout;
      fd_set writable;FD_ZERO(&writable);FD_SET(fd,&writable);timeval timeout{0,10000};
      const int ready=select(fd+1,nullptr,&writable,nullptr,&timeout);
      if(ready<0){if(errno==EINTR)continue;return Error::Connect;}
      if(ready){int error=0;socklen_t length=sizeof(error);return getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)==0&&!error?Error::None:Error::Connect;}
    }
  }
};
int tlsSend(void* context,const unsigned char* bytes,size_t length){
  const int result=send(*static_cast<int*>(context),bytes,length,0);
  if(result>=0)return result;
  return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?MBEDTLS_ERR_SSL_WANT_WRITE:MBEDTLS_ERR_NET_SEND_FAILED;
}
int tlsRead(void* context,unsigned char* bytes,size_t length){
  const int result=recv(*static_cast<int*>(context),bytes,length,0);
  if(result>=0)return result;
  return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?MBEDTLS_ERR_SSL_WANT_READ:MBEDTLS_ERR_NET_RECV_FAILED;
}
bool wants(int result){return result==MBEDTLS_ERR_SSL_WANT_READ||result==MBEDTLS_ERR_SSL_WANT_WRITE;}
struct Tls {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_ssl_config config;
  mbedtls_ssl_context ssl;
  bool bundle=false;
  Tls(){mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&random);mbedtls_ssl_config_init(&config);mbedtls_ssl_init(&ssl);}
  ~Tls(){
    mbedtls_ssl_free(&ssl);
    // This mode owns the network/TLS consumer. Release the bundle index too.
    if(bundle)esp_crt_bundle_detach(&config);
    mbedtls_ssl_config_free(&config);mbedtls_ctr_drbg_free(&random);mbedtls_entropy_free(&entropy);
  }
  Error start(Socket& socket,const char* hostname,uint32_t totalStarted){
    static const unsigned char personalization[]="Moss standalone browser";
    if(mbedtls_ctr_drbg_seed(&random,mbedtls_entropy_func,&entropy,personalization,sizeof(personalization)-1)!=0||
       mbedtls_ssl_config_defaults(&config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0)return Error::Tls;
    mbedtls_ssl_conf_authmode(&config,MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_rng(&config,mbedtls_ctr_drbg_random,&random);
    if(esp_crt_bundle_attach(&config)!=ESP_OK)return Error::Tls;
    bundle=true;
    if(mbedtls_ssl_setup(&ssl,&config)!=0||mbedtls_ssl_set_hostname(&ssl,hostname)!=0)return Error::Tls;
    mbedtls_ssl_set_bio(&ssl,&socket.fd,tlsSend,tlsRead,nullptr);
    const uint32_t started=millis();
    for(;;){
      Error error=interrupted(totalStarted);if(error!=Error::None)return error;
      if(static_cast<uint32_t>(millis()-started)>=kTlsMs)return Error::Timeout;
      const int result=mbedtls_ssl_handshake(&ssl);
      if(result==0)return mbedtls_ssl_get_verify_result(&ssl)==0?Error::None:Error::Tls;
      if(!wants(result))return Error::Tls;
      pause();
    }
  }
};
struct Stream : detail::Reader {
  Socket& socket;Tls* tls;uint32_t totalStarted;Error error=Error::None;
  Stream(Socket& s,Tls* t,uint32_t started):socket(s),tls(t),totalStarted(started){}
  uint8_t buffer[1024];size_t buffered=0,position=0;
  int read(uint8_t* output,size_t capacity) override {
    error=interrupted(totalStarted);if(error!=Error::None)return -1;
    if(position==buffered){const int n=receive(buffer,sizeof(buffer));if(n<=0)return n;buffered=n;position=0;}
    size_t n=buffered-position;if(n>capacity)n=capacity;memcpy(output,buffer+position,n);position+=n;return n;
  }
  int receive(uint8_t* output,size_t capacity) {
    const uint32_t started=millis();
    for(;;){
      error=interrupted(totalStarted);if(error!=Error::None)return -1;
      int result=tls?mbedtls_ssl_read(&tls->ssl,output,capacity):recv(socket.fd,output,capacity,0);
      if(result>0)return result;
      if(tls&&result==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)return 0;
      // TLS transport EOF without authenticated CloseNotify is truncation,
      // especially for responses whose body length is delimited by EOF.
      if(result==0){if(tls){error=Error::Tls;return -1;}return 0;}
      const bool again=tls?wants(result):(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR);
      if(!again){error=tls?Error::Tls:Error::Http;return -1;}
      if(static_cast<uint32_t>(millis()-started)>=kIdleMs){error=Error::Timeout;return -1;}
      pause();
    }
  }
  void progress(size_t count) override {browser_fetch::progress(count);}
  Error write(const char* bytes,size_t length){
    size_t sent=0;uint32_t lastWrite=millis();
    while(sent<length){
      Error error=interrupted(totalStarted);if(error!=Error::None)return error;
      const int count=tls?mbedtls_ssl_write(&tls->ssl,reinterpret_cast<const unsigned char*>(bytes+sent),length-sent):send(socket.fd,bytes+sent,length-sent,0);
      if(count>0){sent+=count;lastWrite=millis();continue;}
      if(count==0)return Error::Http;
      if(tls?!wants(count):!(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return tls?Error::Tls:Error::Http;
      if(static_cast<uint32_t>(millis()-lastWrite)>=kIdleMs)return Error::Timeout;
      pause();
    }
    return Error::None;
  }
};
struct DeleteTls { Tls* value=nullptr;~DeleteTls(){delete value;} };
Error run(Job& job){
  const uint32_t started=millis();
  for(unsigned redirects=0;;++redirects){
    Error error=interrupted(started);if(error!=Error::None)return error;
    browser_url::Parts url{};if(!browser_url::parse(job.requestUrl,url))return Error::Url;
    const time_t now=time(nullptr);
    if(url.secure&&(now<1704067200||now>=4102444800LL))return Error::Clock;
    ip_addr_t address{};error=resolveHost(url.host,address,started);if(error!=Error::None)return error;
    detail::Response response;
    {
      // TLS/socket lifetimes end before redirects, Complete, or renderer access.
      Socket socket;error=socket.connectTo(address,url.port,started);if(error!=Error::None)return error;
      DeleteTls tls;
      if(url.secure){tls.value=new(std::nothrow) Tls;if(!tls.value)return Error::Memory;error=tls.value->start(socket,url.host,started);if(error!=Error::None)return error;}
      Stream stream(socket,tls.value,started);
      char port[8]{};if(url.port!=(url.secure?443:80))snprintf(port,sizeof(port),":%u",url.port);
      char request[1024];const int size=snprintf(request,sizeof(request),
          "GET %s HTTP/1.1\r\nHost: %s%s\r\nUser-Agent: Moss/1.0\r\nAccept: text/html, application/xhtml+xml, application/atom+xml, text/css, image/jpeg, image/png\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",url.target,url.host,port);
      if(size<=0||static_cast<size_t>(size)>=sizeof(request))return Error::Url;
      error=stream.write(request,size);if(error!=Error::None)return error;
      error=detail::headers(stream,response);progress(0,response.status);
      if(error!=Error::None)return stream.error==Error::None?error:stream.error;
      if(!detail::redirect(response.status)){
        if(response.status!=200)return Error::Http;
        if(job.sink){
          if(job.kind==browser_assets::Kind::Css?!response.css:!response.jpeg&&!response.png)return Error::ContentType;
        }else if(!response.html&&!(job.textMode&&response.atom))return Error::ContentType;
        if(response.encoded)return Error::Encoding;
        if(job.sink){
          job.download.jpeg=response.jpeg;job.download.maxAge=response.noStore?0:response.maxAge;
          error=detail::streamBody(stream,response,*job.sink,job.limit,job.result.length);
        }else if(job.textMode){
          if(response.hasLength&&response.length>kMaxTextSource)return Error::TooLarge;
          void* memory=browser_memory_malloc(sizeof(browser_reader::Document));if(!memory)return Error::Memory;
          job.result.reader=new(memory)browser_reader::Document;
          browser_reader::Parser parser(*job.result.reader,response.atom,job.result.url);if(!parser.valid())return Error::Memory;
          struct Sink:detail::Writer {browser_reader::Parser& parser;explicit Sink(browser_reader::Parser& p):parser(p){};void write(const char* p,size_t n)override{parser.feed(p,n);}} sink(parser);
          error=detail::streamBody(stream,response,sink,kMaxTextSource,job.result.length);
          if(error==Error::None)parser.finish();
        }else{
          if(response.hasLength&&response.length>kMaxBody)return Error::TooLarge;
          const size_t capacity=(response.hasLength?response.length:kMaxBody)+1;
          if(heap_caps_get_free_size(MALLOC_CAP_8BIT)+browser_memory_capacity()-browser_memory_live()<capacity+kHeapReserve)return Error::Memory;
          job.result.bytes=static_cast<char*>(browser_memory_malloc(capacity));if(!job.result.bytes)return Error::Memory;
          error=detail::body(stream,response,job.result.bytes,capacity,job.result.length);
        }
        if(error!=Error::None)return stream.error==Error::None?error:stream.error;
        job.result.secure=url.secure;
      }
    }
    error=interrupted(started);if(error!=Error::None)return error;
    if(!detail::redirect(response.status)){
      // Unknown-length bodies can be much smaller than the bounded receive cap.
      char* smaller=job.result.bytes?static_cast<char*>(browser_memory_realloc(job.result.bytes,job.result.length+1)):nullptr;
      if(smaller)job.result.bytes=smaller;
      return Error::None;
    }
    if(redirects>=3||!response.location[0])return Error::Redirect;
    char next[browser_url::kCapacity];browser_url::Parts redirected{};
    if(!browser_url::resolve(job.requestUrl,response.location,next)||!browser_url::parse(next,redirected)||
       (url.secure&&!redirected.secure))return Error::Redirect;
    strcpy(job.requestUrl,next);if(!job.feed)strcpy(job.result.url,next);
  }
}
bool assetCancelled(){return interrupted(assetStarted)!=Error::None;}
bool fetchAsset(const char* url,browser_assets::Kind kind,detail::Writer& sink,size_t limit,browser_assets::Download& result){
  // Nested asset preparation retains scanner/cache state. Keep URL/job storage
  // off the worker stack before entering certificate verification.
  Job* job=new(std::nothrow) Job;if(!job)return false;
  strcpy(job->requestUrl,url);strcpy(job->result.url,url);job->sink=&sink;job->kind=kind;job->limit=limit;
  const Error error=run(*job);result=job->download;delete job;return error==Error::None;
}
void worker(void* argument){
  Job* job=static_cast<Job*>(argument);
  Error error=run(*job);
  if(error==Error::None && !cancelled.load()){
    const Snapshot pageInfo=snapshot();assetStarted=millis();preparingAssets=true;
    job->result.assets=job->textMode?browser_assets::prepareReader(*job->result.reader,job->result.url,fetchAsset,assetCancelled):
      browser_assets::prepare(job->result.bytes,job->result.length,job->result.url,fetchAsset,assetCancelled);
    preparingAssets=false;progress(pageInfo.received,pageInfo.httpStatus);
  }
  if(cancelled.load())error=Error::Cancelled;
  if(error!=Error::None){release(job->result);delete job;job=nullptr;}
  // ESP-IDF reports this watermark in bytes, unlike vanilla FreeRTOS words.
  workerStackLowWater.store(static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)));
  portENTER_CRITICAL(&stateLock);
  current=job;info.error=error;
  info.state=error==Error::None?State::Complete:error==Error::Cancelled?State::Cancelled:State::Failed;
  portEXIT_CRITICAL(&stateLock);
  running.store(false); // All TLS, socket and failed-body allocations are gone.
  vTaskDelete(nullptr);
}
}
void prepare(){
  // Process-wide mbedTLS allocation API; outside browser ownership these fall
  // back to the normal heap. Install once, before any browser TLS allocations.
  static bool installed=false;
  if(!installed)installed=mbedtls_platform_set_calloc_free(browser_memory_calloc,browser_memory_free)==0;
  // The SDK creates these process-lifetime mutexes lazily on the first TLS
  // hardware operation. Keep them out of the canvas hole needed on exit.
#if defined(SOC_SHA_SUPPORTED) || defined(SOC_AES_SUPPORTED)
  esp_crypto_sha_aes_lock_acquire();esp_crypto_sha_aes_lock_release();
#endif
#ifdef SOC_MPI_SUPPORTED
  esp_crypto_mpi_lock_acquire();esp_crypto_mpi_lock_release();
#endif
#ifdef SOC_ECC_SUPPORTED
  esp_crypto_ecc_lock_acquire();esp_crypto_ecc_lock_release();
#endif
}
bool begin(const char* url,bool textMode){
  if(running.load()||current)return false;
  workerStackLowWater.store(0);
  Job* job=new(std::nothrow) Job;
  if(!job){portENTER_CRITICAL(&stateLock);info={State::Failed,Error::Memory,0,0};portEXIT_CRITICAL(&stateLock);return false;}
  if(!browser_url::normalize(url,job->result.url)){delete job;portENTER_CRITICAL(&stateLock);info={State::Failed,Error::Url,0,0};portEXIT_CRITICAL(&stateLock);return false;}
  job->textMode=textMode;
  if(textMode){if(!browser_reader::requestUrl(job->result.url,job->requestUrl,sizeof(job->requestUrl))){delete job;portENTER_CRITICAL(&stateLock);info={State::Failed,Error::Url,0,0};portEXIT_CRITICAL(&stateLock);return false;}job->feed=strcmp(job->requestUrl,job->result.url)!=0;}
  else strcpy(job->requestUrl,job->result.url);
  cancelled.store(false);current=job;
  portENTER_CRITICAL(&stateLock);info={State::Running,Error::None,0,0};portEXIT_CRITICAL(&stateLock);
  running.store(true);
  if(xTaskCreate(worker,"moss-browser",16384,job,1,nullptr)!=pdPASS){
    current=nullptr;delete job;portENTER_CRITICAL(&stateLock);info={State::Failed,Error::Memory,0,0};portEXIT_CRITICAL(&stateLock);running.store(false);return false;
  }
  return true;
}
void cancel(){
  cancelled.store(true);
  if(!running.load()&&current){release(current->result);delete current;current=nullptr;portENTER_CRITICAL(&stateLock);info.state=State::Cancelled;info.error=Error::Cancelled;portEXIT_CRITICAL(&stateLock);}
}
bool busy(){return running.load();}
uint32_t stackLowWaterBytes(){return workerStackLowWater.load();}
Snapshot snapshot(){portENTER_CRITICAL(&stateLock);const Snapshot result=info;portEXIT_CRITICAL(&stateLock);return result;}
bool take(Result& result){
  if(result.bytes||result.reader||running.load()||!current||snapshot().state!=State::Complete)return false;
  result=current->result;delete current;current=nullptr;
  portENTER_CRITICAL(&stateLock);info.state=State::Idle;portEXIT_CRITICAL(&stateLock);return true;
}
void release(Result& result){browser_assets::release(result.assets);browser_memory_free(result.bytes);if(result.reader){result.reader->~Document();browser_memory_free(result.reader);}result=Result{};}
} } // namespace sloth::browser_fetch
