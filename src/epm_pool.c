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
#include "epm_hashtab.h"

typedef int (PoolUnitInit) (void *);
typedef int (PoolUnitFree) (void *);
typedef int (PoolUnitSize) (void *);

#define MIN_AF_NODES  8
typedef int FreeFunc (void * a);
 
typedef struct epm_fifo_ {
    int        size;
    int        start;
    int        num;
    void   ** data;
} epm_fifo_t;
 

typedef struct epm_buffer_pool_ {

    uint8    need_free; /*flag indicated the instance allocated by init*/

    /* fixed buffer unit size usually gotton from defined structure */
    int      unitsize;

    /* allocated buffer unit amounts one time */
    int      allocnum;

    /* the members in buffer unit may be allocated more memory
       during application utilization after being fetched out.
       when recycling it back into pool, check if memory of inner-member-allocated
       exceeds this threshold. release the units that get exceeded */
    int      unit_freesize;

    PoolUnitInit * unitinit;
    PoolUnitFree * unitfree;
    PoolUnitSize * getunitsize;

    /* the following 3 members may be modified by multiple threads */
    int      allocated; /*already allocated number*/
    int      exhausted; /*the number which has been pulled out for usage*/
    int      remaining; /*the available number remaining in the pool*/

    int      freegate;

    /* the memory units organized via the following linked list */
    CRITICAL_SECTION   ulCS;
    void             * fifo;
    void             * refifo;

    epm_hashtab_t    * rmdup_tab;
    int                hashlen;

} epm_pool_t;



void epm_fifo_free (void * vaf)
{
    epm_fifo_t * af = (epm_fifo_t *)vaf;
 
    if (!af) return;
 
    if (af->data) epm_free(af->data);
 
    epm_free(af);
}
 

void * epm_fifo_new (int size)
{
    epm_fifo_t * af = NULL;
 
    if (size < MIN_AF_NODES) size = MIN_AF_NODES;
 
    af = epm_zalloc(sizeof(*af));
    if (!af) return NULL;
 
    af->num = 0;
    af->size = size;
    af->start = 0;
 
    af->data = (void **)epm_zalloc(sizeof(void *) * af->size);
    if (!af->data) {
        epm_fifo_free(af);
        return NULL;
    }
 
    return af;
}

int epm_fifo_num (void * vaf)
{
    epm_fifo_t * af = (epm_fifo_t *)vaf;
 
    if (!af) return 0;
 
    return af->num;
}
 
void * epm_fifo_value (void * vaf, int i)
{
    epm_fifo_t * af = (epm_fifo_t *)vaf;
    int        index = 0;
 
    if (!af) return NULL;
 
    if (i < 0 || i >= af->num) return NULL;
 
    index = (i + af->start) % af->size;
    return af->data[index];
}
 
 
int epm_fifo_push (void * vaf, void * value)
{
    epm_fifo_t * af = (epm_fifo_t *)vaf;
    int        i, index = 0, wrapnum = 0;
    void    ** tmp = NULL;
    int        oldsize = 0;
 
    if (!af) return -1;
 
    if (af->size < af->num + 1) {
        oldsize = af->size;
        tmp = (void **)epm_realloc((void *)af->data, sizeof(void *) * af->size * 2);
        if (tmp == NULL) {
            return -2;
        }
        af->data = tmp;
        af->size *= 2;
 
        if (af->start + af->num > oldsize) { //wrap around
            wrapnum = (af->start + af->num) % oldsize;
            if (wrapnum <= af->num/2) {
                for (i=0; i<wrapnum; i++) {
                    index = (oldsize + i) % af->size;
                    af->data[index] = af->data[i];
                }
            } else {
                for (i=0; i<oldsize-af->start; i++) {
                    af->data[af->size-1-i] = af->data[oldsize-1-i];
                }
                af->start = af->size - oldsize + af->start;
            }
        }
    }
 
    index = (af->start + af->num) % af->size;
    af->data[index] = value;
    af->num++;
 
    return 0;
}
 
void * epm_fifo_out (void * vaf)
{
    epm_fifo_t * af = (epm_fifo_t *)vaf;
    void     * value = NULL;
 
    if (!af || !af->data) return NULL;
    if (af->num < 1) return NULL;
 
    value = af->data[af->start];
    af->start = (af->start + 1) % af->size;
    af->num--;
 
    return value;
}




int epm_pool_hash_cmp(void * a, void * b)
{
    ulong  ua = (ulong)a;
    ulong  ub = (ulong)b;

    if (ua > ub) return 1;
    if (ua < ub) return -1;
    return 0;
}

ulong epm_pool_hash (void * key)
{
    ulong ret = (ulong)key;
    return ret;
}

epm_pool_t * epm_pool_init (epm_pool_t * pool)
{
    epm_pool_t * pmem = NULL;

    if (pool) {
        pmem = pool;
        memset(pmem, 0, sizeof(*pmem));
        pmem->need_free = 0;
    } else {
        pmem = (epm_pool_t *)epm_zalloc(sizeof(*pmem));
        pmem->need_free = 1;
    }

    pmem->allocnum = 1;

    pmem->freegate = 0;

    InitializeCriticalSection(&pmem->ulCS);
    pmem->fifo = epm_fifo_new(128);
    pmem->refifo = epm_fifo_new(128);

    pmem->hashlen = 4096;
    pmem->rmdup_tab = epm_ht_only_new(pmem->hashlen, epm_pool_hash_cmp);
    epm_ht_set_hash_func(pmem->rmdup_tab, epm_pool_hash);

    return pmem;
}


