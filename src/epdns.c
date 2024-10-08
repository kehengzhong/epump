/*
 * Copyright (c) 2003-2024 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 *
 * #####################################################
 * #                       _oo0oo_                     #
 * #                      o8888888o                    #
 * #                      88" . "88                    #
 * #                      (| -_- |)                    #
 * #                      0\  =  /0                    #
 * #                    ___/`---'\___                  #
 * #                  .' \\|     |// '.                #
 * #                 / \\|||  :  |||// \               #
 * #                / _||||| -:- |||||- \              #
 * #               |   | \\\  -  /// |   |             #
 * #               | \_|  ''\---/''  |_/ |             #
 * #               \  .-\__  '-'  ___/-. /             #
 * #             ___'. .'  /--.--\  `. .'___           #
 * #          ."" '<  `.___\_<|>_/___.'  >' "" .       #
 * #         | | :  `- \`.;`\ _ /`;.`/ -`  : | |       #
 * #         \  \ `_.   \_ __\ /__ _/   .-` /  /       #
 * #     =====`-.____`.___ \_____/___.-`___.-'=====    #
 * #                       `=---='                     #
 * #     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   #
 * #               佛力加持      佛光普照              #
 * #  Buddha's power blessing, Buddha's light shining  #
 * #####################################################
 */

#include "btype.h"
#include "memory.h"
#include "dynarr.h"
#include "hashtab.h"
#include "strutil.h"
#include "tsock.h"
#include "frame.h"
#include "mthread.h"
#include "mpool.h"
#include "trace.h"
#include "kemalloc.h"

#include "epcore.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"
#include "epudp.h"

#include "epdns.h"

/* converts a DNS-based hostname into dot-based format, 
   3www5apple3com0 into www.apple.com */

int hostn_to_dot_format (void * name, int len, void * pdst, int dstsize, int * dstlen)
{
    uint8  * hostn = (uint8 * )name;
    uint8  * dst = (uint8 *)pdst;
    int      labellen = 0;
    int      i, j;

    if (dstlen) *dstlen = 0;

    if (!hostn) return -1;
    if (len < 0) len = str_len(hostn);
    if (len <= 0) return -2;

    if (!dst) {
        dst = hostn;
        dstsize = len;
    }
    if (dstsize <= 0) return 0;

    for (i = 0; i < len; i++) {
        labellen = hostn[i];
        if (labellen == 0) break;
 
        for (j = 0; j < labellen; i++, j++) {
            if (i >= dstsize) return i;

            dst[i] = hostn[i + 1];
        }
 
        if (i >= dstsize) return i;
        dst[i] = '.';
    }

    if (i > 0) dst[i - 1] = '\0';
    if (dstlen) *dstlen = i - 1;

    return i + 1;
}

/* converts the dot-based hostname into the DNS format.
   www.apple.com into 3www5apple3com0 */

int hostn_to_dns_format (void * name, int len, void * pdst, int dstlen)
{
    uint8  * hostn = (uint8 * )name;
    uint8  * dst = (uint8 *)pdst;
    int      pos = 0;
    int      labelbgn = 0;
    int      i;
 
    if (!hostn) return -1;
    if (len < 0) len = str_len(hostn);
    if (len <= 0) return -2;
 
    for (i = 0, pos = 0, labelbgn = 0; i <= len; i++) {
        if (i >= len || hostn[i] == '.') {
            if (dst && pos + i - labelbgn + 1 < dstlen) {
                dst[pos++] = i - labelbgn;
                memcpy(dst + pos, hostn + labelbgn, i - labelbgn);
            } else {
                pos++;
            }

            pos += i - labelbgn;
            labelbgn = i + 1;
        }
    }
    if (dst && pos + 1 < dstlen)
        dst[pos] = '\0';

    pos++;

    return pos;
}

/******************************************************
 * DNS NSrv / DNS Host - Name Server handling DNS request
 ******************************************************/

void * dns_host_alloc (int alloctype, void * mpool)
{
    DnsHost * host = NULL;
 
    host = k_mem_zalloc(sizeof(*host), alloctype, mpool);
    if (host) {
        host->alloctype = alloctype;
        host->mpool = mpool;
    }

    return host;
}
 
void dns_host_free (void * vhost)
{
    DnsHost * host = (DnsHost *)vhost;
 
    if (!host) return;
 
    if (host->host) k_mem_free(host->host, host->alloctype, host->mpool);
 
    k_mem_free(host, host->alloctype, host->mpool);
}
 
void * dns_host_new (char * hostn, char * nsip, int port, int alloctype, void * mpool)
{
    DnsHost       * host = NULL;
    int             len = 0;
    ep_sockaddr_t   addr;
 
    if (!nsip) nsip = hostn;
    if (!nsip || (len = strlen(nsip)) <= 0)
        return NULL;
 
    if (port <= 0) port = 53;
 
    if (sock_addr_parse(nsip, len, port, &addr) < 0)
        return NULL;
 
    host = dns_host_alloc(alloctype, mpool);
    if (!host) return NULL;
 
    if (hostn) host->host = k_mem_str_dup(hostn, -1, alloctype, mpool);
    else host->host = k_mem_str_dup(nsip, len, alloctype, mpool);
 
    sock_addr_ntop(&addr.u.addr, host->ip);
    host->port = port;
    host->addr = addr;
 
    return host;
}
 
void * dns_nsrv_alloc (int alloctype, void * mpool)
{
    DnsNSrv * ns = NULL;
 
    ns = k_mem_zalloc(sizeof(*ns), alloctype, mpool);
    if (!ns) return NULL;
 
    ns->alloctype = alloctype;
    ns->mpool = mpool;

    InitializeCriticalSection(&ns->hostCS);
    ns->host_list = arr_alloc(4, alloctype, mpool);
 
    return ns;
}
 
void dns_nsrv_free (void * vnsrv)
{
    DnsNSrv * ns = (DnsNSrv *)vnsrv;
 
    if (!ns) return;
 
    DeleteCriticalSection(&ns->hostCS);
    arr_pop_free(ns->host_list, dns_host_free);
 
    k_mem_free(ns, ns->alloctype, ns->mpool);
}
 
int dns_nsrv_num (void * vnsrv)
{
    DnsNSrv * ns = (DnsNSrv *)vnsrv;
    int       num = 0;
 
    EnterCriticalSection(&ns->hostCS);
    num = arr_num(ns->host_list);
    LeaveCriticalSection(&ns->hostCS);
 
    return num;
}
 
int dns_nsrv_add (void * vnsrv, void * vhost)
{
    DnsNSrv * ns = (DnsNSrv *)vnsrv;
    DnsHost * host = (DnsHost *)vhost;
 
    if (!ns) return -1;
    if (!host) return -2;
 
    EnterCriticalSection(&ns->hostCS);
    arr_push(ns->host_list, host);
    LeaveCriticalSection(&ns->hostCS);
 
    return 0;
}
 
