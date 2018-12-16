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

#include "epm_util.h"
#include "epm_sock.h"

#ifdef UNIX

#include <stdarg.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/poll.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#endif

#ifdef _WIN32
#include <Iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#endif


int epm_sock_nonblock_get (SOCKET fd)
{
#ifdef UNIX
    int flags = fcntl(fd, F_GETFL);

    if (flags < 0) return -1;

    if (flags & O_NONBLOCK) return 1;
#endif
    return 0;
}

int epm_sock_nonblock_set (SOCKET fd, int nbflag)
{
#ifdef _WIN32
    u_long  arg;

    if (nbflag) arg = 1;
    else arg = 0;

    if (ioctlsocket(fd, FIONBIO, &arg) == SOCKET_ERROR)
        return -1;
#endif
 
#ifdef UNIX
    int flags, newflags;
 
    flags = fcntl(fd, F_GETFL);
    if (flags < 0)  return -1;
 
    if (nbflag) newflags = flags | O_NONBLOCK;
    else newflags = flags & ~O_NONBLOCK;
 
    if (newflags != flags) {
        if (fcntl(fd, F_SETFL, newflags) < 0) 
            return -1;
    }
#endif
 
    return 0;
}

int epm_sock_unread_data (SOCKET fd)
{
#ifdef _WIN32
    long count = 0;
#endif
#ifdef UNIX
    int count = 0;
#endif
    int  ret = 0;
 
    if (fd == INVALID_SOCKET) return 0;
 
#if !defined(FIONREAD)
    return 0;
#endif

#ifdef _WIN32
        ret = ioctlsocket(fd, FIONREAD, (u_long *)&count);
#endif
#ifdef UNIX
        ret = ioctl(fd, FIONREAD, &count);
#endif

    if (ret >= 0) return (int)count;

    return ret;
}


void epm_sock_addr_ntop (struct sockaddr * sa, char * buf)
{
    if (!sa || !buf) return;

    if (sa->sa_family == AF_INET) {
        inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), buf, 48);
    } else if (sa->sa_family == AF_INET6) {
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr), buf, 48);
    }
}

