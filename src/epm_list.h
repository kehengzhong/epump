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

#ifndef _EPM_LIST_H_
#define _EPM_LIST_H_

#ifdef __cplusplus
extern "C" {
#endif


typedef struct epm_list_st epm_list_t;

/* Note: the node structure must be reserved 2-pointers space while being defined.
         the 2-pointers must be at the begining of the struction and casted as 
         prev and next of epm_list_t.  */

/* get the next node of the given node */
void * epm_lt_get_next (void * node);


/* get the prev node of the given node */
void * epm_lt_get_prev (void * node);

/* list instance allocation routine. allocate space and initialize all 
 * internal variables. the entry parameter is the default comparing function */
epm_list_t * epm_lt_new ();

/* reset all member values to initial state. */
void epm_lt_zero (epm_list_t * lt);


/* release the resource of the list. */
void epm_lt_free (epm_list_t * lt);


/* release the resources of the list, and call the user-given function to 
 * release actual content that pointer point to. */
void epm_lt_free_all (epm_list_t * lt, int (*func)());


/* add the data to the header of the list.  */
int epm_lt_prepend (epm_list_t * lt, void * data);


/* add the data to the tail of the list */
int epm_lt_append (epm_list_t * lt, void * data);

void * epm_lt_rm_head (epm_list_t * lt);
void * epm_lt_rm_tail (epm_list_t * lt);

/* delete the data member from the list. */
void * epm_lt_delete_ptr (epm_list_t * lt, void * node);

/* return the total number of data member of the list */
int epm_lt_num (epm_list_t * lt);

/* return the first data member that the list stores. */
void * epm_lt_first (epm_list_t * lt);

/* return the last data member that the list controls */
void * epm_lt_last (epm_list_t * lt);

#ifdef __cplusplus
}
#endif

#endif

