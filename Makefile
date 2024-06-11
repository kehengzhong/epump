
#################################################################
#  Makefile for Event Pump - epump
#  Copyright (c) 2003-2024 Ke Hengzhong <kehengzhong@hotmail.com>
#  All rights reserved. See MIT LICENSE for redistribution.
#################################################################

PKGNAME = epump
PKGLIB = lib$(PKGNAME)
PKG_SO_LIB = $(PKGLIB).so
PKG_A_LIB = $(PKGLIB).a

PREFIX = /usr/local
INSTALL_INC_PATH = $(DESTDIR)$(PREFIX)/include
INSTALL_LIB_PATH = $(DESTDIR)$(PREFIX)/lib

ROOT := .
PKGPATH := $(shell basename `/bin/pwd`)

adif_inc = $(PREFIX)/include/adif
adif_lib = $(PREFIX)/lib

epump_inc = $(ROOT)/include
epump_src = $(ROOT)/src
epump_sample = $(ROOT)/sample

inc = $(ROOT)/include
obj = $(ROOT)/obj
dst = $(ROOT)/lib

alib = $(dst)/$(PKG_A_LIB)
solib = $(dst)/$(PKG_SO_LIB)

ADIF_RPATH = -Wl,-rpath,$(adif_lib)

#################################################################
#  Customization of shared object library (SO)

PKG_VER_MAJOR = 2
PKG_VER_MINOR = 2
PKG_VER_RELEASE = 10
PKG_VER = $(PKG_VER_MAJOR).$(PKG_VER_MINOR).$(PKG_VER_RELEASE)

PKG_VERSO_LIB = $(PKG_SO_LIB).$(PKG_VER)
PKG_SONAME_LIB = $(PKG_SO_LIB).$(PKG_VER_MAJOR)
LD_SONAME = -Wl,-soname,$(PKG_SONAME_LIB)


#################################################################
#  Customization of the implicit rules

CC = gcc

IFLAGS = -I$(adif_inc) -I$(epump_inc)

#CFLAGS = -Wall -O3 -fPIC -std=c99
CFLAGS = -Wall -O3 -fPIC
LFLAGS = -L/usr/lib -L/usr/local/lib
LIBS = -lnsl -lm -lz -lpthread
SOFLAGS = $(LD_SONAME)

APPLIBS = -ladif


ifeq ($(MAKECMDGOALS), debug)
  DEFS += -D_DEBUG
  CFLAGS += -g
endif

ifeq ($(MAKECMDGOALS), so)
  CFLAGS += 
endif


#################################################################
# Macro definition check

ifeq ($(shell test -e /usr/include/sys/epoll.h && echo 1), 1)
  DEFS += -DHAVE_EPOLL
else ifeq ($(shell test -e /usr/include/sys/event.h && echo 1), 1)
  DEFS += -DHAVE_KQUEUE
else
  DEFS += -DHAVE_SELECT
endif

ifeq ($(shell test -e /usr/include/sys/eventfd.h && echo 1), 1)
  DEFS += -DHAVE_EVENTFD
endif

ifeq ($(shell test -e /usr/include/openssl/ssl.h && echo 1), 1)
  DEFS += -DHAVE_OPENSSL
endif


#################################################################
# Set long and pointer to 64 bits or 32 bits

ifeq ($(BITS),)
  CFLAGS += -m64
else ifeq ($(BITS),64)
  CFLAGS += -m64
else ifeq ($(BITS),32)
  CFLAGS += -m32
else ifeq ($(BITS),default)
  CFLAGS += 
else
  CFLAGS += $(BITS)
endif


#################################################################
# OS-specific definitions and flags

UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
  DEFS += -DUNIX -D_LINUX_
endif

ifeq ($(UNAME), FreeBSD)
  DEFS += -DUNIX -D_FREEBSD_
endif

ifeq ($(UNAME), Darwin)
  DEFS += -D_OSX_

  PKG_VERSO_LIB = $(PKGLIB).$(PKG_VER).dylib
  PKG_SONAME_LIB = $(PKGLIB).$(PKG_VER_MAJOR).dylib
  LD_SONAME=

  SOFLAGS += -install_name $(dst)/$(PKGLIB).dylib
  SOFLAGS += -compatibility_version $(PKG_VER_MAJOR)
  SOFLAGS += -current_version $(PKG_VER)
endif

ifeq ($(UNAME), Solaris)
  DEFS += -DUNIX -D_SOLARIS_
endif
 

#################################################################
# Merge the rules

CFLAGS += $(DEFS)
LIBS += $(APPLIBS)
 