uint16 epm_sock_addr_port (struct sockaddr * sa)
{
    if (!sa) return 0;

    if (sa->sa_family == AF_INET) {
        return ntohs(((struct sockaddr_in *)sa)->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        return ntohs(((struct sockaddr_in6 *)sa)->sin6_port);
    }
    return 0;
}

int epm_sock_inet_addr_parse (char * text, int len, uint32 * inaddr, int * retlen) 
{ 
    int       segs = 0;     /* Segment count. */ 
    int       chcnt = 0;    /* Character count within segment. */ 
    int       accum = 0;    /* Accumulator for segment. */ 
    uint32    sect[4];
    uint32    addr = INADDR_ANY;
    int       i;
 
    if (retlen) *retlen = 0;

    if (!text) return -1; 
    if (len < 0) len = strlen(text);
    if (len <= 0) return -2;
 
    for (i = 0; i < len; i++) {
        if (text[i] == '.') { 
            /* Must have some digits in segment. */ 
            if (chcnt == 0) return -100; 
 
            /* Limit number of segments. */ 
            if (segs >= 3) return -101; 
 
            sect[segs++] = accum;

            /* Reset segment values and restart loop. */ 
            chcnt = accum = 0; 
            continue; 
        } 

        /* Check numeric. */ 
        if ((text[i] < '0') || (text[i] > '9')) {
            if (segs == 3 && chcnt > 0) {
                break;
            } 
            return -102; 
        }
 
        /* Accumulate and check segment. */ 
        if ((accum = accum * 10 + text[i] - '0') > 255) 
            return -103; 
 
        /* Advance other segment specific stuff and continue loop. */ 
        chcnt++; 
    } 
 
    /* Check enough segments and enough characters in last segment. */ 
    if (segs != 3 || chcnt == 0) return -104; 

    sect[segs] = accum; segs++;
    addr = (sect[0] << 24) + (sect[1] << 16) + (sect[2] << 8) + sect[3];

    if (inaddr) *inaddr = htonl(addr);
    if (retlen) *retlen = i;

    return i; 
} 

int epm_sock_inet6_addr_parse (char * p, int len, char * addr, int * retlen)
{
    char      c, *zero, *digit, *s, *d, *p0;
    int       len4;
    uint32    n, nibbles, word;
 
    if (!p) return -1;
    if (len < 0) len = strlen(p);
    if (len <= 0) return -2;
 
    if (retlen) *retlen = 0;
    p0 = p;

    zero = NULL;
    digit = NULL;
    len4 = 0;
    nibbles = 0;
    word = 0;
    n = 8;
 
    if (p[0] == ':') {
        p++;
        len--;
    }
 
    for (/* void */; len; len--) {
        c = *p++;
 
        if (c == ':') {
            if (nibbles) {
                digit = p;
                len4 = len;
                *addr++ = (char) (word >> 8);
                *addr++ = (char) (word & 0xff);
 
                if (--n) {
                    nibbles = 0;
                    word = 0;
                    continue;
                }
            } else {
                if (zero == NULL) {
                    digit = p;
                    len4 = len;
                    zero = addr;
                    continue;
                }
            }
 
            return -100;
        }
 
        if (c == '.' && nibbles) {
            if (n < 2 || digit == NULL) {
                return -101;
            }
 
            if (epm_sock_inet_addr_parse(digit, len4 - 1, &word, NULL) < 0)
               return -102;

            word = ntohl(word);
            *addr++ = (char) ((word >> 24) & 0xff);
            *addr++ = (char) ((word >> 16) & 0xff);
            n--;
            break;
        }
 
        if (++nibbles > 4) {
            return -103;
        }
 
        if (c >= '0' && c <= '9') {
            word = word * 16 + (c - '0');
            continue;
        }
 
        c |= 0x20;
 
        if (c >= 'a' && c <= 'f') {
            word = word * 16 + (c - 'a') + 10;
            continue;
        }
 
        return -104;
    }
 
    if (nibbles == 0 && zero == NULL) {
        return -105;
    }
 
    *addr++ = (char) (word >> 8);
    *addr++ = (char) (word & 0xff);
 
    if (--n) {
        if (zero) {
            n *= 2;
            s = addr - 1;
            d = s + n;
            while (s >= zero) {
                *d-- = *s--;
            }
            memset(zero, 0, n);
            if (retlen) *retlen = p - p0;
            return p - p0;
        }
 
    } else {
        if (zero == NULL) {
            if (retlen) *retlen = p - p0;
            return p - p0;
        }
    }
 
    return -200;
}

int epm_sock_addr_parse (char * text, int len, ep_sockaddr_t * addr)
{
    int    ret = 0;

    if (!text) return -1;
    if (len < 0) len = strlen(text);
    if (len <= 0) return -2;

    if (!addr) return -3;

    /* try first to parse the string as the inet4 IPv4 */
    ret = epm_sock_inet_addr_parse(text, len, (uint32 *)&addr->u.addr4.sin_addr.s_addr, NULL);

    if (ret > 0) {
        addr->u.addr4.sin_family = AF_INET;
        addr->socklen = sizeof(struct sockaddr_in);

    } else if (epm_sock_inet6_addr_parse(text, len, (char *)addr->u.addr6.sin6_addr.s6_addr, NULL) > 0) {
        addr->u.addr6.sin6_family = AF_INET6;
        addr->socklen = sizeof(struct sockaddr_in6);

    } else {
        return -100;
    }

    return 0;
}


int checkcopy (ep_sockaddr_t * iter, struct addrinfo * rp)
{
    if (rp->ai_family == AF_INET) {
        iter->socklen = rp->ai_addrlen;
        memcpy(&iter->u.addr4, rp->ai_addr, iter->socklen);
        return 1;
    } else if (rp->ai_family == AF_INET6) {
        iter->socklen = rp->ai_addrlen;
        memcpy(&iter->u.addr6, rp->ai_addr, iter->socklen);
        return 1;
    }
    return 0;
}

/* proto options including: 
     IPPROTO_IP(0), IPPROTO_TCP(6), IPPROTO_UDP(17),
     IPPROTO_RAW(255) IPPROTO_IPV6(41)
   socktype value including: SOCK_STREAM(1), SOCK_DGRAM(2), SOCK_RAW(3) */

int epm_sock_addr_acquire (ep_sockaddr_t * addr, char * host, int port, int socktype)
{
    struct addrinfo    hints;
    struct addrinfo  * result;
    struct addrinfo  * rp;
    char               buf[16];
    int                num = 0;
    ep_sockaddr_t    * iter = NULL;
    ep_sockaddr_t    * newa = NULL;
    int                dup = 0;

    if (!addr) return -1;

    sprintf(buf, "%d", port);

    //if (socktype != SOCK_STREAM && socktype != SOCK_DGRAM && socktype != SOCK_RAW)
    //    socktype = SOCK_STREAM;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_protocol = 0;
    hints.ai_flags = 0;

    if (getaddrinfo(host, buf, &hints, &result) != 0) {
        if (epm_sock_addr_parse(host, -1, addr) < 0) return -100;

        switch (addr->u.addr.sa_family) {
        case AF_INET:
            addr->u.addr4.sin_port = htons((uint16)port);
            return 1;
        case AF_INET6:
            addr->u.addr6.sin6_port = htons((uint16)port);
            return 1;
        default:
            return -102;
        }
    }

    for (num = 0, rp = result; rp != NULL; rp = rp->ai_next) {
        if (num == 0) {
            num += checkcopy(addr, rp);
            continue;
        }

        for (dup = 0, iter = addr; iter != NULL; iter = iter->next) {
            if (memcmp(&iter->u.addr, rp->ai_addr, iter->socklen) == 0) {
                dup = 1; break;
            }
        }
        if (dup == 0) {
            newa = epm_zalloc(sizeof(*newa));
            if (checkcopy(newa, rp) > 0) {
                for (iter = addr; iter->next != NULL; iter = iter->next);
                iter->next = newa;
                num++;
            } else epm_free(newa);
        }
    }

    freeaddrinfo(result);

    return num;
}

void epm_sock_addr_freenext (ep_sockaddr_t * addr)
{
    ep_sockaddr_t  * iter, * cur;

    if (!addr) return;

    for (iter = addr->next; iter != NULL; ) {
        cur = iter; iter = iter->next;
        epm_free(cur);
    }
}


int epm_sock_option_set (SOCKET fd, sockopt_t * opt)
{
    struct timeval tv;

    if (fd == INVALID_SOCKET)
        return -1;

    if (!opt) return -2;

    if (opt->reuseaddr && 
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&opt->reuseaddr, sizeof(int)) < 0)
    {
    }

#ifdef SO_REUSEPORT
    if (opt->reuseport &&
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT,
                   (const void *)&opt->reuseport, sizeof(int)) < 0)
    {
    }
