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
#include "mthread.h"
#include "memory.h"
#include "arfifo.h"

#include "trace.h"

#include "epcore.h"
#include "epump_local.h"
#include "iotimer.h"
#include "iodev.h"
#include "ioevent.h"
#include "worker.h"

#ifdef HAVE_IOCP
#include "epiocp.h"
#endif


iodev_t * iodev_alloc ()
{
    iodev_t * pdev = NULL;

    pdev = (iodev_t *)kzalloc(sizeof(*pdev));
    if (pdev) {
        iodev_init(pdev);
    }
    return pdev;
}


int iodev_init (void * vdev)
{
    iodev_t * pdev = (iodev_t *)vdev;

    if (!pdev) return -1;

    pdev->res[0] = pdev->res[1] = pdev->res[2] = pdev->res[3] = NULL;

    InitializeCriticalSection(&pdev->fdCS);
    pdev->id = 0;
    pdev->fd = INVALID_SOCKET;
    pdev->fdtype = 0;

    pdev->family = 0;
    pdev->socktype = 0;
    pdev->protocol = 0;

    pdev->para = NULL;
    pdev->callback = NULL;
    pdev->cbpara = NULL;

#ifdef HAVE_OPENSSL
    pdev->sslctx = NULL;
    pdev->ssl = NULL;
#endif

    pdev->local_ip[0] = '\0';
    pdev->local_port = 0;
    pdev->remote_ip[0] = '\0';
    pdev->remote_port = 0;

    pdev->rwflag = 0;
    pdev->iostate = 0;

#ifdef HAVE_IOCP
    pdev->devfifo = NULL;
    pdev->rcvfrm = NULL;
    memset(&pdev->sock, 0, sizeof(pdev->sock));
    pdev->socklen = 0;
    pdev->iocprecv = 0;
    pdev->iocpsend = 0;
#endif

    pdev->threadid = 0;

    pdev->bindtype = 0;
    pdev->tcp_nopush = TCP_NOPUSH_DISABLE;
    pdev->tcp_nodelay = TCP_NODELAY_DISABLE;

    pdev->reuseaddr = 0;
    pdev->reuseport = 0;
    pdev->keepalive = 0;
    pdev->ssl_handshaked = 0;

    pdev->iot = NULL;
    pdev->epump = NULL;

    return 0;
}


int epcore_iodev_free (void * vpdev)
{
    iodev_t * pdev = (iodev_t *)vpdev;
    epcore_t * pcore = NULL;

    if (!pdev) return -1;

    pcore = pdev->epcore;
    if (!pcore) return -2;

    if (iodev_free(pdev) == 0)
        mpool_recycle(pcore->device_pool, pdev);

    return 0;
}

int iodev_free (void * vpdev)
{
    iodev_t * pdev = (iodev_t *)vpdev;

    if (!pdev) return -1;

    if (pdev->iot) {
        iotimer_stop(pdev->epcore, pdev->iot);
        pdev->iot = NULL;
    }

    if (pdev->fd != INVALID_SOCKET) {
        if (pdev->fd <= 0 && pdev->id == 0) {
            /* invoked during unused memory pool recycling */
        } else {
            /* When calling iodev_free, the thread may have exited.
               The epumps object will not be in memory. Therefore, it
               is not necessary to detach from fd-poll.*/

            if (pdev->fdtype == FDT_CONNECTED || pdev->fdtype == FDT_ACCEPTED) {
            #ifdef UNIX
                shutdown(pdev->fd, SHUT_RDWR);
            #endif
            #if defined(_WIN32) || defined(_WIN64)
                shutdown(pdev->fd, 0x01);//SD_SEND);
            #endif
            }
            closesocket(pdev->fd);
        }
    }

    pdev->fd = INVALID_SOCKET;
    pdev->rwflag = 0;
    pdev->fdtype = 0x00;
    pdev->iostate = 0x00;

#ifdef HAVE_IOCP
    if (pdev->devfifo) {
        ar_fifo_free_all(pdev->devfifo, iodev_free);
        pdev->devfifo = NULL;
    }

    if (pdev->rcvfrm) {
        frame_delete(&pdev->rcvfrm);
    }
#endif

    DeleteCriticalSection(&pdev->fdCS);

    return 0;
}

