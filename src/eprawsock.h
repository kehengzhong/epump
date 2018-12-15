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

#ifndef _EPRAWSOCK_H_
#define _EPRAWSOCK_H_


#ifdef _WIN32

#if _MSC_VER > 1000
#pragma once
#endif

#pragma pack(1)

struct tcp_keepalive {
    u_long onoff;
    u_long keepalivetime;
    u_long keepaliveinterval;
};
#pragma pack()


#define SIO_RCVALL            _WSAIOW(IOC_VENDOR, 1)
#define SIO_RCVALL_MCAST      _WSAIOW(IOC_VENDOR, 2)
#define SIO_RCVALL_IGMPMCAST  _WSAIOW(IOC_VENDOR, 3)
#define SIO_KEEPALIVE_VALS    _WSAIOW(IOC_VENDOR, 4)
#define SIO_ABSORB_RTRALERT   _WSAIOW(IOC_VENDOR, 5)
#define SIO_UCAST_IF          _WSAIOW(IOC_VENDOR, 6)
#define SIO_LIMIT_BROADCASTS  _WSAIOW(IOC_VENDOR, 7)
#define SIO_INDEX_BIND        _WSAIOW(IOC_VENDOR, 8)
#define SIO_INDEX_MCASTIF     _WSAIOW(IOC_VENDOR, 9)
#define SIO_INDEX_ADD_MCAST   _WSAIOW(IOC_VENDOR, 10)
#define SIO_INDEX_DEL_MCAST   _WSAIOW(IOC_VENDOR, 11)

#define RCVALL_OFF              0
#define RCVALL_ON               1
#define RCVALL_SOCKETLEVELONLY  2

#endif

#define ICMP_ECHO_REPLY      0
#define ICMP_DEST_UNREACH    3
#define ICMP_SRC_QUENCH      4
#define ICMP_REDIRECT        5
#define ICMP_ECHO            8
#define ICMP_TIME_EXCEEDED   11
#define ICMP_PARA_PROBLEM    12
#define ICMP_TIMESTAMP       13
#define ICMP_TIMESTAMP_REPLY 14
#define ICMP_INFO_REQUEST    15
#define ICMP_INFO_REPLY      16


#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(1)

typedef struct ip_hdr
{
    uint8  ip_verlen; 
    uint8  ip_tos; 
    uint16 ip_totallength; 
    uint16 ip_id; 
    uint16 ip_offset; 
    uint8  ip_ttl; 
    uint8  ip_protocol; 
    uint16 ip_checksum; 
    uint32 ip_srcaddr; 
    uint32 ip_destaddr; 
} IP_HDR;

typedef struct udp_hdr
{
    uint16 src_portno; 
    uint16 dst_portno; 
    uint16 udp_length; 
    uint16 udp_checksum; 
} UDP_HDR;


/* define the ICMP header, Destination Unreachable Message */
typedef struct imcp_hdr {
    uint8   icmp_type;      /* type */
    uint8   icmp_code;      /* Code */
    uint16  icmp_checksum;  /* Checksum */
    uint16  icmp_id;        /* identification */
    uint16  icmp_seq;       /* sequence no */
    uint32  icmp_timestamp; /* Unused */
} ICMP_HDR, *PICMP_HDR;

#pragma pack()


void * eprawsock_client (void * vpcore, void * para, int protocol, int * retval);

int eprawsock_notify (void * vpdev, int recvall, IOHandler * cb, void * cbpara);

int eprawsock_send_udp (void * vpdev, char * srcip, uint16 srcport, char * dstip,
                        uint16 dstport, char * pbyte, int bytelen);

int eprawsock_send_icmp (void * vpdev, char * srcip, char * dstip, uint8 icmptype,
                         uint16 icmpid, uint16 icmpseq, char * pbyte, int bytelen);

#ifdef __cplusplus
}
#endif

#endif


