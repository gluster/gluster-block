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

CLIENT = gluster-block
CDEP = glfs-operations.o utils.o rpc/block_clnt.c rpc/block_xdr.c gluster-block.o

SERVER = gluster-blockd
SDEP = rpc/block_svc.o rpc/block_xdr.o gluster-blockd.o utils.o

CFLAGS = -g -ggdb -Wall
LIBS := $(shell pkg-config --libs uuid glusterfs-api)

DEPS_LIST = gcc tcmu-runner targetcli

PREFIX ?= /usr/local/sbin
MKDIR_P = mkdir -p
LOGDIR = /var/log/


all: $(CLIENT) $(SERVER)

$(CLIENT): $(CDEP)
	@$(MKDIR_P) $(LOGDIR)$@
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

$(SERVER): $(SDEP)
	$(CC) $(CFLAGS) $^ -o $@

glfs-operations.o: glfs-operations.c glfs-operations.h
	$(foreach x, $(DEPS_LIST),\
		$(if $(shell which $x), \
		$(info -- found $x),\
		$(else, \
		$(error "No $x in PATH, install '$x' and continue ..."))))
	$(CC) $(CFLAGS) -c $< -o $@

$(CLIENT).o: $(CLIENT).c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(CLIENT) $(SERVER)
	cp $^ $(PREFIX)/

.PHONY: clean distclean
clean distclean:
	rm -f ./*.o ./rpc/*.o $(CLIENT) $(SERVER)

uninstall:
	rm -f $(PREFIX)/$(CLIENT) $(PREFIX)/$(SERVER)
