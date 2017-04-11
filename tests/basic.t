#!/bin/env bash
#********************************************************************#
#                                                                    #
# Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>           #
# This file is part of gluster-block.                                #
#                                                                    #
# This file is licensed to you under your choice of the GNU Lesser   #
# General Public License, version 3 or any later version (LGPLv3 or  #
# later), or the GNU General Public License, version 2 (GPLv2), in   #
# all cases as published by the Free Software Foundation.            #
#                                                                    #
#                                                                    #
# Run:   (Make sure you have all the dependent binaries installed)   #
# $ ./tests/basic.t                                                  #
#                                                                    #
#********************************************************************#


HOST=$(hostname)
VOLNAME="block-test"
BLKNAME="sample-block"
BRKDIR="/tmp/block/"


function TEST()
{
  echo "TEST : $@"
  eval $@
  if [ $? -ne 0 ]; then
    echo -e "line $(caller | awk '{print $1}') : NOT OK\n"
    exit 1;
  fi
  echo -e "line $(caller | awk '{print $1}') : OK\n"
}


function cleanup()
{
  echo -e "\nRunning test cleanup ..."

  # Block delete
  gluster-block delete ${VOLNAME}/${BLKNAME} --json-pretty

  gluster --mode=script vol stop ${VOLNAME}
  gluster --mode=script vol del ${VOLNAME}

  rm -rf ${BRKDIR}
}


function force_terminate()
{
  local ret=$?;
  >&2 echo -e "\nreceived external"\
              "signal --$(kill -l $ret)--, calling 'cleanup' ...\n";
  cleanup;
  exit $ret;
}


trap force_terminate INT TERM HUP

pidof glusterd 2>&1 >/dev/null
if [ $? -eq 1 ]; then
  TEST glusterd
fi

# Create gluster volume
TEST gluster vol create ${VOLNAME} ${HOST}:${BRKDIR} force

# Start the volume
TEST gluster vol start ${VOLNAME}

# Start gluster-blockd.service
systemctl daemon-reload
TEST systemctl restart gluster-blockd.service
sleep 1;

# Block create
TEST gluster-block create ${VOLNAME}/${BLKNAME} ha 1 ${HOST} 1GiB

# Modify Block with auth enable
TEST gluster-block modify ${VOLNAME}/${BLKNAME} auth enable

# Block list
TEST gluster-block list ${VOLNAME}

# Block info
TEST gluster-block info ${VOLNAME}/${BLKNAME}

# Modify Block with auth disable
TEST gluster-block modify ${VOLNAME}/${BLKNAME} auth disable

# Block delete
gluster-block delete ${VOLNAME}/${BLKNAME}

# Block create with auth set
TEST gluster-block create ${VOLNAME}/${BLKNAME} ha 1 auth enable ${HOST} 1GiB

# Block delete
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

echo -e "\n*** JSON responses ***\n"

# Block create and expect json response
TEST gluster-block create ${VOLNAME}/${BLKNAME} ha 1 ${HOST} 1GiB --json-pretty

# Modify Block with auth enable and expect json response
TEST gluster-block modify ${VOLNAME}/${BLKNAME} auth enable --json-pretty

# Block list and expect json response
TEST gluster-block list ${VOLNAME} --json-pretty

# Block info and expect json response
TEST gluster-block info ${VOLNAME}/${BLKNAME} --json-pretty

# Modify Block with auth disable and expect json response
TEST gluster-block modify ${VOLNAME}/${BLKNAME} auth disable --json-pretty

# Block delete and expect json response
TEST gluster-block delete ${VOLNAME}/${BLKNAME} --json-pretty

# Block create with auth set and expect json response
TEST gluster-block create ${VOLNAME}/${BLKNAME} ha 1 auth enable ${HOST} 1GiB --json-pretty

cleanup;