#endif

    if (opt->keepalive &&
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                   (const void *)&opt->keepalive, sizeof(int)) < 0)
    {
    }

    if (opt->ipv6only &&
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                   (const void *)&opt->ipv6only, sizeof(int)) < 0)
    {
    }

    if (opt->rcvbuf > 0 &&
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                   (const void *)&opt->rcvbuf, sizeof(int)) < 0)
    {
    }

    if (opt->sndbuf > 0 &&
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                   (const void *)&opt->sndbuf, sizeof(int)) < 0)
    {
    }

    if (opt->rcvtimeo > 0) {
        tv.tv_sec = opt->rcvtimeo / 1000;
        tv.tv_usec = opt->rcvtimeo % 1000;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                   (const void *)&tv, sizeof(struct timeval)) < 0)
        {
        }
    }

    if (opt->sndtimeo > 0) {
        tv.tv_sec = opt->sndtimeo / 1000;
        tv.tv_usec = opt->sndtimeo % 1000;
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                   (const void *)&tv, sizeof(struct timeval)) < 0)
        {
        }
    }

#ifdef TCP_KEEPIDLE
    if (opt->keepidle > 0 &&
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,
                   (const void *)&opt->keepidle, sizeof(int)) < 0)
    {
    }
#endif

#ifdef TCP_KEEPINTVL
    if (opt->keepintvl > 0 &&
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL,
                   (const void *)&opt->keepintvl, sizeof(int)) < 0)
    {
    }
