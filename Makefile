# maildrop - build rules
#
#   make          build maildropd and maildrop
#   make clean    remove the binaries

CC	?= cc
CFLAGS	?= -O2 -g -std=gnu11 -Wall -Wextra -Werror
LDFLAGS	?=

PROGS	= maildropd maildrop

all: $(PROGS)

maildropd: server.c config.h
	$(CC) $(CFLAGS) -o $@ server.c $(LDFLAGS)

maildrop: client.c config.h
	$(CC) $(CFLAGS) -o $@ client.c $(LDFLAGS)

clean:
	rm -rf $(PROGS) *.dSYM

.PHONY: all clean
