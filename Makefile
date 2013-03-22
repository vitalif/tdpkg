CACHE = tokyo
CC = gcc
CFLAGS = -g -Wall -fPIC
LIBS = -lc -ldl
SQLITELIBS = -lsqlite3
TOKYOLIBS = -ltokyocabinet
LDFLAGS = -nostdlib -shared
COMPILE = $(CC) $(CFLAGS)
LINK = $(CC) $(LDFLAGS)
SRCS = tdpkg.c util.c cache-$(CACHE).c
OBJS = $(subst .c,.o,$(SRCS))

all: libtdpkg.so

libtdpkg.so: $(OBJS)
ifeq ($(CACHE),sqlite)
	$(LINK) -o libtdpkg.so $+ $(LIBS) $(SQLITELIBS)
else
	$(LINK) -o libtdpkg.so $+ $(LIBS) $(TOKYOLIBS)
endif

%.o: %.c
	$(COMPILE) -c $<

clean:
	rm -f libtdpkg.so *.o
