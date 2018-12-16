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

#include <math.h>


#define HASH_SHIFT  6
#define HASH_VALUE_BITS  32
static long s_mask = ~0U << (HASH_VALUE_BITS - HASH_SHIFT);

static ulong hash_key (char * str);


static ulong find_a_prime (ulong baseNum)
{
    ulong num = 0, i = 0;

    num = baseNum;
    if ((num % 2) == 0) num++;

    for (i = 3; i < (unsigned long)((1 + (long)(sqrt((double)num)))); i += 2) {
        if ((num % i) == 0) {
            i = 1;
            num += 2;
        }
    }

    return num;
}


static ulong hash_key (char * str)
{
    unsigned long ret = 0;
    char * p = NULL;

    if (!str) return 0;

    p = str;
    while (*p != '\0') {
        ret = (ret & s_mask) ^ (ret << HASH_SHIFT) ^ (*p);
        p++;
    }

    return ret;
}


epm_hashtab_t * epm_ht_new (int num, EpmHTCmp * cmp)
{
    epm_hashtab_t * ret = NULL;
    int     i = 0;

    ret = epm_zalloc(sizeof(*ret));
    if (ret == NULL)
        return NULL;

    ret->num_requested = num;
    ret->len = find_a_prime (num);
    ret->num = 0;
    ret->cmp = cmp;
    ret->hashFunc = (EpmHashFunc *)hash_key;

    ret->linear = 1;
    ret->nodelist = epm_arr_new(4);

    ret->ptab = epm_zalloc(ret->len * sizeof(epm_hashnode_t));
    if (ret->ptab == NULL) {
        epm_arr_free(ret->nodelist);
        epm_free(ret);
        return NULL;
    }
    for (i = 0; i < ret->len; i++) {
        ret->ptab[i].dptr = NULL;
        ret->ptab[i].count = 0;
        memset(&ret->ptab[i], 0, sizeof(ret->ptab[i]));
    }

    return ret;
}
 
epm_hashtab_t * epm_ht_only_new (int num, EpmHTCmp * cmp)
{
    epm_hashtab_t * ht = NULL;

    ht = epm_ht_new(num, cmp);
    if (ht) ht->linear = 0;
    return ht;
}


void epm_ht_set_hash_func (epm_hashtab_t * ht, EpmHashFunc * hashfunc)
{
    if (!ht || !hashfunc) return;

    ht->hashFunc = hashfunc;
}


void epm_ht_free (epm_hashtab_t * ht)
{
    int i;

    if (!ht) return;

    for (i = 0; i < ht->len; i++) {
        if (ht->ptab[i].count > 1) {
            epm_arr_free((epm_arr_t *)ht->ptab[i].dptr);
        }
    }

    epm_arr_free(ht->nodelist);
    epm_free(ht->ptab);
    epm_free(ht);
}


void epm_ht_free_all (epm_hashtab_t * ht, void * vfunc)
{
    EpmHTFree * func = (EpmHTFree *)vfunc;
    int i = 0;

    if (!ht) return;

    if (func == NULL) {
        epm_ht_free(ht);
        return;
    }

    for (i = 0; i < ht->len; i++) {
        if (ht->ptab[i].count == 1) {
            (*func)(ht->ptab[i].dptr);
        } else if (ht->ptab[i].count > 1) {
            epm_arr_pop_free ((epm_arr_t *)ht->ptab[i].dptr, func);
        }
    }

    epm_arr_free(ht->nodelist);
    epm_free(ht->ptab);
    epm_free(ht);
}

void epm_ht_free_member (epm_hashtab_t * ht, void * vfunc)
{ 
    EpmHTFree * func = (EpmHTFree *)vfunc;
    int i = 0;
     
    if (!ht) return;
     
    if (!func) { 
        epm_ht_zero(ht);  
        return;       
    } 

    for (i = 0; i < ht->len; i++) {
        if (ht->ptab[i].count == 1) {
            (*func)(ht->ptab[i].dptr);  
        } else if (ht->ptab[i].count > 1) {
            epm_arr_pop_free((epm_arr_t *)ht->ptab[i].dptr, func);
        }    

        ht->ptab[i].dptr = NULL;
        ht->ptab[i].count = 0;
    }    
     
    if (ht->linear) epm_arr_zero(ht->nodelist);
    ht->num = 0;

#ifdef _DEBUG
    memset(&ht->collide_tab, 0, sizeof(ht->collide_tab));
#endif
}
 
