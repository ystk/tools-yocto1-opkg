PROG=gtkopkg
OBJS=gtkopkg.o pkg_list.o
CFLAGS=-O2 -g -Wall \
       `pkg-config --cflags gtk+-2.0` \
       -I ~/opkg/code/svn
LIBS=`pkg-config --libs gtk+-2.0` \
     -L ~/opkg/code/svn/libopkg/.libs -lopkg
CC=gcc

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(PROG) $(LIBS)

install: $(PROG)
	install -d $(DESTDIR)/usr/bin
	install -m 0755 $(PROG) $(DESTDIR)/usr/bin/$(PROG)

clean:
	rm -f $(OBJS) $(PROG)
