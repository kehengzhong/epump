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

#ifndef _EPTCP_H_
#define _EPTCP_H_

#ifdef __cplusplus
extern "C" {
#endif

void * eptcp_listen_create (void * vpcore, char * localip, int port, void * popt,
                            void * para, IOHandler * cb, void * cbpara,
                            iodev_t ** devlist, int * devnum, int * retval);

void * eptcp_listen (void * vpcore, char * localip, int port, void * popt, void * para,
                     IOHandler * cb, void * cbpara, int bindtype, void ** plist,
                     int * listnum, int * pret);

/* Note: automatically detect if Linux kernel supported REUSEPORT.
   if supported, create listen socket for every current running epump threads
   and future-started epump threads.
   if not, create only one listen socket for all epump threads to bind. */

void * eptcp_mlisten (void * vpcore, char * localip, int port, void * para,
                      IOHandler * cb, void * cbpara);

void * eptcp_accept (void * vpcore, void * vld, void * popt, void * para, IOHandler * cb,
                     void * cbpara, int bindtype, ulong threadid, int * retval);

void * eptcp_connect (void * vpcore, char * host, int port,
                      char * localip, int localport, void * popt, void * para,
                      IOHandler * cb, void * cbpara, ulong threadid, int * retval);

void * eptcp_nb_connect (void * vpcore, char * host, int port, char * localip,
                         int localport, void * popt, void * para, IOHandler * cb,
                         void * cbpara, ulong threadid, int * retval);

#ifdef __cplusplus
}
#endif

#endif


