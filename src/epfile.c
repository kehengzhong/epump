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

#include "btype.h"
#include "tsock.h"
#include "strutil.h"

#include "epcore.h"
#include "iodev.h"
#include "ioevent.h"


void * epfile_bind_fd (void * vpcore, int fd, void * para, IOHandler * ioh, void * iohpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
 
    if (!pcore) return NULL;
    if (fd == INVALID_SOCKET) return NULL;
 
    if (!pdev) pdev = iodev_new(pcore);
    if (!pdev) return NULL;
 
    pdev->fd = fd;
    pdev->fdtype = FDT_FILEDEV;

    pdev->para = para;
    pdev->callback = ioh;
    pdev->cbpara = iohpara;
 
    pdev->iostate = IOS_READWRITE;

    iodev_rwflag_set(pdev, RWF_READ);
 
    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, BIND_ONE_EPUMP, 0, 0);

    return pdev;
}

void * epfile_bind_stdin (void * vpcore, void * para, IOHandler * ioh, void * iohpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
    int        fd = 0;
 
    if (!pcore) return NULL;
 
    if (!pdev) pdev = iodev_new(pcore);
    if (!pdev) return NULL;
 
    pdev->fd = fd;
    pdev->fdtype = FDT_STDIN;

    pdev->para = para;
    pdev->callback = ioh;
    pdev->cbpara = iohpara;
 
    pdev->iostate = IOS_READWRITE;

    iodev_rwflag_set(pdev, RWF_READ);
 
    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, BIND_ONE_EPUMP, 0, 0);

    return pdev;
}

int epstdin_callback (void * vpcore, void * pobj, int event, int fdtype)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = (iodev_t *)pobj;
    char       buf[512];
    char     * p = NULL;
 
    if (!pcore || !pdev) return 0;
 
    if (event == IOE_READ && fdtype == FDT_STDIN) {
        memset(buf, 0, sizeof(buf));
        fgets((char *)buf, sizeof(buf), stdin);
        if (strlen((char *)buf) <= 0) return 0;
        p = str_trim(buf);
        if (strlen((char *)p) <= 0) return 0;
 
        if (strncasecmp(p, "show", 4) == 0) {
            iodev_print(pdev);
        } else if (strncasecmp(p, "quit", 4) == 0) {
        }
    } else if (event == IOE_INVALID_DEV) {
        return -100;
    }
 
    return 0;
}
 
