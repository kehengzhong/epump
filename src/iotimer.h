/*
 * Copyright (c) 2003-2018 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _IOTIMER_H_
#define _IOTIMER_H_

#include "epm_util.h"


#ifdef __cplusplus
extern "C" {
#endif

#define IOTCMD_IDLE  1

#pragma pack(push ,1)

typedef struct IOTimer_ {
    void       * res[2];

    int          cmdid;
    ulong        id;
    void       * para;
    epm_time_t   bintime;

    IOHandler  * callback;
    void       * cbpara;

    void       * epump;
    void       * epcore;

} iotimer_t, *iotimer_p;

#pragma pack(pop)


void iotimer_void_free(void * vtimer);
int  iotimer_free(void * vtimer);

int  iotimer_cmp_iotimer(void * a, void * b);
int  iotimer_cmp_id (void * a, void * b);
ulong iotimer_hash_func (void * key);

iotimer_p iotimer_fetch (void * vpcore);
int       iotimer_recycle (void * viot);

void * iotimer_start (void * vpcore, void * veps, int ms, int cmdid, void * para, 
                              IOHandler * cb, void * cbpara);
int    iotimer_stop  (void * viot);


/* return value: if ret <=0, that indicates has no timer in
 * queue, system should set blocktime infinite,
   if ret >0, then pdiff carries the next timeout value */
int iotimer_check_timeout(void * vepump, epm_time_t * pdiff, int * pevnum);


int    iotimer_cmdid (void * viot);
void * iotimer_para (void * viot);
void * iotimer_epump (void * viot);


#ifdef __cplusplus
}
#endif

#endif

