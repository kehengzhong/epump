
#################################################################
#  Makefile for echosrv
#  (c) 2020 Ke Heng Zhong (Beijing, China)
#  Writen by ke hengzhong (kehengzhong@hotmail.com)
#################################################################

PKGNAME = echosrv

PKGBIN = $(PKGNAME)

PREFIX = /usr/local

ROOT := .

adif_inc = $(PREFIX)/include/adif
adif_lib = $(PREFIX)/lib

epump_inc = $(PREFIX)/include
epump_lib = $(PREFIX)/lib

main_inc = $(ROOT)
main_src = $(ROOT)

obj = $(ROOT)
dst = $(ROOT)

bin = $(dst)/$(PKGBIN)

RPATH = -Wl,-rpath,/usr/local/lib


#################################################################
#  Customization of the implicit rules

CC = gcc

IFLAGS = -I$(adif_inc) -I$(epump_inc)

CFLAGS = -Wall -O3 -fPIC
LFLAGS = -L/usr/lib -L/usr/local/lib
LIBS = -lnsl -lm -lz -lpthread

APPLIBS = -ladif -lepump $(RPATH)


ifeq ($(MAKECMDGOALS), debug)
  DEFS += -D_DEBUG
  CFLAGS += -g
endif

ifeq ($(MAKECMDGOALS), so)
  CFLAGS += 
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
  DEFS += -DOSX
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

cnfs = $(wildcard $(main_inc)/*.h)
sources = $(wildcard $(main_src)/*.c)
objs = $(patsubst $(main_src)/%.c,$(obj)/%.o,$(sources))


#################################################################
#  Standard Rules

.PHONY: all clean debug show

all: $(bin) 
debug: $(bin)
clean: 
	$(RM) $(objs)
	@cd $(dst) && $(RM) $(PKGBIN)
show:
	@echo $(bin)


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


$(bin): $(objs) 
	$(LINK) $@ $? $(LIBS)

$(obj)/%.o: $(main_src)/%.c $(cnfs)
	@mkdir -p $(obj)
	$(COMPILE.c) $< -o $@

