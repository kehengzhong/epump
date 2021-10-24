/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _EPWAKEUP_H_
#define _EPWAKEUP_H_

#ifdef __cplusplus      
extern "C" {           
#endif

int epcore_wakeup_init (void * vpcore);
int epcore_wakeup_clean (void * vpcore);

int epcore_wakeup_send (void * vpcore);
int epcore_wakeup_recv (void * vpcore);

int epump_wakeup_init  (void * vepump);
int epump_wakeup_clean (void * vepump);
int epump_wakeup_send  (void * vepump);
int epump_wakeup_recv  (void * vepump);

#ifdef __cplusplus
}   
#endif

#endif