int iodev_cmp_iodev (void * a, void * b )
{
    iodev_t * pdev = (iodev_t *)a;
    iodev_t * patt = (iodev_t *)b;

    if (!pdev || !patt) return 1;

    if (pdev->fd > patt->fd) return 1;
    else if (pdev->fd == patt->fd) return 0;
    return -1;
}

int iodev_cmp_id (void * a, void * b)
{
    iodev_t * pdev = (iodev_t *)a;
    ulong     id = *(ulong *)b;

    if (!a || !b) return -1;

    if (pdev->id == id) return 0;
    if (pdev->id > id) return 1;
    return -1;
}


int iodev_cmp_fd (void * a, void * b)
{
    iodev_t * pdev = (iodev_t *)a;
    SOCKET    fd = (SOCKET)(long)b;

    if (!a) return -1;

    if (pdev->fd == fd) return 0;
    if (pdev->fd > fd) return 1;
    return -1;
}

ulong iodev_hash_fd_func (void * key)
{
    return (ulong)key;
}

ulong iodev_hash_func (void * key)
{
    ulong  id = *(ulong *)key;

    return id;
}


void * iodev_new (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;

    if (pcore == NULL) return NULL;

    pdev = (iodev_t *)mpool_fetch(pcore->device_pool);
    if (!pdev) return NULL;

    InitializeCriticalSection(&pdev->fdCS);
    pdev->fd = INVALID_SOCKET;
    pdev->fdtype = 0;

    pdev->family = 0;
    pdev->socktype = 0;
    pdev->protocol = 0;

    pdev->remote_ip[0] = '\0';
    pdev->local_ip[0] = '\0';
    pdev->remote_port = 0;
    pdev->local_port = 0;

    pdev->rwflag = 0x00;
    pdev->iostate = 0x00;

    pdev->iot = NULL;
    pdev->epump = NULL;
    pdev->epcore = pcore;

    pdev->threadid = 0;

    EnterCriticalSection(&pcore->devicetableCS);

    if (pcore->deviceID < 100)
        pcore->deviceID = 100;
    pdev->id = pcore->deviceID++;

    ht_set(pcore->device_table, &pdev->id, pdev);

    LeaveCriticalSection(&pcore->devicetableCS);

    return pdev;
}


void iodev_closeit (void * vdev)
{
    iodev_close(vdev);
}

void iodev_close_dbg (void * vdev, char * file, int line)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    epcore_t * pcore = NULL;

    if (!pdev) return;

    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return;

    iodev_close_by_dbg(pcore, pdev->id, file, line);
}