#endif

#ifdef TCP_KEEPCNT
    if (opt->keepcnt > 0 &&
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,
                   (const void *)&opt->keepcnt, sizeof(int)) < 0)
    {
    }
#endif

#ifdef SO_SETFIB
    if (opt->setfib > 0 &&
        setsockopt(fd, SOL_SOCKET, SO_SETFIB,
                   (const void *)&opt->setfib, sizeof(int)) < 0)
    {
    }
#endif

#ifdef TCP_FASTOPEN
    if (opt->fastopen > 0 &&
        setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN,
                   (const void *)&opt->fastopen, sizeof(int)) < 0)
    {
    }
#endif

    if (opt->nodelay > 0 &&
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                   (const void *)&opt->nodelay, sizeof(int)) < 0)
    {
    }

#ifdef SO_ACCEPTFILTER
    if (opt->af != NULL &&
        setsockopt(fd, SOL_SOCKET, SO_ACCEPTFILTER,
                   (const void *)opt->af, sizeof(struct accept_filter_arg)) < 0)
    {
    }
#endif

#ifdef TCP_DEFER_ACCEPT
    if (opt->defer_accept > 0 &&
        setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT,
                   (const void *)&opt->defer_accept, sizeof(int)) < 0)
    {
    }
#endif

#ifdef IP_RECVDSTADDR
    if (opt->recv_dst_addr > 0 &&
        setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR,
                   (const void *)&opt->recv_dst_addr, sizeof(int)) < 0)
    {
    }
#endif

    if (opt->ip_pktinfo > 0 &&
        setsockopt(fd, IPPROTO_IP, IP_PKTINFO,
                   (const void *)&opt->ip_pktinfo, sizeof(int)) < 0)
    {
    }

#ifdef IPV6_RECVPKTINFO
    if (opt->ipv6_recv_pktinfo > 0 &&
        setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO,
                   (const void *)&opt->ipv6_recv_pktinfo, sizeof(int)) < 0)
    {
    }
#endif

    return 0;
}

