/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _EVENT_PUMP_H_
#define _EVENT_PUMP_H_
 
#include "btype.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int IOHandler (void * vmgmt, void * pobj, int event, int fdtype);
typedef int GeneralCB (void * vpara, int status);


/* bind type specifying how iodev_t devices are bound to the underlying epump thread */
#define BIND_NONE                0
#define BIND_ONE_EPUMP           1
#define BIND_GIVEN_EPUMP         2
#define BIND_ALL_EPUMP           3
#define BIND_NEW_FOR_EPUMP       4
#define BIND_CURRENT_EPUMP       5


/* define the event type, including getting connected, connection accepted, readable,
 * writable, timeout. the working threads will be driven by these events */
#define IOE_CONNECTED        1
#define IOE_CONNFAIL         2
#define IOE_ACCEPT           3
#define IOE_READ             4
#define IOE_WRITE            5
#define IOE_INVALID_DEV      6
#define IOE_TIMEOUT          100
#define IOE_DNS_RECV         200
#define IOE_USER_DEFINED     10000

#define RWF_READ             0x02
#define RWF_WRITE            0x04

#define TCP_NOPUSH_UNSET      0
#define TCP_NOPUSH_SET        1
#define TCP_NOPUSH_DISABLE    2

#define TCP_NODELAY_UNSET     0
#define TCP_NODELAY_SET       1
#define TCP_NODELAY_DISABLE   2

 
struct EPCore_;
typedef struct EPCore_ epcore_t;

void * epcore_new (int maxfd, int dispmode);
void   epcore_clean (void * vpcore);
int    epcore_dnsrv_add (void * vpcore, char * nsip, int port);
int    epcore_set_callback (void * vpcore, void * cb, void * cbpara);

void   epcore_start_epump (void * vpcore, int maxnum);
void   epcore_stop_epump (void * vpcore);
void * epump_thread_self (void * vpcore);
void * epump_thread_select (void * vpcore);

void   epcore_start_worker (void * vpcore, int maxnum);
void   epcore_stop_worker (void * vpcore);
void * worker_thread_find (void * vpcore, ulong threadid);
void * worker_thread_self (void * vpcore);
void * worker_thread_select (void * vpcore);

void   epcore_print (void * vpcore);
 

struct EPump_ ;
typedef struct EPump_ epump_t;

void * epump_new (epcore_t * epcore);
void   epump_free (void * vepump);
ulong  epumpid (void * veps);

int    epump_objnum (void * veps, int type);
int    epump_main_start (void * vpcore, int forkone);
void   epump_main_stop (void * vepump);


struct Worker_s;
typedef struct Worker_s worker_t;

void * worker_new (epcore_t * epcore);
void   worker_free (void * vwker);
ulong  workerid (void * vwker);
 
/* worker load is calculated by 2 factors:
   the first one is the ioevent number pending in waiting-queue, the weigth
   is about 70%.  the second one is working time ratio, the weight is 30%. */
int worker_real_load (void * vwker);
 
void worker_perf (void * vwker, ulong * acctime,
                  ulong * idletime, ulong * worktime, ulong * eventnum);

int  worker_main_start (void * vpcore, int forkone);
void worker_main_stop (void * vwker);


/* the definition of FD type in the EventPump device */
#define FDT_LISTEN            0x01
#define FDT_CONNECTED         0x02
#define FDT_ACCEPTED          0x04
#define FDT_UDPSRV            0x08
#define FDT_UDPCLI            0x10
#define FDT_USOCK_LISTEN      0x20
#define FDT_USOCK_CONNECTED   0x40
#define FDT_USOCK_ACCEPTED    0x80
#define FDT_RAWSOCK           0x100
#define FDT_FILEDEV           0x200
#define FDT_TIMER             0x10000
#define FDT_USERCMD           0x20000
#define FDT_LINGER_CLOSE      0x40000
#define FDT_STDIN             0x100000
#define FDT_STDOUT            0x200000
 
/* as the basic structure of EP, epdevice generates read/write
 * events as hardware does. it wraps the file-descriptor of device.  */

struct IODevice_;
typedef struct IODevice_ iodev_t;
 
void   * iodev_new  (void * vpcore);
void   * iodev_new_from_fd (void * vpcore, SOCKET fd, int fdtype,
                             void * para, IOHandler * cb, void * cbpara);
void     iodev_close(void * vpdev);
void     iodev_linger_close(void * vpdev);
 
int      iodev_rwflag_set (void * vpdev, uint8 rwflag);
int      iodev_add_notify (void * vdev, uint8 rwflag);
int      iodev_del_notify (void * vdev, uint8 rwflag);

