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

#include "epm_util.h"
#include "epm_arr.h"


#ifdef _MEMDBG
 
CRITICAL_SECTION  epm_memCS;
uint64            epm_memid = 0;
arr_t           * epm_mem_list = NULL;
uint8             epm_mem_init = 0;
 
uint16  mflag = 0xb05a;
 
#pragma pack(1)
typedef struct epm_memhdr_ {
    uint64          memid;
    uint32          size;
    uint16          kflag;
    int             line;
    char            file[32];
    uint16          reallocate;
} EpmMemHdr;
#pragma pack()
 
uint64 getmemid() {
    uint64 id = 0;
    EnterCriticalSection(&epm_memCS);
    id = epm_memid++;
    LeaveCriticalSection(&epm_memCS);
    return id;
}
 
int epm_mem_cmp_epm_mem_by_id (void * a, void * b) {
    EpmMemHdr * mema = (EpmMemHdr *)a;
    EpmMemHdr * memb = (EpmMemHdr *)b;
 
    if (!a) return -1;
    if (!b) return 1;
 
    if (mema->memid > memb->memid) return 1;
    if (mema->memid < memb->memid) return -1;
    return 0;
}
int epm_mem_add(void * ptr) {
    EpmMemHdr * hdr = (EpmMemHdr *)ptr;
    if (epm_mem_init == 0) {
        epm_mem_init = 1;
        InitializeCriticalSection(&epm_memCS);
        epm_mem_list = epm_arr_new(8*1024*1024);
        epm_memid = 0;
    }
    if (!ptr) return -1;
    EnterCriticalSection(&epm_memCS);
    epm_arr_insert_by(epm_mem_list, hdr, epm_mem_cmp_epm_mem_by_id);
    LeaveCriticalSection(&epm_memCS);
    return 0;
}
int epm_mem_del(void * ptr) {
    EpmMemHdr * hdr = (EpmMemHdr *)ptr;
    if (!ptr) return -1;
    EnterCriticalSection(&epm_memCS);
    epm_arr_delete_by(epm_mem_list, hdr, epm_mem_cmp_epm_mem_by_id);
    LeaveCriticalSection(&epm_memCS);
    return 0;
}
void epm_mem_print() {
    time_t curt = time(0);
    FILE  * fp = NULL;
    int     i, num;
    char  file[32];
    EpmMemHdr * hdr = NULL;
    uint64    msize = 0;
 
    sprintf(file, "epm_mem-%lu.txt", curt);
    fp = fopen(file, "w");
    EnterCriticalSection(&epm_memCS);
    num = epm_arr_num(epm_mem_list);
    for (i=0; i<num; i++) {
        hdr = epm_arr_value(epm_mem_list, i);
        if (!hdr) continue;
        if (hdr->kflag != mflag) {
            fprintf(fp, "%p, %uB, is corrupted\n", hdr, hdr->size);
            continue;
        }
        fprintf(fp, "%p, %uB, id=%llu, re=%u, line=%u, file=%s\n",
                hdr, hdr->size, hdr->memid,
                hdr->reallocate, hdr->line, hdr->file);
        msize += hdr->size;
    }
    fprintf(fp, "Total Number: %u    Total MemSize: %llu\n", num, msize);
    LeaveCriticalSection(&epm_memCS);
    fclose(fp);
}
#else
void epm_mem_print() {
}
#endif
 
void * epm_alloc_dbg (long size, char * file, int line)
{
    void * ptr = NULL;
#ifdef _MEMDBG
    EpmMemHdr * hdr = NULL;
 
    if (size <= 0) return NULL;
 
    ptr = malloc(size + sizeof(EpmMemHdr));
 
    hdr = (EpmMemHdr *)ptr;
    memset(hdr, 0, sizeof(*hdr));
    hdr->size = size;
    hdr->memid = getmemid();
    hdr->kflag = mflag;
    hdr->line = line;
    strncpy(hdr->file, file, sizeof(hdr->file)-1);
    epm_mem_add(ptr);
 
    return (uint8 *)ptr + sizeof(EpmMemHdr);
#else
    if (size <= 0) return NULL;
    ptr = malloc(size);
    return ptr;
#endif
}
 