void addrinfo_print (struct addrinfo * rp)
{
    char buf[512];
    char ipstr[128];

    if (!rp) return;

    buf[0] = '\0';
    if (rp->ai_family == AF_INET) sprintf(buf, "AF_INET");
    else if (rp->ai_family == AF_INET6) sprintf(buf, "AF_INET6");
    else if (rp->ai_family == AF_UNIX) sprintf(buf, "AF_UNIX");
    else sprintf(buf, "UnknownAF");

    if (rp->ai_socktype == SOCK_STREAM) sprintf(buf+strlen(buf), " SOCK_STREAM");
    else if (rp->ai_socktype == SOCK_DGRAM) sprintf(buf+strlen(buf), " SOCK_DGRAM");
    else if (rp->ai_socktype == SOCK_RAW) sprintf(buf+strlen(buf), " SOCK_RAW");
#ifdef SOCK_PACKET
	else if (rp->ai_socktype == SOCK_PACKET) sprintf(buf+strlen(buf), " SOCK_PACKET");
#endif
    else sprintf(buf+strlen(buf), " UnknownSockType");

    if (rp->ai_protocol == IPPROTO_IP) sprintf(buf+strlen(buf), " IPPROTO_IP");
    else if (rp->ai_protocol == IPPROTO_TCP) sprintf(buf+strlen(buf), " IPPROTO_TCP");
    else if (rp->ai_protocol == IPPROTO_UDP) sprintf(buf+strlen(buf), " IPPROTO_UDP");
    else if (rp->ai_protocol == IPPROTO_IPV6) sprintf(buf+strlen(buf), " IPPROTO_IPV6");
    else if (rp->ai_protocol == IPPROTO_RAW) sprintf(buf+strlen(buf), " IPPROTO_RAW");
    else if (rp->ai_protocol == IPPROTO_ICMP) sprintf(buf+strlen(buf), " IPPROTO_ICMP");
    else sprintf(buf+strlen(buf), " UnknownProto");

    if (rp->ai_family == AF_INET) {
        inet_ntop(AF_INET, &(((struct sockaddr_in *)rp->ai_addr)->sin_addr), ipstr, 128);
        sprintf(buf+strlen(buf), " %s:%d", ipstr, ntohs(((struct sockaddr_in *)rp->ai_addr)->sin_port));
    } else if (rp->ai_family == AF_INET6) {
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)rp->ai_addr)->sin6_addr), ipstr, 128);
        sprintf(buf+strlen(buf), " %s:%d", ipstr, ntohs(((struct sockaddr_in6 *)rp->ai_addr)->sin6_port));
    }

    sprintf(buf+strlen(buf), " SockLen: %d  AI_Flags: %d", rp->ai_addrlen, rp->ai_flags);
    printf("\n%s\n\n", buf);
}

SOCKET epm_tcp_listen (char * localip, int port, void * psockopt)
{
    struct addrinfo    hints;
    struct addrinfo  * result;
    struct addrinfo  * rp;
    SOCKET             aifd = INVALID_SOCKET;
    char               buf[128];

    SOCKET             listenfd = INVALID_SOCKET;
    sockopt_t        * sockopt = NULL;
    int                one = 0;
    int                backlog = 511;

    sockopt = (sockopt_t *)psockopt;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;       /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;   /* Datagram socket */
    hints.ai_flags = AI_PASSIVE;       /* For wildcard IP address */
    hints.ai_protocol = IPPROTO_TCP;   /* TCP protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    sprintf(buf, "%d", port);
 
    aifd = getaddrinfo(localip, buf, &hints, &result);
    if (aifd != 0) {
#ifdef _DEBUG
        printf("getaddrinfo: %s:%s return %d\n", localip, buf, aifd);
#endif
        return -100;
    }

    /* getaddrinfo() returns a list of address structures.
       Try each address until we successfully bind(2).
       If socket(2) (or bind(2)) fails, we (close the socket
       and) try the next address. */

    for (rp = result; rp != NULL; rp = rp->ai_next) {
#ifdef _DEBUG
        addrinfo_print(rp);
#endif

        listenfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listenfd == INVALID_SOCKET) {
#ifdef _DEBUG
            perror("epm_tcp_listen socket()");
#endif
            continue;
        }

        if (sockopt) {
            backlog = sockopt->backlog;
            epm_sock_option_set(listenfd, sockopt);
        } else { //set the default options
           one = 1;
           setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(int));
#ifdef SO_REUSEPORT
           setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (void *)&one, sizeof(int));
#endif
           setsockopt(listenfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&one, sizeof(int));
        }

        if (bind(listenfd, rp->ai_addr, rp->ai_addrlen) != 0) {
#ifdef _DEBUG
            perror("epm_tcp_listen bind()");
#endif
            closesocket(listenfd);
            continue; 
        }

        if (backlog <= 0) backlog = 511;
        if (listen(listenfd, backlog) == SOCKET_ERROR) {
#ifdef _DEBUG
            perror("epm_tcp_listen listen()");
#endif
            closesocket(listenfd);
            continue; 
        }
        break;
    }
    freeaddrinfo(result);

    if (rp == NULL) {
#ifdef _DEBUG
            printf("epm_tcp_listen no addrinfo available\n");
#endif
        /* there is no address/port that bound or listened successfully! */
        return INVALID_SOCKET;
    }

    return listenfd;
}

