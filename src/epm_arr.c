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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epm_util.h"
#include "epm_arr.h"

#define MIN_NODES    4

typedef void ArrFree (void * );

#define FP_ICC (int (*)(const void *, const void *))


epm_arr_t * epm_arr_new (int len)
{
    epm_arr_t * ret = NULL;

    if (len < MIN_NODES) len = MIN_NODES;

    if ((ret = (epm_arr_t *) epm_zalloc(sizeof(epm_arr_t))) == NULL)
        return NULL;

    ret->data = (void **) epm_zalloc(sizeof(void *) * len);
    if (ret->data == NULL) {
        epm_free(ret);
        return NULL;
    }

    ret->num_alloc = len;
    ret->num = 0;
    return ret;
}


int epm_arr_insert (epm_arr_t * ar, void * data, int loc)
{
    void ** s;

    if (!ar) return 0;

    if (ar->num_alloc <= ar->num + 1) {
        s = (void **)epm_realloc((void *)ar->data,
                 (unsigned int)sizeof(void *) * ar->num_alloc * 2);
        if (s == NULL) return 0;
        ar->data = s;
        ar->num_alloc *= 2;
    }
    if ((loc >= ar->num) || (loc < 0))
        ar->data[ar->num] = data;
    else {
        memmove(&ar->data[loc+1], &ar->data[loc], (ar->num - loc) * sizeof(void *));
        ar->data[loc] = data;
    }
    ar->num++;
    return ar->num;
}


void * epm_arr_delete_ptr (epm_arr_t * ar, void * p)
{
    int i;	

    if (!ar || !p) return NULL;
	
    for (i = 0; i < ar->num; i++)
        if (ar->data[i] == p)
            return epm_arr_delete(ar,i);

    return NULL;
}


void * epm_arr_delete (epm_arr_t * ar, int loc)
{
    void * ret = NULL;

    if (!ar || ar->num == 0 || loc < 0 || loc >= ar->num)
         return NULL;

    ret = ar->data[loc];
    if (loc != ar->num-1) {
        memmove(&ar->data[loc], &ar->data[loc+1], (ar->num-loc-1)*sizeof(void *));
    }
    ar->num--;
    return ret;
}

int epm_arr_push (epm_arr_t * ar, void * data)
{
    return epm_arr_insert(ar, data, ar->num);
}

void * epm_arr_pop (epm_arr_t * ar)
{
    if (!ar) return NULL;

    if (ar->num <= 0) return NULL;

    return epm_arr_delete(ar, ar->num-1);
}

void epm_arr_zero(epm_arr_t * ar)
{
    if (!ar) return;
    if (ar->num <= 0) return;

    memset((void *)ar->data, 0, sizeof(ar->data) * ar->num);
    ar->num=0;
}

void epm_arr_pop_free (epm_arr_t * ar, void * vfunc)
{
    ArrFree * func = (ArrFree *)vfunc;
    int       i;

    if (!ar) return;

    for (i = 0; i < ar->num; i++) {
        if (ar->data[i] != NULL)
            (*func)(ar->data[i]);
    }

    epm_arr_free(ar);
}

void epm_arr_pop_kfree (epm_arr_t * ar)
{
    int i;
    
    if (!ar) return;

    for (i=0; i<ar->num; i++)
        if (ar->data[i] != NULL)
            epm_free(ar->data[i]);
    epm_arr_free(ar);
}   


void epm_arr_free (epm_arr_t * ar)
{
    if (!ar) return;
    if (ar->data != NULL) epm_free(ar->data);
    epm_free(ar);
}


int epm_arr_num (epm_arr_t * ar)
{
    if (!ar) return 0;

    return ar->num;
}


void * epm_arr_value (epm_arr_t * ar, int i)
{
    if (!ar) return NULL;

    if (ar->num <= 0 || i < 0 || i >= ar->num)
        return NULL;

    return ar->data[i];
}

void epm_arr_sort_by (epm_arr_t * ar, EPMArrCmp * cmp)
{
    if (!ar) return;

    qsort(ar->data, ar->num, sizeof(void *), FP_ICC cmp);
}

int epm_arr_insert_by (epm_arr_t * ar, void * item, EPMArrCmp * cmp)
{
    int lo, mid, hi;
    int cur=0, end=0;
    int result;

    if (!ar || !item || !cmp)
        return -1;

    if (ar->num == 0)
        return epm_arr_push(ar, item);

    lo = 0;
    hi = ar->num-1;

    while (lo <= hi) {
        mid = (lo + hi) / 2;
        if (!(result = (*cmp)(epm_arr_value(ar, mid), item))) {
            for (cur = mid+1; cur <= hi; cur++) {
                if ((*cmp)(epm_arr_value(ar, cur), item) != 0) {
                    end = cur-1;
                    break;
                }
            }
            if (cur > hi) end = hi;
            return epm_arr_insert(ar, item, end + 1);
        }
        else if (result < 0) {
            lo = mid + 1;
        } else {
            hi = mid -1;
        }
    }

    return epm_arr_insert(ar, item, lo);
}


void * epm_arr_find_by (epm_arr_t * ar, void * pattern, EPMArrCmp * cmp)
{
    int lo, hi, mid;
    int result;

    if (!ar || !pattern)
        return NULL;

    lo = 0;
    hi = ar->num - 1;

    while (lo <= hi) {
        mid = (lo + hi) / 2;
        if (!(result = (*cmp)(epm_arr_value(ar, mid), pattern))) {
            return epm_arr_value(ar, mid);
        }
        else if (result < 0) {
            lo = mid + 1;
        } else {
            hi = mid -1;
        }
    }

    return NULL;
}


void * epm_arr_delete_by (epm_arr_t * ar, void * pattern, EPMArrCmp * cmp)
{
    int lo=0, hi=0, mid=0;
    int result=0; 
    
    if (!ar || !pattern)
        return NULL;

    lo = 0;
    hi = ar->num - 1;
    
    while (lo <= hi) {
        mid = (lo + hi) / 2;
        if (!(result = (*cmp)(epm_arr_value(ar, mid), pattern))) {
            return epm_arr_delete(ar, mid);
        }   
        else if (result < 0) {
            lo = mid + 1; 
        } else { 
            hi = mid -1;
        }   
    }   
    
    return NULL;
}   


void * epm_arr_search (epm_arr_t * ar, void * pattern, EPMArrCmp * cmp)
{
    void * item = NULL;
    int i;

    if (!ar || !pattern)
        return NULL;

    for (i=0; i<ar->num; i++) {
        item = epm_arr_value(ar, i);
        if (cmp) {
            if ((*cmp)(item, pattern) == 0) break;
        } else {
            if (item == pattern) break;
        }
    }

    if (i == ar->num)
        item = NULL;

    return item;
}