void iodev_close_by_dbg (void * vpcore, ulong id, char * file, int line)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
    epump_t  * epump = NULL;

    iodev_t  * iter = NULL;
    int        num = 0;

    if (!pcore) return;

    if ((pdev = epcore_iodev_del(pcore, id)) == NULL) {
        tolog(0, "DevClo: devid=%lu NotFound, Dev:%d DPool=%d/%d Tim:%d TPool=%d/%d %s:%d\n",
              id, ht_num(pcore->device_table), mpool_allocated(pcore->device_pool),
              mpool_consumed(pcore->device_pool), ht_num(pcore->timer_table),
              mpool_allocated(pcore->timer_pool), mpool_consumed(pcore->timer_pool),
              file, line);
        return;
    }

    EnterCriticalSection(&pdev->fdCS);

    /* remove ioevents related to current iodev_t waiting in queue */
    worker_ioevent_remove(worker_thread_find(pcore, get_threadid()), pdev);
    ioevent_remove(pdev->epump, pdev);

    if (pdev->bindtype == BIND_ALL_EPUMP) {
        /* remove from global list for the loading in future-starting threads */
        epcore_global_iodev_del(pcore, pdev);
 
        /* clear from poll list */
        epump_thread_delpoll(pcore, pdev); 
    }

    epump = (epump_t *)pdev->epump;
    if (epump) {
        num = rbtree_num(epump->device_tree);
        iter = epump_iodev_del(epump, pdev->fd);
    }

    if (epump && pdev != iter) {
        epump_iodev_print(epump, 1);
        tolog(0, "DevClo: [%lu %d %s %d %d] Dev:%d/%d/%d DPool=%d/%d Tim:%d/%d TPool=%d/%d "
                 "RM[%lu %d %s %d %d]%s ePump=%lu pdev=%p %s:%d\n",
              pdev->id, pdev->fd, pdev->remote_ip, pdev->fdtype, pdev->bindtype,
              num, epump?rbtree_num(epump->device_tree):-1, ht_num(pcore->device_table),
              mpool_allocated(pcore->device_pool), mpool_consumed(pcore->device_pool),
              epump?rbtree_num(epump->timer_tree):0, ht_num(pcore->timer_table),
              mpool_allocated(pcore->timer_pool), mpool_consumed(pcore->timer_pool),
              iter?iter->id:0, iter?iter->fd:-1, iter?iter->remote_ip:"",
              iter?iter->fdtype:0, iter?iter->bindtype:0, iter==pdev?"Succ":"Fail",
              epump?epump->threadid:0, pdev, file, line);
    }

    pdev->rwflag = 0x00;
    pdev->para = NULL;
    pdev->callback = NULL;
    pdev->cbpara = NULL;
    pdev->iostate = 0x00;

    if (pdev->iot) {
        iotimer_stop(pcore, pdev->iot);
        pdev->iot = NULL;
    }

    if (pdev->fd != INVALID_SOCKET) {
        if (epump && epump->delpoll)
            (*epump->delpoll)(epump, pdev);

        if (pdev->fdtype == FDT_CONNECTED) {    
            struct linger L;
            L.l_onoff = 1; 
            L.l_linger = 0; 
            setsockopt(pdev->fd, SOL_SOCKET, SO_LINGER, (char *) &L, sizeof(L));
         
        #ifdef UNIX 
            shutdown(pdev->fd, SHUT_RDWR);
        #endif  
        #if defined(_WIN32) || defined(_WIN64)
            shutdown(pdev->fd, 0x01);//SD_SEND);
        #endif
        } 

        closesocket(pdev->fd);
        pdev->fd = INVALID_SOCKET;
    }
    LeaveCriticalSection(&pdev->fdCS);

#ifdef HAVE_IOCP
    if (pdev->devfifo) {
        ar_fifo_free_all(pdev->devfifo, iodev_closeit);
        pdev->devfifo = NULL;
    }

    if (pdev->rcvfrm) {
        frame_delete(&pdev->rcvfrm);
    }
#endif

    mpool_recycle(pcore->device_pool, pdev);
}


void iodev_linger_close (void * vdev)
{
    iodev_t  * pdev = (iodev_t *)vdev; 
    epcore_t * pcore = NULL;

    if (!pdev) return;

    if (pdev->fdtype != FDT_ACCEPTED) {
        iodev_close(pdev);
        return;
    }

    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return;

    if (epcore_iodev_find(pcore, pdev->id) == NULL) {
        return;
    }

    EnterCriticalSection(&pdev->fdCS);
    if (pdev->fd != INVALID_SOCKET) {
#ifdef UNIX
        shutdown(pdev->fd, SHUT_WR); //SHUT_RD, SHUT_RDWR
#endif
#if defined(_WIN32) || defined(_WIN64)
        shutdown(pdev->fd, 0x01);//SD_SEND=0x01, SD_RECEIVE=0x00, SD_BOTH=0x02);
#endif
        pdev->fdtype = FDT_LINGER_CLOSE;
    }

    if (pdev->iot) {
        iotimer_stop(pcore, pdev->iot);
        pdev->iot = NULL;
    }
    pdev->iot = iotimer_start(pcore, 2 *1000, IOTCMD_IDLE, pdev, NULL, NULL, iodev_epumpid(pdev));
    LeaveCriticalSection(&pdev->fdCS);
}

