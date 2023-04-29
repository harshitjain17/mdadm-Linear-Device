CC=gcc
CFLAGS=-c -Wall -I. -fpic -g -fbounds-check
LDFLAGS=-L.
LIBS=-lcrypto

OBJS=tester.o util.o mdadm.o cache.o net.o

%.o:	%.c %.h
	$(CC) $(CFLAGS) $< -o $@

tester:	$(OBJS) jbod.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(OBJS) tester
