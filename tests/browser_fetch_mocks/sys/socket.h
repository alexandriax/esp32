#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>
using socklen_t=unsigned;
struct sockaddr {uint16_t family;char data[14];};
constexpr int AF_INET=2,SOCK_STREAM=1,IPPROTO_TCP=6,SHUT_RDWR=2,SOL_SOCKET=1,SO_ERROR=4;
int browser_mock_socket(int,int,int);
int browser_mock_connect(int,const sockaddr*,socklen_t);
int browser_mock_close(int);
int browser_mock_shutdown(int,int);
int browser_mock_fcntl(int,int,...);
int browser_mock_select(int,fd_set*,fd_set*,fd_set*,timeval*);
int browser_mock_getsockopt(int,int,int,void*,socklen_t*);
int browser_mock_send(int,const void*,size_t,int);
int browser_mock_recv(int,void*,size_t,int);
#define socket browser_mock_socket
#define connect browser_mock_connect
#define close browser_mock_close
#define shutdown browser_mock_shutdown
#define fcntl browser_mock_fcntl
#define select browser_mock_select
#define getsockopt browser_mock_getsockopt
#define send browser_mock_send
#define recv browser_mock_recv
