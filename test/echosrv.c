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
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include "epump.h"

epcore_t  * gpcore = NULL;

int echo_pump (void * vpcore, void * vobj, int event, int fdtype);

int tcp_nb_recv (int fd, char * rcvbuf, int bufsize, int * actnum);
int tcp_nb_send (int fd, char * sndbuf, int towrite, int * actnum);

void printOctet (FILE * fp, void * data, int start, int count, int margin);


static void signal_handler(int sig)
{
    switch(sig) {
    case SIGHUP:
        printf("hangup signal catched\n");
        break;
    case SIGTERM:
    case SIGKILL:
    case SIGINT:
        printf("terminate signal catched, now exiting...\n");
        epcore_stop(gpcore);
        usleep(1000);
        break;
    }
}


int main (int argc, char ** argv)
{
    epcore_t  * pcore = NULL;
    iodev_t   * listendev = NULL;
    int         listenport = 5544;
    int         ret = 0;

    signal(SIGCHLD, SIG_IGN); /* ignore child */
    signal(SIGTSTP, SIG_IGN); /* ignore tty signals */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP,  signal_handler); /* catch hangup signal */
    signal(SIGTERM, signal_handler); /* catch kill signal */
    signal(SIGINT, signal_handler); /* catch SIGINT signal */

    gpcore = pcore = epcore_new(65536);

    /* do some initialization */
    listendev = eptcp_listen(pcore, listenport, NULL, &ret, echo_pump, pcore);
    if (!listendev) goto exit;

    //all threads are all in charge of monitoring TCP incoming event
    iodev_bind_epump(listendev, BIND_ALL_EPUMP, NULL);

    iotimer_start(pcore, NULL, 10*1000, 1001, NULL, echo_pump, pcore);

    /* start 2 threads */
    epump_main_start(pcore, 1);
    epump_main_start(pcore, 1);

    /* main thread executing the epump_mian_proc */
    epump_main_start(pcore, 0);

    epcore_clean(pcore);
    printf("main thread exited\n");
    return 0;

exit:
    epcore_clean(pcore);
    printf("main thread exception exited\n");
    return -1;
}


int echo_pump (void * vpcore, void * vobj, int event, int fdtype)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    epump_t   * epump = NULL;
    iodev_t   * pdev = NULL;
    int         cmdid;
    int         ret = 0, sndnum = 0;
    char        rcvbuf[2048];
    int         num = 0;
    ulong       thid = 0;

    switch (event) {
    case IOE_ACCEPT:
        if (fdtype != FDT_LISTEN)
            return -1;

        pdev = eptcp_accept(iodev_epcore(vobj), vobj, NULL, &ret, echo_pump, pcore);

        epump = iodev_epump(vobj);
        if (!epump) epump = epcore_thread_self(pcore);
        if (!epump) printf("#### threadid=%lu cannot find self\n", pthread_self());

        thid = epumpid(epump);
        if (!pdev) {
            printf("epump: %lu not accept TCP connection successfully\n", thid);
            return 0;
        }

        /* select one lowest load epump thread to be bound */
        epump = epcore_thread_select(pcore);
        iodev_bind_epump(pdev, BIND_GIVEN_EPUMP, epump);

        if (thid != epumpid(epump))
            printf("epump: %lu accept TCP, fd: %d, but bind another epump: %lu\n",
                    thid, iodev_fd(pdev), epumpid(epump));
        else
            printf("epump: %lu accept TCP, fd: %d\n", thid, iodev_fd(pdev));

        break;

    case IOE_READ:
        epump = iodev_epump(vobj);

        ret = tcp_nb_recv(iodev_fd(vobj), rcvbuf, sizeof(rcvbuf), &num);
        if (ret < 0) {
            printf("Client %s:%d close the connection while receiving, epump: %lu\n",
                   iodev_rip(vobj), iodev_rport(vobj), epumpid(epump));
            iodev_close(vobj);
            return -100;
        }

        printf("\nRecv %d bytes from %s:%d\n", num, iodev_rip(vobj), iodev_rport(vobj));
        printOctet(stderr, rcvbuf, 0, num, 2);

        ret = tcp_nb_send(iodev_fd(vobj), rcvbuf, num, &sndnum);
        if (ret < 0) {
            printf("Client %s:%d close the connection while sending, epump: %lu\n",
                   iodev_rip(vobj), iodev_rport(vobj), epumpid(epump));
            iodev_close(vobj);
            return -100;
        }
        break;

    case IOE_WRITE:
    case IOE_CONNECTED:
        break;

    case IOE_TIMEOUT:
        epump = iotimer_epump(vobj);

        cmdid = iotimer_cmdid(vobj);
        if (cmdid == 1001) {
            printf("iotimer cmdid timeout, curtick=%lu\n", time(0));
            epcore_print(pcore);
            iotimer_start(pcore, NULL, 10*1000, 1001, NULL, echo_pump, pcore);
        }
        break;

    case IOE_INVALID_DEV:
        break;

    default:
       break;
    }

    printf("event: %d  fdtype: %d  epump: %lu\n\n", event, fdtype, epumpid(epump));

    return 0;
}