int      iodev_unbind_epump (void * vdev);
int      iodev_bind_epump   (void * vpdev, int bindtype, void * veps);
 
ulong    iodev_id (void * vpdev);
void   * iodev_para (void * vpdev);
void     iodev_para_set (void * vpdev, void * para);
void   * iodev_epcore (void * vpdev);
void   * iodev_epump (void * vpdev);
SOCKET   iodev_fd (void * vpdev);
int      iodev_fdtype (void * vpdev);
int      iodev_rwflag (void * vpdev);
char   * iodev_rip (void * vpdev);
char   * iodev_lip (void * vpdev);
int      iodev_rport (void * vpdev);
int      iodev_lport (void * vpdev);

ulong    iodev_workerid     (void * vpdev);
void     iodev_workerid_set (void * vpdev, ulong workerid);

int      iodev_tcp_nodelay     (void * vpdev);
int      iodev_tcp_nodelay_set (void * vpdev, int value);
int      iodev_tcp_nopush      (void * vpdev);
int      iodev_tcp_nopush_set  (void * vpdev, int value);

int      iodev_print (void * vpcore);
 

 
void * iotimer_start (void * vpcore, int ms, int cmdid, void * para,
                      IOHandler * cb, void * cbpara);
int    iotimer_stop  (void * viot);
ulong  iotimer_id (void * viot);
int    iotimer_cmdid (void * viot);
void * iotimer_para (void * viot);
void * iotimer_epump (void * viot);

ulong  iotimer_workerid     (void * viot);
void   iotimer_workerid_set (void * viot, ulong workerid);
 

void * mlisten_open  (void * epcore,  char * localip, int port, int fdtype,
                      void * para, IOHandler * cb, void * cbpara);
int    mlisten_close (void * vmln);

int    mlisten_port  (void * vmln);
char * mlisten_lip   (void * vmln);


void * eptcp_listen (void * vpcore, char * localip, int port, void * para, int * retval,
                     IOHandler * cb, void * cbpara, int bindtype,
                     void ** plist, int * listnum);
 
/* Note: automatically detect if Linux kernel supported REUSEPORT. 
   if supported, create listen socket for every current running epump threads
   and future-started epump threads.
   if not, create only one listen socket for all epump threads to bind. */

void * eptcp_mlisten (void * vpcore, char * localip, int port, void * para,
                      IOHandler * cb, void * cbpara);

void * eptcp_accept (void * vpcore, void * vld, void * para, int * retval,
                     IOHandler * cb, void * cbpara, int bindtype);
 
void * eptcp_connect (void * vpcore, char * ip, int port,
                      char * localip, int localport, void * para,
                      int * retval, IOHandler * cb, void * cbpara);

void * eptcp_nb_connect (void * vpcore, char * host, int port,
                         char * localip, int localport, void * para,
                         int * retval, IOHandler * cb, void * cbpara);


void * epudp_listen (void * vpcore, char * localip, int port, void * para, int * pret,
                     IOHandler * cb, void * cbpara, int bindtype, void ** plist, int * listnum);
 
void * epudp_mlisten (void * vpcore, char * localip, int port, void * para,
                      IOHandler * cb, void * cbpara);
 
void * epudp_client (void * vpcore, char * localip, int port, 
                     void * para, int * retval, IOHandler * cb, void * cbpara,
                     iodev_t ** devlist, int * devnum);

int    epudp_recvfrom (void * vdev, void * vfrm, void * addr, int * pnum);


void * epusock_connect (void * vpcore, char * sockname, void * para,
                        int * retval, IOHandler * ioh, void * iohpara);
 
void * epusock_listen (void * vpcore, char * sockname, void * para, int * retval,
                       IOHandler * cb, void * cbpara);
 
void * epusock_accept (void * vpcore, void * vld, void * para, int * retval,
                       IOHandler * cb, void * cbpara, int bindtype);


void * epfile_bind_fd (void * vpcore, int fd, void * para, IOHandler * ioh, void * iohpara);
void * epfile_bind_stdin (void * vpcore, void * para, IOHandler * ioh, void * iohpara);


/* DNS resolving functions */

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

int    dns_query (void * vpcore, char * name, int len, DnsCB * cb, void * cbobj);

int    dns_cache_num      (void * vcache);
int    dns_cache_getip    (void * vcache, int ind, char * iplist, int len);
int    dns_cache_getiplist(void * vcache, char ** iplist, int listnum);
int    dns_cache_sockaddr (void * vcache, int index, int port, ep_sockaddr_t * addr);
int    dns_cache_a_num    (void * vcache);


#ifdef __cplusplus
}
#endif
 
#endif

