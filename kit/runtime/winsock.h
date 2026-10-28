// winsock.h - WSOCK32 / WS2_32 on the host's sockets (winsock.cpp), used by
// the import shims when the game plays over a network: game.toml [game]
// sockets = true, or RECOMP_SOCKETS=1 (0 turns it off). Otherwise the calls
// keep failing as legacy_dlls.cpp's offline stand-ins do.
#pragma once
#include "x86.h"

bool winsock_real();

void winsock_socket(X86 *c);
void winsock_closesocket(X86 *c);
void winsock_bind(X86 *c);
void winsock_connect(X86 *c);
void winsock_listen(X86 *c);
void winsock_accept(X86 *c);
void winsock_send(X86 *c);
void winsock_sendto(X86 *c);
void winsock_recv(X86 *c);
void winsock_recvfrom(X86 *c);
void winsock_select(X86 *c);
void winsock_fd_isset(X86 *c);
void winsock_ioctlsocket(X86 *c);
void winsock_setsockopt(X86 *c);
void winsock_getsockopt(X86 *c);
void winsock_getsockname(X86 *c);
void winsock_getpeername(X86 *c);
void winsock_shutdown(X86 *c);
void winsock_gethostname(X86 *c);
void winsock_gethostbyname(X86 *c);
void winsock_gethostbyaddr(X86 *c);
void winsock_WSAIoctl(X86 *c);
