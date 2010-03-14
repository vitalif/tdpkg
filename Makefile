CC = gcc
CFLAGS = -g -Wall -fPIC
LIBS = -lc -ldl -lsqlite3
LDFLAGS = -nostdlib -shared
COMPILE = $(CC) $(CFLAGS)
LINK = $(CC) $(LDFLAGS) $(LIBS)
SRCS = tdpkg.c cache.c
OBJS = $(subst .c,.o,$(SRCS))

all: libtdpkg.so

libtdpkg.so: $(OBJS)
	$(LINK) -o libtdpkg.so $+

%.o: %.c
	$(COMPILE) -c $<

clean:
	rm -f libtdpkg.so *.o