SOCKET epm_tcp_connect_full (char * host, int port, int nonblk, char * lip, int lport, int * succ)
{
    struct addrinfo    hints;
    struct addrinfo  * result;
    struct addrinfo  * rp;
    SOCKET             aifd = INVALID_SOCKET;
    char               buf[128];
 
    SOCKET             confd = INVALID_SOCKET;
    ep_sockaddr_t      addr;
    int                one = 0;
 
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;       /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;   /* Stream socket */
    hints.ai_flags = 0;               /* For wildcard IP address */
    hints.ai_protocol = IPPROTO_TCP;   /* TCP protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
 
    sprintf(buf, "%d", port);
 
    aifd = getaddrinfo(host, buf, &hints, &result);
    if (aifd != 0) {
#ifdef _DEBUG
        printf("epm_tcp_connect: getaddrinfo: %s:%s return %d\n", host, buf, aifd);
#endif
        return -100;
    }
 
    for (rp = result; rp != NULL; rp = rp->ai_next) {
#ifdef _DEBUG
        addrinfo_print(rp);
#endif
        confd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (confd == INVALID_SOCKET)
            continue;

        one = 1;
        setsockopt(confd, SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(int));
#ifdef SO_REUSEPORT
        setsockopt(confd, SOL_SOCKET, SO_REUSEPORT, (void *)&one, sizeof(int));
#endif
		setsockopt(confd, SOL_SOCKET, SO_KEEPALIVE, (void *)&one, sizeof(int));
 
        if (nonblk) epm_sock_nonblock_set(confd, 1);

        if (lip || lport > 0) {
            memset(&addr, 0, sizeof(addr));
            one = epm_sock_addr_acquire(&addr, lip, lport, SOCK_STREAM);
            if (one <= 0 || bind(confd, (struct sockaddr *)&addr.u.addr, addr.socklen) != 0) {
                if (one > 1) epm_sock_addr_freenext(&addr);
                closesocket(confd);
                continue;
            }
        }

        if (connect(confd, rp->ai_addr, rp->ai_addrlen) == 0) {
            if (succ) *succ = 1;
        } else {
            if (succ) *succ = 0;
    #ifdef UNIX
            if (errno != 0  && errno != EINPROGRESS && errno != EALREADY && errno != EWOULDBLOCK) {
                closesocket(confd);
                continue;
            }
    #endif
    #ifdef _WIN32
            if (WSAGetLastError() != WSAEWOULDBLOCK) { 
                closesocket(confd);
                continue;
            }
    #endif
        }

        break;
    }
    freeaddrinfo(result);
 
    if (rp == NULL) {
        /* there is no address/port that bound or listened successfully! */
        return INVALID_SOCKET;
    }
 
    return confd;
}

SOCKET epm_tcp_connect (char * host, int port, char * lip, int lport)
{
    return epm_tcp_connect_full(host, port, 0, lip, lport, NULL);
}

SOCKET epm_tcp_nb_connect (char * host, int port, char * lip, int lport, int * consucc)
{
    return epm_tcp_connect_full(host, port, 1, lip, lport, consucc);
}