#################################################################
#  Customization of the implicit rules - BRAIN DAMAGED makes (HP)

AR = ar
ARFLAGS = rv
RANLIB = ranlib
RM = /bin/rm -f
COMPILE.c = $(CC) $(CFLAGS) $(IFLAGS) -c
LINK = $(CC) $(CFLAGS) $(IFLAGS) $(LFLAGS) -o
SOLINK = $(CC) $(CFLAGS) $(IFLAGS) $(LFLAGS) -shared $(SOFLAGS) -o

#################################################################
#  Modules

cnfs = $(wildcard $(epump_inc)/*.h)
sources = $(wildcard $(epump_src)/*.c)
objs = $(patsubst $(epump_src)/%.c,$(obj)/%.o,$(sources))


#################################################################
#  Standard Rules

.PHONY: all clean debug show

all: $(alib) $(solib)
	@(if cd $(epump_sample);then $(MAKE) $@;fi)
so: $(solib)
debug: $(alib) $(solib)
clean: 
	$(RM) $(objs)
	$(RM) -r $(obj)
	@cd $(dst) && $(RM) $(PKG_A_LIB)
	@cd $(dst) && $(RM) $(PKG_SO_LIB)
	@cd $(dst) && $(RM) $(PKG_SONAME_LIB)
	@cd $(dst) && $(RM) $(PKG_VERSO_LIB)
	@(if cd $(epump_sample);then $(MAKE) $@;fi)
show:
	@echo $(alib)
	@echo $(solib)

dist: $(cnfs) $(sources)
	cd $(ROOT)/.. && tar czvf $(PKGNAME)-$(PKG_VER).tar.gz $(PKGPATH)/src \
	    $(PKGPATH)/include $(PKGPATH)/lib $(PKGPATH)/Makefile $(PKGPATH)/README.md \
	    $(PKGPATH)/LICENSE $(PKGPATH)/sample $(PKGPATH)/$(PKGNAME).*

install: $(alib) $(solib)
	mkdir -p $(INSTALL_INC_PATH) $(INSTALL_LIB_PATH)
	install -s $(dst)/$(PKG_A_LIB) $(INSTALL_LIB_PATH)
	cp -af $(dst)/$(PKG_VERSO_LIB) $(INSTALL_LIB_PATH)
	@cd $(INSTALL_LIB_PATH) && $(RM) $(PKG_SONAME_LIB) && ln -sf $(PKG_VERSO_LIB) $(PKG_SONAME_LIB)
	@cd $(INSTALL_LIB_PATH) && $(RM) $(PKG_SO_LIB) && ln -sf $(PKG_SONAME_LIB) $(PKG_SO_LIB)
	cp -af $(inc)/epump.h $(INSTALL_INC_PATH)

uninstall:
	cd $(INSTALL_LIB_PATH) && $(RM) $(PKG_SO_LIB)
	cd $(INSTALL_LIB_PATH) && $(RM) $(PKG_SONAME_LIB) 
	cd $(INSTALL_LIB_PATH) && $(RM) $(PKG_VERSO_LIB) 
	cd $(INSTALL_LIB_PATH) && $(RM) $(PKG_A_LIB) 
	$(RM) $(INSTALL_INC_PATH)/epump.h


#################################################################
#  Additional Rules
#
#  target1 [target2 ...]:[:][dependent1 ...][;commands][#...]
#  [(tab) commands][#...]
#
#  $@ - variable, indicates the target
#  $? - all dependent files
#  $^ - all dependent files and remove the duplicate file
#  $< - the first dependent file
#  @echo - print the info to console
#
#  SOURCES = $(wildcard *.c *.cpp)
#  OBJS = $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCES)))
#  CSRC = $(filter %.c,$(files))
#  SRCOBJ = $(SOURCES:.c=.o)


$(solib): $(objs) 
	$(SOLINK) $(dst)/$(PKG_VERSO_LIB) $? 
	@cd $(dst) && $(RM) $(PKG_SONAME_LIB) && ln -s $(PKG_VERSO_LIB) $(PKG_SONAME_LIB)
	@cd $(dst) && $(RM) $(PKG_SO_LIB) && ln -s $(PKG_SONAME_LIB) $(PKG_SO_LIB)
     
$(alib): $(objs) 
	$(AR) $(ARFLAGS) $@ $?
	$(RANLIB) $(RANLIBFLAGS) $@

$(obj)/%.o: $(epump_src)/%.c $(cnfs)
	@mkdir -p $(obj)
	$(COMPILE.c) $< -o $@