void * iodev_new_from_fd (void * vpcore, SOCKET fd, int fdtype, void * para, IOHandler * cb, void * cbpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;

    if (!pcore) return NULL;
    if (fd == INVALID_SOCKET) return NULL;

    pdev = iodev_new(pcore);
    if (!pdev) return NULL;

    pdev->fd = fd;
    pdev->fdtype = fdtype;

    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;

    pdev->iostate = IOS_READWRITE;

    iodev_rwflag_set(pdev, RWF_READ);

    return pdev;
}


int iodev_rwflag_set(void * vpdev, uint8 rwflag)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return -1;
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        return 0;
    }
 
    EnterCriticalSection(&pdev->fdCS);
    pdev->rwflag = rwflag;
    LeaveCriticalSection(&pdev->fdCS);

    return 0;
}


int iodev_set_poll (void * vdev)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    epcore_t * pcore = NULL;
    epump_t  * epump = NULL;

    if (!pdev) return -1;

    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        return -2;
    }

    if (pdev->bindtype == BIND_ALL_EPUMP) { //3
        pcore = (epcore_t *)pdev->epcore;
        if (pcore)
            return epump_thread_setpoll(pcore, pdev);
    } else {
        epump = (epump_t *)pdev->epump;
        if (epump)
            return (*epump->setpoll)(epump, pdev);
    }

    return -100;
}

int iodev_clear_poll (void * vdev)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    epcore_t * pcore = NULL;
    epump_t  * epump = NULL;

    if (!pdev) return -1;

    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        return -2;
    }

    if (pdev->bindtype == BIND_ALL_EPUMP) { //3
        pcore = (epcore_t *)pdev->epcore;
        if (pcore)
            return epump_thread_delpoll(pcore, pdev);
    } else {
        epump = (epump_t *)pdev->epump;
        if (epump)
            return (*epump->delpoll)(epump, pdev);
    }

    return -100;
}

int iodev_add_notify (void * vdev, uint8 rwflag)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    uint8      tmpflag = 0;

    if (!pdev) return -1;

    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        return 0;
    }
 
    if (rwflag == 0) return 0;

    EnterCriticalSection(&pdev->fdCS);
    tmpflag = pdev->rwflag | rwflag;
    if (pdev->rwflag != tmpflag) {
        pdev->rwflag = tmpflag;
    }
    LeaveCriticalSection(&pdev->fdCS);

#ifdef HAVE_IOCP
    if (pdev->fdtype == FDT_ACCEPTED || pdev->fdtype == FDT_CONNECTED) {
        if (rwflag & RWF_READ && pdev->iostate == IOS_READWRITE) {
            iocp_event_recv_post(pdev, NULL, 0);
        } 
        if (rwflag & RWF_WRITE && pdev->iostate == IOS_READWRITE) {
            iocp_event_send_post(pdev, NULL, 0, 0);
        }

    } else if (pdev->fdtype == FDT_UDPSRV || pdev->fdtype == FDT_UDPCLI) {
        if (rwflag & RWF_READ && pdev->iostate == IOS_READWRITE) {
            iocp_event_recvfrom_post(pdev, NULL, 0);
        } 
    }
    return 0;
#else
    return iodev_set_poll(pdev);
#endif
}

int iodev_del_notify (void * vdev, uint8 rwflag)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    uint8      tmpflag = 0;
    int        setpoll = 0;
 
    if (!pdev) return -1;

    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        return 0;
    }
 
    if (rwflag == 0) return 0;

    EnterCriticalSection(&pdev->fdCS);
    tmpflag = pdev->rwflag & ~rwflag;
    if (pdev->rwflag != tmpflag) {
        pdev->rwflag = tmpflag;
        setpoll = 1;
    }
    LeaveCriticalSection(&pdev->fdCS);

    if (!setpoll) return 0;

