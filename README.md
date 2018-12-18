# What's ePump
This is a C-language library based on non-blocking communication and multi-threaded event-driven model, which can help you develop server with high-performance and numerous concurrent connections.

The full name of ePump is event pump, which means a pump of various underlying events, including read/write readiness of file descriptors, timeout of timers etc. ePump is responsible for managing and monitoring the file descriptors in non-blocking mode and the timers, generating corresponding events to the FIFO event queue and calling back the processing functions set by the application layer. The application calls the API of ePump library to create the same number of threads as the CPU core, which enable full use of CPU parallel computing​. Each thread uses epoll or select to watch fd-sets and timers and dispatch events, and handles the events in FIFO queues.​

# The application scenario of ePump
Many server programs must handle a large quantity of TCP connections from client sides, such as Web server, online server, message server, etc. In earlier implementation of communication server, a connection request is usually received and processed by one standalone process or thread, the typical application is the eralier version of apahce web server. Based on asynchromous readiness notification of file descriptor, the process or thread can be unnecessary to block till the coming of data. By setting the file descriptors as non-blocking and monitoring the state of fd-sets read or write with epoll/select facilities, the epump library implementation creates IO-device objects for the management of fds, adds IO-timer facility for the timing-driven requirements. Fully utlizing the capacity by starting the same number of threads as CPU cores, the epump library adopts the callback mechanism for application developer.

Lots of complicated underlying details are encapsulated and easy APIs are provided for fast development of high performance server program.

# How to build
The library ePump can run on most Unix-like system and Windows OS, especially work
better on Linux.

If you get the copy of ePump package on Unix-like system and find the configure
scripts in the top directory have no execute permission, please type the following
commands before getting the library running:

```bash
$ chmod +x autogen.sh configure ltmain.sh config.* depcomp install-sh
```

then start the script configure to generate Makefile

```bash
$ ./configure
```

After you see the Makefile in current and src directory, make the library:

```bash
$ make && make install
```

# How to integrate
The new generated ePump libraries will be installed into the default directory /usr/local/lib,
and the header file epump.h is copied to the location /usr/local/include.

After including the header "epump.h", your program can call the APIs provided in it.

  `#include <epump.h>`
  
Adding the following compiler options in Makefile, you'll be ready to go!

  `-I/usr/local/include -L/usr/local/lib -lepump`

Please refer to the test program for your coding. Further tutorial or documentation
will be coming later. 

Hope you enjoy it!
