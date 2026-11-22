include $(ROOTDIR)/rules.mk

THISDIR := $(shell pwd)
SRCDIR := $(THISDIR)/src
PROG := nanoad

NANoad_SOURCES := $(SRCDIR)/main.c $(SRCDIR)/util.c $(SRCDIR)/rules.c \
                  $(SRCDIR)/dns.c $(SRCDIR)/sni.c $(SRCDIR)/web.c
NANoad_OBJECTS := $(NANoad_SOURCES:.c=.o)

NANoad_CFLAGS := -std=c99 -Os -g0 -Wall -Wextra -Wshadow -Wformat=2 \
                 -fno-strict-aliasing -ffunction-sections -fdata-sections \
                 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
NANoad_LDFLAGS := -static -Wl,--gc-sections

.PHONY: all clean romfs

all: $(PROG)

$(PROG): $(NANoad_OBJECTS)
	$(TARGET_CC) $(NANoad_LDFLAGS) -o $@ $(NANoad_OBJECTS)
	$(TARGET_CROSS)strip --strip-all $@

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/nanoad.h
	$(TARGET_CC) $(NANoad_CFLAGS) -c -o $@ $<

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(THISDIR)/$(PROG) $(ROMFSDIR)/usr/bin/$(PROG)
	$(INSTALL_BIN) $(THISDIR)/adbyby.sh $(ROMFSDIR)/usr/bin/adbyby.sh
	$(INSTALL_BIN) $(THISDIR)/scripts/nanoad-ctl $(ROMFSDIR)/usr/bin/nanoad-ctl
	$(INSTALL_BIN) $(THISDIR)/scripts/nanoad-update $(ROMFSDIR)/usr/bin/nanoad-update
	$(INSTALL_DIR) $(ROMFSDIR)/etc_ro/nanoad/web
	$(INSTALL_DATA) $(THISDIR)/config/nanoad.conf $(ROMFSDIR)/etc_ro/nanoad/nanoad.conf
	$(INSTALL_DATA) $(THISDIR)/config/subscriptions.txt $(ROMFSDIR)/etc_ro/nanoad/subscriptions.txt
	$(INSTALL_DATA) $(THISDIR)/config/custom-allow.txt $(ROMFSDIR)/etc_ro/nanoad/custom-allow.txt
	$(INSTALL_DATA) $(THISDIR)/config/custom-block.txt $(ROMFSDIR)/etc_ro/nanoad/custom-block.txt
	$(INSTALL_DATA) $(THISDIR)/config/rules.compact.txt.gz $(ROMFSDIR)/etc_ro/nanoad/rules.compact.txt.gz
	$(INSTALL_DATA) $(THISDIR)/web/index.html $(ROMFSDIR)/etc_ro/nanoad/web/index.html
	$(INSTALL_DATA) $(THISDIR)/LICENSE $(ROMFSDIR)/etc_ro/nanoad/LICENSE

clean:
	rm -f $(NANoad_OBJECTS) $(PROG)