void * epm_zalloc_dbg (long size, char * file, int line)
{
    void * ptr = NULL;
#ifdef _MEMDBG
    EpmMemHdr * hdr = NULL;
 
    if (size <= 0) return NULL;
 
    ptr = malloc(size + sizeof(EpmMemHdr));
    if (ptr)
        memset(ptr, 0, size + sizeof(EpmMemHdr));
 
    hdr = (EpmMemHdr *)ptr;
    hdr->size = size;
    hdr->memid = getmemid();
    hdr->kflag = mflag;
    hdr->line = line;
    strncpy(hdr->file, file, sizeof(hdr->file)-1);
    epm_mem_add(ptr);
 
    return (uint8 *)ptr + sizeof(EpmMemHdr);
#else
    if (size <= 0) return NULL;
    ptr = malloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
#endif
}
 
void * epm_realloc_dbg(void * ptr, long size, char * file, int line)
{
    void * oldp = NULL;
#ifdef _MEMDBG
    EpmMemHdr * hdr = NULL;
    uint64    memid = 0;
 
    oldp = ptr;
 
    if (ptr != NULL) {
        hdr = (EpmMemHdr *)(ptr-sizeof(EpmMemHdr));
        memid = hdr->memid;
        epm_mem_del(hdr);
 
        if (hdr->kflag != mflag)
            printf("###Panic: %s:%d epm_realloc %ld bytes, old %p:%llu %u bytes alloc by %s:%d ruined\n",
                     file, line, size, ptr, memid, hdr->size, hdr->file, hdr->line);
 
        ptr = realloc((uint8 *)ptr - sizeof(EpmMemHdr), size + sizeof(EpmMemHdr));
    } else
        ptr = malloc(size + sizeof(EpmMemHdr));
 
    if (ptr == NULL) {
        if (oldp) free((uint8 *)oldp - sizeof(EpmMemHdr));
        return NULL;
    }
 
    hdr = (EpmMemHdr *)ptr;
    hdr->size = size;
    hdr->memid = memid > 0 ? memid : getmemid();
    hdr->kflag = mflag;
    hdr->line = line;
    strncpy(hdr->file, file, sizeof(hdr->file)-1);
    hdr->reallocate++;
    epm_mem_add(ptr);
 
    return (uint8 *)ptr + sizeof(EpmMemHdr);
#else
    oldp = ptr;
    if (ptr != NULL)
        ptr = realloc((uint8 *)ptr, size);
    else
        ptr = realloc(ptr, size);
 
    if (ptr == NULL) {
        if (oldp) epm_free(oldp);
    }
 
    return ptr;
#endif
}
 
void  epm_free_dbg (void * ptr, char * file, int line)
{
    if (ptr) {
#ifdef _MEMDBG
        EpmMemHdr * hdr = (EpmMemHdr *)((uint8 *)ptr - sizeof(EpmMemHdr));
        epm_mem_del(hdr);
        if (hdr->kflag != mflag) {
            printf("###Panic: %s:%d epm_free %p:%llu %u bytes alloc by %s:%d ruined\n",
                  file, line, ptr, hdr->memid, hdr->size, hdr->file, hdr->line);
            return;
        }
        free(hdr);
#else
        free(ptr);
#endif
    }
}


#ifdef _WIN32
int gettimeofday(struct timeval * tv, struct timezone * tz)
{
    union {
        int64    ns100;
        FILETIME ft;
    } now;
     
    GetSystemTimeAsFileTime(&now.ft);
    tv->tv_usec = (long) ((now.ns100 / 10LL) % 1000000LL);
    tv->tv_sec = (long) ((now.ns100 - 116444736000000000LL) / 10000000LL);
    return 0;
 }
#endif

long epm_time (epm_time_t * tp)
{
    struct timeval tval;

    gettimeofday(&tval, NULL);
    if (tp) {
        tp->s = tval.tv_sec;
        tp->ms = tval.tv_usec/1000;
    }
    return tval.tv_sec * 1000000 + tval.tv_usec;
}

void epm_time_add (epm_time_t * tp, epm_time_t tv)
{
    long  ms = 0;

    if (!tp) return;

    ms = tp->ms + tv.ms;
    tp->s += tv.s + ms / 1000;
    tp->ms = ms % 1000;
}

void epm_time_add_ms (epm_time_t * tp, long ms)
{        
    if (!tp) return;
 
    ms += tp->ms; 
    tp->s += ms / 1000;
    tp->ms = ms % 1000;
} 
 
void epm_time_now_add (epm_time_t * tp, long ms)
{
    epm_time(tp);
    epm_time_add_ms(tp, ms);
}

epm_time_t epm_time_diff (epm_time_t * tp0, epm_time_t * tp1)
{
    epm_time_t ret = {0};

    if (!tp0 || !tp1) return ret;

    ret.s = tp1->s - tp0->s;
    if (tp1->ms >= tp0->ms){
        ret.ms = tp1->ms - tp0->ms;
    } else {
        ret.ms = 1000 - (tp0->ms - tp1->ms);
        ret.s--;
    }

    return ret;
}

