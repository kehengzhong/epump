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

#ifndef _IODEV_H_
#define _IODEV_H_

#include "btype.h"
#include "tsock.h"
#include "mthread.h"
#include "frame.h"

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
//#include <openssl/x509.h>
//#include <openssl/x509v3.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RWF_READ  0x02
#define RWF_WRITE 0x04

/* the state of device I/O */
#define IOS_CONNECTING   0x01
#define IOS_ACCEPTING    0x02
#define IOS_READWRITE    0x04
#define IOS_RESOLVING    0x08

/* bind type specifying how iodev_t devices are bound to the underlying epump thread */
#define BIND_NONE                0
#define BIND_ONE_EPUMP           1
#define BIND_GIVEN_EPUMP         2
#define BIND_ALL_EPUMP           3
#define BIND_NEW_FOR_EPUMP       4
#define BIND_CURRENT_EPUMP       5


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

#define TCP_NOPUSH_UNSET      0
#define TCP_NOPUSH_SET        1
#define TCP_NOPUSH_DISABLE    2

#define TCP_NODELAY_UNSET     0
#define TCP_NODELAY_SET       1
#define TCP_NODELAY_DISABLE   2

/* as the basic structure of EP, epdevice generates read/write
 * events as hardware does. it wraps the file-descriptor of device.  */

typedef struct IODevice_ {
    void      * res[4];

    CRITICAL_SECTION fdCS;

    ulong       id;
    SOCKET      fd;
    int         fdtype; 

    int         family;
    int         socktype;
    int         protocol;

    void      * para;
    IOHandler * callback;
    void      * cbpara;

#ifdef HAVE_OPENSSL
    SSL_CTX   * sslctx;
    SSL       * ssl;
#endif

    char        local_ip[41];
    uint16      local_port;
    char        remote_ip[41];
    uint16      remote_port;

    uint8       rwflag;
    uint8       iostate;

#ifdef HAVE_IOCP
    void          * devfifo;
    frame_t       * rcvfrm;
    ep_sockaddr_t   sock;
    int             socklen;
    int             iocprecv;
    int             iocpsend;
#endif

    /* worker thread id */
    ulong       threadid;

    unsigned    bindtype:8;  //1-system-decided 2-caller-given 3-all epumps

    unsigned    tcp_nodelay:2;
    unsigned    tcp_nopush:2;

    unsigned    reuseaddr:1;
    unsigned    reuseport:1;
    unsigned    keepalive:1;

    unsigned    ssl_handshaked:1;

    void      * iot;

    void      * epump;
    void      * epcore;

} iodev_t, *iodev_p;


iodev_t * iodev_alloc ();
int       iodev_init (void * vpdev);
int       epcore_iodev_free (void * vpdev);
int       iodev_free (void * vpdev);

int       iodev_cmp_iodev (void * a, void * b);
int       iodev_cmp_id (void * a, void * b);
int       iodev_cmp_fd (void * a, void * b);
ulong     iodev_hash_fd_func (void * key);
ulong     iodev_hash_func (void * key);


void * iodev_new  (void * vpcore);

void     iodev_closeit (void * vdev);
#define  iodev_close(pdev) iodev_close_dbg((pdev), __FILE__, __LINE__)
void     iodev_close_dbg(void * vpdev, char * file, int line);

#define  iodev_close_by(pcore, id) iodev_close_by_dbg((pcore), (id), __FILE__, __LINE__)
void     iodev_close_by_dbg (void * vpcore, ulong id, char * file, int line);

void     iodev_linger_close(void * vpdev);

void   * iodev_new_from_fd (void * vpcore, SOCKET fd, int fdtype, 
                             void * para, IOHandler * cb, void * cbpara);

int      iodev_rwflag_set(void * vpdev, uint8 rwflag);

int      iodev_set_poll   (void * vdev);
int      iodev_clear_poll (void * vdev);

int      iodev_add_notify (void * vpdev, uint8 rwflag);
int      iodev_del_notify (void * vpdev, uint8 rwflag);

int      iodev_unbind_epump (void * vdev);
int      iodev_bind_epump   (void * vpdev, int bindtype, ulong epumpid, int nopoll);

ulong    iodev_id (void * vpdev);
void   * iodev_para (void * vpdev);
void     iodev_para_set (void * vpdev, void * para);
void   * iodev_epcore (void * vpdev);
void   * iodev_epump (void * vpdev);
ulong    iodev_epumpid (void * vpdev);
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


int iodev_print (void * vpcore);

#ifdef __cplusplus
}
#endif

#endif