int epm_pool_clean (epm_pool_t * pool)
{
    int     i = 0, num = 0;
    void  * punit = NULL;

    if (!pool) return -1;

    EnterCriticalSection(&pool->ulCS);

    num = epm_fifo_num(pool->refifo);
    for (i=0; i<num; i++) {
        punit = epm_fifo_value(pool->refifo, i);
        if (pool->unitfree)
            (*pool->unitfree)(punit);
        else
            epm_free(punit);
    }
    epm_fifo_free(pool->refifo);
    pool->refifo = NULL;

    num = epm_fifo_num(pool->fifo);
    for (i=0; i<num; i++) {
        punit = epm_fifo_value(pool->fifo, i);
        epm_free(punit);
    }
    epm_fifo_free(pool->fifo);
    pool->fifo = NULL;

    epm_ht_free(pool->rmdup_tab);
    pool->rmdup_tab = NULL;

    LeaveCriticalSection(&pool->ulCS);

    DeleteCriticalSection(&pool->ulCS);

    if (pool->need_free) epm_free(pool);
    return 0;
}

int epm_pool_set_initfunc (epm_pool_t * pool, void * init)
{
    if (!pool) return -1;
    pool->unitinit = (PoolUnitFree *)init;
    return 0;
}

int epm_pool_set_freefunc (epm_pool_t * pool, void * free)
{
    if (!pool) return -1;
    pool->unitfree = (PoolUnitFree *)free;
    return 0;
}

int epm_pool_set_getsizefunc (epm_pool_t * pool, void * vgetsize)
{
    PoolUnitSize  * getsize = (PoolUnitSize *)vgetsize;

    if (!pool) return -1;
    pool->getunitsize = getsize;
    return 0;
}

int epm_pool_set_unitsize (epm_pool_t * pool, int size)
{
    if (!pool) return -1;
    pool->unitsize = size;
    return size;
}


int epm_pool_set_allocnum (epm_pool_t * pool, int escl)
{
    if (!pool) return -1;

    if (escl <= 0) escl = 1;
    pool->allocnum = escl;
    return escl;
}

int epm_pool_set_freesize (epm_pool_t * pool, int size)
{
    if (!pool) return -1;
    pool->unit_freesize = size;
    return size;
}


int epm_pool_get_state (epm_pool_t * pool, int * allocated, int * remaining, int * exhausted)
{
    if (!pool) return -1;

    if (allocated) *allocated = pool->allocated;
    if (remaining) *remaining = pool->remaining;
    if (exhausted) *exhausted = pool->exhausted;

    return 0;
}


void * epm_pool_fetch (epm_pool_t * pool)
{
    void * punit = NULL;
    int    i = 0;

    if (!pool) return NULL;

    EnterCriticalSection(&pool->ulCS);

    /* pool->fifo stores objects pre-allocated but not handed out.
       pool->refifo stores the recycled objects. */
    if (epm_fifo_num(pool->fifo) <= 0 && epm_fifo_num(pool->refifo) <= 0) {
        for (i = 0; i < pool->allocnum; i++) {
            punit = epm_zalloc(pool->unitsize);
            if (!punit)  continue;

            epm_fifo_push(pool->fifo, punit);
            pool->allocated++;
            pool->remaining++;
        }
    }

    punit = NULL;
    if (epm_fifo_num(pool->fifo) > 0)
        punit = epm_fifo_out(pool->fifo);
    if (!punit) {
        punit = epm_fifo_out(pool->refifo);
    }

    if (punit) {
        pool->exhausted += 1;
        if (--pool->remaining < 0) pool->remaining = 0;

        /* record it in hashtab before handing out.  when recycling,
           the pbuf should be verified based on records in hashtab.
           only those fetched from pool can be recycled, just once!
           the repeated recycling to one pbuf is very dangerous and
           must be prohibited! */
        epm_ht_set(pool->rmdup_tab, punit, punit);
    }

    LeaveCriticalSection(&pool->ulCS);

    if (punit && pool->unitinit)
        (*pool->unitinit)(punit);

    return punit;
}

int epm_pool_recycle (epm_pool_t * pool, void * punit)
{
    if (!pool || !punit) return -1;

    EnterCriticalSection(&pool->ulCS);

    if (epm_ht_delete(pool->rmdup_tab, punit) != punit) {
        LeaveCriticalSection(&pool->ulCS);
        return -10;
    }

    if (pool->unitfree && pool->unit_freesize > 0 && pool->getunitsize != NULL) {
        if ((*pool->getunitsize)(punit) >= pool->unit_freesize) {
            (*pool->unitfree)(punit);
            pool->allocated--;
            pool->exhausted--;
            LeaveCriticalSection(&pool->ulCS);
            return 0;
        }
    }

    epm_fifo_push(pool->refifo, punit);
    pool->remaining += 1;
    pool->exhausted -= 1;

    /* when the peak time of daily visiting passed, the loads
       of CPU/Memory will go down. the resouces allocated in highest
       load should be released partly and kept in normal level. */

    if (pool->remaining > pool->allocnum) pool->freegate++;
    else pool->freegate = 0;

    if (pool->freegate > pool->allocnum / 3) {
        int   i = 0;
        for (i = 0; i < pool->allocnum && pool->remaining > pool->allocnum; i++) {
            punit = epm_fifo_out(pool->refifo);
            if (punit) {
                if (pool->unitfree) (*pool->unitfree)(punit);
                else epm_free(punit);

                pool->remaining--;
                pool->allocated--;
            } else {
                punit = epm_fifo_out(pool->fifo);
                if (punit) {
                    epm_free(punit);
                    pool->remaining--;
                    pool->allocated--;
                }
            }
        }
        pool->freegate = 0;
    }

    LeaveCriticalSection(&pool->ulCS);
    return 0;
}

