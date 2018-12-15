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


typedef struct epm_list_node_st {
    struct epm_list_node_st * prev;
    struct epm_list_node_st * next;
} epmlistnode_t;

typedef struct epm_list_st {
    int             num;
    epmlistnode_t * first;
    epmlistnode_t * last;
} epm_list_t;


void * epm_lt_get_next (void * node)
{
    epmlistnode_t * ret = NULL;

    if (!node) return NULL;

    ret = (epmlistnode_t *)node;

    return ret->next;
}


void * epm_lt_get_prev (void * node)
{
    epmlistnode_t * ret = NULL;

    if (!node) return NULL;

    ret = (epmlistnode_t *)node;

    return ret->prev;
}


epm_list_t * epm_lt_new ()
{
    epm_list_t * ret = NULL;

    if ((ret = (epm_list_t *) epm_zalloc (sizeof(epm_list_t))) == NULL)
        return NULL;

    ret->num = 0;
    ret->first = NULL;
    ret->last = NULL;

    return ret;
}


void epm_lt_zero (epm_list_t * lt)
{
    if (!lt) return;

    lt->num = 0;
    lt->first = NULL;
    lt->last = NULL;
}


void epm_lt_free (epm_list_t * lt)
{
    if (!lt) return;

    epm_free (lt);
}

void epm_lt_free_all (epm_list_t * lt, int (*func)())
{
    epmlistnode_t * cur = NULL, * old = NULL;
    int i;
    
    if (!lt) return;

    for (i = 0, cur = lt->first; cur && i < lt->num; i++) {
        old = cur;
        cur = cur->next;
        (*func)((void *)old);
    }

    epm_lt_free(lt);
}

int epm_lt_prepend (epm_list_t * lt, void * data)
{
    epmlistnode_t * node = NULL;

    if (!lt || !data) return -1;

    node = (epmlistnode_t *)data;
    node->prev = NULL;
    node->next = lt->first;

    if (lt->first) {
        lt->first->prev = node;
        lt->first = node;
        if (lt->first) lt->first->prev = NULL;
    } else {
        lt->first = node;
        lt->last = node;
        if (lt->first) lt->first->prev = NULL;
        if (lt->last) lt->last->next = NULL;
    }

    lt->num++;
    return lt->num;
}

int epm_lt_append (epm_list_t * lt, void * data)
{
    epmlistnode_t * node = NULL;

    if (!lt || !data) return -1;

    node = (epmlistnode_t *)data;
    node->prev = lt->last;
    node->next = NULL;

    if (lt->first) {
        lt->last->next = node;
        lt->last = node;
        if (lt->last) lt->last->next = NULL;
    } else {
        lt->first = node;
        lt->last = node;
        if (lt->first) lt->first->prev = NULL;
        if (lt->last) lt->last->next = NULL;
    }

    lt->num++;
    return lt->num;
}

int epm_lt_insert_before (epm_list_t * lt, void * curData, void * data)
{
    epmlistnode_t * node = NULL, * cur = NULL;

    if (!lt || !curData || !data) return -1;

    cur = (epmlistnode_t *) curData;
    node = (epmlistnode_t *) data;

    if (lt->first == cur)
        return epm_lt_prepend(lt, data);
    
    node->prev = cur->prev;
    node->next = cur;

    cur->prev->next = node;
    cur->prev = node;

    lt->num++;
    return lt->num;
}


int epm_lt_insert_after (epm_list_t * lt, void * curData, void * data)
{
    epmlistnode_t * node = NULL, * cur = NULL;

    if (!lt || !curData || !data) return -1;
    
    cur = (epmlistnode_t *) curData;
    node = (epmlistnode_t *) data;

    if (lt->last == cur)
        return epm_lt_append(lt, data);
    
    node->prev = cur;
    node->next = cur->next;

    cur->next->prev = node;
    cur->next = node;

    lt->num++;
    return lt->num;
}

/* the following are the routines that delete the elements of the list */

void * epm_lt_rm_head (epm_list_t * lt)
{
    epmlistnode_t * ret = NULL;

    if (!lt || lt->num == 0)
        return NULL;

    ret = lt->first;
    if (!ret) return NULL;

    lt->first = ret->next;
    if (lt->first) lt->first->prev = NULL;

    if (ret->next) {
        ret->next->prev = ret->prev;
    } else {
        lt->last = ret->prev;
        if (lt->last) lt->last->next = NULL;
    }

    lt->num -= 1;

    ret->next = ret->prev = NULL;

    return ret;
}

void * epm_lt_rm_tail (epm_list_t * lt)
{
    epmlistnode_t * ret = NULL;

    if (!lt || lt->num == 0)
        return NULL;

    ret = lt->last;
    if (!ret) return NULL;

    if (ret->prev) {
        ret->prev->next = NULL;
    } else {
        lt->first = NULL;
    }

    lt->last = ret->prev;
    if (lt->last) lt->last->next = NULL;

    lt->num -= 1;

    ret->next = ret->prev = NULL;

    return ret;
}


void * epm_lt_delete_ptr (epm_list_t * lt, void * node)
{
    epmlistnode_t * ret = NULL;

    if (!lt || lt->num == 0 || !node) return NULL;

    ret = (epmlistnode_t *) node;

    if (!ret->prev && lt->first != ret) return NULL;
    if (!ret->next && lt->last != ret) return NULL;

    if (ret->prev) {
        ret->prev->next = ret->next;
    } else {
        lt->first = ret->next;
        if (lt->first)
            lt->first->prev = NULL;
    }

    if (ret->next) {
        ret->next->prev = ret->prev;
    } else {
        lt->last = ret->prev;
        if (lt->last)
            lt->last->next = NULL;
    }

    lt->num--;

    ret->next = ret->prev = NULL;
    return ret;
}


/* following is the routines that access the elements of the list */

int epm_lt_num (epm_list_t * lt)
{
    if (!lt) return 0;

    return lt->num;
}


void * epm_lt_first (epm_list_t * lt) 
{
    epmlistnode_t * ret = NULL;

    if (!lt || lt->num == 0)
        return NULL;

    ret = lt->first;
    return ret;        
}


void * epm_lt_last (epm_list_t * lt)
{
    epmlistnode_t * ret = NULL;

    if (!lt || lt->num == 0)
        return NULL;

     ret = lt->last;
     return ret;
}