void epm_ht_zero (epm_hashtab_t * ht)
{
    int i = 0;

    if (!ht) return;

    for (i = 0; i < ht->len; i++) {
        if (ht->ptab[i].count > 1) {
            epm_arr_free((epm_arr_t *)ht->ptab[i].dptr);
        }
        ht->ptab[i].dptr = NULL;
        ht->ptab[i].count = 0;
    }
    ht->num = 0;

    if (ht->linear) epm_arr_zero(ht->nodelist);

#ifdef _DEBUG
    memset(&ht->collide_tab, 0, sizeof(ht->collide_tab));
#endif
}


int epm_ht_num (epm_hashtab_t * ht)
{
    if (!ht) return 0;

    return ht->num;
}


void * epm_ht_get (epm_hashtab_t * ht, void * key)
{
    ulong hash = 0;

    if (!ht || !key) return NULL;

    hash = (*ht->hashFunc)(key);
    hash %= ht->len;

    switch (ht->ptab[hash].count) {
    case 0:
        return NULL;

    case 1:
        if ((*ht->cmp)(ht->ptab[hash].dptr, key) == 0) {
            return ht->ptab[hash].dptr;
        } else
            return NULL;

    default:
        return epm_arr_search(ht->ptab[hash].dptr, key, ht->cmp);
    }

    return NULL;
}


int epm_ht_sort (epm_hashtab_t * ht, EpmHTCmp * cmp)
{
    if (!ht) return -1;

    if (ht->linear) epm_arr_sort_by(ht->nodelist, cmp);
    return 0;
}


void * epm_ht_value (epm_hashtab_t * ht, int index)
{
    int  i = 0;
    int  num = 0;

    if (!ht) return NULL;

    if (index < 0 || index >= ht->num) return NULL;

    if (ht->linear) return epm_arr_value(ht->nodelist, index);

    for (num = 0, i = 0; i < ht->len; i++) {
        switch(ht->ptab[i].count) {
        case 0: 
            continue;
        case 1:
            if (index == num) return ht->ptab[i].dptr;
            if (index < num) return NULL;
            num += 1;
            break;
        default:
            if (index >= num + ht->ptab[i].count) {
                num += ht->ptab[i].count;
                continue;
            } else {
                return epm_arr_value(ht->ptab[i].dptr, index - num);
            }
        }
    }

    return NULL;
}


void epm_ht_traverse (epm_hashtab_t * ht, void * usrInfo, void (*check)(void *, void *))
{
    int i = 0, j = 0;

    if (!ht || !check) return;

    for (i = 0; i < ht->len; i++) {
        switch (ht->ptab[i].count) {
        case 0: 
            break;
        case 1:
            (*check)(usrInfo, ht->ptab[i].dptr);
            break;
        default:
            for (j = 0; j < epm_arr_num(ht->ptab[i].dptr); j++) {
                check(usrInfo, epm_arr_value(ht->ptab[i].dptr, j));
            }
            break;
        }
    }
}



int epm_ht_set (epm_hashtab_t * ht, void * key, void * value)
{
    ulong   hash = 0;
    epm_arr_t * valueList = NULL;

    if (!ht || !key) return -1;

    hash = (*ht->hashFunc)(key);

    hash %= ht->len;

    switch (ht->ptab[hash].count) {
    case 0:
        ht->ptab[hash].dptr = value;
        ht->ptab[hash].count++;
        ht->num++;

        if (ht->linear) epm_arr_push(ht->nodelist, value);

#ifdef _DEBUG
        ht->collide_tab[ht->ptab[hash].count] += 1;
#endif
        return ht->ptab[hash].count;
 
    case 1:
        if ((*ht->cmp)(ht->ptab[hash].dptr, key) == 0) {
            return ht->ptab[hash].count;
        }

        ht->ptab[hash].count++;
        valueList = epm_arr_new(4);
        epm_arr_push(valueList, ht->ptab[hash].dptr);
        epm_arr_push(valueList, value);
        ht->ptab[hash].dptr = valueList;
        ht->num++;

        if (ht->linear) epm_arr_push(ht->nodelist, value);

#ifdef _DEBUG
        ht->collide_tab[ht->ptab[hash].count-1] -= 1;
        ht->collide_tab[ht->ptab[hash].count] += 1;
#endif
        return ht->ptab[hash].count;

    default:
        if (epm_arr_search(ht->ptab[hash].dptr, key, ht->cmp) != NULL)
            return 0;

        ht->ptab[hash].count++;
        epm_arr_push(ht->ptab[hash].dptr, value);
        ht->num++;

        if (ht->linear) epm_arr_push(ht->nodelist, value);

#ifdef _DEBUG
        if (ht->ptab[hash].count <= sizeof(ht->collide_tab)/sizeof(int) - 1) {
            ht->collide_tab[ht->ptab[hash].count-1] -= 1;
            ht->collide_tab[ht->ptab[hash].count] += 1;
        } else {
            if (ht->ptab[hash].count-1 <= sizeof(ht->collide_tab)/sizeof(int) - 1)
                ht->collide_tab[ht->ptab[hash].count-1] -= 1;
            ht->collide_tab[0] += 1;
        }
#endif
        return ht->ptab[hash].count;
    }

    return 0;
}


