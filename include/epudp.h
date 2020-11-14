/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _EPUDP_H_
#define _EPUDP_H_

#ifdef __cplusplus
extern "C" {
#endif

void * epudp_listen_create (void * vpcore, char * localip, int port,
                     void * para, int * retval, IOHandler * cb, void * cbpara);

void * epudp_listen (void * vpcore, char * localip, int port,
                     void * para, int * retval, IOHandler * cb, void * cbpara);

void * epudp_mlisten (void * vpcore, char * localip, int port, void * para,  
                      IOHandler * cb, void * cbpara);

void * epudp_client (void * veps, struct in_addr * localip, int port,
                     void * para, int * retval, IOHandler * cb, void * cbpara);

#ifdef __cplusplus
}
#endif

#endif

