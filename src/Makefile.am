AUTOMAKE_OPTIONS = foreign
 
VERSION_INFO = 2:1:0
 
noinst_HEADERS = epm_pool.h        \
	epm_util.h        \
	epm_list.h        \
	epm_arr.h         \
	epm_hashtab.h     \
	epm_sock.h        \
	epcore.h          \
	epepoll.h         \
	ephware.h         \
	eprawsock.h       \
	epselect.h        \
	eptcp.h           \
	epudp.h           \
	epump_internal.h  \
	epusock.h         \
	epwakeup.h        \
	iodev.h           \
	ioevent.h         \
	iotimer.h
 
include_HEADERS = epump.h
 
lib_LTLIBRARIES = libepump.la
 
libepump_la_SOURCES =      \
	epcore.c           \
	epump.c            \
	epepoll.c          \
	epselect.c         \
	epwakeup.c         \
	iodev.c            \
	iotimer.c          \
	ioevent.c          \
	eptcp.c            \
	epudp.c            \
	epusock.c          \
	eprawsock.c        \
	ephware.c          \
	epm_arr.c          \
	epm_hashtab.c      \
	epm_list.c         \
	epm_pool.c         \
	epm_util.c         \
	epm_sock.c

libepump_la_LDFLAGS = -version-info $(VERSION_INFO)