void * epm_ht_delete (epm_hashtab_t * ht, void * key)
{
    ulong  hash = 0;
    void * value = NULL;

    if (!ht || !key) return NULL;

    hash = (*ht->hashFunc)(key);
    if (hash < 0) return NULL;

    hash %= ht->len;

    switch (ht->ptab[hash].count) {
    case 0:
        return NULL;

    case 1:
        if ((*ht->cmp)(ht->ptab[hash].dptr, key) == 0) {
            ht->ptab[hash].count--;
            ht->num--;
            if (ht->linear) epm_arr_delete_ptr(ht->nodelist, ht->ptab[hash].dptr);
#ifdef _DEBUG
            ht->collide_tab[ht->ptab[hash].count+1] -= 1;
#endif
            return ht->ptab[hash].dptr;
        } else
            return NULL;

    default:
        value = epm_arr_search(ht->ptab[hash].dptr, key, ht->cmp);
        if (value == NULL) return NULL;

        if (ht->linear) epm_arr_delete_ptr(ht->nodelist, value);

        epm_arr_delete_ptr(ht->ptab[hash].dptr, value);
        ht->ptab[hash].count--;
        ht->num--;

        if (epm_arr_num((epm_arr_t *)ht->ptab[hash].dptr) == 1) {
            void * tmp = epm_arr_value(ht->ptab[hash].dptr, 0);
            epm_arr_free(ht->ptab[hash].dptr);
            ht->ptab[hash].dptr = tmp;
            ht->ptab[hash].count = 1;
        }
#ifdef _DEBUG
        if (ht->ptab[hash].count < sizeof(ht->collide_tab)/sizeof(int) - 1) {
            ht->collide_tab[ht->ptab[hash].count] += 1;
            ht->collide_tab[ht->ptab[hash].count+1] -= 1;
        } else {
            if (ht->ptab[hash].count <= sizeof(ht->collide_tab)/sizeof(int) - 1)
                ht->collide_tab[ht->ptab[hash].count] += 1;
            ht->collide_tab[0] -= 1;
        }
#endif
        return value;
    }

    return NULL;
}


#ifdef _DEBUG
void epm_print_hashtab (void * vht)
{
    epm_hashtab_t * ht = (epm_hashtab_t *)vht;
    int i=0;
    int total = 0;
    int num = sizeof(ht->collide_tab)/sizeof(int);

    if (!ht) return;

    printf("\n");
    printf("-----------------------Hash Table---------------------\n");
    printf("Total Bucket Number: %d\n", ht->len);
    printf("Req Bucket Number  : %d\n", ht->num_requested);
    printf("Stored Data Number : %d\n", ht->num);
    for (i=1; i<num; i++) {
        total += ht->collide_tab[i];
        if (ht->collide_tab[i] > 0)
            printf("    Buckets Storing %d Data: %d\n", i, ht->collide_tab[i]);
    }
    total += ht->collide_tab[0];
    if (ht->collide_tab[0] > 0)
        printf("    Buckets Storing >= %d Data: %d\n", num, ht->collide_tab[0]);
    printf("Buckets Storing at least 1 data: %d\n", total);
    printf("-------------------------------------------------------\n\n");
    return;
}
#endif
