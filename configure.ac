#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([epump], [2.1.8], [kehengzhong@hotmail.com])

AM_INIT_AUTOMAKE(epump, 2.1.8) 

AC_CONFIG_SRCDIR([src/iodev.h])
AC_CONFIG_HEADERS([src/config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# AC_PROG_RANLIB

AC_PROG_LIBTOOL

if test "$prefix" = "NONE"; then
   prefix="/usr/local"
fi

if test "$GCC" = "yes" ; then
        # Enable many gcc warnings by default...
        CFLAGS="$CFLAGS -O3 -Wall"
        # And disable the strict-aliasing optimization, since it breaks
        # our sockaddr-handling code in strange ways.
        if test x$have_gcc295 = xyes; then
                CFLAGS="$CFLAGS -fno-strict-aliasing"
        fi
fi
 

# Checks for libraries.
# FIXME: Replace `main' with a function in `-lm':
AC_CHECK_LIB([m], [main])
# FIXME: Replace `main' with a function in `-lpthread':
AC_CHECK_LIB([pthread], [main])

# Checks for header files.
AC_CHECK_HEADERS([ \
  stdlib.h \
  string.h \
  stdarg.h \
  sys/eventfd.h \
  fcntl.h  \
  unistd.h \
  sys/ioctl.h \
  sys/socket.h \
  arpa/inet.h \
  netdb.h \
  netinet/in.h \
  netinet/in6.h \
  netinet/tcp.h \
  sys/epoll.h \
  poll.h  \
  sys/select.h \
  sys/stat.h \
  sys/time.h \
  errno.h \
])

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC

AC_CHECK_FUNCS([ \
  fcntl \
  inet_ntop \
  inet_pton \
  poll \
  select \
  eventfd \
  signal \
  usleep \
  gettimeofday \
  memmove \
  memset \
  socket \
  sqrt \
  strerror \
  strncasecmp \
  getaddrinfo \
])

haveepoll=no
AC_CHECK_FUNCS(epoll_ctl, [haveepoll=yes], )
if test "x$haveepoll" = "xyes" ; then
        AC_DEFINE(HAVE_EPOLL, 1,
                [Define if your system supports the epoll system calls])
fi

AC_CHECK_SIZEOF(long long)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(size_t)
AC_CHECK_SIZEOF(void *)
AC_CHECK_SIZEOF(off_t)


# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE

AC_CONFIG_FILES([Makefile src/Makefile test/Makefile])
AC_OUTPUT
