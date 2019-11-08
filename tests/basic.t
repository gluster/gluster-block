#!/bin/bash
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


HOST=$(hostname -I | awk '{print $1}')
VOLNAME="hosting-volume"
BLKNAME="block-volume"
SBLKNAME="sblock-volume"
BRKDIR="/brick"


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

##### Create Gluster Volume Test Start #####
TEST gluster vol create ${VOLNAME} ${HOST}:${BRKDIR} force

# Start the volume
TEST gluster vol start ${VOLNAME}
##### End #####


##### Gfapi Access Test Start #####
TEST ./tests/gfapi-test ${VOLNAME} ${HOST}
##### End #####


##### Restart gluster-blockd.service Test Start #####
systemctl daemon-reload
TEST systemctl restart gluster-blockd.service
sleep 5
##### End #####


##### gluster-block 'version' and 'help' Test Start #####
TEST gluster-block version
TEST gluster-block help
##### End #####


##### Block 'create' and 'delete' Test Start #####
# Simple block create/delete
TEST gluster-block create ${VOLNAME}/${BLKNAME} ${HOST} 1MiB
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

# Simple block create/delete with 'timeout' set
TEST gluster-block timeout 350 create ${VOLNAME}/${BLKNAME} ${HOST} 1MiB
TEST gluster-block timeout 350 delete ${VOLNAME}/${BLKNAME}

# Block create with 'auth enable' set/delete
TEST gluster-block create ${VOLNAME}/${BLKNAME} auth enable ${HOST} 1MiB
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

# Block create with 'auth disable' set/delete
TEST gluster-block create ${VOLNAME}/${BLKNAME} auth disable ${HOST} 1MiB
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

# Block create with 'ha' set/delete
TEST gluster-block create ${VOLNAME}/${BLKNAME} ha 1 ${HOST} 1MiB
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

# Block create with 'prealloc full' set/delete
TEST gluster-block create ${VOLNAME}/${BLKNAME} prealloc full ${HOST} 1MiB
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

# Block create with 'prealloc no' set/delete
TEST gluster-block create ${VOLNAME}/${BLKNAME} prealloc no ${HOST} 1MiB
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

# Block create with 'ring-buffer' set/delete
TEST gluster-block create ${VOLNAME}/${BLKNAME} ring-buffer 32 ${HOST} 1MiB
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

# Block create with 'io-timeout' set/delete
TEST gluster-block create ${VOLNAME}/${BLKNAME} io-timeout 44 ${HOST} 1MiB
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

# Block create with 'block-size' set/delete
TEST gluster-block create ${VOLNAME}/${BLKNAME} block-size 1024 ${HOST} 1MiB
TEST gluster-block delete ${VOLNAME}/${BLKNAME}

# Block create with 'storage' set and delete with 'unlink-storage no' set
TEST gluster-block create ${VOLNAME}/${BLKNAME} ${HOST} 1MiB
LINK=`eval gluster-block info ${VOLNAME}/${BLKNAME} | grep GBID | awk -F' ' '{print $2}'`
TEST gluster-block create ${VOLNAME}/${SBLKNAME} storage ${LINK} ${HOST}

TEST gluster-block delete ${VOLNAME}/${SBLKNAME} unlink-storage no
TEST gluster-block delete ${VOLNAME}/${BLKNAME}
##### End #####


# Block create
TEST gluster-block create ${VOLNAME}/${BLKNAME} ha 1 ${HOST} 1MiB

##### Genconfig Block Test Start #####
TEST gluster-block genconfig ${VOLNAME} enable-tpg ${HOST}
##### End #####


###### Modify Block Test Start #####
# Enable 'auth'
TEST gluster-block modify ${VOLNAME}/${BLKNAME} auth enable

# Increase size 5 times
n=2
while [ $n -le 6 ]
do
    TEST gluster-block modify ${VOLNAME}/${BLKNAME} size ${n}MiB
    (( n++ ))
    sleep 1
done

# Decrease size 5 times
n=5
while [ $n != 0 ]
do
    TEST gluster-block modify ${VOLNAME}/${BLKNAME} size ${n}MiB force
    (( n-- ))
    sleep 1
done

# Disable 'auth'
TEST gluster-block modify ${VOLNAME}/${BLKNAME} auth disable
##### End #####


##### Block 'list' and 'info' Test Start #####
# List blocks
TEST gluster-block list ${VOLNAME}

# Block info
TEST gluster-block info ${VOLNAME}/${BLKNAME}
##### End #####

# Block delete
gluster-block delete ${VOLNAME}/${BLKNAME}

echo -e "\n*** JSON responses ***\n"

# Block create and expect json response
TEST gluster-block create ${VOLNAME}/${BLKNAME} ha 1 ${HOST} 1MiB --json-pretty

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
TEST gluster-block create ${VOLNAME}/${BLKNAME} ha 1 auth enable ${HOST} 1MiB --json-pretty

cleanup;
