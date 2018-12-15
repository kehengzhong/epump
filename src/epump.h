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

#ifndef _EVENT_PUMP_H_
#define _EVENT_PUMP_H_
 
typedef unsigned long       ulong;
typedef unsigned short int  uint16;
typedef unsigned char       uint8;

#ifndef SOCKET
#define SOCKET int
#endif

typedef int IOHandler (void * vmgmt, void * pobj, int event, int fdtype);
typedef int GeneralCB (void * vpara, int status);


/* bind type specifying how iodev_t devices are bound to the underlying worker thread */
#define BIND_SYSTEM_DECIDED  1
#define BIND_GIVEN_EPUMP     2
#define BIND_ALL_EPUMP       3


/* define the event type, including getting connected, connection accepted, readable,
 * writable, timeout. the working threads will be driven by these events */
#define IOE_CONNECTED        1
#define IOE_CONNFAIL         2
#define IOE_ACCEPT           3
#define IOE_READ             4
#define IOE_WRITE            5
#define IOE_INVALID_DEV      6
#define IOE_TIMEOUT          100
#define IOE_USER_DEFINED     10000


#ifdef __cplusplus
extern "C" {
#endif
 

struct EPCore_;
typedef struct EPCore_ epcore_t;

void * epcore_new (int maxfd);
void   epcore_clean (void * vpcore);
void   epcore_start (void * vpcore, int maxnum);
void   epcore_stop (void * vpcore);
int    epcore_set_callback (void * vpcore, void * cb, void * cbpara);
void * epcore_thread_self (void * vpcore);
void * epcore_thread_select (void * vpcore);
void   epcore_print (void * vpcore);
 

struct EPump_ ;
typedef struct EPump_ epump_t;

void * epump_new (epcore_t * epcore);
void   epump_free (void * vepump);
int    epump_objnum (void * veps, int type);
int    epump_main_start (void * vpcore, int forkone);
//int  epump_worker_run (void * veps);

ulong  epumpid (void * veps);


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
#define FDT_HWARE             0x200
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
 
int      iodev_rwflag_set(void * vpdev, uint8 rwflag);
int      iodev_bind_epump (void * vpdev, int bindtype, void * veps);
 
void   * iodev_para (void * vpdev);
void   * iodev_epcore (void * vpdev);
void   * iodev_epump (void * vpdev);
SOCKET   iodev_fd (void * vpdev);
int      iodev_fdtype (void * vpdev);
int      iodev_rwflag (void * vpdev);
char   * iodev_rip (void * vpdev);
char   * iodev_lip (void * vpdev);
int      iodev_rport (void * vpdev);
int      iodev_lport (void * vpdev);
int      iodev_print (void * vpcore);
 

 
void * iotimer_start (void * vpcore, void * veps, int ms, int cmdid, void * para,
                      IOHandler * cb, void * cbpara);
int    iotimer_stop  (void * viot);
int    iotimer_cmdid (void * viot);
void * iotimer_para (void * viot);
void * iotimer_epump (void * viot);
 

void * eptcp_listen (void * vpcore, uint16 port, void * para, int * retval,
                     IOHandler * cb, void * cbpara);
 
void * eptcp_accept (void * vpcore, void * vld, void * para, int * retval,
                     IOHandler * cb, void * cbpara);
 
void * eptcp_connect (void * vpcore, char * host, int port,
                      char * localip, int localport, void * para,
                      int * retval, IOHandler * cb, void * cbpara);


void * epudp_listen (void * vpcore, char * localip, uint16 port,
                     void * para, int * retval, IOHandler * cb, void * cbpara);
 
void * epudp_client (void * vpcore, char * localip, uint16 port,
                     void * para, int * retval, IOHandler * cb, void * cbpara);


void * epusock_connect (void * vpcore, char * sockname, void * para,
                        int * retval, IOHandler * ioh, void * iohpara);
 
void * epusock_listen (void * vpcore, char * sockname, void * para, int * retval,
                       IOHandler * cb, void * cbpara);
 
void * epusock_accept (void * vpcore, void * vld, void * para, int * retval,
                       IOHandler * cb, void * cbpara);


void * ephware_bind_fd (void * vpcore, int fd, void * para, IOHandler * ioh, void * iohpara);
void * ephware_bind_stdin (void * vpcore, void * para, IOHandler * ioh, void * iohpara);


#ifdef __cplusplus
}
#endif
 
#endif

