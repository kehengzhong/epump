/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _EPUDP_H_
#define _EPUDP_H_

#ifdef __cplusplus
extern "C" {
#endif

void * epudp_listen_create (void * vpcore, char * localip, int port, void * para,
                            int * retval, IOHandler * cb, void * cbpara,
                            iodev_t ** devlist, int * devnum);

void * epudp_listen (void * vpcore, char * localip, int port, void * para, int * pret,
                     IOHandler * cb, void * cbpara, int bindtype, void ** plist, int * listnum);

void * epudp_mlisten (void * vpcore, char * localip, int port, void * para,  
                      IOHandler * cb, void * cbpara);

void * epudp_client (void * vpcore, char * localip, int port,
                     void * para, int * retval, IOHandler * cb, void * cbpara,
                     iodev_t ** devlist, int * devnum);

int epudp_recvfrom (void * vdev, void * vfrm, void * addr, int * pnum);

#ifdef __cplusplus
}
#endif

#endif


