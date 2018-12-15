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

#ifndef _EPM_ARR_H_
#define _EPM_ARR_H_

#ifdef  __cplusplus
extern "C" {
#endif

typedef int EPMArrCmp(void * a, void * b);

typedef struct EPMArr_ {
    int      num_alloc;
    int      num;
    void  ** data;
} epm_arr_t;

int     epm_arr_num  (epm_arr_t *);
void  * epm_arr_value(epm_arr_t *, int);

epm_arr_t * epm_arr_new  (int len);

void    epm_arr_free      (epm_arr_t * ar);
void    epm_arr_pop_free  (epm_arr_t * ar, void * vfunc);
void    epm_arr_pop_kfree (epm_arr_t * ar);

int     epm_arr_insert    (epm_arr_t * ar, void * data, int where);
int     epm_arr_push      (epm_arr_t * ar, void * data);
void  * epm_arr_pop       (epm_arr_t * ar);
void    epm_arr_zero      (epm_arr_t * ar);

void  * epm_arr_delete    (epm_arr_t * ar, int loc);
void  * epm_arr_delete_ptr(epm_arr_t * ar, void * p);


/* call qsort to sort all the members with given comparing function */
void epm_arr_sort_by (epm_arr_t * ar, EPMArrCmp * cmp);


/* the array should be sorted beforehand, or the member count is not greater
 *  than 1. seek a position that suits for the new member, and insert it */
int epm_arr_insert_by (epm_arr_t * ar, void * item, EPMArrCmp * cmp);


/* find one member that matches the specified pattern through the comparing
 * function provided by user. the array must be sorted through epm_arr_sort_by 
 * before invoking this function */
void * epm_arr_find_by (epm_arr_t * ar, void * pattern, EPMArrCmp * cmp);


/* delete one member that matches the specified pattern through the comparing
 * function provided by user. the array must be sorted through epm_arr_sort_by
 * before invoking this function */
void * epm_arr_delete_by (epm_arr_t * ar, void * pattern, EPMArrCmp * cmp);

/* linear search one by one. it's a slow search method. */
void * epm_arr_search (epm_arr_t * ar, void * pattern, EPMArrCmp * cmp);

#ifdef  __cplusplus
}
#endif

#endif

