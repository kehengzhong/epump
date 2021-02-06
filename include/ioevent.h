/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
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
#define IOE_DNS_RECV         200
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

    ulong       epumpid;
    ulong       workerid;

    void      * ignitor;
    void      * igpara;
    int         ignresult;
} ioevent_t, *ioevent_p;


int    ioevent_free (void * vioe);

int    ioevent_dispatch (void * vepump, void * vioe);

int    ioevent_push (void * vepump, int event, void * obj, void * cb, void * cbpara);
void * ioevent_pop  (void * vepump);
int    ioevent_remove (void * vepump, void * obj);

void * ioevent_execute (void * vpcore, void * vioe);

int    ioevent_handle (void * vepump);

void   ioevent_print (void * vioe, char * title);

#define PushConnectedEvent(epump, obj)   ioevent_push((epump), IOE_CONNECTED, (obj), NULL, NULL)
#define PushConnfailEvent(epump, obj)    ioevent_push((epump), IOE_CONNFAIL, (obj), NULL, NULL)
#define PushConnAcceptEvent(epump, obj)  ioevent_push((epump), IOE_ACCEPT, (obj), NULL, NULL)
#define PushReadableEvent(epump, obj)    ioevent_push((epump), IOE_READ, (obj), NULL, NULL)
#define PushWritableEvent(epump, obj)    ioevent_push((epump), IOE_WRITE, (obj), NULL, NULL)
#define PushTimeoutEvent(epump, obj)     ioevent_push((epump), IOE_TIMEOUT, (obj), NULL, NULL)
#define PushInvalidDevEvent(epump, obj)  ioevent_push((epump), IOE_INVALID_DEV, (obj), NULL, NULL)
#define PushDnsRecvEvent(epump, obj)     ioevent_push((epump), IOE_DNS_RECV, (obj), NULL, NULL)

#define PushUserEvent (epump, obj, cb, para) \
          ioevent_push((epump), IOE_USER_DEFINED, (obj), (cb), (para))

#ifdef __cplusplus   
}   
#endif

#endif

