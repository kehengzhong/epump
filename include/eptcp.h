/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _EPTCP_H_
#define _EPTCP_H_

#ifdef __cplusplus
extern "C" {
#endif

void * eptcp_listen_create (void * vpcore, char * localip, int port,
                            void * para, int * retval,
                            IOHandler * cb, void * cbpara);

void * eptcp_listen (void * vpcore, char * localip, int port, void * para, int * retval,
                     IOHandler * cb, void * cbpara, int bindtype);

/* Note: only supported in Linux kernel with version >= 3.9.x
   create listen socket for every current running epump threads and 
   future-started epump threads */
void * eptcp_mlisten (void * vpcore, char * localip, int port, void * para,
                      IOHandler * cb, void * cbpara);

void * eptcp_accept (void * vpcore, void * vld, void * para, int * retval,
                     IOHandler * cb, void * cbpara, int bindtype);

void * eptcp_connect (void * vpcore, struct in_addr ip, int port,
                      char * localip, int localport, void * para,
                      int * retval, IOHandler * cb, void * cbpara);

#ifdef __cplusplus
}
#endif

#endif