#ifdef HAVE_IOCP
    return 0;
#else
    return iodev_set_poll(pdev);
#endif
}


int iodev_unbind_epump (void * vdev)
{
    iodev_t  * pdev = (iodev_t *)vdev;
    epump_t  * epump = NULL;
    epcore_t * pcore = NULL;

    if (!pdev) return -1;

    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return -2;

    if (pdev->bindtype == BIND_ALL_EPUMP) {
        /* remove from global list for the loading in future-starting threads */
        epcore_global_iodev_del(pcore, pdev);

        /* clear from poll list */
        epump_thread_delpoll(pcore, pdev);
    }

    epump = (epump_t *)pdev->epump;
    if (epump) {
        epump_iodev_del(epump, pdev->fd);
        if (epump->delpoll)
            (*epump->delpoll)(epump, pdev);
    }

    pdev->bindtype = BIND_NONE;
    pdev->epump = NULL;

    return 0;
}

int iodev_bind_epump (void * vdev, int bindtype, ulong epumpid, int nopoll)
{
#ifdef HAVE_IOCP
    iodev_t   * pdev = (iodev_t *)vdev;
    epcore_t  * pcore = NULL;

    if (!pdev) return -1;

    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return -2;

    if (epumpid > 0)
        pdev->epump = epump_thread_find(pcore, epumpid);

    if (!pdev->epump)
        pdev->epump = epump_thread_select(pdev->epcore);
    pdev->bindtype = BIND_ONE_EPUMP;

    return epump_iocp_setpoll(NULL, pdev);

#else
    iodev_t   * pdev = (iodev_t *)vdev;
    epcore_t  * pcore = NULL;
    epump_t   * epump = NULL;
    worker_t  * wker = NULL;
    epump_t   * curep = NULL;
    ioevent_t * ioe = NULL;
    ulong       threadid = 0;

    if (!pdev) return -1;

    pcore = (epcore_t *)pdev->epcore;
    if (!pcore) return -2;

    /* The purpose of locking fdCS here is to ensure that the release of
       iodev and the bind operation are linear and serial.*/
    EnterCriticalSection(&pdev->fdCS);

    if (pdev->fd == INVALID_SOCKET) {
        LeaveCriticalSection(&pdev->fdCS);
        return -3;
    }

    /* unbind from the previous epump thread */
    iodev_unbind_epump(pdev);

    if (bindtype == BIND_NONE) { //0, do not bind any epump
        LeaveCriticalSection(&pdev->fdCS);
        return 0;
    }

    if (bindtype == BIND_CURRENT_EPUMP) { //5, epump will be system-decided
        pdev->bindtype = BIND_CURRENT_EPUMP; //5

        threadid = get_threadid();
        if (ht_num(pcore->worker_tab) <= 0) {
            epump = epump_thread_find(pcore, threadid);
        } else {
            wker = worker_thread_find(pcore, threadid);
            if (wker && (ioe = wker->curioe)) {
                epumpid = ioe->epumpid;
            } else {
                curep = epump_thread_find(pcore, threadid);
                if (curep && (ioe = curep->curioe)) {
                    epumpid = ioe->epumpid;
                }
            }

            if (epumpid > 0)
                epump = epump_thread_find(pcore, epumpid);
        }

        if (!epump) {
            epump = epump_thread_select(pcore);

            tolog(1, "Panic: dev:[%lu %d %s %d %d] in curePump %lu epumpid=%lu BindTo different epump %lu\n",
                  pdev->id, pdev->fd, pdev->remote_ip, pdev->fdtype, pdev->bindtype,
                  threadid, epumpid, epump?epump->threadid:0);
        }

        if (!epump) {
            /* add to global list for the loading in future-starting threads */
            epcore_global_iodev_add(pcore, pdev);
            LeaveCriticalSection(&pdev->fdCS);
            return 0;
        }

        pdev->epump = epump;

        epump_iodev_add(epump, pdev);
        if (!nopoll) (*epump->setpoll)(epump, pdev);

    } else if (bindtype == BIND_ONE_EPUMP) { //1, epump will be system-decided
        pdev->bindtype = BIND_ONE_EPUMP; //1

        epump = epump_thread_select(pcore);
        if (!epump) {
            /* add to global list for the loading in future-starting threads */
            epcore_global_iodev_add(pcore, pdev);
            LeaveCriticalSection(&pdev->fdCS);
            return 0;
        }

        pdev->epump = epump;

        epump_iodev_add(epump, pdev);
        if (!nopoll) (*epump->setpoll)(epump, pdev);

    } else if (bindtype == BIND_GIVEN_EPUMP) { //2, epump is the para-given
        epump = epump_thread_find(pcore, epumpid);
        if (!epump && epumpid == get_threadid()) {
            wker = worker_thread_find(pcore, epumpid);
            if (wker && (ioe = wker->curioe)) {
                epumpid = ioe->epumpid;
                epump = epump_thread_find(pcore, epumpid);
            }
        }
        if (!epump) epump = epump_thread_select(pcore);

        pdev->bindtype = BIND_GIVEN_EPUMP;  //2
        pdev->epump = epump;

        epump_iodev_add(epump, pdev);
        if (!nopoll) (*epump->setpoll)(epump, pdev);

    } else if (bindtype == BIND_ALL_EPUMP) { //3, all epumps need to be bound
        pdev->bindtype = BIND_ALL_EPUMP;  //3
        pdev->epump = NULL;

        /* add to global list for the loading in future-starting threads */
        epcore_global_iodev_add(pcore, pdev);

        /* add to the global iodev-lists of the threads that have been started, and poll fd */
        epump_thread_setpoll(pcore, pdev);

    } else {
        if (epump) {
            pdev->bindtype = BIND_GIVEN_EPUMP;
            pdev->epump = epump;

        } else {
            pdev->bindtype = BIND_ONE_EPUMP; //1
            epump = epump_thread_select(pcore);
            if (!epump) {
                /* add to global list for the loading in future-starting threads */
                epcore_global_iodev_add(pcore, pdev);
                LeaveCriticalSection(&pdev->fdCS);
                return 0;
            }
        }

        epump_iodev_add(epump, pdev);
        if (!nopoll) (*epump->setpoll)(epump, pdev);
    }

    if (epump && ht_num(pcore->worker_tab) <= 0) {
        pdev->threadid = epump->threadid;
    }

    LeaveCriticalSection(&pdev->fdCS);

    return 1;
#endif
}


