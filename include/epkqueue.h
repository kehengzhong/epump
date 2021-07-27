/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifdef HAVE_KQUEUE

#ifndef _EPKQUEUE_H_
#define _EPKQUEUE_H_

#ifdef __cplusplus
extern "C" {
#endif

int epump_kqueue_init (epump_t * epump, int maxfd);
int epump_kqueue_clean (epump_t * epump);

int epump_kqueue_setpoll (void * vepump, void * vpdev);
int epump_kqueue_clearpoll (void * vepump, void * vpdev);

int epump_kqueue_dispatch (void * veps, btime_t * delay);

#ifdef __cplusplus
}
#endif

#endif

#endif
