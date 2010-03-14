CACHE = tokyo
CC = gcc
CFLAGS = -g -Wall -fPIC
LIBS = -lc -ldl
SQLITELIBS = -lsqlite3
TOKYOLIBS = -ltokyocabinet
LDFLAGS = -nostdlib -shared
COMPILE = $(CC) $(CFLAGS)
LINK = $(CC) $(LDFLAGS) $(LIBS)
SRCS = tdpkg.c cache-$(CACHE).c
OBJS = $(subst .c,.o,$(SRCS))

all: libtdpkg.so

libtdpkg.so: $(OBJS)
ifeq ($(CACHE),sqlite)
	$(LINK) $(SQLITELIBS) -o libtdpkg.so $+
else
	$(LINK) $(TOKYOLIBS) -o libtdpkg.so $+
endif

%.o: %.c
	$(COMPILE) -c $<

clean:
	rm -f libtdpkg.so *.o