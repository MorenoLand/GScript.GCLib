#ifndef GCLIB_PLATFORM_SOCKET_H
#define GCLIB_PLATFORM_SOCKET_H

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static const socket_t invalid_socket_value = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t invalid_socket_value = -1;
#endif

#endif
