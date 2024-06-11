## ePump - An Event-Driven, Multi-Threaded C Framework

[README in Chinese 中文版介绍](https://github.com/kehengzhong/epump/blob/master/README.md)

*An C-language framework based on I/O event notification, non-blocking communication, and multi-threaded event-driven model that helps you develop servers with high performance and numerous concurrent connections.*

## Table of Contents
* [1. What is ePump?](#1-What-is-ePump)
* [2. What Problems Does ePump Solve?](#2-What-Problems-Does-ePump-Solve)
* [3. Working Principle of the ePump Framework](#3-Working-Principle-of-the-ePump-Framework)
    * [3.1 Basic Data Structures of ePump](#31-Basic-Data-Structures-of-ePump)
        * [3.1.1 Device (iodev_t)](#311-Device-iodev_t)
        * [3.1.2 Timer (iotimer_t)](#312-Timer-iotimer_t)
        * [3.1.3 Event (ioevent_t)](#313-Event-ioevent_t)
    * [3.2 Multi-threading Architecture of ePump](#32-Multi-threading-Architecture-of-ePump)
* [4. Working Models of the ePump Framework](#4-Working-Models-of-the-ePump-Framework)
    * [4.1 Fast Service Model - No worker threads, only ePump threads](#41-Fast-Service-Model-No-worker-threads-only-ePump-threads)
    * [4.2 Composite Service Model - A few ePump threads, most worker threads](#42-Composite-Service-Model---A-few-ePump-threads-most-worker-threads)
* [5. File Descriptors in the ePump Framework](#5-File-Descriptors-in-the-ePump-Framework)
* [6. Callback Mechanism of the ePump Framework](#6-Callback-Mechanism-of-the-ePump-Framework)
* [7. Scheduling Mechanism of the ePump Framework](#7-Scheduling-Mechanism-of-the-ePump-Framework)
    * [7.1 Binding iodev_t Device to ePump Threads](#71-Binding-iodev_t-Device-to-ePump-Threads)
        * [7.1.1 Listening iodev_t Devices](#711-Listening-iodev_t-Devices)
        * [7.1.2 Non-Listening iodev_t Devices](#712-Non-Listening-iodev_t-Devices)
    * [7.2 iotimer_t Timer](#72-iotimer_t-Timer)
    * [7.3 ioevent_t Events](#73-ioevent_t-Events)
    * [7.4 ePump Threads](#74-ePump-Threads)
    * [7.5 Worker Threads](#75-Worker-Threads)
* [8. Handling of the Thundering Herd Problem in the ePump Framework](#8-Handling-of-the-Thundering-Herd-Problem-in-the-ePump-Framework)
    * [8.1 What is the Thundering Herd Problem?](#81-What-is-the-Thundering-Herd-Problem)
    * [8.2 Overheads of the Thundering Herd Problem](#82-Overheads-of-the-Thundering-Herd-Problem)
    * [8.3 ePump Framework's Thundering Herd Problems](#83-ePump-Frameworks-Thundering-Herd-Problems)
        * [8.3.1 Worker Thread Group Has No Thundering Herd Problems](#831-Worker-Thread-Group-Has-No-Thundering-Herd-Problems)
        * [8.3.2 ePump Thread Group's Thundering Herd Problem](#832-ePump-Thread-Groups-Thundering-Herd-Problem)
        * [8.3.3 Measures to Avoid or Weaken the Thundering Herd Problems in the ePump Framework](#833-Measures-to-Avoid-or-Weaken-the-Thundering-Herd-Problems-in-the-ePump-Framework)
* [9. How to Build ePump](#9-How-to-Build-ePump)
* [10. How to Integrate](#10-How-to-Integrate)
* [11. Two Other Open Source Projects Related to the ePump Framework](#11-Two-Other-Open-Source-Projects-Related-to-the-ePump-Framework)
    * [adif Project](#adif-Project)
    * [eJet Web Server Project](#eJet-Web-Server-Project)
* [12. About the Author Lao Ke](#12-About-the-Author-Lao-Ke)



## 1. What is ePump?
------

ePump is an event-driven C language application development framework that leverages I/O event notifications, non-blocking communication, and a multi-threaded event-driven model to facilitate the development of high-performance server programs capable of handling a large number of concurrent connections.

ePump, short for "event pump", is essentially a pump that cyclically processes various network read/write events, timer events, and so on. These underlying events include file descriptor (FD) read readiness, write readiness, connection establishment, timer expiration, etc.

ePump is responsible for managing and monitoring file descriptors in non-blocking mode, generating corresponding events based on their state changes, and dispatching them to the event queues of the corresponding worker threads or ePump threads. These threads handle the events by calling the associated callback functions.

The application calls the interface functions provided by the ePump framework to pre-create and open various network communication Socket file descriptors (FDs), or to start timers, etc., and adds or binds them to the monitoring queue of the ePump thread. The status monitoring of these FDs and timers uses the I/O event notification facilities provided by the operating system, such as epoll, select, poll, kqueue, completion port, etc.


## 2. What Problems Does ePump Solve?
------

Many server programs need to handle a large number of concurrent TCP connection requests and UDP requests initiated from the client side, such as web servers, online servers, messaging systems, etc. Early communication server systems typically used a separate process or thread to accept and handle each connection request, or they utilized the OS's I/O asynchronous event notification and multiplexing mechanisms to handle multiple non-blocking concurrent connection requests within a single process.

These systems either block themselves while waiting for data from network and other I/O devices or adopt a single-process multiplexing model, which has certain limitations on CPU utilization efficiency. The ePump framework, on the other hand, is an event-driven model framework that fully and efficiently utilizes CPU processing capabilities.

The ePump framework is a multi-threaded (with future support for multi-process) event-driven model framework based on the asynchronous readiness notification mechanism of file descriptors. It eliminates the need for work threads or processes to block and wait for "data in transit."

The framework creates an `iodev_t` object for each file descriptor and a timer object `iotimer_t` for timer-driven applications. It utilizes the I/O event notification facilities provided by the operating system, such as epoll, kqueue or select, to set the created or opened file descriptor FD to non-blocking mode and add it to the system's monitoring and management list, performing asynchronous callback notifications for its status changes.

The monitoring and management of these two types of objects and the dispatching of event notifications are implemented by the ePump thread pool, with the callback processing of events carried out by the worker thread pool or the ePump thread pool. To fully leverage the performance of server hardware, the number of working threads is generally aligned with the number of CPU cores.

The complex low-level processing details are encapsulated into simple and easy-to-use API interface functions. Through these API functions, developers can quickly develop high-performance server programs that support large concurrency.


## 3. Working Principle of the ePump Framework
------

The ePump framework evolved from the eProbe framework developed by the author in 2003 and is a shorthand for "Event Pump," which, as the name implies, is an event-driven architecture.

For various I/O event notifications, non-blocking communications, and multiplexing mechanisms, including epoll, select, kqueue, and completion port I/O, the basic working principles include:
* Adding FDs to the monitoring list
* Removing FDs from the monitoring list
* Setting the blocking time for monitoring
* Blocking and waiting for R/W readiness to occur in the monitoring list of FD sets
* Polling the FD set list, detecting R/W readiness for each FD, and executing the corresponding callback functions
* Checking for timeouts and executing callback functions for timeout events


### 3.1 Basic Data Structures of ePump

Based on the above working principles, we have designed several basic data structures for the ePump framework:

#### 3.1.1 Device (iodev_t)

For each FD, a data structure called `iodev_t` is used for management. The file descriptor FD is treated as an `iodev_t` device, encapsulating the information related to the file descriptor. This includes managing readable/writable state, FD type, events to be processed, callback functions and parameters, quadruple address, etc. ePump manages various types of sockets such as TCP listening sockets, TCP connected sockets (actively connected, passively accepted), UDP listening sockets, UDP client sockets, Unix Sockets, ICMP Raw Sockets, UDP Raw Sockets, etc., through `iodev_t` devices.

All `iodev_t` devices generate events, and the ePump system processes events generated by `iodev_t` devices by calling the callback functions through event-driven multi-threading.

#### 3.1.2 Timer (iotimer_t)

Similar to `iodev_t` devices, `iotimer_t` timers can also generate driving events. After setting a time and starting the timer, the system will produce a Timeout event from the current moment until the specified time is reached.

`iotimer_t` timer instances are existing in one-shot and will be destroyed after the timeout event occurs or when the program actively calls the `iotimer_stop` interface. The `iotimer_t` timer data structure includes timer ID, callback function and parameters, timing time, etc.

In Unix-like OS systems, a process can only set one clock timer, which is set by the system-provided interface, commonly used are alarm() and setitimer(). For communication systems with a large number of timer needs, and considering cross-platform features, the system-provided timer interfaces generally cannot meet the requirements. In the ePump system, the `iotimer_t` data structure is designed to provide millisecond-level precision and support for a large number of concurrent timers.

In the ePump architecture, timers are regarded as an important infrastructure, managed and monitored by ePump threads just like file descriptor devices.

#### 3.1.3 Event (ioevent_t)

`ioevent_t` events act as the messengers of ePump, managing the types of events, the objects that generate events, and the callback functions and parameters associated with them.

`iodev_t` devices trigger the creation of `ioevent_t` events based on the read/write readiness status changes of various hardware devices. Meanwhile, `iotimer_t` timers initiate a Timeout event when the set time expires.

Additionally, the application can register user-defined hook events, which need to bind callback functions and parameters, and most importantly, define the conditions for triggering user events.

These events, generated under various conditions, are all dispatched to the event queues of the working threads or ePump threads. This drives the working threads or ePump threads to handle the events by invoking the corresponding callback functions.

### 3.2 Multi-threading Architecture of ePump

The ePump architecture consists of multiple threads, which are divided into two categories based on the workflow: ePump threads and worker threads. The primary function of ePump threads is to monitor the R/W readiness state of file descriptors and the timer queue, create readable/writable events and timeout events, and dispatch these `ioevent_t` events to the event queues of the various worker threads or the ePump thread itself. Moreover, ePump threads also handle events in their own event queue by calling the callback functions of these events one by one, then deleting the event. The main function of worker threads is to monitor the event queue and execute the callback functions associated with the events in the queue.

Each ePump thread utilizes I/O event asynchronous notification, non-blocking communication, and multiplexing mechanisms and models. Using system calls such as select/poll/epoll/kqueue, when the monitored file descriptors are ready for I/O read/write, ePump creates R/W readable/writable events for these file descriptors. These events are packaged into standard `ioevent_t` instances within the ePump framework and dispatched to the FIFO event queues of various ePump threads or worker threads. The Event Queue FIFOs are the core of the thread event-driven model, with each ePump thread and worker thread having its own FIFO event queue. Additionally, ePump threads maintain and handle the timer list, creating timeout `ioevent_t` events and dispatching them to the event queues of corresponding ePump threads or worker threads.

The main function of worker threads is to wait for events in their bound event queue in a blocking manner. When events come, the worker thread in suspended state, is awakened through an activation mechanism, and sequentially and cyclically retrieves and processes `ioevent_t` events from its FIFO event queue.

The processing flow of the `ioevent_t` event is based on a callback function registration mechanism. When the application creates or opens a file descriptor FD or starts a timer, it registers and binds a callback function to the corresponding `iodev_t` device and timer instance. Consequently, when an `ioevent_t` event is generated for `iodev_t` devices and `iotimer_t` timers, the callback function and parameters registered in devices or timers will be set as the members of events. Once worker threads retrieve the `ioevent_t` event from the event queue, they simply execute the designated callback function accordingly.

In addition to monitoring the `iodev_t` device corresponding to the file descriptor FD, managing the `iotimer_t` timer list, creating `ioevent_t` events, and dispatching `ioevent_t` events to various event queues, ePump threads can also bind a FIFO event queue and handle events in it by invoking event callback functions.

To ensure running efficiency, the total number of threads in the ePump architecture, including both ePump and worker threads, should align with the number of CPU core processors for fully parallel processing capability.


## 4. Working Models of the ePump Framework
------

Let's first define the terms "fast service" and "slow service" mentioned later in this document. Fast service refers to a server system that, after receiving a client's request, has a relatively simple and quick service responding without long periods of blocking or waiting for I/O. On the contrary, slow service refers to a server system that requires a longer responding time to block and wait for data I/O when processing a client's request, such as services with slow database queries or slow insertions.

The ePump framework is highly flexible and can be divided into two types of working models based on service scenarios:

### 4.1 Fast Service Model - No worker threads, only ePump threads

* In the fast service model, ePump threads are responsible for both monitoring to `iodev_t` and `iotimer_t`, generating and dispatching `ioevent_t` events, and can also perform the functions of worker threads by handling `ioevent_t` events in their FIFO event queue. An application sample similar to this working model is the Nginx Web server.

* The disadvantage of this model is that once a slow service situation occurs during the event processing by calling the callback function, such as long I/O waiting or blocking when reading or writing databases, it will prevent the timely and effective processing of subsequent I/O readiness states of other `iodev_t` devices and timeout states of `iotimer_t` timers. The delay in processing one event can lead to an overall delay, blocking, or even unresponsiveness or crash.

* For such slow services, a dedicated process/thread architecture similar to the Apache Web server is more suitable. However, overall, this dedicated process/thread model has low efficiency in utilizing the parallel processing capabilities of multi-core CPUs, and the number of concurrent transactions is relatively low.

* The advantage of this model is that it makes very efficient use of multi-core CPU parallel computing and processing capabilities, making it suitable for communication or service systems that require rapid response.

### 4.2 Composite Service Model - A few ePump threads, most worker threads

* In the composite service model, ePump threads are configured to only be responsible for monitoring to `iodev_t` and `iotimer_t`, generating and dispatching `ioevent_t` events, and not for handling events. Worker threads are responsible for handling all generated `ioevent_t` events by calling the callback functions of these events, thereby processing the application layer service procedures.

* When worker threads execute the callback functions registered by the upper application, the blocking of the handling will not paralyze the handling of other `iodev_t` devices or timer events, ensuring that other devices or timer events can be promptly and effectively handled by other worker threads.

* The advantage of this model is that it can solve the blocking problems of slow service applications to a certain extent while efficiently utilizing the parallel computing capabilities of multi-core CPUs.

* The disadvantages of this model is that after ePump threads generate events, they need to be delivered to the event queues of corresponding worker thread and use som mechanisms to wake up the worker threads, thereby leading to issues with data sharing and thread synchronization. Since involving data addition, deletion, access operations and running synchronization in multiple threads, it requires frequent use of locking mechanisms and context switch of running/suspension/awakening of the threads, which greatly reduces thread efficiency.

* Another disadvantage is the issue of serializing the handling of multiple events generated by the same `iodev_t` device. Events generated by each `iodev_t` device must be strictly delivered to a specific worker thread for serialized processing. Otherwise, if events from the same `iodev_t` device are delivered to different worker threads, and multiple worker threads handle multiple events for the same `iodev_t` device in parallel, such as one thread processing read data while another one is processing connection closure, it will lead to various exceptions or crashes.

* When using the ePump framework's composite service model, the total number of threads is recommended to be the number of CPU Core Processors, with the number of ePump threads being 10-20% of the total CPU cores, and the number of worker threads being 80-90% of the CPU cores. For example, on a server with a 32-core CPU running a program developed with the ePump architecture, the number of ePump threads should be set to 3-6, and the number of worker threads should be set to 26-29.


## 5. File Descriptors in the ePump Framework
------

In Unix and Linux operating systems, all physical or virtual devices related to I/O reading and writing are regarded as files, including regular files, directories, character device files (such as keyboards, mouse), block device files (such as hard disks, optical drives), network sockets, etc., all abstracted into files. A file descriptor is an integer value allocated by the operating system kernel to manage the index of opened file structures. The kernel maintains a file descriptor table for each process, and the index of this table, that is the file descriptor, starts from 0, with 0 being the standard input, 1 being the standard output, and 2 being the standard error output. Each file opened in the process is assigned a file descriptor fd, corresponding to a certain index item in the process's file descriptor table, and the file is read, written, and accessed through fd.

By default, the total number of file descriptors that a process can open is limited. In Linux systems, this limit includes two aspects: user-level limits and kernel-level limits. The kernel-level limit is set based on hardware resources and the operating system's ability to handle I/O, and can be viewed using the shell commands:
```bash
sysctl -a | grep file
```
or
```bash
cat /proc/sys/fs/file-max
```
Due to the limited total number of files that the system kernel can open at the same time, each user and process is correspondingly limited to the maximum number of open files, which is the user-level limit. This number is usually 1024 by default, meaning that the total number of file descriptors that a process can open by default is 1024.

During the initialization of the ePump system, the total number of open file descriptors is taken as an input parameter. The system call `setrlimit` is used to modify the number of files that can be opened simultaneously, thereby increasing the total number of concurrent opened file descriptors, including network sockets. This enhances the system's maximum I/O concurrency processing capability.

The ePump framework encapsulates file descriptors using the `iodev_t` data structure to manage each file descriptor and its associated information. It encapsulates file descriptors, types, callback functions, quadruple addresses (IP and port), readable/writable state, associated threads, and other information for unified management. ePump threads are responsible for monitoring the I/O readable/writable state of `iodev_t` devices. Once an I/O read/write readiness notification is received, an `ioevent_t` event is generated. Different events are created based on the various I/O readable/writable states of the file descriptors, with different callback functions registered for these events, ultimately achieving a closed loop of the event-driven model processing.

In the ePump architecture, various types of file descriptors are defined for different I/O read/write statuses:
```c
#define FDT_LISTEN            0x01    // Listening socket
#define FDT_CONNECTED         0x02    // Connected TCP socket
#define FDT_ACCEPTED          0x04    // Accepted connection socket
#define FDT_UDPSRV            0x08    // UDP server socket
#define FDT_UDPCLI            0x10    // UDP client socket
#define FDT_USOCK_LISTEN      0x20    // Unix domain socket, listening
#define FDT_USOCK_CONNECTED   0x40    // Unix domain socket, connected
#define FDT_USOCK_ACCEPTED    0x80    // Accepted Unix domain socket connection
#define FDT_RAWSOCK           0x100   // Raw socket
#define FDT_FILEDEV           0x200   // File device
#define FDT_TIMER             0x10000 // Timer
#define FDT_USERCMD           0x20000 // User command
#define FDT_LINGER_CLOSE      0x40000 // Linger close
#define FDT_STDIN             0x100000 // Standard input
#define FDT_STDOUT            0x200000 // Standard output
```

The `iodev_t` device built based on the file descriptor is the most fundamental physical facility of the ePump framework. Essentially, ePump is a system for managing file descriptors. Events generated by file descriptors drive the entire ePump framework like blood.


## 6. Callback Mechanism of the ePump Framework
------

According to business logic, software modules generally adopt a layered model, and different modules are usually called through function interfaces. However, in the layered logic, the lower-level modules usually serve as basic capabilities, such as performing calculations, I/O reading and writing, etc., providing function call interfaces to the upper-level modules. The upper-level modules use the functions of the lower-level modules through their interfaces. How can the lower-level modules call the functions of the upper-level modules? This is the callback mechanism.

The ePump framework, as a basic infrastructure, provides functional support for different service systems. The implementation of service systems is complex and varied. Through the callback mechanism, the function pointers of the upper-level service systems are registered into the file descriptor devices or timers of the ePump framework. When ePump detects changes in the I/O readable/writable state of the device and timer expiration, it uses an event-driven model to execute the callback functions registered by the upper system to the devices and timers where the state changes have occurred. This leverages the ePump's underlying multi-threaded CPU parallel processing capabilities to address complex business procedures.

The callback mechanism of ePump is encapsulated within the interface functions to the upper-level modules. Generally, these interface functions include function pointers that need to be passed in, which point to the upper-level service functions, and are commonly named the callback functions of ePump. The prototype definition of the callback function is as follows:

```c
typedef int IOHandler (void * vmgmt, void * pobj, int event, int fdtype);
```

The first parameter is registered by the upper-level module when calling the ePump interface function as an argument. The second parameter `pobj`, the third parameter `event`, and the fourth parameter `fdtype` are passed by the ePump system when calling the callback function. Specifically:

- `pobj` is the `iodev_t` device object or `iotimer_t` timer object that generates the I/O event.
- `event` is the type of event.
- `fdtype` is the type of file descriptor.

The `iodev_t` device objects and `iotimer_t` timer objects managed by ePump generate corresponding events when their state changes. These event types include:

```c
/* Event types include connection establishment, connection acceptance, readability,
 * writability, timeout. The working threads will be driven by these events */
#define IOE_CONNECTED        1    // Connection established
#define IOE_CONNFAIL         2    // Connection failed
#define IOE_ACCEPT           3    // Connection accepted
#define IOE_READ             4    // Data readable
#define IOE_WRITE            5    // Data writable
#define IOE_INVALID_DEV      6    // Invalid device
#define IOE_TIMEOUT          100  // Timeout event
#define IOE_DNS_RECV         200  // DNS receive event
#define IOE_DNS_CLOSE        201  // DNS close event
#define IOE_USER_DEFINED     10000 // User-defined event
```

The basic interface functions provided by ePump include:

```c
void * eptcp_listen (void * vpcore, char * localip, int port, void * popt, void * para,
                     IOHandler * cb, void * cbpara, int bindtype, void ** plist,
                     int * listnum, int * pret);
void * eptcp_mlisten (void * vpcore, char * localip, int port, void * popt,
                      void * para, IOHandler * cb, void * cbpara);
void * eptcp_accept (void * vpcore, void * vld, void * popt, void * para, IOHandler * cb,
                     void * cbpara, int bindtype, ulong threadid, int * retval);
void * eptcp_connect (void * vpcore, char * host, int port,
                      char * localip, int localport, void * popt, void * para,
                      IOHandler * cb, void * cbpara, ulong threadid, int * retval);
void * eptcp_nb_connect (void * vpcore, char * host, int port, char * localip,
                         int localport, void * popt, void * para, IOHandler * cb,
                         void * cbpara, ulong threadid, int * retval);

void * epudp_listen (void * vpcore, char * localip, int port, void * popt, void * para,
                     IOHandler * cb, void * cbpara, int bindtype, void ** plist,
                     int * listnum, int * pret);
void * epudp_mlisten (void * vpcore, char * localip, int port, void * popt,
                      void * para, IOHandler * cb, void * cbpara);
void * epudp_client (void * vpcore, char * localip, int port, void * popt,
                     void * para, IOHandler * cb, void * cbpara,
                     iodev_t ** devlist, int * devnum, int * retval);
int    epudp_recvfrom (void * vdev, void * vfrm, void * pbuf, int bufsize, void * addr, int * pnum);

void * epusock_connect (void * vpcore, char * sockname, void * para, IOHandler * ioh,
                        void * iohpara, ulong threadid, int * retval);
void * epusock_listen (void * vpcore, char * sockname, void * para,
                       IOHandler * cb, void * cbpara, int * retval);
void * epusock_accept (void * vpcore, void * vld, void * para, IOHandler * cb,
                       void * cbpara, int bindtype, ulong threadid, int * retval);

void * epfile_bind_fd    (void * pcore, int fd, void * para, IOHandler * cb, void * cbp);
void * epfile_bind_stdin (void * pcore, void * para, IOHandler * cb, void * cbp);

void * iotimer_start (void * pcore, int ms, int cmdid, void * para,
                      IOHandler * cb, void * cbp, ulong epumpid);
int    iotimer_stop  (void * pcore, void * viot);

int    dns_query (void * vpcore, char * name, int len, DnsCB * cb, void * cbobj, ulong objid);
```

These functions cover event monitoring for communication facilities such as TCP, UDP, and Unix Sockets, which generate file descriptors and timers. For file descriptors other than TCP, UDP, and Unix Sockets, you can use the `epfile_bind_fd` interface to create and bind file descriptor devices, allowing any file descriptor FD to be managed and event-driven within the ePump architecture.


## 7. Scheduling Mechanism of the ePump Framework
------

Scheduling is the process of allocating resources according to certain mechanisms and algorithms. The main resources of the ePump framework are `iodev_t` devices, `iotimer_t` timers, `ioevent_t` events, ePump threads, and worker threads. The scheduling mechanism is designed around the allocation of these resources.

### 7.1 Binding iodev_t Devices to ePump Threads

After creating an `iodev_t` device through various application interfaces, you need to select an ePump thread to perform the listening and readiness notification for the device, and establish a binding relationship between the current `iodev_t` device and the selected ePump thread. The bound ePump thread will monitor and generate various R/W events. How to allocate ePump threads depends on the device type and binding type of `iodev_t`.

After creating an `iodev_t` device through various application interfaces, you need to select an ePump thread to execute the device's monitoring and readiness notifications. A binding relationship must be established between the `iodev_t` device and the chosen ePump thread, with the bound ePump thread taking responsibility for monitoring and generating various R/W events. How to assign ePump threads depends on the device type and binding type of the `iodev_t`.

#### 7.1.1 Listening iodev_t Devices

For operating systems that do not support the `SO_REUSEPORT` socket option, `iodev_t` devices, created through the Listening interface function, need to be bound by all ePump threads. This ensures that when a client's network connection request arrives, the load of connection accept is evenly distributed among all ePump threads. On the other hand, for operating systems that support the `SO_REUSEPORT` socket option, a separate `iodev_t` listening device should be created for each ePump thread and bound to it. This approach ensures that when a client's TCP three-way handshake is successful, the kernel will evenly distribute the connection request to one of the threads, solving the contention issue at the kernel level.

Of course, for systems with Linux kernel versions lower than 3.9.x, that is, operating systems that do not support SO_REUSEPORT, there is a thundering herd problem. For details on how to handle this, please refer to section 8.3.2.

#### 7.1.2 Non-Listening iodev_t Devices

* **Specify an ePump thread**
    Establish the binding relationship according to the ePump thread specified by the calling parameter.

* **Select the ePump thread with the lowest load**
    The load of an ePump thread is primarily measured by indicators such as the number of `iodev_t` devices and `iotimer_t` timers bound to the thread, and the number of `ioevent_t` events generated by the thread in the recent time unit. Choosing the ePump thread with the lowest load can help balance the load evenly among all ePump threads, thereby improving system work efficiency.

### 7.2 iotimer_t Timers

When the application starts an `iotimer_t` timer, you can specify a particular ePump thread to bind to. If no ePump thread is specified, the ePump framework generally selects the current calling thread as the bound ePump thread. The timer instance `iotimer_t` is managed and monitored by the bound ePump thread, which is responsible for generating timeout events.

Binding an ePump thread generally involves adding the `iotimer_t` timer object to the red-black tree structure of the timer list of that ePump thread. If the current ePump thread in a blocked and suspended state is awakened through an activation mechanism, the system call for monitoring is restarted based on the shortest time difference from the current moment in the timer tree structure.

### 7.3 ioevent_t Events

Except for user events, all `ioevent_t` events are generated by ePump threads and are scheduled by ePump threads according to relevant mechanisms and algorithms. They are dispatched to the FIFO event queues of worker thread or ePump thread for callback function handling. The life cycle of `ioevent_t` events is relatively short. It is created, dispatched to the event queue, executed by the thread for its callback function. Once executed, the instance object is recycled and its life ends.

`ioevent_t` events are generally bound a specific `iodev_t` device or `iotimer_t` timer. The dispatching of the current `ioevent_t` event to a particular worker thread or ePump thread directly affects the efficiency of the ePump framework system operation.

Identical and repeated `ioevent_t` events continuously generated by the same `iodev_t` device will be discarded by the scheduling mechanism.

### 7.4 ePump Threads

ePump threads are the core facilities of the ePump framework, responsible for managing the list of `iodev_t` devices and `iotimer_t` timers. They block and wait for device R/W readiness notifications or timer expirations through system calls such as `epoll_wait` or `select`, and generate `ioevent_t` events. They are responsible for scheduling and dispatching these events.

In the early eProbe framework, there was only one global FIFO event queue where all generated events were added to the end of the queue. All idle worker threads would then compete for the events in the FIFO queue for processing. This scheduling model is simple and can evenly distribute work tasks. Once an event handling is blocked, it does not affect the handling of other events. However, due to factors such as the shared lock of the FIFO queue, the thundering herd problem of waking up all worker threads by broadcasting mode,  and the dispatching of multiple events generated by the same device to different threads for handling, it seriously reduces CPU processing efficiency and may even lead to unpredictable faults and other issues.

The improved ePump framework creates an independent FIFO event queue in each worker thread and ePump thread. These threads only retrieve events from their own FIFO queue and process them. ePump threads dispatch each generated event to the FIFO event queues and call wake-up mechanisms to awaken threads in suspended state. Of course, if worker threads are present, the scheduling mechanism will prioritize the dispatching of `ioevent_t` events to worker threads.

The algorithm for ePump thread event scheduling and dispatching is as follows:

* The basic algorithm for event scheduling is the low-load-first algorithm, which selects the worker thread with the lowest load and dispatches the event to that thread's event queue.

* For all subsequent `ioevent_t` events generated by the same `iodev_t` device, they will be scheduled to the same worker thread in a pipeline manner.

* For the same type of `ioevent_t` events continuously generated by an `iodev_t` device, when delivering to the message queue, check whether there are any completely identical events in the queue that have not been handled yet. If such event exist, then discard and delete the current event directly.

* A `iotimer_t` timer started by a thread will have its timeout event ultimately handled by that same thread.

* If there are no worker threads started in the ePump framework, then the specified ePump thread of `iodev_t` or `iotimer_t` is used first. If none is specified, the current ePump thread is selected. The event is dispatched to the event queue of that thread.

For large-scale instant messaging communication systems, a single server may simultaneously maintain 300,000 or even more TCP concurrent connections, and each connection may generate readable/writable events for data transmission and processing at any time. The multiple ePump threads of the ePump framework can evenly managed these devices, and the events generated by these devices are quickly and evenly scheduled to various worker threads or ePump threads. There are no contension caused by shared locks, and events generated by the same device are all dispatched to the same thread in a pipeline manner. This avoids access conflicts caused by multi-threaded contention for device resources, and exceptions where one thread has closed and released the device resources of `iodev_t` while another thread is still using them.

### 7.5 Worker Threads

In the ePump framework, worker threads are an important support for handling `ioevent_t` events. The basic process includes continuously retrieving an event from the FIFO event queue, invoking the callback functions of the event, releasing the `ioevent_t` event object after handling, and continuing to retrieve the next `ioevent_t` event for processing until all events are processed. At this point, the thread will be blocked by an asynchronous notification condition variable to waits for new events to arrive.

The real-time load of worker threads is a primary variation in the ePump scheduling algorithm. The load is calculated based on several factors:

- The number of `ioevent_t` events waiting in the current thread's event queue, serving as the base load.
- The proportion of CPU time the current thread occupies for processing within a unit of time, with a weight of 1000.
- The proportion of the total number of events processed by the current thread, with a weight of 100.

The load of a worker thread is calculated in real-time by adding together the values obtained from these factors, where each proportion value is multiplied by their respective weight.

The event scheduling and dispatching mechanism for worker threads primarily depends on the load of the worker threads, using a low-load priority algorithm. The ultimate result of using this algorithm is that multiple worker threads will evenly share all processing tasks in the system.


## 8. Handling of the Thundering Herd Problem in the ePump Framework
------

### 8.1 What is the Thundering Herd Problem?

The Thundering Herd Problem refers to the situation where multiple processes (or threads) are blocked, waiting for the same event to occur (in a suspended state). If the awaited event occurs, it wakes up all the processes (or threads) that were waiting. However, only one process (or thread) can obtain the "control" of the event for processing, while the others, failing to gain control, must return to a suspension state. This phenomenon and the resulting waste of computational resources and performance are termed the Thundering Herd Problem.

### 8.2 Overheads of the Thundering Herd Problem

The frequent and ineffective scheduling and context switching of user processes (threads) by the operating system kernel can significantly degrade system performance. Excessive context switching means that the CPU is constantly switching between registers and the run queue, spending more time on process (thread) switching rather than actual work. Direct resource overhead includes the saving and loading CPU registers (such as the program counter) and the execution of the system's scheduler code. Indirect overhead involves the sharing of data between multi-core caches.

### 8.3 ePump Framework's Thundering Herd Problems

Unlike the libevent framework, which does not contain processes or threads and leaves the use of these to the application program, the ePump framework utilizes multiple threads (and will support multiple processes in future versions) to generate and handle various events. Systems that employ multiple processes or threads will inevitably face some degree of the herd problem due to contention for shared resources.

#### 8.3.1 Worker Thread Group Has No Thundering Herd Problems

In the ePump framework, each worker thread has its own dedicated FIFO queue for receiving and processing events. A single worker thread waits on a condition variable kernel object based on its FIFO queue when there are no events to process, and is awakened by it when a new event is added to the FIFO queue.

The worker thread group does not share a single large FIFO event queue, so the addition of new events does not awaken all worker threads in sleeping state. Instead, the ePump thread acts like a postman, delivering the event directly to the thread's queue, signifying only that specific thread, while other threads in the worker group do not receive a wake-up signal.

This method completely avoids the thundering herd effect for the worker thread group, enhancing system scheduling efficiency and CPU utilization.

#### 8.3.2 ePump Thread Group's Thundering Herd Problem

In the ePump framework, ePump threads in idle state block on system call, such as select, poll, epoll_wait, etc., waiting for the R/W readiness notification of file descriptors or for a timeout of timers. The conditions for awakening the blocked ePump threads are twofold:

- The file descriptor becomes readable or writable.
- The timeout period set by timer elapses.

If there is an `iodev_t` device whose file descriptor is monitored by all ePump threads, when this device is ready for R/W, all ePump threads will be signified, and all awakened threads will compete for the right to process the file descriptor. Of course, only one thread will ultimately gain the right to handle the R/W event of the device, resulting in a thundering herd effect among ePump threads.

There is indeed an `iodev_t` device type in the ePump framework that requires binding to all ePump threads, such as when listening to a service port with TCP Listen or UDP Listen, the created `iodev_t` device needs to bind all ePump threads to monitor and handle the R/W state of the device. The purpose of this approach is to evenly distribute requests from different end users to different ePump threads. Without balanced processing, it would lead to a situation where one ePump thread is extremely busy while others are excessively idle.

All ePump threads would bind the `iodev_t` device that listens to the service port. There are two situations that need to be handled differently:

* **1. When the operating system kernel supports the `SO_REUSEPORT` socket option**

    * Operating systems that support the `SO_REUSEPORT` Socket option, such as Linux systems with a kernel version higher than 3.9.x, can create multiple Sockets bound to the same IP address and port, and bind each Socket to different processes or threads to listen for client connection requests. When the TCP three-way handshake is successful, the kernel will evenly hand over the current connection request to one of the threads or processes for acceptance, not to all. This solves the contention issues for processing rights of multiple threads or processes at the kernel level when the R/W state of the Socket file descriptor is ready.

    * In the ePump framework, if it is determined that the operating system supports the `SO_REUSEPORT` Socket option, interfaces such as `tcp_mlisten` or `udp_mlisten` are called to create a separate Listen `iodev_t` device for each ePump thread for the same listening port, and establish a binding relationship. In this way, each ePump thread will listen to this port, receive client requests, and handle these requests.

    * Based on the above process, when a client initiates a connection request, only one of the ePump thread groups listening to the port service will be activated, and other ePump threads will not receive the R/W Readiness Notification.

    * In this case, there is no thundering herd effect.

* **2. When the operating system kernel does not support the `SO_REUSEPORT` option.**

    * In the ePump framework, if the operating system kernel does not support the `SO_REUSEPORT` Socket option, when listening to a service port, the ePump system only creates one listening Socket's `iodev_t` device and bind this Listen `iodev_t` device to all ePump threads.

    * There is a shared lock built into the `iodev_t` device. When a client request arrives, all ePump threads will receive the R/W readiness notification initiated by the kernel, and all ePump threads will be awakened for competing to handle the client request. The using of a shared lock can ensure that only one ePump thread obtains the processing of the client request.

    * This situation is a typical thundering herd effect.


#### 8.3.3 Measures to Avoid or Weaken the Thundering Herd Problems in the ePump Framework

* **Utilize Operating Systems with `SO_REUSEPORT` Support**: Try to use operating system versions that support the `SO_REUSEPORT` option as much as possible. Operating systems that support the `SO_REUSEPORT` option will completely resolve the thundering herd problem for groups of ePump threads.

* **Adopt the Composite Service Model**: If you are concerned about the thundering herd effect leading to reduced system performance, use the composite service model of the ePump framework, where there are fewer ePump threads and a greater number of worker threads. The fewer ePump threads there are when readable/writable requests occur on the listening port, the lower the negative impact of the thundering herd problem. Of course, this requires finding a balance between handling concurrent user requests and the number of threads used.

* **Optimize Thread Management**: Efficiently manage thread scheduling and event processing to minimize the thundering herd effect. This involves reducing the number of threads that are woken up simultaneously in response to a single event.

* **Implement Advanced Locking Mechanisms**: Use advanced locking strategies such as lock-free programming or fine-grained locking to minimize contention and reduce the time threads spend waiting for locks.


## 9. How to Build ePump
------

The ePump framework can run on most Unix-like systems and Windows OS, with optimal performance on Linux.

If you have obtained the ePump package on a Unix-like system and found the Makefile in the top directory, please type the following commands before installing the library:

```bash
$ make && make install
```

## 10. How to Integrate
------

The newly generated ePump libraries will be installed in the default directory `/usr/local/lib`, and the header file `epump.h` will be copied to the location `/usr/local/include`.

After including the header "epump.h", your program can call the APIs provided in it:

```c
#include "epump.h"
```

Add the following compiler options in the Makefile, and you will be ready to go:

```makefile
-I/usr/local/include -L/usr/local/lib -lepump
```

Please refer to the test program for your coding. Further tutorials or documentation will be available later.


## 11. Two Other Open Source Projects Related to the ePump Framework
------

### [adif Project](https://github.com/kehengzhong/adif)

The ePump framework project relies on the adif project to provide basic data structures and algorithm libraries. adif is a standard C language development of common data structures and algorithm base libraries, serving as a basic library for application development interfaces, facilitating the writing of high-performance programs, greatly shortening the development cycle of software projects, and ensuring the reliability and stability of the software system's operation. The data structures and algorithm libraries provided by the adif project mainly include basic data structures, special data structures, common data processing algorithms, common string, byte stream, character set, date and time processing, memory and memory pool allocation and release management, configuration files, log debugging, file access, file caching, JSON, MIME, and other management, communication programming, file locks, semaphores, mutexes, event notification, shared memory, etc.


### [eJet Web Server Project](https://github.com/kehengzhong/ejet)

Another open-source project developed based on the adif library and ePump framework is the eJet Web server. The eJet is a lightweight, high-performance Web server developed using the adif library and ePump framework. The system makes extensive use of Zero-Copy technology, supports all functions of HTTP/1.1, HTTPS, provides virtual hosting, URI rewrite, Script, variable, Cookie processing, TLS/SSL, automatic Redirect, Cache in local storage, access-log file functions, and is an ideal platform for static file access, downloads, and PHP hosting. In addition, it also supports efficient uploading and publishing large files. Furthermore, it supports advanced features such as Proxy, forward proxy, reverse proxy, TLS/SSL, FastCGI, uWSGI, local Cache storage management, CDN node services, etc. The eJet system can be used as a Web server to host PHP applications, Python applications, and at the same time, using caching and Proxy functions, it can be easily configured as an important distribution node of the CDN distribution system.


## 12. About the Author Lao Ke
------

With extensive experience in the development of application platforms and communication systems on Linux and other systems, the author is a senior programmer and engineer. You can contact the author by email at kehengzhong@hotmail.com, or leave a message to the author through QQ number [571527](http://wpa.qq.com/msgrd?V=1&Uin=571527&Site=github.com&Menu=yes) or WeChat ID [beijingkehz](http://wx.qq.com/).

The ePump framework project is the author's second of three related open-source projects. As a high-performance system software base framework, it is refined from a large number of system R&D practices, providing framework support for the development of high-concurrency server systems. This project originated from the eProbe project, which was developed and completed in 2003, and has undergone a lot of optimization on this basis, making the code more concise and efficient.

