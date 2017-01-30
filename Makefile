########################################################################
#                                                                      #
#  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>            #
#  This file is part of gluster-block.                                 #
#                                                                      #
#  This file is licensed to you under your choice of the GNU Lesser    #
#  General Public License, version 3 or any later version (LGPLv3 or   #
#  later), or the GNU General Public License, version 2 (GPLv2), in all#
#  cases as published by the Free Software Foundation.                 #
#                                                                      #
########################################################################


CC = gcc

COMMON = utils.o common.o rpc/block_xdr.o

CLIENT = gluster-block
CDEP = rpc/block_clnt.o gluster-block.o

SERVER = gluster-blockd
SDEP = glfs-operations.o rpc/block_svc.o rpc/block_clnt.o gluster-blockd.o

CFLAGS = -g -ggdb -Wall -lpthread
LIBS := $(shell pkg-config --libs uuid glusterfs-api)

DEPS_LIST = gcc tcmu-runner targetcli

PREFIX ?= /usr/local/sbin
MKDIR_P = mkdir -p
INSTALL = /usr/bin/install -c
INSTALLDATA = /usr/bin/install -c -m 644
SYSTEMD_DIR = /usr/lib/systemd/system
LOGDIR = /var/log/gluster-block


all: $(SERVER) $(CLIENT)

$(CLIENT): $(CDEP) $(COMMON)
	@$(MKDIR_P) $(LOGDIR)$@
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

$(SERVER): $(SDEP) $(COMMON)
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

glfs-operations.o: glfs-operations.c glfs-operations.h
	$(foreach x, $(DEPS_LIST),\
		$(if $(shell which $x), \
		$(info -- found $x),\
		$(else, \
		$(error "No $x in PATH, install '$x' and continue ..."))))
	$(CC) $(CFLAGS) -c $< -o $@

$(CLIENT).o: $(CLIENT).c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(SERVER) $(CLIENT)
	$(INSTALL) $^ $(PREFIX)/
	@if [ -d $(SYSTEMD_DIR) ]; then \
		$(MKDIR_P) $(SYSTEMD_DIR); \
		$(INSTALLDATA) systemd/$(SERVER).service $(SYSTEMD_DIR)/; \
	fi

.PHONY: clean distclean
clean distclean:
	$(RM) ./*.o ./rpc/*.o $(CLIENT) $(SERVER)

uninstall:
	$(RM) $(PREFIX)/$(CLIENT) $(PREFIX)/$(SERVER) $(SYSTEMD_DIR)/$(SERVER).service