SOCKET epm_udp_listen (char * localip, int port)
{
    struct addrinfo    hints;
    struct addrinfo  * result;
    struct addrinfo  * rp;
    SOCKET             aifd = INVALID_SOCKET;
    char              buf[128];
    SOCKET             listenfd = INVALID_SOCKET;
 
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;       /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM;   /* Datagram socket */
    hints.ai_flags = AI_PASSIVE;       /* For wildcard IP address */
    hints.ai_protocol = IPPROTO_UDP;   /* TCP protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
 
    sprintf(buf, "%d", port);
 
    aifd = getaddrinfo(localip, buf, &hints, &result);
    if (aifd != 0)  return -100;
 
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        listenfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listenfd == INVALID_SOCKET)
            continue;
 
         if (bind(listenfd, rp->ai_addr, rp->ai_addrlen) != 0) {
             closesocket(listenfd);
             continue;
         }
         break;
    }
    freeaddrinfo(result);

    if (rp == NULL) {
        /* there is no address/port that bound or listened successfully! */
        return INVALID_SOCKET;
    }

    return listenfd;
}



#ifdef UNIX
 
#define QLEN 100
#define TMP_PATH    "/tmp/cdn.XXXXXX"
 
 
/* Create a server endpoint of a connection.
 * Returns fd if all OK, <0 on error. */
int epm_usock_create (const char *name)
{
    int                 fd, len, err, rval;
    struct sockaddr_un  un;
 
    /* create a UNIX domain stream socket */
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        return -1;
 
    len = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&len, sizeof(len)) < 0) {
        rval = -4;
        goto errout;
    }
    unlink(name);   /* in case it already exists */
 
    /* fill in socket address structure */
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strcpy(un.sun_path, name);
    len = sizeof(un.sun_family) + strlen(un.sun_path);
 
    /* bind the name to the descriptor */
    if (bind(fd, (struct sockaddr *)&un, len) < 0) {
        rval = -2;
        goto errout;
    }
    if (listen(fd, QLEN) < 0) { /* tell kernel we're a server */
        rval = -3;
        goto errout;
    }
 
    chmod(name, 0777);
    return fd;
 
errout:
    err = errno;
    close(fd);
    errno = err;
    return rval;
}
 
/* Accept a client connection request.
 * Returns fd if all OK, <0 on error.  */
int epm_usock_accept (int listenfd)
{
    int                 clifd, len, err, rval;
    struct sockaddr_un  un;
    struct stat         statbuf;
 
    len = sizeof(un);
    if ((clifd = accept(listenfd, (struct sockaddr *)&un, (socklen_t *)&len)) < 0)
        return -1;     /* often errno=EINTR, if signal caught */
 
    len -= sizeof(un.sun_family);
    if (len >= 0) un.sun_path[len] = 0;
 
    if (stat(un.sun_path, &statbuf) < 0) {
        rval = -2;
        goto errout;
    }
 
    if (S_ISSOCK(statbuf.st_mode) == 0) {
        rval = -3;      /* not a socket */
        goto errout;
    }
 
    unlink(un.sun_path);        /* we're done with pathname now */
 
    return(clifd);
 
errout:
    err = errno;
    close(clifd);
    errno = err;
    return(rval);
}
 
/* Create a client endpoint and connect to a server.
 * Returns fd if all OK, <0 on error.  */
int epm_usock_connect (const char *name)
{
    int                fd, len, err, rval;
    struct sockaddr_un un;
 
    /* create a UNIX domain stream socket */
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        return(-1);
 
#if 0
    /* fill socket address structure with our address */
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    sprintf(un.sun_path, "%s", TMP_PATH);
    mkstemp(un.sun_path);
    len = sizeof(un.sun_family) + strlen(un.sun_path);
 
    unlink(un.sun_path);        /* in case it already exists */
    if (bind(fd, (struct sockaddr *)&un, len) < 0) {
        rval = -2;
        goto errout;
    }
#endif
 
    /* fill socket address structure with server's address */
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strcpy(un.sun_path, name);
    len = sizeof(un.sun_family) + strlen(un.sun_path);
    if (connect(fd, (struct sockaddr *)&un, len) < 0) {
        rval = -4;
        goto errout;
    }
    return(fd);
 
errout:
    err = errno;
    close(fd);
    errno = err;
    return(rval);
}
 
#endif