int dns_nsrv_append (void * vmgmt, char * nsip, int port)
{
    DnsMgmt * mgmt = (DnsMgmt *)vmgmt;
    DnsHost * host = NULL;

    if (!mgmt) return -1;
    if (!nsip) return -2;

    host = dns_host_new(NULL, nsip, port, mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
    if (!host) return -100;

    dns_nsrv_add(mgmt->nsrv, host);

    return 0;
}

void dns_nsrv_print (void * vmgmt)
{
#ifdef _DEBUG
    DnsMgmt * mgmt = (DnsMgmt *)vmgmt;
    DnsNSrv * ns = NULL;
    DnsHost * host = NULL;
    int       i, num;

    if (!mgmt) return;

    ns = (DnsNSrv *)mgmt->nsrv;

    EnterCriticalSection(&ns->hostCS);
    num = arr_num(ns->host_list);
    for (i = 0; i < num; i++) {
        host = arr_value(ns->host_list, i);
        if (!host) continue;

        printf("name server - %s:%d\n", host->ip, host->port);
    }
    LeaveCriticalSection(&ns->hostCS);
#endif
}

int dns_nsrv_load (void * vmgmt, char * nsip, char * resolv_file)
{
    DnsMgmt * mgmt = (DnsMgmt *)vmgmt;
    DnsHost * host = NULL;
#ifdef UNIX
    FILE    * fp = NULL;
    char      buf[1024];
    char    * p = NULL;
    int       len = 0;
#endif
#if defined(_WIN32) || defined(_WIN64)
    FIXED_INFO       fi;
    ULONG            ulen = sizeof(fi);
    IP_ADDR_STRING * paddr = NULL;
#endif

    if (!mgmt) return -1;

    if (nsip) {
        host = dns_host_new(NULL, nsip, 0, mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
        if (host)
            dns_nsrv_add(mgmt->nsrv, host);
    }

#ifdef UNIX
    if (!resolv_file)
        resolv_file = "/etc/resolv.conf";
 
    fp = fopen(resolv_file, "r");
    if (!fp) return 0;
 
    while (!feof(fp)) {
        fgets(buf, sizeof(buf)-1, fp);
        p = str_trim(buf);
        len = strlen(buf);
 
        if (len <= 0 || *p == '#')
            continue;
 
        if (strncasecmp(p, "nameserver", 10) == 0) {
            p += 10; len -= 10;
            p = skipOver(p, len, " \t\r\n\f\v", 6);
 
            host = dns_host_new(NULL, p, 0, mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
            if (host) dns_nsrv_add(mgmt->nsrv, host);
        }
        buf[0] = '\0';
    }
 
    fclose(fp);
#endif

#if defined(_WIN32) || defined(_WIN64)
    /* retrieves network parameters for the local computer */
    if (GetNetworkParams(&fi, &ulen) != ERROR_SUCCESS) {
        return -100;
    }

    if (fi.DomainName && strlen(fi.DomainName) > 0) {
        host = dns_host_new(NULL, fi.DomainName, 0,
                            mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
        if (host) dns_nsrv_add(mgmt->nsrv, host);
    }

    if (fi.DnsServerList.IpAddress.String && strlen(fi.DnsServerList.IpAddress.String) > 0) {
        host = dns_host_new(NULL, fi.DnsServerList.IpAddress.String, 0,
                            mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
        if (host) dns_nsrv_add(mgmt->nsrv, host);
    }

    paddr = fi.DnsServerList.Next;
    while(paddr != NULL) {
        if (paddr->IpAddress.String && strlen(paddr->IpAddress.String) > 0) {
            host = dns_host_new(NULL, paddr->IpAddress.String, 0,
                                mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
            if (host) dns_nsrv_add(mgmt->nsrv, host);
        }
        paddr = paddr->Next;
    }
#endif

    /*if (dns_nsrv_num(mgmt->nsrv) <= 1) {
        host = dns_host_new(NULL, "8.8.8.8", 0,
                            mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
        if (host) dns_nsrv_add(mgmt->nsrv, host);
    }*/
 
    return 0;
}

/******************************************************
 * DNS RR - Resource Record parsed and received from NS
 ******************************************************/

void * dns_rr_alloc (int alloctype, void * mpool)
{
    DnsRR  * rr = NULL;
 
    rr = k_mem_zalloc(sizeof(*rr), alloctype, mpool);
    if (rr) {
        rr->alloctype = alloctype;
        rr->mpool = mpool;
    }
 
    return rr;
}
 
void dns_rr_free (void * vrr)
{
    DnsRR  * rr = (DnsRR *)vrr;
 
    if (!rr) return;
 
    if (rr->name) k_mem_free(rr->name, rr->alloctype, rr->mpool);
    if (rr->rdata) k_mem_free(rr->rdata, rr->alloctype, rr->mpool);
 
    k_mem_free(rr, rr->alloctype, rr->mpool);
}
 
void * dns_rr_dup (void * vrr)
{
    DnsRR  * rr = (DnsRR *)vrr;
    DnsRR  * dup = NULL;
 
    if (!rr) return NULL;
 
    dup = dns_rr_alloc(rr->alloctype, rr->mpool);
    if (!dup) return NULL;
 
    dup->namelen = rr->namelen;
    dup->name = k_mem_str_dup(rr->name, rr->namelen, rr->alloctype, rr->mpool);
 
    dup->type = rr->type;
    dup->class = rr->class;
    dup->ttl = rr->ttl;
 
    dup->rdlen = rr->rdlen;
    dup->rdata = k_mem_str_dup(rr->rdata, rr->rdlen, rr->alloctype, rr->mpool);
 
    memcpy(dup->ip, rr->ip, sizeof(rr->ip));
 
    dup->rcvtick = rr->rcvtick;
    dup->outofdate = 0;
 
    return dup;
}
 
void dns_rr_print (void * vrr)
{
    DnsRR  * rr = (DnsRR *)vrr;
 
    if (!rr) return;
 
    printf("%s", rr->name);
 
    if (!rr) {
        printf(" Null\n");
        return;
    }
 
    switch (rr->type) {
    case RR_TYPE_A:     printf(" A"); break;
    case RR_TYPE_NS:    printf(" NS"); break;
    case RR_TYPE_CNAME: printf(" CNAME"); break;
    case RR_TYPE_AAAA:  printf(" AAAA"); break;
    case RR_TYPE_SOA:   printf(" SOA"); break;
    case RR_TYPE_PTR:   printf(" PTR"); break;
    case RR_TYPE_MX:    printf(" MX"); break;
    case RR_TYPE_TXT:   printf(" TXT"); break;
    case RR_QTYPE_ALL:  printf(" ALL"); break;
    default: printf(" %u-Unknown", rr->type); break;
    }
 
    switch (rr->class) {
    case RR_CLASS_IN:     printf(" IN"); break;
    case RR_CLASS_ANY:    printf(" ANY"); break;
    default: printf(" %u-Unknown", rr->class); break;
    }
 
    printf(" %u/%d", rr->ttl, rr->outofdate);
 
    if (rr->type == RR_TYPE_A || rr->type == RR_TYPE_AAAA)
        printf(" %s", rr->ip);
 
    else if (rr->type == RR_TYPE_CNAME || rr->type == RR_TYPE_NS || rr->type == RR_TYPE_SOA)
        printf(" %s", rr->rdata);
 
    printf("\n");
}
 
int dns_rr_name_parse (void * vrr, uint8 * p, int len, uint8 * bufbgn, uint8 ** nextptr, uint8 ** pname, int * namelen)
{
    DnsRR  * rr = (DnsRR *)vrr;
    uint8  * pnext = p;
    uint8  * pend = NULL;
    uint8  * name = NULL;
    uint8    hostn[512];
    uint32   offset = 0;
    int      iter = 0;
    int      jumpnum = 0;
    int      ret;
 
    if (!rr) return -1;
 
    pend = p + len;
 
    while (p < pend && iter < sizeof(hostn) - 8) {
        if (*p >= 0xC0) {
            /* DNS name compression */
            offset = (p[0] & 0x3F) * 256 + p[1];
            p = bufbgn + offset;
            if (jumpnum++ == 0) pnext += 2;
            continue;
        }
 
        hostn[iter++] = *p++;
        if (jumpnum == 0) pnext++;
 
        if (hostn[iter-1] == '\0') break;
    }
 
    if (nextptr) *nextptr = pnext;
 
    name = k_mem_zalloc(iter + 1, rr->alloctype, rr->mpool);
 
    ret = hostn_to_dot_format(hostn, iter, name, iter, namelen);
 
    if (pname) *pname = name;
 
    return ret;
}
 
int dns_rr_parse (void * vrr, uint8 * p, int len, uint8 * bufbgn, uint8 ** nextrr)
{
    DnsRR  * rr = (DnsRR *)vrr;
    uint8  * pend = NULL;
    uint8  * piter = NULL;
    uint8  * pnext = NULL;
    uint32   val32 = 0;
    uint16   val16 = 0;
    int      i, iplen = 0;
    uint8  * pname = NULL;
    int      ret;
 
    if (!rr) return -1;
 
    pend = p + len;
 
    /* parsing the Name */
    if ((ret = dns_rr_name_parse(rr, p, len, bufbgn, &pnext, &pname, &rr->namelen)) < 0)
        return -100;
    piter = pnext;
 
    if (rr->name) k_mem_free(rr->name, rr->alloctype, rr->mpool);
    rr->name = (char *)pname;
 
    if (piter + 10 >= pend) return -101;
 
    /* Type */
    memcpy(&val16, piter, 2);  piter += 2;
    rr->type = ntohs(val16);
 
    /* Class */
    memcpy(&val16, piter, 2);  piter += 2;
    rr->class = ntohs(val16);
 
    /* TTL */
    memcpy(&val32, piter, 4);  piter += 4;
    rr->ttl = ntohl(val32);
    //if (rr->ttl <= 10) rr->ttl = 600;
 
    /* RDLen */
    memcpy(&val16, piter, 2);  piter += 2;
    rr->rdlen = ntohs(val16);
 
    if (piter + rr->rdlen > pend)
        return -110;
 
    if (rr->type == RR_TYPE_A) {
        if (rr->rdlen < 4 || (piter[0] == 0 && piter[1] == 0 && piter[2] == 0))
            return -111;

        rr->rdata = k_mem_zalloc(rr->rdlen + 1, rr->alloctype, rr->mpool);
        memcpy(rr->rdata, piter, rr->rdlen);
        piter += rr->rdlen;
 
        sprintf(rr->ip, "%u.%u.%u.%u", rr->rdata[0], rr->rdata[1], rr->rdata[2], rr->rdata[3]);
 
    } else if (rr->type == RR_TYPE_AAAA) {
        rr->rdata = k_mem_zalloc(rr->rdlen + 1, rr->alloctype, rr->mpool);
        memcpy(rr->rdata, piter, rr->rdlen);
        piter += rr->rdlen;
 
        for (i = 0, iplen = 0; i < rr->rdlen; i++) {
            if (iplen > 0 && (iplen % 2) == 0) {
                rr->ip[iplen++] = ':';
            }
            sprintf(rr->ip + iplen, "%02x", rr->rdata[i]);
            iplen += 2;
        }
 
    } else {
        if ((ret = dns_rr_name_parse(rr, piter, rr->rdlen, bufbgn, &pnext, &pname, &iplen)) < 0)
            return -120;
        piter = pnext;
 
        if (rr->rdata) { k_mem_free(rr->rdata, rr->alloctype, rr->mpool); rr->rdata = NULL; }
        rr->rdata = pname;
        rr->rdlen = iplen;
 
        rr->ip[0] = '\0';
    }
 
    rr->rcvtick = time(0);
    rr->outofdate = 0;
 
    if (nextrr) *nextrr = piter;
 
    return piter - p;
}

/***************************************************
 * DNS Cache - storing the RR list of Domain/IP pair
 ***************************************************/

int dns_cache_cmp_name (void * a, void * pat)
{
    DnsCache * cache = (DnsCache *)a;
    char     * name = (char *)pat;
 
    if (!cache) return -1;
    if (!name) return 1;
 
    return strcasecmp(cache->name, name);
}
 
void * dns_cache_alloc (void * vmgmt)
{
    DnsMgmt  * mgmt = (DnsMgmt *)vmgmt;
    DnsCache * cache = NULL;
 
    if (!mgmt) return NULL;

    cache = k_mem_zalloc(sizeof(*cache), mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
    if (!cache) return NULL;
 
    InitializeCriticalSection(&cache->rrlistCS);
    cache->rr_list = arr_alloc(2, mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
 
    cache->stamp = time(0);
    cache->anum = 0;

    InitializeCriticalSection(&cache->tryCS);
    cache->trymsg = 0;
    cache->failmsg = 0;
    cache->succmsg = 0;

    cache->dnsmgmt = NULL;
 
    return cache;
}
 
int dns_cache_init (void * vca)
{
    DnsCache * cache = (DnsCache *)vca;
    DnsMgmt  * mgmt = NULL;

    if (!cache) return -1;

    mgmt = (DnsMgmt *)cache->dnsmgmt;
    if (!mgmt) return -2;

    cache->name[0] = '\0';

    InitializeCriticalSection(&cache->rrlistCS);
    cache->rr_list = arr_alloc(2, mgmt->fragmem_alloctype, mgmt->fragmem_kempool);

    cache->stamp = time(0);
    cache->anum = 0;

    InitializeCriticalSection(&cache->tryCS);
    cache->trymsg = 0;
    cache->failmsg = 0;
    cache->succmsg = 0;

    cache->dnsmgmt = NULL;

    return 0;
}

int dns_cache_free (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;
 
    if (!cache) return -1;
 
    DeleteCriticalSection(&cache->rrlistCS);
    if (cache->rr_list) {
        arr_pop_free(cache->rr_list, dns_rr_free);
        cache->rr_list = NULL;
    }
 
    DeleteCriticalSection(&cache->tryCS);

    return 0;
}
 
int dns_cache_recycle (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsMgmt * mgmt = NULL;

    if (!cache) return -1;

    mgmt = (DnsMgmt *)cache->dnsmgmt;
    if (!mgmt) return -2;

    DeleteCriticalSection(&cache->rrlistCS);
    if (cache->rr_list) {
        arr_pop_free(cache->rr_list, dns_rr_free);
        cache->rr_list = NULL;
    }

    DeleteCriticalSection(&cache->tryCS);

    mpool_recycle(mgmt->cache_pool, cache);
    return 0;
}

void dns_cache_trymsg_fail (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;

    if (!cache) return;

    EnterCriticalSection(&cache->tryCS);
    cache->trymsg++;
    cache->failmsg++;
    LeaveCriticalSection(&cache->tryCS);
}

void dns_cache_trymsg_succ (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;

    if (!cache) return;

    EnterCriticalSection(&cache->tryCS);
    cache->trymsg++;
    cache->succmsg++;
    LeaveCriticalSection(&cache->tryCS);
}


int dns_cache_add (void * vcache, void * vrr)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsRR    * rr = (DnsRR *)vrr;
    DnsRR    * iter = NULL;
    int        i, num;
 
    if (!cache) return -1;
    if (!rr) return -2;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    num = arr_num(cache->rr_list);
 
    for (i = 0; i < num; i++) {
        iter = arr_value(cache->rr_list, i);
        if (!iter) {
            arr_delete(cache->rr_list, i); 
            i--; num--;
            continue;
        }
 
        if (iter->type == rr->type && iter->class == rr->class && 
            iter->rdlen == rr->rdlen && memcmp(iter->rdata, rr->rdata, rr->rdlen) == 0)
        {
            iter->rcvtick = rr->rcvtick;
            iter->outofdate = rr->outofdate;
            LeaveCriticalSection(&cache->rrlistCS);
            return 0;
        }
    }
 
    if (i >= num) {
        arr_push(cache->rr_list, rr);
    }
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    return 1;
}
 
int dns_cache_zap (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;
    int        i, num;
    DnsRR    * rr = NULL;
 
    if (!cache) return -1;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    num = arr_num(cache->rr_list);
 
    for (i = 0; i < num; i++) {
        rr = arr_value(cache->rr_list, i);
        dns_rr_free(rr);
    }
    arr_zero(cache->rr_list);
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    return 0;
}
 
int dns_cache_verify (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsRR    * rr = NULL;
    int        i, num;
    int        anum = 0;
    time_t     curt = 0;
 
    if (!cache) return -1;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    num = arr_num(cache->rr_list);
    curt = time(0);
 
    for (i = 0; i < num; i++) {
        rr = arr_value(cache->rr_list, i);
        if (!rr || (rr->ttl > 0 && curt - rr->rcvtick > rr->ttl * 2)) {
            arr_delete(cache->rr_list, i); i--; num--;
            dns_rr_free(rr);
            continue;
        }

        if (rr->ttl > 0 && curt - rr->rcvtick > rr->ttl * 2) {
            rr->outofdate = 1;
            continue;
        }
        rr->outofdate = 0;
        anum++;
    }

    cache->anum = anum;
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    return anum;
}
 
int dns_cache_copy_ip (void * vcache, char * ip, int len)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsRR    * rr = NULL;
    int        i, num;
 
    if (!cache) return -1;
    if (!ip || len <= 0) return -2;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    time(&cache->stamp);

    num = arr_num(cache->rr_list);
    for (i = 0; i < num; i++) {
        rr = arr_value(cache->rr_list, i);
        if (!rr) {
            arr_delete(cache->rr_list, i); i--; num--;
            dns_rr_free(rr);
            continue;
        }
 
        str_secpy(ip, len, rr->ip, strlen(rr->ip));
        break;
    }
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    return 0;
}
 
int dns_cache_getip (void * vcache, int ind, char * ip, int len)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsRR    * rr = NULL;
    int        i, num;
 
    if (!cache) return 0;
    if (!ip || len <= 0) return 0;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    time(&cache->stamp);

    num = arr_num(cache->rr_list);
    if (ind >= num) ind = num - 1;
    if (ind < 0) ind = 0;

    for (i = 0; i < num && ip && len > 0; i++) {
        rr = arr_value(cache->rr_list, ind % num);
        if (rr && ip) {
            str_secpy(ip, len, rr->ip, strlen(rr->ip));
            if (!rr->outofdate) break;
        }
    }
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    return num;
}
 
int dns_cache_getiplist (void * vcache, char iplist[][41], int listnum)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsRR    * rr = NULL;
    int        i, num;
    int        iter = 0;
 
    if (!cache) return -1;
    if (!iplist || listnum <= 0) return -2;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    time(&cache->stamp);

    num = arr_num(cache->rr_list);
    for (i = 0; i < num && i < listnum; i++) {
        rr = arr_value(cache->rr_list, i);
        if (!rr) continue;
 
        strcpy(iplist[iter], rr->ip);
        iter++;
    }
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    return iter;
}

int dns_cache_sockaddr (void * vcache, int index, int port, ep_sockaddr_t * addr)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsRR    * rr = NULL;
    int        i, num;
 
    if (!cache) return -1;
    if (!addr) return -2;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    time(&cache->stamp);

    num = arr_num(cache->rr_list);
    if (index >= num || index < 0) index = 0;
 
    for (i = 0; i < num; i++) {
        rr = arr_value(cache->rr_list, (i + index) % num);
        if (!rr) continue;
 
        if (sock_addr_parse(rr->ip, -1, port, addr) > 0) {
            return 1;
        }
    }
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    return 0;
}
 
int dns_cache_copy (void * vsrc, void * vdst)
{
    DnsCache * src = (DnsCache *)vsrc;
    DnsCache * dst = (DnsCache *)vdst;
    DnsRR    * rr = NULL;
    DnsRR    * dup = NULL;
    int        i, num;
    int        anum = 0;
    time_t     tick;
 
    if (!src) return -1;
    if (!dst) return -2;
 
    EnterCriticalSection(&src->rrlistCS);
 
    num = arr_num(src->rr_list);
    tick = time(0);
 
    for (i = 0; i < num; i++) {
        rr = arr_value(src->rr_list, i);
        if (!rr) {
            arr_delete(src->rr_list, i); i--; num--;
            continue;
        }
 
        if (rr->ttl > 0 && tick - rr->rcvtick > rr->ttl * 2) {
            rr->outofdate = 1;
            continue;
        }
 
        rr->outofdate = 0;
        anum++;

        dup = dns_rr_dup(rr);
        if (dns_cache_add(dst, dup) <= 0)
            dns_rr_free(dup);
    }
 
    LeaveCriticalSection(&src->rrlistCS);
 
    return anum;
}
 
int dns_cache_num (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;
    int        num = 0;

    if (!cache) return 0;

    EnterCriticalSection(&cache->rrlistCS);
    num = arr_num(cache->rr_list);
    LeaveCriticalSection(&cache->rrlistCS);
 
    return num;
}

int dns_cache_a_num (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsRR    * rr = NULL;
    int        i, num;
    int        anum = 0;
    time_t     curt = 0;
 
    if (!cache) return -1;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    num = arr_num(cache->rr_list);
    curt = time(0);
 
    for (i = 0; i < num; i++) {
        rr = arr_value(cache->rr_list, i);
        if (!rr) {
            arr_delete(cache->rr_list, i); i--; num--;
            dns_rr_free(rr);
            continue;
        }
 
        if (rr->ttl > 0 && curt - rr->rcvtick > rr->ttl * 2) {
            rr->outofdate = 1;
            continue;
        }
        rr->outofdate = 0;
 
        if (rr->type == RR_TYPE_A || rr->type == RR_TYPE_AAAA)
            anum++;
    }
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    cache->anum = anum;

    return anum;
}
 
int dns_cache_find_A_rr (void * vcache, char * name)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsRR    * rr = NULL;
    int        i, num;
 
    if (!cache) return -1;
    if (!name) return -2;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    num = arr_num(cache->rr_list);
 
    for (i = 0; i < num; i++) {
        rr = arr_value(cache->rr_list, i);
        if (!rr) {
            arr_delete(cache->rr_list, i); i--; num--;
            continue;
        }
 
        if ((rr->type == RR_TYPE_A || rr->type == RR_TYPE_AAAA) &&
            strcasecmp(rr->name, name) == 0)
        {
            LeaveCriticalSection(&cache->rrlistCS);
            return 1;
        }
    }
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    return 0;
}
 
int dns_cache_check (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsRR    * rr = NULL;
    int        i, num;
    int        anum = 0;
    time_t     curt = 0;
 
    if (!cache) return -1;
 
    EnterCriticalSection(&cache->rrlistCS);
 
    num = arr_num(cache->rr_list);
    curt = time(0);
 
    for (i = 0; i < num; i++) {
        rr = arr_value(cache->rr_list, i);
        if (!rr) {
            arr_delete(cache->rr_list, i); i--; num--;
            continue;
        }

        if (rr->ttl > 0 && curt - rr->rcvtick > rr->ttl * 2) {
            rr->outofdate = 1;
            continue;
        }

        rr->outofdate = 0;
        anum++;
    }
 
    LeaveCriticalSection(&cache->rrlistCS);
 
    cache->anum = anum;

    return anum;
}
 
 
int dns_cache_mgmt_add (void * vmgmt, void * vcache)
{
    DnsMgmt  * mgmt = (DnsMgmt *)vmgmt;
    DnsCache * cache = (DnsCache *)vcache;
    DnsCache * tmp = NULL;
 
    if (!mgmt) return -1;
    if (!cache) return -2;
 
    EnterCriticalSection(&mgmt->cacheCS);
    tmp = ht_get(mgmt->cache_table, cache->name);
    if (!tmp) {
        ht_set(mgmt->cache_table, cache->name, cache);
    } else {
        if (tmp != cache) {
            ht_delete(mgmt->cache_table, cache->name);
            dns_cache_recycle(tmp);
 
            ht_set(mgmt->cache_table, cache->name, cache);
        }
    }
 
    if (ht_num(mgmt->cache_table) > 0 && mgmt->cachetimer == NULL)
        mgmt->cachetimer = iotimer_start(mgmt->pcore, 30*1000,
                                  t_dns_cache_life, NULL,
                                  dns_pump, mgmt, 0);

    LeaveCriticalSection(&mgmt->cacheCS);

    return 0;
}
 
void * dns_cache_mgmt_get (void * vmgmt, char * name, int namelen)
{
    DnsMgmt  * mgmt = (DnsMgmt *)vmgmt;
    char       sname[256];
    DnsCache * cache = NULL;
 
    if (!mgmt) return NULL;
 
    sname[0] = '\0';
    str_secpy(sname, sizeof(sname)-1, name, namelen);
 
    EnterCriticalSection(&mgmt->cacheCS);
    cache = ht_get(mgmt->cache_table, sname);
    LeaveCriticalSection(&mgmt->cacheCS);
 
    return cache;
}
 
void * dns_cache_mgmt_del (void * vmgmt, char * name, int namelen)
{
    DnsMgmt  * mgmt = (DnsMgmt *)vmgmt;
    char       sname[256];
    DnsCache * cache = NULL;
 
    if (!mgmt) return NULL;
 
    str_secpy(sname, sizeof(sname)-1, name, namelen);
 
    EnterCriticalSection(&mgmt->cacheCS);
    cache = ht_delete(mgmt->cache_table, sname);
    LeaveCriticalSection(&mgmt->cacheCS);
 
    return cache;
}
 
void * dns_cache_open (void * vmgmt, char * name, int len)
{
    DnsMgmt  * mgmt = (DnsMgmt *)vmgmt;
    DnsCache * cache = NULL;
 
    if (!mgmt) return NULL;
 
    cache = dns_cache_mgmt_get(mgmt, name, len);
    if (!cache) {
        cache = mpool_fetch(mgmt->cache_pool);
        if (!cache) return NULL;

        cache->dnsmgmt = mgmt;
 
        dns_cache_init(cache);

        str_secpy(cache->name, sizeof(cache->name)-1, name, len);
        cache->dnsmgmt = mgmt;
 
        dns_cache_mgmt_add(mgmt, cache);
    }
 
    time(&cache->stamp);
 
    return cache;
}
 
int dns_cache_close (void * vcache)
{
    DnsCache * cache = (DnsCache *)vcache;
    DnsMgmt  * mgmt = NULL;
 
    if (!cache) return -1;
 
    mgmt = (DnsMgmt *)cache->dnsmgmt;
    if (!mgmt) return -2;

    if (dns_cache_mgmt_del(mgmt, cache->name, -1) == NULL)
        return -100;
 
    dns_cache_recycle(cache);
    
    return 0;
}
 
 
int dns_cache_lifecheck (void * vmgmt)
{
    DnsMgmt  * mgmt = (DnsMgmt *)vmgmt;
    DnsCache * cache = NULL;
    int        i, num;
    time_t     tick;
 
    if (!mgmt) return -1;
 
    EnterCriticalSection(&mgmt->cacheCS);
 
    tick = time(0);
    num = ht_num(mgmt->cache_table);
 
    for (i = 0; i < num; i++) {
        cache = ht_value(mgmt->cache_table, i);
        if (!cache) continue;
 
        EnterCriticalSection(&cache->tryCS);
        cache->trymsg = 0;
        cache->failmsg = 0;
        cache->succmsg = 0;
        LeaveCriticalSection(&cache->tryCS);

        dns_cache_check(cache);
 
        if (tick - cache->stamp > 300) { // && cache->anum <= 0) {
            ht_delete(mgmt->cache_table, cache->name);
            dns_cache_recycle(cache);

            i--, num--;
        }
    }
 
    LeaveCriticalSection(&mgmt->cacheCS);
 
    return 0;
}

/*******************************************************
 * DNS Msg - packing request/response for resolving name 
 *******************************************************/

int dns_msg_cmp_msgid (void * a, void * pat) 
{    
    DnsMsg * msg = (DnsMsg *)a;
    uint16   msgid = *(uint16 *)pat;
     
    if (!msg) return -1; 
     
    if (msg->msgid == msgid) return 0; 
    if (msg->msgid > msgid) return 1;
    return -1; 
}
 
ulong dns_msg_hash_msgid (void * key)
{
    uint16 msgid = *(uint16 *)key;
    return (ulong)msgid;
}
 
int dns_msg_mgmt_add (void * vmgmt, void * vmsg)
{    
    DnsMgmt   * mgmt = (DnsMgmt *)vmgmt;
    DnsMsg    * pmsg = (DnsMsg *)vmsg;
    int         ret = 0;
     
    if (!mgmt) return -1;
    if (!pmsg) return -2;
 
    EnterCriticalSection(&mgmt->msgCS);
    ret = ht_set(mgmt->msg_table, &pmsg->msgid, pmsg);
    LeaveCriticalSection(&mgmt->msgCS);
 
    return ret; 
}
 
void * dns_msg_mgmt_get (void * vmgmt, uint16 msgid)
{
    DnsMgmt   * mgmt = (DnsMgmt *)vmgmt;
    DnsMsg    * pmsg = NULL;
 
    if (!mgmt) return NULL;
 
    EnterCriticalSection(&mgmt->msgCS);
    pmsg = ht_get(mgmt->msg_table, &msgid);
    LeaveCriticalSection(&mgmt->msgCS);
 
    return pmsg;
}
 
void * dns_msg_mgmt_del (void * vmgmt, uint16 msgid)
{
    DnsMgmt   * mgmt = (DnsMgmt *)vmgmt;
    DnsMsg    * pmsg = NULL;
 
    if (!mgmt) return NULL;
 
    EnterCriticalSection(&mgmt->msgCS);
    pmsg = ht_delete(mgmt->msg_table, &msgid);
    LeaveCriticalSection(&mgmt->msgCS);
 
    return pmsg;
}
 
int dns_msg_init (void * vmsg)
{
    DnsMsg  * msg = (DnsMsg *)vmsg;
    DnsMgmt * mgmt = NULL;
 
    if (!msg) return -1;
 
    mgmt = (DnsMgmt *)msg->dnsmgmt;
    if (!mgmt) return -2;

    msg->name[0] = '\0';
    msg->nlen = 0;
 
    msg->qname[0] = '\0';
    msg->qnlen = 0;
    msg->qtype = 0;
    msg->qclass = 0;
 
    if (msg->nsrv) {
        dns_nsrv_free(msg->nsrv);
        msg->nsrv = NULL;
    }
    msg->nsrvind = 0;
 
    msg->destaddr = NULL;
 
    msg->rcode = 0;
 
    msg->an_num = 0;
    msg->ns_num = 0;
    msg->ar_num = 0;
 
    if (!msg->an_list) {
        msg->an_list = arr_alloc(2, mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
    } else {
        while (arr_num(msg->an_list) > 0)
            dns_rr_free(arr_pop(msg->an_list));
    }
 
    if (!msg->ns_list) {
        msg->ns_list = arr_alloc(2, mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
    } else {
        while (arr_num(msg->ns_list) > 0)
            dns_rr_free(arr_pop(msg->ns_list));
    }
 
    if (!msg->ar_list) {
        msg->ar_list = arr_alloc(2, mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
    } else {
        while (arr_num(msg->ar_list) > 0)
            dns_rr_free(arr_pop(msg->ar_list));
    }
 
    msg->dnscb = NULL;
    msg->cbobj = NULL;
    msg->cbobjid = 0;
    msg->cbexec = 0;
 
    msg->lifetimer = NULL;
    msg->sendtimes = 0;
 
    msg->cache = NULL;

    msg->threadid = 0;

    return 0;
}
 
int dns_mgmt_msg_free (void * vmsg)
{
    DnsMsg  * msg = (DnsMsg *)vmsg;
    DnsMgmt * mgmt = NULL;

    if (!msg) return -1;

    mgmt = (DnsMgmt *)msg->dnsmgmt;
    if (!mgmt) return -2;
 
    if (dns_msg_free(msg) == 0)
        mpool_recycle(mgmt->msg_pool, msg);

    return 0;
}

int dns_msg_free (void * vmsg)
{
    DnsMsg  * msg = (DnsMsg *)vmsg;
    DnsMgmt * mgmt = NULL;
 
    if (!msg) return -1;

    mgmt = (DnsMgmt *)msg->dnsmgmt;
    if (!mgmt) return -2;
 
    if (msg->lifetimer) {
        iotimer_stop(mgmt->pcore, msg->lifetimer);
        msg->lifetimer = NULL;
    }
 
    if (msg->nsrv) {
        dns_nsrv_free(msg->nsrv);
        msg->nsrv = NULL;
    }
 
    if (msg->an_list) {
        arr_pop_free(msg->an_list, dns_rr_free);
        msg->an_list = NULL;
    }
    if (msg->ns_list) {
        arr_pop_free(msg->ns_list, dns_rr_free);
        msg->ns_list = NULL;
    }
    if (msg->ar_list) {
        arr_pop_free(msg->ar_list, dns_rr_free);
        msg->ar_list = NULL;
    }
 
    return 0;
}
 
 
void * dns_msg_fetch (void * vmgmt) 
{
    DnsMgmt * mgmt = (DnsMgmt *)vmgmt;
    DnsMsg  * msg = NULL;
 
    if (!mgmt) return NULL;
     
    msg = mpool_fetch(mgmt->msg_pool);
    if (!msg) return NULL;
 
    msg->dnsmgmt = mgmt;
 
    dns_msg_init(msg);
 
    EnterCriticalSection(&mgmt->msgCS);
    msg->msgid = mgmt->msgid++;
    if (msg->msgid == 0) msg->msgid = mgmt->msgid++;
    LeaveCriticalSection(&mgmt->msgCS);
 
    if (dns_msg_mgmt_add(mgmt, msg) <= 0) {
        tolog(1, "Panic: dns_msg_fetch failed, msgid=%u exist in msg_table\n", msg->msgid);
        dns_msg_recycle(msg);
        return NULL;
    }
 
    return msg;
}
 
int dns_msg_recycle (void * vmsg)
{
    DnsMsg  * msg = (DnsMsg *)vmsg;
    DnsMgmt * mgmt = NULL;
 
    if (!msg) return -1;
 
    mgmt = (DnsMgmt *)msg->dnsmgmt;
    if (!mgmt) 
        return -2;
 
    if (msg->lifetimer) {
        iotimer_stop(mgmt->pcore, msg->lifetimer);
        msg->lifetimer = NULL;
    }
 
    if (msg->nsrv) {
        dns_nsrv_free(msg->nsrv);
        msg->nsrv = NULL;
    }

    if (msg->an_list) {
        while (arr_num(msg->an_list) > 0)
            dns_rr_free(arr_pop(msg->an_list));
    }
 
    if (msg->ns_list) {
        while (arr_num(msg->ns_list) > 0)
            dns_rr_free(arr_pop(msg->ns_list));
    }
 
    if (msg->ar_list) {
        while (arr_num(msg->ar_list) > 0)
            dns_rr_free(arr_pop(msg->ar_list));
    }

    mpool_recycle(mgmt->msg_pool, msg);
    return 0;
}
 
int dns_msg_encode (void * vmsg, char * name, int len, uint8 * pbuf, int bufsize)
{
    DnsMsg    * msg = (DnsMsg *)vmsg;
    DnsHeader   hdr = {0};
    int         enclen = 0;
    uint16      val16 = 0;
 
    if (!msg) return 0;
 
    hdr.ID = htons(msg->msgid);
 
    hdr.RD = 1;
    hdr.TC = 0;
    hdr.AA = 0;
    hdr.Opcode = 0;
    hdr.QR = 0;
 
    hdr.RCODE = 0;
    hdr.Z = 0;
    hdr.RA = 0;
 
    hdr.qdcount = htons(1);
    hdr.ancount = 0;
    hdr.nscount = 0;
    hdr.arcount = 0;
 
    /* Question: Dot-format name converts to DNS name */
    msg->qnlen = hostn_to_dns_format(name, len, msg->qname, sizeof(msg->qname)-1);
    if (msg->qnlen <= 0)
        return -100;
 
    msg->qtype = RR_TYPE_A;
    msg->qclass = RR_CLASS_IN;
 
    /* Header */
    if (pbuf && bufsize - enclen >= sizeof(hdr))
        memcpy(pbuf + enclen, &hdr, sizeof(hdr));
    enclen += sizeof(hdr);
 
    /* Question Name */
    if (pbuf && bufsize - enclen >= msg->qnlen)
        memcpy(pbuf + enclen, msg->qname, msg->qnlen);
    enclen += msg->qnlen;
 
    /* QType and QClass */
    val16 = htons(msg->qtype);
    if (pbuf && bufsize - enclen >= 2)
        memcpy(pbuf + enclen, &val16, 2);
    enclen += 2;

    val16 = htons(msg->qclass);
    if (pbuf && bufsize - enclen >= 2)
        memcpy(pbuf + enclen, &val16, 2);
    enclen += 2;
 
    return enclen;
}
 
int dns_msg_decode (void * vmsg, uint8 * pbgn, int len)
{
    DnsMsg    * msg = (DnsMsg *)vmsg;
    DnsMgmt   * mgmt = NULL;
    int         iter = 0;
    DnsHeader   hdr = {0};
    DnsRR     * rr = NULL;
    uint16      val16 = 0;
    int         i, ret;
    char        name[256];
    int         namelen = 0;
 
    if (!msg) return -1;
    if (!pbgn || len < sizeof(hdr)) return -10;
 
    mgmt = msg->dnsmgmt;
    if (!mgmt) return -11;

    memcpy(&hdr, pbgn, sizeof(hdr));
    iter += sizeof(hdr);
 
    hdr.ID = ntohs(hdr.ID);
    if (hdr.ID != msg->msgid)
        return -100;
 
    hdr.qdcount = ntohs(hdr.qdcount);
    hdr.ancount = ntohs(hdr.ancount);
    hdr.nscount = ntohs(hdr.nscount);
    hdr.arcount = ntohs(hdr.arcount);
 
    msg->rcode = hdr.RCODE;
    msg->an_num = hdr.ancount;
    msg->ns_num = hdr.nscount;
    msg->ar_num = hdr.arcount;
 
    /* verify if Question Name of response is same as request QName */
    ret = hostn_to_dot_format(pbgn + iter, len - iter, name, sizeof(name)-1, &namelen);
    if (ret <= 0) return -101;
    if (str_ncasecmp(msg->qname, pbgn + iter, msg->qnlen) != 0)
        return -102;
    iter += ret;
 
    /* verify if response QType is same as request QType */
    if (iter + 2 > len) return -103;
    memcpy(&val16, pbgn + iter, 2); iter += 2;
    val16 = ntohs(val16);
    if (msg->qtype != val16) return -104;
 
    /* verify if response QClass is same as request QClass */
    if (iter + 2 > len) return -105;
    memcpy(&val16, pbgn + iter, 2); iter += 2;
    val16 = ntohs(val16);
    if (msg->qclass != val16) return -106;
 
    /* Decoding all Answer PDU */
    for (i = 0; i < msg->an_num && iter < len; i++) {
        rr = dns_rr_alloc(mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
        ret = dns_rr_parse(rr, pbgn + iter, len - iter, pbgn, NULL);
        if (ret < 0) {
            dns_rr_free(rr);
            return ret;
        }
 
        arr_push(msg->an_list, rr);
        iter += ret;
    }
 
    /* Decoding all Authority PDU */
    for (i = 0; i < msg->ns_num && iter < len; i++) {
        rr = dns_rr_alloc(mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
        ret = dns_rr_parse(rr, pbgn + iter, len - iter, pbgn, NULL);
        if (ret < 0) {
            dns_rr_free(rr);
            return ret;
        }
 
        arr_push(msg->ns_list, rr);
        iter += ret;
    }
 
    /* Decoding all Additional PDU */
    for (i = 0; i < msg->ar_num && iter < len; i++) {
        rr = dns_rr_alloc(mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
        ret = dns_rr_parse(rr, pbgn + iter, len - iter, pbgn, NULL);
        if (ret < 0) {
            dns_rr_free(rr);
            return ret;
        }
 
        arr_push(msg->ar_list, rr);
        iter += ret;
    }
 
    return iter;
}
 
void * dns_msg_open (void * vmgmt, char * name, int len, DnsCB * cb, void * cbobj, ulong objid)
{
    DnsMgmt  * mgmt = (DnsMgmt *)vmgmt;
    DnsMsg   * msg = NULL;
 
    if (!mgmt) return NULL;
 
    if (!name) return NULL;
    if (len < 0) len = strlen(name);
    if (len <= 0) return NULL;
 
    msg = dns_msg_fetch(mgmt);
    if (!msg) return NULL;
 
    str_secpy(msg->name, sizeof(msg->name)-1, name, len);
    msg->nlen = len;
 
    msg->dnscb = cb;
    msg->cbobj = cbobj;
    msg->cbobjid = objid;
 
    /* When the upper-layer application calls dns_query, it calls the current
       function to create a DnsMsg instance, and the DnsMsg records the caller
       thread ID. In this way, dns_query callback function will be invoked by
       the same thread as the caller to process the DNS resolution result, 
       ensuring that the upper-layer application data is processed in the same thread. */
    msg->threadid = get_threadid();

    return msg;
}
 
int dns_msg_close (void * vmsg)
{
    DnsMsg   * msg = (DnsMsg *)vmsg;
 
    if (!msg) return -1;
 
    if (dns_msg_mgmt_del(msg->dnsmgmt, msg->msgid) != msg)
        return -100;
 
    if (!msg->cbexec) {
        msg->cbexec = 1;
        (*msg->dnscb)(msg->cbobj, msg->cbobjid, msg->name, msg->nlen, NULL, msg->rcode);
    }
 
    return dns_msg_recycle(msg);
}
 
 
int dns_msg_send (void * vmsg, char * name, int len, void * vnsrv)
{
    DnsMsg   * msg = (DnsMsg *)vmsg;
    DnsNSrv  * nsrv = (DnsNSrv *)vnsrv;
    DnsMgmt  * mgmt = NULL;
    DnsHost  * host = NULL;
    iodev_t  * pdev = NULL;
    int        i, j, num, ret;
    uint8      buf[16384];
    int        enclen = 0;
 
    if (!msg) return -1;
 
    mgmt = (DnsMgmt *)msg->dnsmgmt;
    if (!mgmt) return -2;
 
    if (!name) {
        name = msg->name;
        len = msg->nlen;
    }
    if (!name) return -3;
    if (len < 0) len = strlen(name);
    if (len <= 0) return -100;
 
    enclen = dns_msg_encode(msg, name, len, buf, sizeof(buf));
 
    if (nsrv && nsrv != mgmt->nsrv) {
        if (msg->nsrv && msg->nsrv != nsrv)
            dns_nsrv_free(msg->nsrv);
        msg->nsrv = nsrv;
    }
    if (!nsrv) nsrv = mgmt->nsrv;
 
    if (nsrv == mgmt->nsrv && dns_nsrv_num(mgmt->nsrv) <= 0) {
        host = dns_host_new(NULL, "8.8.8.8", 0,
                            mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
        if (host) dns_nsrv_add(mgmt->nsrv, host);
    }

    num = arr_num(nsrv->host_list);
 
    for (i = 0; i < num; i++) {
        host = arr_value(nsrv->host_list, (i + msg->nsrvind) % num);
        if (!host) continue;
 
        for (j = 0; j < mgmt->cli_dev_num; j++) {
            pdev = mgmt->cli_dev[j];
            if (pdev->family == host->addr.u.addr.sa_family)
                break;
        }

        ret = sendto(iodev_fd(pdev),
                     buf, enclen, 0,
                     (struct sockaddr *)&host->addr.u.addr,
                     host->addr.socklen);
        if (ret < 0 || ret != enclen) {
            continue;
        }
 
        msg->destaddr = &host->addr;
        msg->sendtimes++;
        msg->nsrvind++;
 
        if (msg->lifetimer) iotimer_stop(mgmt->pcore, msg->lifetimer);

        /* If the epumpid is set to 0 when the life timer is started, the system randomly
           allocates an ePump thread to bind the iotimer_t instance and process the timeout
           of DnsMsg instance.
           Then, the current DnsMsg instance is sent and received by a UDP iodev device.
           If the ePump thread bound by the iodev is different from the thread bound by
           the timer, it will lead to the situation that two ePump threads handle a DnsMsg
           instance at the same time, which will result in an abnormal crash.
           Therefore, the correct way is to set the epumpid of the timer to the ePump thread
           ID bound by the iodev_t that sends the current DnsMsg.*/
        msg->lifetimer = iotimer_start(mgmt->pcore, 12*1000,
                                  t_dns_msg_life, (void *)(ulong)msg->msgid,
                                  dns_pump, mgmt, iodev_epumpid(pdev));
        return 0;
    }
 
    return -200;
}
 
int dns_msg_nsrv_cb (void * vmsg, ulong msgid, char * name, int namelen, void * vnscac, int status)
{
    DnsMsg    * msg = (DnsMsg *)vmsg;
    DnsCache  * nscac = (DnsCache *)vnscac;
    DnsMgmt   * mgmt = NULL;
    DnsRR     * rr = NULL;
    DnsHost   * nshost = NULL;
    DnsNSrv   * nsrv = NULL;
    int         i, num;
 
    if (!msg) return -1;
    if (!nscac) return -2;
 
    mgmt = (DnsMgmt *)msg->dnsmgmt;
    if (!mgmt) return -3;
 
    nsrv = dns_nsrv_alloc(mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
 
    EnterCriticalSection(&nscac->rrlistCS);
 
    num = arr_num(nscac->rr_list);
    for (i = 0; i < num; i++) {
        rr = arr_value(nscac->rr_list, i);
        if (!rr) {
            arr_delete(nscac->rr_list, i); i--; num--;
            continue;
        }
 
        if (rr->type == RR_TYPE_A || rr->type == RR_TYPE_AAAA) {
            nshost = dns_host_new(rr->name, rr->ip, 0,
                                  mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
            dns_nsrv_add(nsrv, nshost);
        }
    }
 
    LeaveCriticalSection(&nscac->rrlistCS);
 
    if (arr_num(nsrv->host_list) <= 0) {
        /* resolved NS contains no A/AAA record, need to going on resloving
           CNAME or others */
        //to be implemented
    }
 
    return dns_msg_send(msg, NULL, 0, nsrv);
}
 
int dns_msg_handle (void * vmsg)
{
    DnsMsg    * msg = (DnsMsg *)vmsg;
    DnsMgmt   * mgmt = NULL;
    DnsCache  * cache = NULL;
    DnsCache  * nscac = NULL;
    DnsRR     * rr = NULL;
    DnsHost   * nshost = NULL;
    DnsNSrv   * nsrv = NULL;
    char      * rdata = NULL;
    int         rdlen = 0;
    int         i, num, ret;
    int         j, jnum;
    DnsRR     * jrr = NULL;
    int         anum = 0;
 
    if (!msg) return -1;
 
    mgmt = (DnsMgmt *)msg->dnsmgmt;
    if (!mgmt) return -2;
 
    cache = dns_cache_open(mgmt, msg->name, msg->nlen);
    if (!cache) {
        tolog(1, "Panic: dns_cache_open failed, name=%s namelen=%d\n", msg->name, msg->nlen);
        dns_cache_trymsg_fail(cache);
        msg->rcode = DNS_ERR_RESOURCE_FAIL;
        dns_msg_close(msg);
        return -100;
    }
 
    num = arr_num(msg->an_list);
    if (num <= 0 && msg->sendtimes < 3) {
        ret = dns_msg_send(msg, NULL, 0, NULL);
        if (ret < 0) {
            tolog(1, "Panic: dns_msg_send again failed, msgid=%u name=%s times=%d cache:%d/%d/%d\n",
                  msg->msgid, msg->name, msg->sendtimes, cache->trymsg, cache->succmsg, cache->failmsg);
            dns_cache_trymsg_fail(cache);
            msg->rcode = DNS_ERR_SEND_FAIL;
            dns_msg_close(msg);
            return -101;
        }
        return ret;
    }

    for (i = 0; i < num; i++) {
        rr = arr_value(msg->an_list, i);
        if (!rr) continue;
 
        if (rr->type == RR_TYPE_A || rr->type == RR_TYPE_AAAA) {
            arr_delete(msg->an_list, i); i--; num--;
 
            ret = dns_cache_add(cache, rr);
            if (ret >= 0) anum++;
            if (ret <= 0) dns_rr_free(rr);
        } 
    }
 
    /* if A Resource Record found, just return */
    if (anum > 0) {
        dns_cache_trymsg_succ(cache);
        goto got_resolv;
    }
 
    num = arr_num(msg->an_list);
    for (i = 0; i < num; i++) {
        rr = arr_delete(msg->an_list, 0);
        if (!rr) continue;
 
        rdata = (char *)rr->rdata;
        rdlen = rdata ? strlen(rdata) : 0;
 
        if (rr->type == RR_TYPE_CNAME) {
            ret = dns_cache_find_A_rr(cache, rdata);
            if (ret <= 0) {
                nscac = dns_cache_mgmt_get(mgmt, rdata, rdlen);
 
                if (nscac && (ret = dns_cache_a_num(nscac)) > 0) {
                    dns_cache_copy(nscac, cache);
                } else {
                    ret = dns_msg_send(msg, rdata, rdlen, NULL);
                    if (ret >= 0) {
                        dns_rr_free(rr);
                        return 0;
                    }
                }
            }
            dns_rr_free(rr);
 
        } else {
            dns_rr_free(rr);
        }
    }
 
    if (dns_cache_a_num(cache) > 0) {
        dns_cache_trymsg_succ(cache);
        goto got_resolv;
    }
 
    /* coming here shows that there is no valid AnswerRR in response.
       check AuthorityRR list and re-query with new NameServer in list */
 
    num = arr_num(msg->ns_list);
    for (i = 0; i < num; i++) {
        rr = arr_value(msg->ns_list, i);
        if (!rr) continue;
 
        rdata = (char *)rr->rdata;
        rdlen = rdata ? strlen(rdata) : 0;
 
        if (rr->type == RR_TYPE_NS) {
            jnum = arr_num(msg->ar_list);
            for (j = 0; j < jnum; j++) {
                jrr = arr_value(msg->ar_list, j);
                if (!jrr) continue;
 
                if ((jrr->type == RR_TYPE_A || jrr->type == RR_TYPE_AAAA) &&
                    strcasecmp(jrr->name, rdata) == 0)
                {
                    nshost = dns_host_new(rdata, jrr->ip, 0,
                                          mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
                    nsrv = dns_nsrv_alloc(mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
                    dns_nsrv_add(nsrv, nshost);
 
                    /* if rdata in AuthorityRR (Name Server) of response is same as 
                       the name of AdditionalRR, and AdditionalRR is an IP address,
                       the IP address will be used as new NameServer to resolve hostname */
                    return dns_msg_send(msg, NULL, 0, nsrv);
                }
            }
 
            /* if AuthorityRR has no attached AdditionalRR, the Name Sever should be resolved first */
            if (j > jnum) {
                return dns_nb_query(mgmt, rdata, rdlen, NULL, NULL, dns_msg_nsrv_cb, msg, 0);
            }
        }
    }
    
got_resolv:

    if (msg->dnscb) {
        msg->cbexec = 1;
        (*msg->dnscb)(msg->cbobj, msg->cbobjid, msg->name, msg->nlen, cache, msg->rcode);
    }
 
    dns_msg_close(msg);
    return 0;
}
 
int dns_msg_lifecheck (void * vmsg)
{
    DnsMsg * msg = (DnsMsg *)vmsg;
 
    if (!msg) return -1;
 
    msg->lifetimer = NULL;
 
    return dns_msg_close(msg);
}


/*******************************************************
 * DNS Mgmt - entry structure for DNS resolving management
 *******************************************************/

void * dns_mgmt_init (void * pcore, char * nsip, char * resolv_file)
{
    DnsMgmt * mgmt = NULL;
 
    mgmt = kzalloc(sizeof(*mgmt));
    if (!mgmt) return NULL;
 
    mgmt->resolv_conf = resolv_file;
 
    InitializeCriticalSection(&mgmt->cacheCS);
    mgmt->cache_table = ht_new(2000, dns_cache_cmp_name);
 
    InitializeCriticalSection(&mgmt->msgCS);
    mgmt->msgid = 1;
    mgmt->msg_table = ht_only_new(200, dns_msg_cmp_msgid);
    ht_set_hash_func(mgmt->msg_table, dns_msg_hash_msgid);
 
    if (!mgmt->cache_pool) {
        mgmt->cache_pool = mpool_alloc();
        mpool_set_freefunc(mgmt->cache_pool, dns_cache_free);
        mpool_set_unitsize(mgmt->cache_pool, sizeof(DnsCache));
        mpool_set_allocnum(mgmt->cache_pool, 1024);
    }
 
    if (!mgmt->msg_pool) {
        mgmt->msg_pool = mpool_alloc();
        mpool_set_freefunc(mgmt->msg_pool, dns_msg_free);
        mpool_set_unitsize(mgmt->msg_pool, sizeof(DnsMsg));
        mpool_set_allocnum(mgmt->msg_pool, 200);
    }
 
    if (!mgmt->fragmem_kempool) {
        mgmt->fragmem_kempool = kempool_alloc(256*1024, 2600);
    }
    mgmt->fragmem_alloctype = 2;

    mgmt->pcore = pcore;
 
    mgmt->nsrv = dns_nsrv_alloc(mgmt->fragmem_alloctype, mgmt->fragmem_kempool);
    dns_nsrv_load(mgmt, nsip, resolv_file);

    mgmt->cli_dev_num = 4;
    epudp_client(pcore, NULL, 0, NULL, mgmt, dns_pump, mgmt, mgmt->cli_dev, &mgmt->cli_dev_num, NULL);
 
    return mgmt;
}
 
void dns_mgmt_clean (void * vmgmt)
{
    DnsMgmt  * mgmt = (DnsMgmt *)vmgmt;
    int        i;
 
    if (!mgmt) return;
 
    if (mgmt->cachetimer) {
        iotimer_stop(mgmt->pcore, mgmt->cachetimer);
        mgmt->cachetimer = NULL;
    }
 
    for (i = 0; i < mgmt->cli_dev_num; i++)
        iodev_close(mgmt->cli_dev[i]);
 
    dns_nsrv_free(mgmt->nsrv);
 
    ht_free_all(mgmt->cache_table, dns_cache_recycle);
    DeleteCriticalSection(&mgmt->cacheCS);
 
    DeleteCriticalSection(&mgmt->msgCS);
    ht_free_all(mgmt->msg_table, dns_mgmt_msg_free);
 
    if (mgmt->msg_pool) {
        mpool_free(mgmt->msg_pool);
        mgmt->msg_pool = NULL;
    }
 
    if (mgmt->cache_pool) {
        mpool_free(mgmt->cache_pool);
        mgmt->cache_pool = NULL;
    }
 
    if (mgmt->fragmem_kempool) {
        kempool_free(mgmt->fragmem_kempool);
        mgmt->fragmem_kempool = NULL;
    }

    kfree(mgmt);
}
 
int dns_nb_query (void * vmgmt, char * name, int len, void * nsrv, void ** pcache, DnsCB * cb, void * cbobj, ulong objid)
{
    DnsMgmt       * mgmt = (DnsMgmt *)vmgmt;
    DnsCache      * cache = NULL;
    DnsMsg        * msg = NULL;
    int             ret;
    ep_sockaddr_t   addr;
 
    if (!mgmt) return -1;
 
    if (!name) return -2;
    if (len < 0) len = strlen(name);
    if (len <= 0) return -3;
 
    if (sock_addr_parse(name, len, 0, &addr) > 0) {
        if (addr.family == AF_INET) {
            if (cb) (*cb)(cbobj, objid, name, len, NULL, DNS_ERR_IPV4);
            return 1;
        } else if (addr.family == AF_INET6) {
            if (cb) (*cb)(cbobj, objid, name, len, NULL, DNS_ERR_IPV6);
            return 2;
        }
    }

    /* find the im-memory cache for the Name */
    cache = dns_cache_open(mgmt, name, len);
    if (cache && (ret = dns_cache_verify(cache)) > 0) {
        if (pcache) *pcache = cache;
        if (cb) (*cb)(cbobj, objid, name, len, cache, DNS_ERR_NO_ERROR);
        return 3;
    }
 
    if (!cache || (cache->trymsg > 16 && cache->failmsg * 100 / cache->trymsg >= 95)) {
        /* if the success ratio of connecting to DNS Server is lower than 5% */
        if (pcache) *pcache = cache;
        if (cb) (*cb)(cbobj, objid, name, len, cache, DNS_ERR_SERVER_FAILURE);
        return -20;
    }

    msg = dns_msg_open(mgmt, name, len, cb, cbobj, objid);
    if (!msg) {
        return -100;
    }
 
    msg->cache = cache;

    ret = dns_msg_send(msg, name, len, nsrv);
    if (ret < 0) {
        dns_cache_trymsg_fail(cache);
    }
 
    return ret;
}
 
 
int dns_recv (void * vmgmt, void * pobj)
{
    DnsMgmt       * mgmt = (DnsMgmt *)vmgmt;
    iodev_t       * pdev = (iodev_t *)pobj;
    ep_sockaddr_t   sock;
    uint8           buf[32768];
    int             rcvlen = 0;
    int             ret = 0;
    uint16          msgid = 0;
    DnsMsg        * msg = NULL;
 
    if (!mgmt) return -1;
 
    while (1) {
        ret = epudp_recvfrom(pdev, NULL, buf, sizeof(buf), &sock, &rcvlen);
        if (ret <= 0) return 0;

        memcpy(&msgid, buf, 2);
        msgid = ntohs(msgid);
 
        msg = dns_msg_mgmt_get(mgmt, msgid);
        if (!msg) continue;
 
        if ((ret = dns_msg_decode(msg, buf, rcvlen)) < 0) {
            dns_cache_trymsg_fail(msg->cache);

            tolog(1, "Warning: DnsMsg decode failed, msgid=%u name=%s ret=%d\n", msgid, msg->name, ret);

            msg->rcode = DNS_ERR_FORMAT_ERROR;
            PushDnsCloseEvent(iodev_epump(pdev), msg);
            continue;
        }
 
        PushDnsRecvEvent(iodev_epump(pdev), msg);
    }
 
    return 0;
}

void print_dnspump_arg (void * vobj, int event, int fdtype)
{
#ifdef _DEBUG
    char         buf[256];
 
    buf[0] = '\0';
    sprintf(buf+strlen(buf), "DNS_Pump: ");
 
    if (event == IOE_CONNECTED)        sprintf(buf+strlen(buf), "IOE_CONNECTED");
    else if (event == IOE_CONNFAIL)    sprintf(buf+strlen(buf), "IOE_CONNFAIL");
    else if (event == IOE_ACCEPT)      sprintf(buf+strlen(buf), "IOE_ACCEPT");
    else if (event == IOE_READ)        sprintf(buf+strlen(buf), "IOE_READ");
    else if (event == IOE_WRITE)       sprintf(buf+strlen(buf), "IOE_WRITE");
    else if (event == IOE_TIMEOUT)     sprintf(buf+strlen(buf), "IOE_TIMEOUT");
    else if (event == IOE_INVALID_DEV) sprintf(buf+strlen(buf), "IOE_INVALID_DEV");
    else                               sprintf(buf+strlen(buf), "Unknown");
 
    if (event != IOE_TIMEOUT) {
        sprintf(buf+strlen(buf), " ");
        if (fdtype == FDT_LISTEN)               sprintf(buf+strlen(buf), "FDT_LISTEN");
        else if (fdtype == FDT_CONNECTED)       sprintf(buf+strlen(buf), "FDT_CONNECTED");
        else if (fdtype == FDT_ACCEPTED)        sprintf(buf+strlen(buf), "FDT_ACCEPTED");
        else if (fdtype == FDT_UDPSRV)          sprintf(buf+strlen(buf), "FDT_UDPSRV");
        else if (fdtype == FDT_UDPCLI)          sprintf(buf+strlen(buf), "FDT_UDPCLI");
        else if (fdtype == FDT_RAWSOCK)         sprintf(buf+strlen(buf), "FDT_RAWSOCK");
        else if (fdtype == FDT_TIMER)           sprintf(buf+strlen(buf), "FDT_TIMER");
        else if (fdtype == FDT_LINGER_CLOSE)    sprintf(buf+strlen(buf), "FDT_LINGER_CLOSE");
        else if (fdtype == FDT_STDIN)           sprintf(buf+strlen(buf), "FDT_STDIN");
        else if (fdtype == FDT_STDOUT)          sprintf(buf+strlen(buf), "FDT_STDOUT");
        else if (fdtype == FDT_USOCK_LISTEN)    sprintf(buf+strlen(buf), "FDT_USOCK_LISTEN");
        else if (fdtype == FDT_USOCK_CONNECTED) sprintf(buf+strlen(buf), "FDT_USOCK_CONNECTED");
        else if (fdtype == FDT_USOCK_ACCEPTED)  sprintf(buf+strlen(buf), "FDT_USOCK_ACCEPTED");
        else                                    sprintf(buf+strlen(buf), "Unknown Type");
 
        sprintf(buf+strlen(buf), " FD=%d R<%s:%d> L<%s:%d>",
                 iodev_fd(vobj), iodev_rip(vobj), iodev_rport(vobj),
                 iodev_lip(vobj), iodev_lport(vobj));
    } else {
        if (vobj) {
            sprintf(buf+strlen(buf), " CmdID=%d ID=%lu WID=%lu",
                    iotimer_cmdid(vobj), iotimer_id(vobj), iotimer_workerid(vobj));
        }
    }
    printf("%s\n", buf);
#endif
}

int dns_pump (void * vmgmt, void * pobj, int event, int fdtype)
{
    DnsMgmt * mgmt = (DnsMgmt *)vmgmt;
    DnsMsg  * msg = NULL;
    uint16    msgid = 0;
    int       cmd = 0;
 
    if (!mgmt) return -1;
 
    print_dnspump_arg (pobj, event, fdtype);
    dns_nsrv_print(mgmt);

    if (event == IOE_READ && fdtype == FDT_UDPCLI) {
        return dns_recv(mgmt, pobj);
 
    } else if (event == IOE_TIMEOUT) {
        cmd = iotimer_cmdid(pobj);
 
        if (cmd == t_dns_msg_life) {
            msgid = (uint16)(ulong)iotimer_para(pobj);
            msg = dns_msg_mgmt_get(mgmt, msgid);
            if (msg && (ulong)msg->lifetimer == iotimer_id(pobj)) {
                msg->lifetimer = NULL;
                msg->rcode = DNS_ERR_NO_RESPONSE;
                PushDnsCloseEvent(iotimer_epump(pobj), msg);
            } else
                tolog(1, "Panic: DnsMsgLife timer invalid, msgid=%u msg %s lifetimer=%lu timerid=%lu\n",
                      msgid, msg?"exist":"NULL", msg?(ulong)msg->lifetimer:0, iotimer_id(pobj));
 
        } else if (cmd == t_dns_cache_life) {
            if ((ulong)mgmt->cachetimer == iotimer_id(pobj)) {
                mgmt->cachetimer = NULL;
 
                dns_cache_lifecheck(mgmt);
 
                if (ht_num(mgmt->cache_table) > 0)
                    mgmt->cachetimer = iotimer_start(mgmt->pcore, 30*1000,
                                          t_dns_cache_life, NULL,
                                          dns_pump, mgmt, iotimer_epumpid(pobj));
            }
        }
    }
 
    return 0;
}
 

int dns_query (void * vpcore, char * name, int len, DnsCB * cb, void * cbobj, ulong objid)
{
    epcore_t  * pcore = (epcore_t *)vpcore;

    if (!pcore) return -1;

    return dns_nb_query(pcore->dnsmgmt, name, len, NULL, NULL, cb, cbobj, objid);
}

