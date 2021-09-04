/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */
 
#ifndef _EPUMP_DNS_H_
#define _EPUMP_DNS_H_
 
#include "frame.h"

#ifdef  __cplusplus
extern "C" {
#endif
 
/* rfc1035, domain name implementation and specification
   https://tools.ietf.org/html/rfc1035
 */

/*
  The top level format of message is divided into 5 sections (some of which
  are empty in certain cases) shown below:
    +---------------------+
    |        Header       |
    +---------------------+
    |       Question      | the question for the name server
    +---------------------+
    |        Answer       | RRs answering the question
    +---------------------+
    |      Authority      | RRs pointing toward an authority
    +---------------------+
    |      Additional     | RRs holding additional information
    +---------------------+

  The header format contains following fields:
                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      ID                       |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    QDCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ANCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    NSCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ARCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

  Question section format as following:
                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    /                     QNAME                     /
    /                                               /
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     QTYPE                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     QCLASS                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

  The answer, Authority, and Additional sections all share the same
  format: a variable number of resource records, where the number of
  records is specified in the corresponding count field in the header.
  Each resource record has the following format:
                                    1  1  1  1  1  1
      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    /                                               /
    /                      NAME                     /
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      TYPE                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     CLASS                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      TTL                      |
    |                                               |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                   RDLENGTH                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--|
    /                     RDATA                     /
    /                                               /
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

*/

/* Type of RR (Resource Record) definition */
#define RR_TYPE_A       1   /* IPv4 Host Address */
#define RR_TYPE_NS      2   /* Authoritative Name Server */
#define RR_TYPE_CNAME   5   /* Canonical Name for an Alias */
#define RR_TYPE_SOA     6   /* Start of a Zone of Authority */
#define RR_TYPE_WKS     11  /* Well-Known Service Description */
#define RR_TYPE_PTR     12  /* Domain Name Pointer */
#define RR_TYPE_HINFO   13  /* Host Information */
#define RR_TYPE_MINFO   14  /* Mailbox or Mail list information */
#define RR_TYPE_MX      15  /* Mail Exchange */
#define RR_TYPE_TXT     16  /* Text String */
#define RR_TYPE_AAAA    28  /* IPv6 Host Address */

#define RR_QTYPE_AXFR   252 /* a Transfer of an Entire Zone */
#define RR_QTYPE_MAILB  253 /* Mailbox-related Records */
#define RR_QTYPE_ALL    255 /* All Records */


/* Class in Resource Record */
#define RR_CLASS_IN     1   /* Internet */
#define RR_CLASS_CS     2   /* CSNET class */
#define RR_CLASS_CH     3   /* CHAOS class */
#define RR_CLASS_HS     4   /* Hesiod */
#define RR_CLASS_ANY    255 /* Any class */

/* Response Code definition */
#define DNS_ERR_NO_ERROR       0
#define DNS_ERR_FORMAT_ERROR   1
#define DNS_ERR_SERVER_FAILURE 2
#define DNS_ERR_NAME_ERROR     3
#define DNS_ERR_UNSUPPORTED    4
#define DNS_ERR_REFUSED        5
#define DNS_ERR_IPV4           200
#define DNS_ERR_IPV6           201
#define DNS_ERR_NO_RESPONSE    404
#define DNS_ERR_SEND_FAIL      405
#define DNS_ERR_RESOURCE_FAIL  500

typedef int DnsCB (void * cbobj, char * name, int namelen, void * cache, int status);

/* converts a DNS-based hostname into dot-based format,
   3www5apple3com0 into www.apple.com */
int hostn_to_dot_format (void * name, int len, void * pdst, int dstlen);

/* converts the dot-based hostname into the DNS format.
   www.apple.com into 3www5apple3com0 */
int hostn_to_dns_format (void * name, int len, void * pdst, int dstlen);

/******************************************************
 * DNS NSrv / DNS Host - Name Server handling DNS request
 ******************************************************/

typedef struct dns_host_s {
    char             * host;
    char               ip[41];
    int                port;
    ep_sockaddr_t      addr;
} DnsHost;
 
void * dns_host_alloc ();
void   dns_host_free  (void * vhost);
void * dns_host_new   (char * nshost, char * nsip, int port);
 
typedef struct dns_nsrv {
 
    CRITICAL_SECTION   hostCS;
    arr_t            * host_list;
 
} DnsNSrv;
 
void * dns_nsrv_alloc ();
void   dns_nsrv_free  (void * vnsrv);
 
int    dns_nsrv_num   (void * vnsrv);
int    dns_nsrv_add   (void * vnsrv, void * vhost);
 
int    dns_nsrv_append (void * vmgmt, char * nsip, int port);
int    dns_nsrv_load   (void * vmgmt, char * nsip, char * resolv_file);

/******************************************************
 * DNS RR - Resource Record parsed and received from NS
 ******************************************************/

typedef struct dns_rr_s {
    int       namelen;
    char    * name;
 
    uint16    type;
    uint16    class;
    uint32    ttl;
 
    uint16    rdlen;
    uint8   * rdata;
 
    char      ip[41];
    uint8     outofdate;
 
    time_t    rcvtick;
} DnsRR;
 
void * dns_rr_alloc ();
void   dns_rr_free  (void * vrr);
void * dns_rr_dup   (void * vrr);
 
void   dns_rr_print (void * vrr);
 
int    dns_rr_name_parse (void * vrr, uint8 * p, int len, uint8 * bufbgn, uint8 ** nextptr, uint8 ** pname);
int    dns_rr_parse      (void * vrr, uint8 * p, int len, uint8 * bufbgn, uint8 ** nextrr);

/***************************************************
 * DNS Cache - storing the RR list of Domain/IP pair
 ***************************************************/

typedef struct dns_cache_s {
    char               name[256];
 
    CRITICAL_SECTION   rrlistCS;
    arr_t            * rr_list;
 
    time_t             stamp;
    int                anum;
 
    void             * dnsmgmt;
} DnsCache;
 
int    dns_cache_cmp_name (void * a, void * pat);
 
void * dns_cache_alloc ();
void   dns_cache_free  (void * vcache);
 
int    dns_cache_add    (void * vcache, void * vrr);
int    dns_cache_zap    (void * vcache);
int    dns_cache_verify (void * vcache);
 
int    dns_cache_copy    (void * vsrc, void * vdst);
int    dns_cache_copy_ip (void * vcache, char * ip, int len);
int    dns_cache_getip   (void * vcache, int ind, char * ip, int len);
int    dns_cache_getiplist (void * vcache, char ** iplist, int listnum);
int    dns_cache_sockaddr(void * vcache, int index, int port, ep_sockaddr_t * addr);
 
int    dns_cache_num       (void * vcache);
int    dns_cache_a_num     (void * vcache);
int    dns_cache_find_A_rr (void * vcache, char * name);
 
int    dns_cache_check (void * vcache);
 
int    dns_cache_mgmt_add (void * vmgmt, void * vcache);
void * dns_cache_mgmt_get (void * vmgmt, char * name, int namelen);
void * dns_cache_mgmt_del (void * vmgmt, char * name, int namelen);
 
void * dns_cache_open  (void * vmgmt, char * name, int len);
int    dns_cache_close (void * vcache);
 
int    dns_cache_lifecheck (void * vmgmt);
 

/*******************************************************
 * DNS Msg - packing request/response for resolving name
 *******************************************************/

#pragma pack(1)
typedef struct dns_header_s {
    uint16    ID;           /* Identifier to match up replies to outstanding queries */
 
    uint8     RD     : 1;   /* Recursion Desired, RD-set directs name server to pursue query recursively */
    uint8     TC     : 1;   /* TrunCation, specify message was truncated */
    uint8     AA     : 1;   /* Authoritative Answer, specify responding name server is an authority */
    uint8     Opcode : 4;   /* query type: 0-standard 1-inverse query 2-server status request */
    uint8     QR     : 1;   /* message type: 0-Query  1-Response */
 
    uint8     RCODE  : 4;   /* Response code */
    uint8     Z      : 3;   /* Reserved for future, must be zero in query/response */
    uint8     RA     : 1;   /* Recursion Available, denotes if recursive query is supported in name server */
 
    uint16    qdcount;      /* number of entries in the question */
    uint16    ancount;      /* number of resource records in the answer */
    uint16    nscount;      /* number of name server resource records in the authority records*/
    uint16    arcount;      /* number of resource records in additional records */
} DnsHeader;
#pragma pack()
 
 
typedef struct dns_msg_s {
    uint16          msgid;
 
    char          * name;
    int             nlen;
 
    /* question of query */
    char          * qname;
    int             qnlen;
    uint16          qtype;
    uint16          qclass;
 
    frame_t       * reqfrm;
 
    void          * nsrv;
    int             nsrvind;
    ep_sockaddr_t * destaddr;
 
    /* response from name server */
    int             rcode;
 
    int             an_num;
    int             ns_num;
    int             ar_num;
 
    arr_t         * an_list;
    arr_t         * ns_list;
    arr_t         * ar_list;
 
    frame_t       * resfrm;
 
    DnsCB         * dnscb;
    void          * cbobj;
    uint8           cbexec;
 
    void          * lifetimer;
    int             sendtimes;
 
    /*jmp_buf         jbenv;*/
 
    ulong           threadid;
    void          * dnsmgmt;
} DnsMsg;
 
int    dns_msg_cmp_msgid  (void * a, void * pat);
ulong  dns_msg_hash_msgid (void * key);
 
int    dns_msg_mgmt_add (void * vmgmt, void * vmsg);
void * dns_msg_mgmt_get (void * vmgmt, uint16 msgid);
void * dns_msg_mgmt_del (void * vmgmt, uint16 msgid);
 
int    dns_msg_init    (void * vmsg);
int    dns_msg_free    (void * vmsg);
 
void * dns_msg_fetch   (void * vmgmt);
int    dns_msg_recycle (void * vmsg);
 
int    dns_msg_encode (void * vmsg, char * name, int len);
int    dns_msg_decode (void * vmsg);
 
void * dns_msg_open  (void * vmgmt, char * name, int len, DnsCB * cb, void * cbobj);
int    dns_msg_close (void * vmsg);
 
int    dns_msg_send  (void * vmsg, char * name, int len, void * vnsrv);
 
int    dns_msg_handle (void * vmsg);
 
int    dns_msg_lifecheck (void * vmsg);
 
/*******************************************************
 * DNS Mgmt - entry structure for DNS resolving management
 *******************************************************/

#define t_dns_msg_life     1130
#define t_dns_cache_life   1131 
 
typedef struct dns_mgmt_s {
    char             * resolv_conf;
    void             * nsrv;
 
    CRITICAL_SECTION   cacheCS;
    hashtab_t        * cache_table;
 
    int                cli_dev_num;
    iodev_t          * cli_dev[4];
 
    uint32             msgid;
    hashtab_t        * msg_table;
    CRITICAL_SECTION   msgCS;
 
    bpool_t          * msg_pool;
 
    void             * cachetimer;
 
    void             * pcore;
} DnsMgmt;
 
void * dns_mgmt_init  (void * pcore, char * nsip, char * resolv_file);
void   dns_mgmt_clean (void * vmgmt);
 
int    dns_nb_query (void * vmgmt, char * name, int len, void * vnsrv, void ** pcache, DnsCB * cb, void * cbobj);
 
int    dns_recv  (void * vmgmt, void * pobj);
 
int    dns_pump  (void * vmgmt, void * pobj, int event, int fdtype);


int    dns_query (void * vpcore, char * name, int len, DnsCB * cb, void * cbobj);

#ifdef  __cplusplus
}
#endif
 
#endif

