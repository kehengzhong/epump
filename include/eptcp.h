/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _EPTCP_H_
#define _EPTCP_H_

#ifdef __cplusplus
extern "C" {
#endif

void * eptcp_listen_create (void * vpcore, char * localip, int port,
                            void * para, int * retval,
                            IOHandler * cb, void * cbpara,
                            iodev_t ** devlist, int * devnum);

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

#ifdef __cplusplus
}
#endif

#endif