ulong iodev_id (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->id;
}

void * iodev_para (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return NULL;

    return pdev->para;
}

void iodev_para_set (void * vpdev, void * para)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return;

    pdev->para = para;
}

void * iodev_epcore (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return NULL;

    return pdev->epcore;
}

void * iodev_epump (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return NULL;

    return pdev->epump;
}

ulong iodev_epumpid (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;
    epump_t  * epump = NULL;

    if (!pdev) return 0;

    epump = (epump_t *)pdev->epump;
    if (epump) return epump->threadid;

    return 0;
}

SOCKET iodev_fd (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return INVALID_SOCKET;

    return pdev->fd;
}

int iodev_fdtype (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->fdtype;
}

int iodev_rwflag (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->rwflag;
}

char * iodev_rip (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return "0.0.0.0";

    return pdev->remote_ip;
}

int iodev_rport (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->remote_port;
}

char * iodev_lip (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return "0.0.0.0";

    return pdev->local_ip;
}

int iodev_lport (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->local_port;
}

ulong iodev_workerid (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return 0;

    return pdev->threadid;
}

void iodev_workerid_set (void * vpdev, ulong workerid)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return;

    if (workerid == 1)
        pdev->threadid = get_threadid();
    else
        pdev->threadid = workerid;
}

int iodev_tcp_nodelay (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return TCP_NODELAY_DISABLE;

    return pdev->tcp_nodelay;
}

