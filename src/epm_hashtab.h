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

#ifndef _EPM_HASH_TAB_H_
#define _EPM_HASH_TAB_H_

#include "epm_arr.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef int (EpmHTCmp) (void * a, void * b);
typedef void (EpmHTFree) (void * a);
typedef ulong (EpmHashFunc) (void * key);

#pragma pack(push ,1)

typedef struct epm_hash_node {
    int   count;
    void * dptr;
} epm_hashnode_t;


typedef struct EpmHashTab_ {

    int    len;
    int    num_requested;
    int    num;

    int               linear;
    epm_arr_t       * nodelist;
    epm_hashnode_t  * ptab;

    EpmHashFunc     * hashFunc;
    EpmHTCmp        * cmp;

#ifdef _DEBUG
    int               collide_tab[50];
#endif

} epm_hashtab_t;

#pragma pack(pop)


/* create an instance of HASH TABLE. first find a prime number near to the 
 * given number. set the comparing function. allocate hash nodes of the prime
 * number. set the default hash function provided by system. if succeeded, 
 * the instance of HASH TABLE return.  else, NULL returned. 
 * the comparing function will execute comparing operation between the 
 * instance that hash node points to and the key.*/
epm_hashtab_t * epm_ht_new (int num, EpmHTCmp * cmp);

/* there is no linear list existing in epm_hashtab_t via this API */
epm_hashtab_t * epm_ht_only_new (int num, EpmHTCmp * cmp);

/* set the hash function as the user-defined function. */
void epm_ht_set_hash_func (epm_hashtab_t * ht, EpmHashFunc * hashfunc);

/* release the space of the hash table instance. if the value numbers of 
 * the same hash value is greater than 1, then the stack instance will be
 * released. release the hash node list and release the hash table inst. */
void epm_ht_free (epm_hashtab_t * ht);

/* free all the space including the instance that hash table points to using
 * the given free function. */
void epm_ht_free_all (epm_hashtab_t * ht, void * vfunc);

/* free all the member using given free-function, and empty the hashtab */
void epm_ht_free_member (epm_hashtab_t * ht, void * vfunc);

/* clear all the nodes that set before. after invoking the api, the hashtab
 * will be as if the initial state just allocated using epm_ht_new() */
void epm_ht_zero (epm_hashtab_t * ht);

/* return the actual number of hash value. */
int epm_ht_num (epm_hashtab_t * ht);

/* get the hash value that the key corresponds to. if the value corresponding
 * to the key is never set, NULL is returned. */
void * epm_ht_get (epm_hashtab_t * ht, void * key);

int epm_ht_sort (epm_hashtab_t * ht, EpmHTCmp * cmp);

/* get the hash value according to the index location. the index value must
 * be from 0 to the total number minor 1. */
void * epm_ht_value (epm_hashtab_t * ht, int index);

/* set a hash value for a key. */
int epm_ht_set (epm_hashtab_t * ht, void * key, void * value);

/* delete the value corresponding to the key. */
void * epm_ht_delete (epm_hashtab_t * ht, void * key);

#ifdef _DEBUG
void epm_print_hashtab (void * vht);
#endif

#ifdef __cplusplus
}
#endif

#endif

