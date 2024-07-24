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

#ifndef _EPUDP_H_
#define _EPUDP_H_

#ifdef __cplusplus
extern "C" {
#endif

void * epudp_listen_create (void * vpcore, char * localip, int port, void * popt,
                            void * para, IOHandler * cb, void * cbpara,
                            iodev_t ** devlist, int * devnum, int * retval);

void * epudp_listen (void * vpcore, char * localip, int port, void * popt,
                     void * para, IOHandler * cb, void * cbpara, int bindtype,
                     void ** plist, int * listnum, int * pret);

void * epudp_mlisten (void * vpcore, char * localip, int port, void * popt,
                      void * para, IOHandler * cb, void * cbpara);

void * epudp_client (void * vpcore, char * localip, int port, void * popt,
                     void * para, IOHandler * cb, void * cbpara,
                     iodev_t ** devlist, int * devnum, int * retval);

int epudp_recvfrom (void * vdev, void * vfrm, void * pbuf, int bufsize, void * addr, int * pnum);

#ifdef __cplusplus
}
#endif

#endif


