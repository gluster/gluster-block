#!/bin/bash

# Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
# This file is part of gluster-block.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

# This script is meant for running upgrade activities,
# it is triggered by gluster-blockd unit startup.

GB_STATUSFILE="/var/lib/gluster-block/gb_upgrade.status"
LOGDIR="${TCMU_LOGDIR:-/var/log/gluster-block}"
GB_LOGFILE="${LOGDIR}/gluster-block-upgrade-activities.log"
GB_SAVEFILE="/etc/target/saveconfig.json"
GB_TMP_SAVEFILE="/tmp/gb_saveconfig.json"
USED_COMMANDS="ps sed grep awk head echo pkill gluster gluster-block gluster-blockd"  # append new use of commands here
mkdir -p "${LOGDIR}"
GBD_STARTED=0
GBD_PID=0


function kill_gluster_blockd_wait()
{
  if [[ ${GBD_STARTED} -ne 0 && ${GBD_PID} -ne 0 ]]
  then
    # kill gluster-blockd
    kill -15 ${GBD_PID} > /dev/null 2>&1

    # let's wait until the child process is finished
    wait ${GBD_PID}
  fi
}


function my_exit()
{
  local ret=${1}

  kill_gluster_blockd_wait

  exit ${ret}
}

function printLog()
{
  local msg=${1}

  echo "[$(date -u +'%Y-%m-%d %H:%M:%S')] ${msg}" >> ${GB_LOGFILE}
}


function get_bhv_list()
{
  local status=${1}

  gluster volume info | \
  # get volume name, status, shard-status, number-of-bricks
  grep -P "(^Volume Name:|^Status:|^features.shard:|^Number of Bricks:)" | \
  # get the volumes based on status
  sed '/^Volume Name:/{x;p;x;}' | sed -e '/./{H;$!d;}' -e "x;/Status: ${status}/!d;" | \
  # get shard enabled volumes
  sed -e '/./{H;$!d;}' -e 'x;/features.shard: on/b' -e '/features.shard: yes/b' -e '/features.shard: enable/b' -e '/features.shard: true/b' -e '/features.shard: 1/b' -e d | \
  # get comma separated volume names
  grep -P "(^Volume Name:)" | awk '{print $NF}' | sed -e '{:a;N;$!ba};s/\n/,/g'
}


function get_local_hostip()
{
  local ipaddr

  while read -r ipaddr
  do
    if [[ $(ip addr | grep -wc ${ipaddr}) -eq 1 ]]
    then
      echo ${ipaddr}
      break
    fi
  done < <(gluster volume info | grep -P "(^Brick[0-9]*:)" | awk -F':' '{print $2;}')
}


function check_daemons_running()
{
  if ! ps cax | grep -wq '[g]lusterd'
  then
    printLog "ERROR: Glusterd is not running"
    my_exit 1
  fi

  # try 5 times to make sure the backgroud process is fired.
  count=1
  while [[ ${count} -le 5 ]]
  do
    if ps cax | grep -wq '[g]luster-blockd'
    then
      break
    fi
    sleep 1
    let "count++"
  done

  if [[ ${count} -gt 5 ]]
  then
    printLog "ERROR: Gluster-blockd is not running"
    my_exit 1
  fi
}


function check_dependent_commands_exist()
{
  for i in ${USED_COMMANDS}
  do
    if ! command -v "${i}" > /dev/null 2>&1
    then
      printLog "ERROR: \'${i}\' command is not found"
      my_exit 1
    fi
  done
}


# get the hint about bricks offline state
/usr/libexec/gluster-block/wait-for-bricks.sh 120
bricksup_ret=${?}
if [[ ${bricksup_ret} -ne 0 ]]
then
  printLog "WARNING: few bricks are down"
fi

# skip running this script, if status file exist
if [[ -f ${GB_STATUSFILE} ]]
then
  printLog "INFO: skipping upgrade activities"
  my_exit 0
fi

printLog "INFO: started upgrade activities"

check_dependent_commands_exist
# start gluster-blockd
gluster-blockd --no-remote-rpc & > /dev/null 2>&1
GBD_PID=${!}
GBD_STARTED=1
check_daemons_running

stopped_bhvs_list=$(get_bhv_list "Stopped")
if [[ ! -z ${stopped_bhvs_list} ]]
then
  stopped_vols_exist=1
  printLog "WARNING: block hosting volumes \'${stopped_bhvs_list}\' are in stopped state"
fi

started_bhvs_list=$(get_bhv_list "Started")
# check if this setup has valid block hosting volumes
if [[ -z ${started_bhvs_list} ]]
then
  printLog "INFO: no block hosting volumes found"
  if [[ ${stopped_vols_exist} -eq 0 ]]
  then
    touch ${GB_STATUSFILE}
    printLog "INFO: successfully completed the upgrade activities"
    my_exit 0
  fi
  printLog "WARNING: not completely successful with upgrade activities, on next start of gluster-blockd service will try again!"
  my_exit 1
fi

# generate the block volumes target configuration
tpg=$(get_local_hostip)
printLog "INFO: about to run '#gluster-block genconfig ${started_bhvs_list} enable-tpg ${tpg}'"
gluster-block genconfig ${started_bhvs_list} enable-tpg ${tpg} > ${GB_TMP_SAVEFILE}
genconfig_ret=${?}
if [[ ${genconfig_ret} -ne 0 ]]
then
  printLog "WARNING: genconfig returns \'${genconfig_ret}\'"
else
  printLog "INFO: gluster-block genconfig returned ${genconfig_ret}"
fi

kill_gluster_blockd_wait

# copy the generated saveconfig file to right path
if [[ -f ${GB_TMP_SAVEFILE} ]]
then
  mv ${GB_SAVEFILE} "${GB_SAVEFILE}-bak-$(date -u +'%Y-%m-%d %I:%M:%S' | sed 's/ /-/g')" > /dev/null 2>&1
  mv ${GB_TMP_SAVEFILE} ${GB_SAVEFILE} > /dev/null 2>&1
  rm -rf ${GB_TMP_SAVEFILE}
fi

if [[ ${bricksup_ret} -eq 0 ]] && [[ ${genconfig_ret} -eq 0 ]] && [[ ${stopped_vols_exist} -eq 0 ]]
then
  touch ${GB_STATUSFILE}
  printLog "INFO: successfully completed the upgrade activities"
  exit 0
fi
printLog "WARNING: not completely successful with upgrade activities, on next start of gluster-blockd service will try again!"
exit 1
