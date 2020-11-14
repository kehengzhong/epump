/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _IOTIMER_H_
#define _IOTIMER_H_

#include "btype.h"
#include "btime.h"


#ifdef __cplusplus
extern "C" {
#endif

#define IOTCMD_IDLE  1

#pragma pack(push ,1)

typedef struct IOTimer_ {
    void       * res[4];

    int          cmdid;
    ulong        id;
    void       * para;
    btime_t      bintime;

    IOHandler  * callback;
    void       * cbpara;

    void       * epump;
    void       * epcore;

    ulong        threadid;

} iotimer_t, *iotimer_p;

#pragma pack(pop)


void iotimer_void_free(void * vtimer);
int  iotimer_free(void * vtimer);

int  iotimer_cmp_iotimer(void * a, void * b);
int  iotimer_cmp_id (void * a, void * b);
ulong iotimer_hash_func (void * key);

iotimer_p iotimer_fetch (void * vpcore);
int       iotimer_recycle (void * viot);

void * iotimer_start (void * vpcore, int ms, int cmdid, void * para, 
                              IOHandler * cb, void * cbpara);
int    iotimer_stop  (void * viot);


/* return value: if ret <=0, that indicates has no timer in
 * queue, system should set blocktime infinite,
   if ret >0, then pdiff carries the next timeout value */
int iotimer_check_timeout(void * vepump, btime_t * pdiff, int * pevnum);


ulong  iotimer_id (void * viot);
int    iotimer_cmdid (void * viot);
void * iotimer_para (void * viot);
void * iotimer_epump (void * viot);

ulong  iotimer_workerid     (void * viot);
void   iotimer_workerid_set (void * viot, ulong workerid);

#ifdef __cplusplus
}
#endif

#endif

