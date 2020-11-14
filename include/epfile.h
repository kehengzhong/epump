/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _EPFILE_H_
#define _EPFILE_H_

#ifdef __cplusplus
extern "C" {
#endif

void * epfile_bind_fd (void * vpcore, int fd, void * para, IOHandler * ioh, void * iohpara);
void * epfile_bind_stdin (void * vpcore, void * para, IOHandler * ioh, void * iohpara);
int    epstdin_callback (void * vpcore, void * pobj, int event, int fdtype);

#ifdef __cplusplus
}
#endif

#endif


