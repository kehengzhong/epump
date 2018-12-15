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

#ifndef _IOEVENT_H_
#define _IOEVENT_H_

#ifdef __cplusplus
extern "C" {
#endif


/* define the event type, including getting connected, connection accepted, readable,
 * writable, timeout. the working threads will be driven by these events */
#define IOE_CONNECTED        1
#define IOE_CONNFAIL         2
#define IOE_ACCEPT           3
#define IOE_READ             4
#define IOE_WRITE            5
#define IOE_INVALID_DEV      6
#define IOE_TIMEOUT          100
#define IOE_USER_DEFINED     10000


typedef struct IOEvent_ {
    void      * res[2];

    uint8       externflag;  //0-general event  1-extern event 2-user generated event

    int         type;
    void      * obj;
    ulong       objid;
    void      * callback;
    void      * cbpara;

    time_t      stamp;

    void      * ignitor;
    void      * igpara;
    int         ignresult;
} ioevent_t, *ioevent_p;



int ioevent_free (void * vioe);

int ioevent_push (void * vepump, int event, void * obj, void * cb, void * cbpara);
ioevent_t * ioevent_pop (void * vepump);

int ioevent_handle (void * vepump);

void ioevent_print (void * vioe, char * title);

#define PushConnectedEvent(epump, obj) ioevent_push((epump), IOE_CONNECTED, (obj), NULL, NULL)
#define PushConnfailEvent(epump, obj) ioevent_push((epump), IOE_CONNFAIL, (obj), NULL, NULL)
#define PushConnAcceptEvent(epump, obj) ioevent_push((epump), IOE_ACCEPT, (obj), NULL, NULL)
#define PushReadableEvent(epump, obj) ioevent_push((epump), IOE_READ, (obj), NULL, NULL)
#define PushWritableEvent(epump, obj) ioevent_push((epump), IOE_WRITE, (obj), NULL, NULL)
#define PushTimeoutEvent(epump, obj) ioevent_push((epump), IOE_TIMEOUT, (obj), NULL, NULL)
#define PushInvalidDevEvent(epump, obj) ioevent_push((epump), IOE_INVALID_DEV, (obj), NULL, NULL)

#define PushUserEvent (epump, obj, cb, para) \
          ioevent_push((epump), IOE_USER_DEFINED, (obj), (cb), (para))


#ifdef __cplusplus   
}   
#endif

#endif

