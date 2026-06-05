# SPDX-License-Identifier: MIT

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

BINS := \
	rk3506_rpmsg_can \
	rk3506_rpmsg_can_tx \
	rk3506_rpmsg_char_ping \
	rk3506_rpmsg_char_stress \
	rk3506_socketcan_send

.PHONY: all clean install

all: $(BINS)

rk3506_rpmsg_can: rk3506_rpmsg_can.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

rk3506_rpmsg_can_tx: rk3506_rpmsg_can_tx.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

rk3506_rpmsg_char_ping: rk3506_rpmsg_char_ping.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

rk3506_rpmsg_char_stress: rk3506_rpmsg_char_stress.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

rk3506_socketcan_send: rk3506_socketcan_send.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: all
	install -d $(DESTDIR)/userdata
	install -m 0755 $(BINS) $(DESTDIR)/userdata/

clean:
	rm -f $(BINS)
