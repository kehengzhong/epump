/*
 * Copyright (c) 2003-2018 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _EPM_SOCK_H_
#define _EPM_SOCK_H_

#ifdef UNIX
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif


typedef struct SockOption_ {
    int  backlog;   //listen backlog, TCP connection buffer for 3-way handshake finishing

    //setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&val, sizeof(int))
    int  reuseaddr;
    //setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void *)&val, sizeof(int))
    int  reuseport;

    //setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const void *)&val, sizeof(int))
    int  keepalive;

    //setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const void *)&val, sizeof(int))
    int  ipv6only;  //only for AF_INET6


    //setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const void *)&val, sizeof(int))
    int  rcvbuf;
    //setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const void *)&val, sizeof(int))
    int  sndbuf;

    //setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const void *)&val, sizeof(struct timeval))
    int  rcvtimeo;
    //setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const void *)&val, sizeof(struct timeval))
    int  sndtimeo;

    //setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (const void *)&val, sizeof(int))
    int  keepidle;
    //setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (const void *)&val, sizeof(int))
    int  keepintvl;
    //setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (const void *)&val, sizeof(int))
    int  keepcnt;

    //setsockopt(fd, SOL_SOCKET, SO_SETFIB, (const void *)&val, sizeof(int))
    int  setfib;

    //setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, (const void *)&val, sizeof(int))
    int  fastopen;
    //setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const void *)&val, sizeof(int))
    int  nodelay;

    //setsockopt(fd, SOL_SOCKET, SO_ACCEPTFILTER, af, sizeof(struct accept_filter_arg))
    struct accept_filter_arg * af;

    //setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, (const void *)&val, sizeof(int))
    int  defer_accept;

    //setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR, (const void *)&val, sizeof(int))
    int  recv_dst_addr;

    //setsockopt(fd, IPPROTO_IP, IP_PKTINFO, (const void *)&val, sizeof(int))
    int  ip_pktinfo;  //only SOCK_DGRAM

    //setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, (const void *)&val, sizeof(int))
    int  ipv6_recv_pktinfo;  //only SOCK_DGRAM

} sockopt_t;


typedef struct EP_SocketAddr_ {

    union {
        struct sockaddr        addr;
        struct sockaddr_in     addr4;
        struct sockaddr_in6    addr6;
#ifdef UNIX
		struct sockaddr_un     addrun;
#endif
    } u;

    int socklen;

    struct EP_SocketAddr_ * next;
} ep_sockaddr_t; 


int epm_sock_nonblock_get (SOCKET fd);
int epm_sock_nonblock_set (SOCKET fd, int nbflag);

int epm_sock_unread_data (SOCKET fd);


void   epm_sock_addr_ntop (struct sockaddr * sa, char * buf);
uint16 epm_sock_addr_port (struct sockaddr * sa);

/* parset string into sockaddr based on the format of IPv4 or IPv6  */
int epm_sock_addr_parse (char * text, int len, ep_sockaddr_t * addr);

/* proto options including: 
     IPPROTO_IP(0), IPPROTO_TCP(6), IPPROTO_UDP(17),
     IPPROTO_RAW(255) IPPROTO_IPV6(41)
   socktype value including: SOCK_STREAM(1), SOCK_DGRAM(2), SOCK_RAW(3) */
int  epm_sock_addr_acquire (ep_sockaddr_t * addr, char * host, int port, int socktype);

void epm_sock_addr_freenext (ep_sockaddr_t * addr);


int epm_sock_option_set (SOCKET fd, sockopt_t * opt);


SOCKET epm_tcp_listen       (char * localip, int port, void * psockopt);

SOCKET epm_tcp_connect_full (char * host, int port, int nonblk, char * lip, int lport, int * succ);
SOCKET epm_tcp_connect      (char * host, int port, char * lip, int lport);
SOCKET epm_tcp_nb_connect   (char * host, int port, char * lip, int lport, int * consucc);

SOCKET epm_udp_listen (char * localip, int port);


/* Create a server endpoint of a connection.
 * Returns fd if all OK, <0 on error. */
int epm_usock_create (const char *name);
 
 
/* Accept a client connection request.
 * Returns fd if all OK, <0 on error.  */
int epm_usock_accept (int listenfd);
 
 
/* Create a client endpoint and connect to a server.
 * Returns fd if all OK, <0 on error.  */
int epm_usock_connect (const char *name);


#ifdef __cplusplus
}
#endif

#endif

