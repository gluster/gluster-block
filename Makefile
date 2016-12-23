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

BIN = gluster-block
OBJS = glfs-operations.o ssh-common.o utils.o gluster-block.o

CFLAGS = -g -Wall
LIBS := $(shell pkg-config --libs uuid glusterfs-api libssh)

DEPS_LIST = gcc tcmu-runner targetcli

PREFIX ?= /usr/local/sbin
MKDIR_P = mkdir -p
LOGDIR = /var/log/


all: $(BIN)

$(BIN): $(OBJS)
	@$(MKDIR_P) $(LOGDIR)$@
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

glfs-operations.o: glfs-operations.c glfs-operations.h
	$(foreach x, $(DEPS_LIST),\
		$(if $(shell which $x), \
		$(info -- found $x),\
		$(else, \
		$(error "No $x in PATH, install '$x' and continue ..."))))
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN).o: $(BIN).c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BIN)
	cp $< $(PREFIX)/

.PHONY: clean distclean
clean distclean:
	rm -f ./*.o $(BIN)

uninstall:
	rm -f $(PREFIX)/$(BIN)
