/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _EPSELECT_H_
#define _EPSELECT_H_

#ifdef __cplusplus
extern "C" {
#endif

int epump_select_init (epump_t * epump);
int epump_select_clean (epump_t * epump);

int epump_select_setpoll (void * vepump, void * vpdev);
int epump_select_clearpoll (void * vepump, void * vpdev);

int epump_select_dispatch (void * veps, btime_t * delay);

#ifdef __cplusplus
}
#endif

#endif


