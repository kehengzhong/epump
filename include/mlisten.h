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

#ifndef _MLISTEN_H_
#define _MLISTEN_H_

#ifdef __cplusplus
extern "C" {
#endif

/* The SO_REUSEPORT socket option allows multiple sockets on the same host to bind
   to the same port, and is intended to improve the performance of multithreaded 
   network server applications running on top of multicore systems.

   multiple listen sockets binding to the same port is only supported in Linux kernel
   with version >= 3.9.x

   mlisten_t is defined for the multi-listen function. each epump thread will have
   one listen socket with same port. The accept will be balanced for multi-threads 
   in kernel that only one epump thread got accept success!
 */

typedef struct mlisten_st_ {
    char         localip[41];
    int          port;

    /* if supported or started REUSEPORT function */
    int          reuseport;

    void       * popt;

    void       * para;
    IOHandler  * cb;
    void       * cbpara;

    int          fdtype; 
    arr_t      * devlist;

    void       * pcore;
} mlisten_t;

void * mlisten_alloc (char * localip, int port, int fdtype, void * popt, void * para, IOHandler * cb, void * cbpara);
void   mlisten_free  (void * vmln);
int    mlisten_iodev_add (void * vmln, void * pdev);

int    mlisten_port (void * vmln);
char * mlisten_lip (void * vmln);

int    epcore_mlisten_init (void * epcore);
int    epcore_mlisten_clean (void * epcore);

int    epcore_mlisten_add (void * epcore, void * vmln);
void * epcore_mlisten_get (void * epcore, char * localip, int port, int fdtype);
void * epcore_mlisten_del (void * epcore, void * vmln);

int    epcore_mlisten_create (void * epcore, void * vepump);


void * mlisten_open  (void * epcore,  char * localip, int port, int fdtype,
                      void * popt, void * para, IOHandler * cb, void * cbpara);
int    mlisten_close (void * vmln);


#ifdef __cplusplus
}
#endif

#endif


