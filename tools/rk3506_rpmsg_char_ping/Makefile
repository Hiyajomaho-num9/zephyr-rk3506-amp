# SPDX-License-Identifier: MIT

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=
O ?= ../../output/tools/rk3506_rpmsg_char_ping

BINS := \
	rk3506_rpmsg_can \
	rk3506_rpmsg_can_tx \
	rk3506_rpmsg_char_ping \
	rk3506_rpmsg_char_stress \
	rk3506_socketcan_send

.PHONY: all clean install

TARGETS := $(addprefix $(O)/,$(BINS))

all: $(TARGETS)

$(O):
	mkdir -p $@

$(O)/%: %.c | $(O)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: all
	install -d $(DESTDIR)/userdata
	install -m 0755 $(TARGETS) $(DESTDIR)/userdata/

clean:
	rm -f $(TARGETS)