int iodev_tcp_nodelay_set (void * vpdev, int value)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return TCP_NODELAY_DISABLE;

    if (pdev->tcp_nodelay != value) {

        if (value == TCP_NODELAY_SET) {
            if (sock_nodelay_set(pdev->fd) >= 0)
                pdev->tcp_nodelay = value;

        } else if (value == TCP_NODELAY_UNSET) {
            if (sock_nodelay_unset(pdev->fd) >= 0)
                pdev->tcp_nodelay = value;
        }
    }

    return pdev->tcp_nodelay;
}

int iodev_tcp_nopush (void * vpdev)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return TCP_NOPUSH_DISABLE;

    return pdev->tcp_nopush;
}

int iodev_tcp_nopush_set (void * vpdev, int value)
{
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!pdev) return TCP_NOPUSH_DISABLE;

    if (pdev->tcp_nopush != value) {

        if (value == TCP_NOPUSH_SET) {
            if (sock_nopush_set(pdev->fd) >= 0)
                pdev->tcp_nopush = value;

        } else if (value == TCP_NOPUSH_UNSET) {
            if (sock_nopush_unset(pdev->fd) >= 0)
                pdev->tcp_nopush = value;
        }
    }

    return pdev->tcp_nopush;
}



int iodev_print (void * vpcore)
{
    epcore_t   * pcore = (epcore_t *) vpcore;
    iodev_t    * pdev = NULL;
    int          i, num;
    char         buf[256];

    if (!pcore) return -1;

#ifdef _DEBUG
    printf("\n-------------------------------------------------------------\n");
#endif

    EnterCriticalSection(&pcore->devicetableCS);

    num = ht_num(pcore->device_table);

    for (i = 0; i < num; i++) {

        pdev = ht_value(pcore->device_table, i);
        if (!pdev) continue;

        buf[0] = '\0';

        sprintf(buf+strlen(buf), "ID=%lu FD=%d ", pdev->id, pdev->fd);

        switch (pdev->fdtype) {
        case FDT_LISTEN:          sprintf(buf+strlen(buf), "TCP LISTEN");      break;
        case FDT_CONNECTED:       sprintf(buf+strlen(buf), "TCP CONNECTED");   break;
        case FDT_ACCEPTED:        sprintf(buf+strlen(buf), "TCP ACCEPTED");    break;
        case FDT_UDPSRV:          sprintf(buf+strlen(buf), "UDP LISTEN");      break;
        case FDT_UDPCLI:          sprintf(buf+strlen(buf), "UDP CLIENT");      break;
        case FDT_RAWSOCK:         sprintf(buf+strlen(buf), "RAW SOCKET");      break;
        case FDT_TIMER:           sprintf(buf+strlen(buf), "TIMER");           break;
        case FDT_USERCMD:         sprintf(buf+strlen(buf), "USER CMD");        break;
        case FDT_LINGER_CLOSE:    sprintf(buf+strlen(buf), "TCP LINGER");      break;
        case FDT_STDIN:           sprintf(buf+strlen(buf), "STDIN");           break;
        case FDT_STDOUT:          sprintf(buf+strlen(buf), "STDOUT");          break;
        case FDT_USOCK_LISTEN:    sprintf(buf+strlen(buf), "USOCK LISTEN");    break;
        case FDT_USOCK_CONNECTED: sprintf(buf+strlen(buf), "USOCK CONNECTED"); break;
        case FDT_USOCK_ACCEPTED:  sprintf(buf+strlen(buf), "USOCK ACCEPTED");  break;
        default:                  sprintf(buf+strlen(buf), "Unknown");         break;
        }

        sprintf(buf+strlen(buf), " Local<%s:%d>", pdev->local_ip, pdev->local_port);
        sprintf(buf, " Remote<%s:%d>", pdev->remote_ip, pdev->remote_port);

        printf("%s\n", buf);
    }

    LeaveCriticalSection(&pcore->devicetableCS);

#ifdef _DEBUG
    printf("-------------------------------------------------------------\n\n");
#endif

    return 0;
}

