## ePump - an event-driven, multi-threaded c-framework

*A C-language framework based on I/O event notification, non-blocking communication and multi-threaded event-driven model helps you to develop servers with high-performance and numerous concurrent connections.*

*ePump是一个基于I/O事件通知、非阻塞通信、多路复用、多线程等机制开发的事件驱动模型的 C 语言应用开发框架，利用该框架可以很容易地开发出高性能、大并发连接的服务器程序。*


## 目录
* [一. ePump是什么？](#一-epump是什么)
* [二. ePump解决什么？](#二-epump解决什么)
* [三. ePump框架工作原理](#三-ePump框架工作原理)
    * [3.1 ePump基础数据结构](#31-ePump基础数据结构)
        * [3.1.1 设备（iodev_t）](#311-设备iodev_t)
        * [3.1.2 定时器（iotimer_t）](#312-定时器iotime_rt)
        * [3.1.3 事件（ioevent_t）](#313-事件ioevent_t)
    * [3.2 ePump多线程架构](#32-epump多线程架构)
* [四. ePump框架工作模型](#四-epump框架工作模型)
    * [4.1 快业务模型 -- 没有worker线程，只有ePump线程](#41-快业务模型----没有worker线程只有ePump线程)
    * [4.2 复合业务模型 -- 少数ePump线程，大多数worker线程](#42-复合业务模型----少数ePump线程大多数worker线程)
* [五. ePump框架中的文件描述符FD](#五-epump框架中的文件描述符fd)
* [六. ePump框架的回调（Call Back）机制](#六-epump框架的回调call-back机制)
* [七. ePump框架的调度（Scheduling）机制](#七-epump框架的调度scheduling机制)
    * [7.1 iodev_t设备绑定ePump线程](#71-iodev_t设备绑定epump线程)
        * [7.1.1 Listen服务端口类的iodev_t设备](#711-listen服务端口类的iodev_t设备)
        * [7.1.2 非Listen的iodev_t设备](#712-非Listen的iodev_t设备)
    * [7.2 iotimer_t定时器](#72-iotimer_t定时器)
    * [7.3 ioevent_t事件](#73-ioevent_t事件)
    * [7.4 ePump线程](#74-epump线程)
    * [7.5 worker工作线程](#75-worker工作线程)
* [八. ePump框架中惊群效应的处理机制](#八-epump框架中惊群效应的处理机制)
    * [8.1 惊群效应（Thundering Herd Problem）](#81-惊群效应thundering-herd-problem)
    * [8.2 惊群效应消耗什么？](#82-惊群效应消耗什么)
    * [8.3 ePump框架中存在的惊群问题](#83-epump框架中存在的惊群问题)
        * [8.3.1 worker线程组不存在惊群问题](#831-worker线程组不存在惊群问题)
        * [8.3.2 ePump线程组的惊群问题](#832-epump线程组的惊群问题)
        * [8.3.3 规避或弱化ePump框架惊群问题的措施](#833-规避或弱化epump框架惊群问题的措施)
* [九. How to build ePump](#九-how-to-build-epump)
* [十. How to integrate](#十-how-to-integrate)
* [十一. ePump框架相关的另外两个开源项目](#十一-epump框架相关的另外两个开源项目)
    * [adif 项目](#adif-项目)
    * [eJet Web服务器项目](#ejet-web服务器项目)
* [十二. 关于作者 老柯 (laoke)](#十二-关于作者-老柯-laoke)


***

一. ePump是什么？
------

ePump是一个基于I/O事件通知、非阻塞通信、多路复用、多线程等机制开发的事件驱动模型的 C 语言应用开发框架，利用该框架可以很容易地开发出高性能、大并发连接的服务器程序。

ePump是事件泵（Event Pump）的英文简称，顾名思义，意思是对各种网络读写事件、定时器事件等进行循环处理的泵，这些底层事件包括文件描述符FD的读就绪（Read Readiness）、写就绪（Write Readiness）、连接成功（Connected）、定时器超时（Timeout）等等。

ePump负责管理和监控处于非阻塞模式的文件描述符FD和定时器，根据其状态变化产生相应的事件，并派发到相应的工作线程或ePump线程的事件队列中，这些线程通过调用该事件关联的回调函数（Callback）来处理事件。

应用程序调用ePump框架提供的接口函数来预先创建、打开各种网络通信Socket文件描述符FD，或启动定时器等，并将其添加或绑定到ePump线程的监控队列中，对这些FD和定时器的状态监控是采用操作系统提供的I/O事件通知设施，如epoll、select、poll、kqueue、completion port等。


二. ePump解决什么？
------

许多服务器程序需要处理来自客户侧发起的大并发TCP连接请求、UDP请求，如Web服务器、Online服务器、消息系统等。早期实现的通信服务器类系统中，一个连接请求通常是由一个独立的进程或线程来接受并处理通信细节，如早先的Apache Web服务器；或者是利用OS提供的I/O异步事件通知、多路复用机制实现单进程下处理多个非阻塞并发连接请求，如SQUID系统。

这些系统采用的框架，要不在等待网络等I/O设备的数据到来之前阻塞自己，要不采用单进程多路复用模型，它们对CPU的利用效率多少存在一定的局限，而ePump框架是一种充分高效利用CPU处理能力的事件驱动模型框架。

ePump框架是一个多线程（未来增加多进程）事件驱动模型框架，基于文件描述符的异步就绪通知（Readiness Notification）机制，无需为等待"在路上"的数据而阻塞等待工作线程或工作进程。

该框架为每个文件描述符创建iodev_t对象，为定时驱动的应用程序创建定时器iotimer_t对象，利用操作系统提供的I/O事件通知设施如epoll、select等，将创建或打开的文件描述符FD设置为非阻塞模式，并添加到系统的监控管理列表中，对其状态变化进行异步回调通知。

对这两类对象的监控管理和事件通知派发是由ePump线程池来实现，对事件的回调处理是由Worker工作线程池或ePump线程池来完成。为了充分利用服务器硬件的性能，工作处理线程的个数一般跟CPU Core核数量一致。

大量复杂的底层处理细节都被封装成一些简单易用的API接口函数，通过这些API函数，开发者可以快速开发出支撑大并发的高性能服务器程序。


三. ePump框架工作原理
------
 
ePump框架是作者在其2003年开发的eProbe框架的基础上发展而来，是Event Pump的缩写，顾名思义这是一个事件驱动架构。
 
对于不同的I/O事件通知、非阻塞通信、多路复用机制，包括epoll、select、kqueue、completion port i/o等，其基本工作原理包括：
* 将FD增加到监听列表中
* 将FD从监听列表中删除
* 设置阻塞监听的时间
* 阻塞等待监听列表中FD set，等候R/W事件发生
* 轮询FD set列表检测各FD是否产生R/W事件，执行该事件对应的回调函数
* 检查Timeout，执行Timeout事件对应的回调函数


### 3.1 ePump基础数据结构

根据以上工作原理，我们设计ePump框架的几个基础数据结构：

#### 3.1.1 设备（iodev_t）

针对每个FD，用数据结构为iodev_t来管理，将文件描述符FD当作iodev_t设备，针对设备来管理读写状态、FD类型、要处理的读写事件、回调函数和回调参数、四元组地址等等. 我们把TCP监听socket、TCP连接socket（主动连接的、被动接收的）、UDP监听socket、UDP客户socket、Unix Socket、ICMP Raw Socket、UDP Raw Socket等等，都通过iodev_t设备来管理。

所有的iodev_t设备都会产生事件，ePump系统对iodev_t设备产生的事件进行处理，即通过事件驱动多线程来调用回调函数。
 
#### 3.1.2 定时器（iotimer_t）

类似iodev_t设备，能产生驱动事件的还有iotimer_t定时器, 设定一个时间并启动定时器后，系统将从当前时刻起到指定时间到达时，产生Timeout事件。

iotimer_t定时器有一次性的和周期性的，iotimer_t定时器数据结构管理定时器id、回调函数和回调参数、定时的时间等。
 
在Unix类OS系统，一个进程只能设置一个时钟定时器，由系统提供的接口来设置，常用的有alarm()和setitimer()。对于通信系统中大量存在各种定时器需求，同时考虑跨平台性等，系统提供的定时器接口一般都不能满足需求。我们在ePump系统中设计了iotimer_t数据结构，可提供毫秒级精度、同时大并发数量的定时器功能实现。
 
ePump架构中把定时器当做一个重要的基础设施，与文件描述符设备一样被ePump线程监听和管理。


#### 3.1.3 事件（ioevent_t）

ioevent_t事件是ePump的信使，管理事件类型、产生事件的对象、事件的回调函数和参数。

iodev_t设备基于各种硬件设备的R/W状态变动，触发ioevent_t事件的产生，而iotimer_t定时器根据设定的定时时间，当指定时间超时，就触发超时Timeout事件。

此外，应用程序可以注册用户钩子（Hook）事件，注册的用户钩子（Hook）事件需要绑定Callback回调函数和回调参数，最主要的是定义用户事件触发条件。

各种条件下产生的这些事件，都会被派送到工作线程的事件队列，驱动工作线程来进行事件处理，或者激活相应的回调函数来处理事件。


### 3.2 ePump多线程架构

ePump架构是由多线程来构成的，按照工作流程，这些线程分成两类，一类是ePump线程，另一类是worker线程。ePump线程职能主要是负责监听文件描述的R/W读写状态和定时器队列，创建读写事件和定时器事件，并将ioevent_t事件派发到各个worker线程的事件队列中。worker线程职能是监听事件队列，执行事件队列中各个事件关联的回调函数。
 
每个ePump线程采用I/O事件异步通知、非阻塞通信、多路复用等机制和模型，利用select/poll/epoll等系统调用，当被监听的文件描述符处于I/O读写就绪（I/O Readiness）时，ePump就会创建针对这些文件描述符的R/W读写事件，将这些R/W读写事件包装成ePump框架中标准的ioevent_t事件，并将其派发到各个worker工作线程的FIFO事件队列中。这些Event Queue FIFO事件队列是线程事件驱动模型的核心，每个ePump线程和每个worker线程都有一个这样的FIFO事件队列。此外，ePump线程还要维持并处理定时器队列，当定时器超时时，创建定时器超时ioevent_t事件，派发到相应的worker工作线程的事件队列中。
 
worker线程的主要职能就是阻塞等待该线程绑定的事件队列，当有事件到达时，通过唤醒机制，唤醒处于挂起状态的worker线程，被唤醒的工作线程将从其FIFO事件队列中，逐个地、循环地取走并处理事件队列的ioevent_t事件。
 
ioevent_t事件的处理流程基于回调函数注册机制，应用层在创建或打开文件描述符FD，或启动定时器时，将该FD对应的iodev_t设备和定时器实例，注册并绑定回调函数。这样，ePump框架中iodev_t设备、iotimer_t定时器等在创建ioevent_t事件时，都会将其注册绑定的回调函数和回调参数，设置到ioevent_t事件中。各worker工作线程从事件队列中获取到ioevent_t事件后，执行其设置的回调函数即可。
 
ePump线程除了监听文件描述符FD对应的iodev_t设备、管理iotimer_t定时器队列、创建ioevent_t事件、派发ioevent_t事件到各个事件队列外，也可以绑定一个FIFO事件队列，并以调用事件回调函数的方式处理事件队列的事件。
 
为了保证工作效率，ePump架构的线程总数，即包括ePump线程和worker工作线程，最好为CPU的Core Processor数量，这样能确保完全并行处理。
 
 
四. ePump框架工作模型
------

先定义清楚什么是快业务和慢业务。快业务是指接收到客户端的请求后，其业务处理过程相对简单快速，没有长时间阻塞和等待的业务处理流程；相反，慢业务则是指在处理客户端的请求时，需要较长时间的阻塞和等待，如存在数据库慢查询、慢插入的业务流程等。

ePump框架结构非常灵活，基于业务情况，可分成两类工作模型：

### 4.1 快业务模型 -- 没有worker线程，只有ePump线程

* ePump线程既负责iodev_t和iotimer_t的监听、ioevent_t事件的创建和分发，同时还可以充当工作线程的职能，处理其FIFO事件队列中的ioevent_t事件。类似这个工作模型的应用系统是Nginx Web服务器。

* 这个模型最大的缺点是：一旦通过调用回调函数处理事件期间，出现慢业务情况，即长时间等待或阻塞等，譬如读写数据库时，长时间阻塞等待查询结果等，就会导致后续其他的iodev_t设备中的文件描述FD的I/O就绪（Readiness）状态，及iotimer_t定时器超时状态，不能被及时有效地处理。一个事件的处理延迟，会导致大量其他iodev_t设备的状态变化、或定时器的超时等得不到及时快速的处理，从而产生总体处理上的延迟、阻塞、甚至没有响应或者崩溃。

* 针对这类慢业务，采用类似于Apache Web服务器那种独占式进程/线程架构模型比较适合，但总体来说，这种独占式进程/线程模型，对多核CPU并行处理能力的利用效率非常低下，并发数量较低。

* 该模型最大的好处是：对多核CPU并行计算和处理的利用效率可达到极致，适合处理那种需要快速响应型的通信或业务系统。
 
### 4.2 复合业务模型 -- 少数ePump线程，大多数worker线程

* ePump线程只负责iodev_t和iotimer_t的监听、ioevent_t事件的创建和分发，不负责处理事件。worker线程负责处理所有产生的ioevent_t事件，调用这些事件的回调函数，从而处理应用层业务流程。

* worker线程执行上层应用注册的回调函数时，执行过程的阻塞并不会瘫痪其他iodev_t设备或定时器等的事件，能确保其他设备或定时器事件能通过其他worker工作线程进行及时有效的处理。

* 这种模型的好处是可以一定程度很好地解决了慢业务类应用的需求，同时非常高效地利用多核CPU的并行计算处理能力。

* 使用ePump框架的复合业务模型时，线程总数建议为CPU的Core Processor的数量，其中ePump线程数量为CPU Core总数的10-20%，worker线程数量为CPU Core总数的80-90%。譬如CPU为32核的服务器，运行ePump架构开发的程序时，ePump线程数配置为3-6个，worker工作线程数配置为26-29个。

 
五. ePump框架中的文件描述符FD
------
 
在Unix、Linux操作系统中，将一切与I/O读写相关的物理设备或虚拟设备都看作是文件，包括普通文件，目录，字符设备文件（如键盘、鼠标），块设备文件（如硬盘、光驱），网络套接字Socket等，均抽象成文件。文件描述符是操作系统内核kernel管理被打开的文件结构而分配的索引，是一个整型数值。内核为每个进程维护一个文件描述表，针对该表的索引即文件描述符fd从0开始，0为标准输入，1为标准输出，2为标准错误输出。在进程中打开的每个文件，都会分配一个文件描述符fd，来对应到该进程的文件描述表某个索引项中，通过fd来读写和访问文件。
 
缺省地，一个进程打开的文件描述符总数是有限制的，Linux系统，这种限制包括两个方面，用户级限制和内核级限制。内核级限制是受限于硬件资源和操作系统处理的I/O能力，而制定的一个所有用户进程总计能打开的最大文件描述符总数，可以用shell命令：
```bash
        sysctl -a | grep file
```
或
```bash
        cat /proc/sys/fs/file-max
```
查看内核级限制。由于系统内核同时打开的文件总数有限制，对每个用户和进程相应地限制打开的文件最大数量，这个是用户级的限制，这个数量缺省一般是1024，即缺省情况下，进程能打开的文件描述符总数是1024。
 
ePump系统在初始化时，把打开的文件描述符总数作为初始化输入参数，通过系统调用setrlimit来修改，以提高包括网络socket在内的文件描述符总数，从而提升系统最大I/O并发处理能力。
 
ePump框架对文件描述符进行了封装，采用iodev_t数据结构来管理每一个文件描述符，将文件描述符、类型、回调函数、四元组地址、读写状态、关联线程等信息统一封装管理，ePump线程负责对iodev_t设备的I/O读写状态进行监听，一旦收到I/O读写就绪通知（Readiness Notification）就创建ioevent_t事件，不同的I/O读写状态，就会创建不同的事件，通过对这些事件注册不同的回调函数，来实现事件驱动模型的处理闭环。

针对文件描述符的各种不同的I/O读写状态，ePump架构中定义了多种文件描述符类型：
```c
    #define FDT_LISTEN            0x01
    #define FDT_CONNECTED         0x02
    #define FDT_ACCEPTED          0x04
    #define FDT_UDPSRV            0x08
    #define FDT_UDPCLI            0x10
    #define FDT_USOCK_LISTEN      0x20
    #define FDT_USOCK_CONNECTED   0x40
    #define FDT_USOCK_ACCEPTED    0x80
    #define FDT_RAWSOCK           0x100
    #define FDT_FILEDEV           0x200
    #define FDT_TIMER             0x10000
    #define FDT_USERCMD           0x20000
    #define FDT_LINGER_CLOSE      0x40000
    #define FDT_STDIN             0x100000
    #define FDT_STDOUT            0x200000
```

基于文件描述符构建的iodev_t设备是ePump框架最基础的物理设施，本质上说，ePump就是一个管理文件描述符的系统。文件描述符产生的事件就像血液一样驱动运转整个ePump框架。


六. ePump框架的回调（Call Back）机制
------
 
根据业务逻辑，软件模块一般采用分层模型，不同的模块之间一般通过函数接口来相互调用，但在分层逻辑中下层模块通常作为基础能力设施，譬如进行运算、I/O读写等功能，提供函数调用接口给上层模块，上层模块通过下层模块的接口函数来使用其运算、读写等功能。作为底层支撑模块，下层模块如何调用上层模块的函数功能呢？这就是回调（CallBack）机制。
 
ePump框架作为底层基础设施，给不同的业务系统提供功能支撑，业务系统的流程实现纷繁复杂，通过回调（Callback）机制，将实现上层业务系统的函数指针注册到ePump框架的文件描述符设备或定时器中，当ePump监听到设备和定时器的I/O读写状态、定时器超时状态发生变化时，通过事件驱动模型，执行上层系统注册到发生状态变化的设备和定时器的回调函数，从而运用ePump底层多线程CPU并行处理运算处理能力来解决复杂的业务流程的目的。
 
ePump的回调（CallBack）机制封装在ePump对上层模块提供的接口函数中，在ePump的接口函数中，一般包含有需要传入的函数指针，这个函数指针指向的是上层业务函数，它就是ePump的回调函数，回调函数的原型定义如下：

```c
   typedef int IOHandler (void * vmgmt, void * pobj, int event, int fdtype);
```

第一个参数由上层模块ePump接口函数的参数传入，第二参数pobj、第三个参数event、第四个参数fdtype，是ePump回调返回时传递的参数。其中
   * pobj   是ePump产生I/O读写就绪ready时的iodev_t设备对象或者iotimer_t定时器对象
   * event  是事件类型
   * fdtype 是文件描述符类型
 
ePump中管理的iodev_t设备对象和iotime_t定时器对象，在状态发生变化时，ePump会产生相应的事件，这些事件类型如下：
```c
    /* event types include getting connected, connection accepted, readable,
     * writable, timeout. the working threads will be driven by these events */
    #define IOE_CONNECTED        1
    #define IOE_CONNFAIL         2
    #define IOE_ACCEPT           3
    #define IOE_READ             4
    #define IOE_WRITE            5
    #define IOE_INVALID_DEV      6
    #define IOE_TIMEOUT          100
    #define IOE_USER_DEFINED     10000
```

ePump对上层提供的基本接口函数如下：
```c
void * eptcp_listen  (void * vpcore, char * localip, int port, void * para, int * retval,
                      IOHandler * cb, void * cbpara, int bindtype);
void * eptcp_mlisten (void * vpcore, char * localip, int port, void * para,
                      IOHandler * cb, void * cbpara);
void * eptcp_accept  (void * vpcore, void * vld, void * para, int * retval,
                      IOHandler * cb, void * cbpara, int bindtype);
void * eptcp_connect (void * vpcore, struct in_addr ip, int port, char * localip, int localport,
                      void * para, int * retval, IOHandler * cb, void * cbpara);
 
void * epudp_listen (void * pcore, char * lip, int port, void * para, int * ret, IOHandler * cb, void * cbp);
void * epudp_client (void * pcore, char * lip, int port, void * para, int * ret, IOHandler * cb, void * cbp);
 
void * epusock_connect (void * pcore, char * sock, void * para, int * ret, IOHandler * cb, void * cbp);
void * epusock_listen  (void * pcore, char * sock, void * para, int * ret, IOHandler * cb, void * cbp);
void * epusock_accept  (void * pcore, void * vld, void * para, int * ret, IOHandler * cb, void * cbp);
 
void * epfile_bind_fd    (void * pcore, int fd, void * para, IOHandler * cb, void * cbp);
void * epfile_bind_stdin (void * pcore, void * para, IOHandler * cb, void * cbp);
 
void * iotimer_start (void * pcore, int ms, int cmdid, void * para, IOHandler * cb, void * cbp);
int    iotimer_stop  (void * viot);
```
 
ePump框架提供的功能接口函数涵盖了TCP、UDP、Unix Socket等通信设施所产生的文件描述符事件监听，和定时器事件的监听。对于除了TCP、UDP、Unix Socket之外的文件描述符，可以使用epfile_bind_fd接口来创建并绑定文件描述符设备，这样可以扩展到任意文件描述符FD都可以加入到ePump架构中进行管理和事件驱动。
 

七. ePump框架的调度（Scheduling）机制
------
 
调度（scheduling）是按照一定的机制和算法对相关资源进行分配的过程，ePump框架的资源主要是iodev_t设备、iodev_t定时器、ioevent_t事件、ePump线程、worker工作线程，调度机制也是围绕这些资源的分派来设计。

### 7.1 iodev_t设备绑定ePump线程

通过各种应用接口创建iodev_t设备后，需要选择一个ePump线程来执行该设备的监听和就绪通知（Readiness Notification），并将当前iodev_t设备和选择的ePump线程建立绑定关系，有绑定的ePump线程来监听和产生各种R/W事件。如何分配ePump线程需要取决于iodev_t的设备类型和绑定类型。

####  7.1.1 Listen服务端口类的iodev_t设备

需要所有ePump线程都绑定该iodev_t设备，或对于支持SO_REUSEPORT Socket选项的操作系统，需要为每一个ePump线程在同一个主机、同一个Listen端口上创建多个iodev_t Listen设备，并绑定到该ePump线程中。这样做的目的是确保当有客户端网络连接请求时，所有ePump线程都能均衡地平分负载。当然，对于Linux内核版本低于3.9.x的系统，存在惊群效应，如何处理请参见第8.3.2节。

#### 7.1.2 非Listen的iodev_t设备

* **指定ePump线程**  
    根据调用参数指定的ePump线程来建立绑定关系。

* **根据ePump线程的最低负载**  
    ePump的负载主要是该线程绑定的iodev_t设备数量、iotimer_t定时器数量、该线程最近单位时间内产生的ioevent_t数量等指标来衡量，选择最低负载的ePump线程，可以让负载均衡地分摊到各个ePump线程中，从提升系统工作效率。

### 7.2 iotimer_t定时器

应用程序启动iotimer_t定时器时，ePump框架一般根据ePump线程的当前负载，选择负载最低的ePump线程来绑定，由绑定的ePump线程来管理和监控，并负责产生超时事件。

绑定ePump线程一般是将iotimer_t定时器对象添加到该ePump线程的管理定时器列表的红黑树结构中，如果当前ePump线程处于阻塞挂起状态，通过激活机制唤醒当前ePump线程，并基于定时器树型结构中离当前时刻最短时长来重新启动系统调用。

### 7.3 ioevent_t事件

除了用户事件外，基本所有ioevent_t事件都由ePump线程产生，当然也由ePump线程根据相关机制和算法来调度，将其派发到worker工作线程或ePump线程的FIFO事件队列中，进行事件回调函数的调用处理。ioevent_t事件的寿命周期较短，即被创建、被分派到事件队列、被线程执行其回调函数、执行完毕，其实例对象会被回收而结束寿命。

ioevent_t事件一般都绑定了某个iodev_t设备或iotimer_t定时器，当前ioevent_t事件派发调度到哪一个worker线程，直接决定ePump框架系统运行的效率。

同一个iodev_t设备连续产生的基本相同的ioevent_t事件则会被调度机制抛弃。

### 7.4 ePump线程

ePump线程是ePump框架的核心设施，负责对iodev_t设备列表和iotimer_t定时器列表进行管理，通过epoll_wait或select等系统调用，阻塞等待设备R/W就绪通知或定时器超时，并产生ioevent_t事件，负责对这些事件进行调度派发。

早期的eProbe框架中，只有一个全局的FIFO事件队列，产生的所有事件都添加到事件队列尾部，然后所有空闲的worker工作线程都来争抢FIFO队列事件并进行处理。这个调度模型简单，能均衡地分配工作任务，一旦某个事件处理过程中堵塞时，并不影响其他事件的处理。但由于FIFO队列的共享锁、广播式唤醒所有工作线程的惊群效应、同一个设备的连续产生的多个事件派发到不同线程处理等等因素，严重降低CPU处理效率，甚至会产生不可预知的故障等问题。

改进版的ePump框架是在每个worker工作线程和ePump线程中，配置一个独立的FIFO事件队列，这些线程也只从自己的FIFO队列中获取事件并处理事件。ePump线程将产生的每一个事件调度分发到这些线程的FIFO事件队列中，并调用激活机制，唤醒当前处于挂起状态的线程。当然如果存在worker工作线程，则调度机制将优先把ioevent_t事件分配给工作线程。

ePump线程调度派发ioevent_t事件的算法流程如下：

* 事件调度的基础算法是低负载优先算法，即选择当前负载最低的worker工作线程，并将事件派发到该线程的事件队列中。

* 对于同一个iodev_t设备产生的后续所有ioevent_t事件，都会以pipeline方式调度到同一个worker线程中。

* 对于同一个iodev_t设备连续产生的同一类型的ioevent_t事件，如果还在同一个worker工作线程的FIFO事件队列中，尚未被取走执行，那么后续的这样同设备同类型事件就会被抛弃。

* 由哪个worker线程启动的iotimer_t定时器，其超时事件最终仍然由该worker工作线程处理。

* 如果ePump框架中没有启动worker工作线程，则选择当前负载最低的ePump线程，并将事件派发到该线程的事件队列。

对于大规模即时消息通信系统，单台服务器可能会同时维持30万甚至更大规模的TCP并发连接，每个连接随时会产生读写事件进行数据收发处理操作。ePump框架的多个ePump线程可以均衡分布式地分担30万个iodev_t设备，这些设备产生的事件，也很快地均衡调度到各个worker工作线程中，没有共享锁造成的冲突，相同设备产生的事件都以pipeline方式在同一个线程处理，规避了多线程争抢设备资源的冲突访问问题，也回避了一个线程关闭释放了iodev_t设备资源、另外一个线程还在使用该资源的异常故障问题。

### 7.5 worker工作线程

ePump框架中，worker工作线程是处理ioevent_t事件的主要载体，基本流程是循环地提取FIFO事件队列中的事件，执行该事件中的回调函数，处理完后释放该ioevent_t事件对象，继续读取下一个ioevent_t事件进行处理，直到处理完全部事件后，通过异步通知的条件变量进行阻塞等待新事件的到来。

worker工作线程的实时负载是ePump调度算法的主要变量，负载的计算依赖于如下几个因子：

* 当前工作线程事件队列中的排队等候的ioevent_t事件数量，占所有线程事件队列中的ioevent_t事件总数的百分比，其权重为600；
* 当前工作线程在单位时间内占用CPU进行处理的时间比例，其权重为300；
* 当前工作线程累计处理事件数量，占所有事件总数的比例，其权重为100；

根据以上因子的百分数值和权重比例，实时计算得出的值即为worker工作线程的负载。

ePump线程的事件调度派发机制主要依赖于工作线程的负载，即低负载优先算法。运用这种算法的最终结果是多个工作线程终将平衡地承担系统中的所有处理任务。
 

八. ePump框架中惊群效应的处理机制
------

### 8.1 惊群效应（Thundering Herd Problem）

惊群效应是指多进程（多线程）在同时阻塞等待同一个事件的时候（休眠状态），如果等待的这个事件发生，那么他就会唤醒等待的所有进程（或者线程），但是最终却只能有一个进程（线程）获得这个时间的“控制权”，对该事件进行处理，而其他进程（线程）获取“控制权”失败，只能重新进入休眠状态，这种现象和性能浪费就叫做惊群效应。
 
### 8.2 惊群效应消耗什么？
 
操作系统内核对用户进程（线程）频繁地做无效的调度、上下文切换等任务，会使系统性能大打折扣。上下文切换（context switch）过高会导致 CPU 频繁地在寄存器和运行队列之间奔波，更多的时间花在了进程（线程）切换，而不是在真正工作的进程（线程）上面。直接的消耗包括 CPU 寄存器要保存和加载（例如程序计数器）、系统调度器的代码需要执行。间接的消耗在于多核 cache 之间的共享数据。

### 8.3 ePump框架中存在的惊群问题

不像libevent框架没有设计进程或线程，只定义了接口调用，将进程和线程的使用交给了应用程序来处理。ePump框架采用了多线程（未来版本将支持多进程）来产生和处理各种事件。使用多进程或多线程的系统，由于争抢共同资源，多少都会存在进程或线程的惊群问题。

#### 8.3.1 worker线程组不存在惊群问题

ePump框架中，为每个worker工作线程单独设计了接收和处理事件的FIFO队列，单个worker工作线程在没有事件处理时，阻塞挂起并等候FIFO队列的条件变量内核对象上，直至有新事件添加到FIFO队列后，被条件变量内核对象唤醒。

worker线程组没有共享一个大FIFO事件队列，这样新添加的事件并不会唤醒所有处于休眠的worker工作线程，而是直接由ePump线程像邮递员一样，将信件送达家庭邮箱中，也即是新增加的事件会由ePump线程调度派发到某个worker工作线程的FIFO事件队列中，并直接唤醒该线程，worker工作线程组中的其他线程就不会接受到唤醒指令。

这种方式彻底规避了worker线程组的惊群效应，提升了系统调度效率和CPU的利用率。

#### 8.3.2 ePump线程组的惊群问题

ePump框架中的ePump线程都阻塞挂起在I/O事件通知的系统调用上，如select、poll、epoll_wait等，等候文件描述符的R/W就绪状态，或等待定时时间超时，处于阻塞挂起状态的ePump线程，被唤醒的条件只有两类：  
* 一是文件描述符可读（readable）或可写（writable）
* 二是设置的timeout时间到期了

如果有一个iodev_t设备的文件描述符被所有的ePump线程都监听（monitor）了，当这个设备有R/W Readiness读写就绪时，所有的ePump线程就会被唤醒，所有被唤醒的线程将去抢夺该文件描述符的处理权，当然最终也只有一个线程能取得处理该设备R/W事件的权限，这样就造成了ePump线程的惊群效应。

ePump框架中确实存在一种iodev_t设备类型，就是监听某个服务端口的Listen设备，如用TCP Listen或UDP Listen监听某个服务端口时，创建的iodev_t设备就是需要绑定所有的ePump线程，让所有ePump线程对该设备进行R/W状态监控处理。这样处理的目的是将不同终端用户对该端口服务的请求能够均衡地分配到不同的ePump线程中，如果不做均衡处理，将会导致某一个ePump线程非常繁忙，而其他ePump线程则过分清闲的状态。

所有ePump线程都绑定监听端口服务的iodev_t设备，有两种情形需要分别处理。

* **1. 操作系统内核支持SO_REUSEPORT Socket选项情况**

   * 支持SO_REUSEPORT Socket选项的操作系统，如内核版本高于3.9.x的Linux系统，可以创建多个Socket绑定到同一个IP地址的同一个端口上，并将该Socket分别用不同的进程或线程来监听客户端的连接请求。当客户端的TCP三路握手成功后，内核就会均衡地将当前连接请求交给某一个线程来accept，从内核层面解决了多个线程在该Socket文件描述符R/W状态为连接就绪时，争抢处理权的竞争问题。
    * ePump框架中，如果判断操作系统支持SO_REUSEPORT Socket选项，调用tcp_mlisten或udp_mlisten等接口针对同一个监听端口，为每一个ePump线程单独创建一个Listen iodev_t设备，并建立绑定关系。这样每个ePump线程都会监听这一个端口，接收客户端请求，并处理这些请求。
    * 基于以上处理过程，当客户端发起连接请求后，监听该端口服务的ePump线程组中只有一个线程才会被激活，其他ePump线程并不会接收到R/W Readiness Notification事件通知。

* **2. 操作系统内核不支持SO_REUSEPORT选项情况**

    * ePump框架中，如果操作系统内核不支持SO_REUSEPORT Socket选项，监听某个服务端口时，系统只需要创建一个监听Socket的iodev_t设备，并将该Listen iodev_t设备绑定到所有的ePump线程中；
    * iodev_t设备中内置一个共享锁，当有客户端请求到来时，所有ePump线程都会收到内核发起的R/W Readiness Notification就绪通知，所有ePump线程都会被唤醒，所有线程都争夺处理该客户请求，采用共享锁确保只有一个ePump线程能够获得该客户请求的处理。
    * 这种情况就是典型的惊群效应。


#### 8.3.3 规避或弱化ePump框架惊群问题的措施

* 尽量使用支持SO_REUSEPORT选项的OS版本。支持SO_REUSEPORT选项的操作系统，会彻底解决ePump线程组的惊群问题。
* 尽量使用ePump框架的复合业务模型，即ePump线程数量较少，worker工作线程数量较多，当监听端口有读写请求时，ePump线程数量越少，惊群问题的负面效果也就越低，当然这需要在处理用户并发请求之间寻找平衡。


九. How to build ePump
------

The framework ePump can run on most Unix-like system and Windows OS, especially work better on Linux.

If you get the copy of ePump package on Unix-like system and find the Makefile in the top directory, please type the following commands before getting the library installed:

```bash
$ make && make install
```

十. How to integrate
------

The new generated ePump libraries will be installed into the default directory /usr/local/lib, and the header file epump.h is copied to the location /usr/local/include.

After including the header "epump.h", your program can call the APIs provided in it.  
  `#include <epump.h>`
  
Adding the following compiler options in Makefile, you'll be ready to go!  
  `-I/usr/local/include -L/usr/local/lib -lepump`

Please refer to the test program for your coding. Further tutorial or documentation will be coming later. 
Hope you enjoy it!


十一. ePump框架相关的另外两个开源项目
------
 
### adif 项目
 
ePump框架项目依赖于 adif 项目提供的基础数据结构和算法库。adif 是用标准 c 语言开发的常用数据结构和算法基础库，作为应用程序开发接口基础库，为编写高性能程序提供便利，可极大地缩短软件项目的开发周期，提升工程开发效率，并确保软件系统运行的>可靠性、稳定性。adif 项目提供的数据结构和算法库，主要包括基础数据结构、特殊数据结构、常用数据处理算法，常用的字符串、字节流、字符集、日期时间等处理，内存和内存池的分配释放管理，配置文件、日志调试、文件访问、文件缓存、JSon、MIME等管理，通信编程、文件锁、信号量、互斥锁、事件通知、共享内存等等。

 
### eJet Web服务器项目
 
基于 adif 库和 ePump 框架开发的另外一个开源项目是 eJet Web 服务器，eJet Web 服务器项目是利用 adif 库和 ePump 框架开发的轻量级、高性能 Web 服务器，系统大量运用 Zero-Copy 技术，支持 HTTP/1.1、HTTPS 的全部功能，提供虚拟主机、URI rewrite、Script脚本、变量、Cookie处理、TLS/SSL、自动Redirect、Cache本地存储、日志文件等功能，是静态文件访问、下载、以及PHP承载的理想平台，并对超大文件的上传发布提供高效支撑。此外，还支持 Proxy、 正向代理、反向代理、 TLS/SSL、FastCGI、 uWSGI、本地 Cache 存储管理、CDN节点服务等高级功能。eJet系统可以作为Web服务器承载 PHP 应用、Python 应用，同时利用缓存和 Proxy 功能，可以轻易地配置为 CDN 分发系统的重要分发节点。
 
***
 
十二. 关于作者 老柯 (laoke)
------

有大量Linux等系统上的应用平台和通信系统开发经历，是资深程序员、工程师，发邮件kehengzhong@hotmail.com可以找到作者，或者通过QQ号码[571527](http://wpa.qq.com/msgrd?V=1&Uin=571527&Site=github.com&Menu=yes)或微信号[beijingkehz](http://wx.qq.com/)给作者留言。

ePump框架项目是作者三个关联开源项目的第二个项目，作为高性能系统软件基础框架，是大量系统研发实践中提炼出来的，为开发大并发服务器系统提供框架支撑。本项目源自于2003年开发完成的eProbe项目，在其基础上做了大量的优化，代码变得更加简洁高效。

