/*
 * Copyright (c) 2003-2024 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 *
 * #####################################################
 * #                       _oo0oo_                     #
 * #                      o8888888o                    #
 * #                      88" . "88                    #
 * #                      (| -_- |)                    #
 * #                      0\  =  /0                    #
 * #                    ___/`---'\___                  #
 * #                  .' \\|     |// '.                #
 * #                 / \\|||  :  |||// \               #
 * #                / _||||| -:- |||||- \              #
 * #               |   | \\\  -  /// |   |             #
 * #               | \_|  ''\---/''  |_/ |             #
 * #               \  .-\__  '-'  ___/-. /             #
 * #             ___'. .'  /--.--\  `. .'___           #
 * #          ."" '<  `.___\_<|>_/___.'  >' "" .       #
 * #         | | :  `- \`.;`\ _ /`;.`/ -`  : | |       #
 * #         \  \ `_.   \_ __\ /__ _/   .-` /  /       #
 * #     =====`-.____`.___ \_____/___.-`___.-'=====    #
 * #                       `=---='                     #
 * #     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   #
 * #               佛力加持      佛光普照              #
 * #  Buddha's power blessing, Buddha's light shining  #
 * #####################################################
 */

#ifndef _IOTIMER_H_
#define _IOTIMER_H_

#include "btype.h"
#include "btime.h"


#ifdef __cplusplus
extern "C" {
#endif

#define IOTCMD_IDLE  1

#pragma pack(push ,1)

typedef struct IOTimer_ {
    void       * res[4];

    int          cmdid;
    ulong        id;
    void       * para;
    btime_t      bintime;

    IOHandler  * callback;
    void       * cbpara;

    void       * epump;
    void       * epcore;

    ulong        threadid;

} iotimer_t, *iotimer_p;

#pragma pack(pop)


int  iotimer_init (void * vtimer);
void iotimer_void_free(void * vtimer);
int  iotimer_free(void * vtimer);

int  epcore_iotimer_free (void * vtimer);

int  iotimer_cmp_iotimer(void * a, void * b);
int  iotimer_cmp_id (void * a, void * b);
ulong iotimer_hash_func (void * key);

iotimer_p iotimer_fetch (void * vpcore);
int       iotimer_recycle (void * vpcore, ulong iotid);

void * iotimer_start(void * vpcore, int ms, int cmdid, void * para, 
                     IOHandler * cb, void * cbpara, ulong epumpid);
#define iotimer_stop(pcore, iot) iotimer_stop_dbg((pcore), (iot), __FILE__, __LINE__)
int    iotimer_stop_dbg (void * vpcore, void * viot, char * file, int line);

void   epump_iotimer_print (void * vepump, int printtype);


/* return value: if ret <=0, that indicates has no timer in
 * queue, system should set blocktime infinite,
   if ret >0, then pdiff carries the next timeout value */
int iotimer_check_timeout(void * vepump, btime_t * pdiff, int * pevnum);

ulong  iotimer_id (void * viot);
int    iotimer_cmdid (void * viot);
void * iotimer_para (void * viot);
void * iotimer_epump (void * viot);
ulong  iotimer_epumpid (void * viot);

ulong  iotimer_workerid     (void * viot);
void   iotimer_workerid_set (void * viot, ulong workerid);

#ifdef __cplusplus
}
#endif

#endif

