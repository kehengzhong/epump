/*
 * Copyright (c) 2003-2024 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved.
 */

#include "adifall.ext"
#include <signal.h>
#include "epump.h"

epcore_t  * gpcore = NULL;

int echo_pump (void * vpcore, void * vobj, int event, int fdtype);


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
        epcore_stop_epump(gpcore);
        epcore_stop_worker(gpcore);
        usleep(1000);
        break;
    }
}


int main (int argc, char ** argv)
{
    epcore_t  * pcore = NULL;
    void      * mlisten = NULL;
    int         listenport = 8080;

    signal(SIGCHLD, SIG_IGN); /* ignore child */
    signal(SIGTSTP, SIG_IGN); /* ignore tty signals */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP,  signal_handler); /* catch hangup signal */
    signal(SIGTERM, signal_handler); /* catch kill signal */
    signal(SIGINT, signal_handler); /* catch SIGINT signal */

    if (argc > 1 && argv[1])
        listenport = (int)strtol(argv[1], NULL, 10);
    if (listenport < 10 || listenport > 65535)
        listenport = 8080;

    gpcore = pcore = epcore_new(65536);

    /* do some initialization */
    mlisten = eptcp_mlisten(pcore, NULL, listenport,
                            NULL, /* socket option */
                            NULL, /* given listen-device parameter */
                            echo_pump, pcore);
    if (!mlisten) goto exit;

    printf("EchoSrv TCP Port: %d being listened\n\n", listenport);

    iotimer_start(pcore, 90*1000, 1001, NULL, echo_pump, pcore, 0);

    /* start 2 worker threads */
    //epcore_start_worker(pcore, 2);

    /* start 2 epump threads */
    epcore_start_epump(pcore, 2);

    /* main thread executing the epump_main_proc as an epump thread */
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
    iodev_t   * pdev = NULL;
    int         cmdid;
    int         ret = 0, sndnum = 0;
    char        rcvbuf[2048];
    int         num = 0;

    switch (event) {
    case IOE_ACCEPT:
        if (fdtype != FDT_LISTEN)
            return -1;

        while (1) {
            pdev = eptcp_accept(iodev_epcore(vobj), vobj,
                                NULL, /* socket option */
                                NULL, /* accepted device parameter */
                                echo_pump, pcore, BIND_ONE_EPUMP,
                                0, &ret);
            if (!pdev) break;

            printf("\nThreadID=%lu, ListenFD=%d EPumpID=%lu WorkerID=%lu "
               " ==> Accept NewFD=%d EPumpID=%lu\n",
               get_threadid(), iodev_fd(vobj), epumpid(iodev_epump(vobj)),
               workerid(worker_thread_self(pcore)),
               iodev_fd(pdev), epumpid(iodev_epump(pdev)));
        }
        break;

    case IOE_READ:
        ret = tcp_nb_recv(iodev_fd(vobj), rcvbuf, sizeof(rcvbuf), &num);
        if (ret < 0) {
            printf("Client %s:%d close the connection while receiving, epump: %lu\n",
                   iodev_rip(vobj), iodev_rport(vobj), epumpid(iodev_epump(vobj)) );
            iodev_close(vobj);
            return -100;
        }

        printf("\nThreadID=%lu FD=%d EPumpID=%lu WorkerID=%lu Recv %d bytes from %s:%d\n",
               get_threadid(), iodev_fd(vobj), epumpid(iodev_epump(vobj)),
               workerid(worker_thread_self(pcore)),
               num, iodev_rip(vobj), iodev_rport(vobj));
        printOctet(stderr, rcvbuf, 0, num, 2);

        ret = tcp_nb_send(iodev_fd(vobj), rcvbuf, num, &sndnum);
        if (ret < 0) {
            printf("Client %s:%d close the connection while sending, epump: %lu\n",
                   iodev_rip(vobj), iodev_rport(vobj), epumpid(iodev_epump(vobj)));
            iodev_close(vobj);
            return -100;
        }
        break;

    case IOE_WRITE:
    case IOE_CONNECTED:
        break;

    case IOE_TIMEOUT:
        cmdid = iotimer_cmdid(vobj);
        if (cmdid == 1001) { 
            printf("\nThreadID=%lu IOTimerID=%lu EPumpID=%lu timeout, curtick=%lu\n",
                   get_threadid(), iotimer_id(vobj), 
                   epumpid(iotimer_epump(vobj)), time(0));
            epcore_print(pcore, NULL, stdout);
            iotimer_start(pcore, 90*1000, 1001, NULL, echo_pump, pcore, 0);
        }
        break;

    case IOE_INVALID_DEV:
        break;

    default:
        break;
    }

    printf("ThreadID=%lu event: %d  fdtype: %d  WorkerID=%lu\n\n",
            get_threadid(), event, fdtype,
            workerid(worker_thread_self(pcore)));

    return 0;
}