int tcp_nb_recv (int fd, char * rcvbuf, int bufsize, int * actnum)
{
    int     ret=0, readLen = 0;
    int     errcode;
    int     errtimes = 0;

    if (actnum) *actnum = 0;

    if (fd < 0) return -70;
    if (!rcvbuf || bufsize <= 0) return 0;

    for (readLen = 0, errtimes = 0; readLen < bufsize; ) {
        errno = 0;
        ret = recv(fd, rcvbuf+readLen, bufsize-readLen, MSG_NOSIGNAL);
        if (ret > 0) {
            readLen += ret;
            continue;
        }

        if (ret == 0) {
            if (actnum) *actnum = readLen;
            return -20; /* connection closed by other end */

        } else if (ret == -1) {
            errcode = errno;
            if (errcode == EINTR) {
                continue;
            }
            if (errcode == EAGAIN || errcode == EWOULDBLOCK) {
                if (++errtimes >= 3) break;
                continue;
            }
            ret = -30;
            goto error;
        }
    }

    if (actnum) *actnum = readLen;
    return readLen;

error:
    if (actnum) *actnum = readLen;
    return ret;
}


int tcp_nb_send (int fd, char * sndbuf, int towrite, int * actnum)
{
    int     sendLen = 0;
    int     ret, errcode;
    int     errtimes = 0;

    if (actnum) *actnum = 0;

    if (fd < 0) return -1;
    if (!sndbuf) return 0;
    if (towrite <= 0) return 0;

    for (sendLen = 0, errtimes = 0; sendLen < towrite; ) {
        errno = 0;
        ret = send (fd, sndbuf+sendLen, towrite-sendLen, MSG_NOSIGNAL);
        if (ret == -1) {
            errcode = errno;
            if (errcode == EINTR || errcode == EAGAIN || errcode == EWOULDBLOCK) {
                if (++errtimes >= 3) break;
                continue;
            }
            ret = -30;
            goto error;
        } else {
            sendLen += ret;
        }
    }

    if (actnum) *actnum = sendLen;
    return sendLen;
error:
    if (actnum) *actnum = sendLen;
    return ret;
}

static char toASCII (char ch, int upercase)
{
    char bch = 'a';

    if (upercase) bch = 'A';

    if (ch <= 9) return ch + '0';
    if (ch >= 10 && ch <= 15) return ch - 10 + bch;
    return '.';
}

void printOctet (FILE * fp, void * data, int start, int count, int margin)
{
#define CHARS_ON_LINE 16
#define MARGIN_MAX    20

    char  hexbyte[CHARS_ON_LINE * 5];
    int  ch;
    char  marginChar [MARGIN_MAX + 1];
    int lines, i, j, hexInd, ascInd, iter;

    if (start < 0) start = 0;

    lines = count / CHARS_ON_LINE;
    if (count % CHARS_ON_LINE) lines++;

    memset (marginChar, ' ', MARGIN_MAX + 1);
    if (margin < 0) margin = 0;
    if (margin > MARGIN_MAX) margin = MARGIN_MAX;
    marginChar[margin] = '\0';

    for (i = 0; i < lines; i++) {
        hexInd = 0;
        ascInd = 4 + CHARS_ON_LINE * 3;
        memset(hexbyte, ' ', CHARS_ON_LINE * 5);

        for (j = 0; j < CHARS_ON_LINE; j++) {
            if ( (iter = j + i * CHARS_ON_LINE) >= count)
                break;
            ch = ((unsigned char *)data)[iter+start];

            hexbyte[hexInd++] = toASCII(((ch>>4)&15), 1);
            hexbyte[hexInd++] = toASCII((ch&15), 1);
            hexInd++;

            hexbyte[ascInd++] = (ch>=(unsigned char)32 && ch<=(uint8)126) ? ch : '.';
        }
        hexbyte[CHARS_ON_LINE * 4 + 4] = '\n';
        hexbyte[CHARS_ON_LINE * 4 + 5] = '\0';

        fprintf(fp, "%s0x%04X   %s", marginChar, i, hexbyte);
        fflush(fp);
    }
}

