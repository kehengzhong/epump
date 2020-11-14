/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifdef HAVE_EPOLL

#ifndef _EPEPOLL_H_
#define _EPEPOLL_H_

#ifdef __cplusplus
extern "C" {
#endif


int epump_epoll_init (epump_t * epump, int maxfd);
int epump_epoll_clean (epump_t * epump);

int epump_epoll_setpoll (void * vepump, void * vpdev);
int epump_epoll_clearpoll (void * vepump, void * vpdev);

int epump_epoll_dispatch (void * veps, btime_t * delay);


#ifdef __cplusplus
}
#endif

#endif

#endif