long epm_time_diff_ms (epm_time_t * tp0, epm_time_t * tp1)
{
    long ms = 0;
 
    if (!tp0 || !tp1) return 0;
 
    ms = (tp1->s - tp0->s) * 1000;
    ms += tp1->ms - tp0->ms;
    return ms;
}


#ifdef UNIX
 
int InitializeCriticalSection(CRITICAL_SECTION * cs)
{
    return pthread_mutex_init(cs, NULL);
}
 
int DeleteCriticalSection(CRITICAL_SECTION * cs)
{
    return pthread_mutex_destroy(cs);
}
 
int EnterCriticalSection (CRITICAL_SECTION * cs)
{
    return pthread_mutex_lock(cs);
}
 
int LeaveCriticalSection(CRITICAL_SECTION * cs)
{
    return pthread_mutex_unlock(cs);
}
 
 
typedef struct EventObj_ {
    int              value;
    pthread_cond_t   cond;
    pthread_mutex_t  mutex;
#ifdef _DEBUG
    int              eventid;
#endif
} eventobj_t;
 
void * event_create ()
{
    eventobj_t * event = NULL;
#ifdef _DEBUG
    static int geventid = 0;
#endif
 
    event = epm_zalloc(sizeof(*event));
    if (!event) return NULL;
 
    event->value = -1;
    pthread_cond_init (&event->cond, NULL);
    pthread_mutex_init (&event->mutex, NULL);
 
#ifdef _DEBUG
    event->eventid = ++geventid;
#endif
 
    return event;
}
 
int event_wait (void * vevent, int millisec)
{
    eventobj_t     * event = (eventobj_t *)vevent;
    struct timeval   tv;
    struct timespec  ts;
    int              ret = -1;
 
    if (!event) return -1;
 
    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + millisec/1000;
    ts.tv_nsec = tv.tv_usec * 1000 + (millisec%1000) * 1000 * 1000;
 
    pthread_mutex_lock(&event->mutex);
    event->value = -1;
    ret = pthread_cond_timedwait (&event->cond, &event->mutex, &ts);
    pthread_mutex_unlock(&event->mutex);
 
    if (ret == ETIMEDOUT) return -1;
 
    return event->value;
}
 
void event_set (void * vevent, int val)
{
    eventobj_t * event = (eventobj_t *)vevent;
 
    if (!event) return;
 
    pthread_mutex_lock(&event->mutex);
    event->value = val;
    pthread_cond_broadcast (&event->cond);
    pthread_mutex_unlock(&event->mutex);
}
 
void event_destroy (void * vevent)
{
    eventobj_t * event = (eventobj_t *)vevent;
 
    if (!event) return;
 
    pthread_mutex_destroy (&event->mutex);
    pthread_cond_destroy (&event->cond);
 
    epm_free(event);
}
 
#endif
 
#ifdef _WIN32
 
typedef struct EventObj_ {
    int              value;
    HANDLE           hev;
#ifdef _DEBUG
    int              eventid;
#endif
} eventobj_t;
 
 
void * event_create ()
{
    eventobj_t * pev = NULL;
#ifdef _DEBUG
    static int geventid = 0;
#endif
 
    pev = epm_zalloc(sizeof(*pev));
    if (!pev) return NULL;
 
    pev->value = -1;
    pev->hev = CreateEvent(NULL, FALSE, FALSE, NULL);
 
#ifdef _DEBUG
    pev->eventid = ++geventid;
#endif
 
    return pev;
}
 
int event_wait (void * vobj, int millisec)
{
    eventobj_t * pev = (eventobj_t *)vobj;
    DWORD         ret = -1;
 
    if (!pev) return -1;
 
    pev->value = -1;
    if (millisec < 0)
        ret = WaitForSingleObject(pev->hev, INFINITE);
    else
        ret = WaitForSingleObject(pev->hev, (DWORD)millisec);
 
    if (ret == WAIT_TIMEOUT) return -1;
 
    return pev->value;
}
 
 
void event_set (void * vobj, int val)
{
    eventobj_t * pev = (eventobj_t *)vobj;
 
    if (!pev) return;
 
    pev->value = val;
    SetEvent(pev->hev);
}
 
void event_destroy (void * vobj)
{
    eventobj_t * pev = (eventobj_t *)vobj;
 
    if (!pev) return;
 
    CloseHandle(pev->hev);
 
    epm_free(pev);
}
#endif
 

