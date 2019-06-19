#!/bin/bash
#Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
#This file is part of gluster-block.
#
#This file is licensed to you under your choice of the GNU Lesser
#General Public License, version 3 or any later version (LGPLv3 or
#later), or the GNU General Public License, version 2 (GPLv2), in all
#cases as published by the Free Software Foundation.

# This file waits for gluster block hosting volume bricks to be up. If all the
# bricks are up, it exits with success status, otherwise waits for the $timeout
# seconds before erroring out with the status about
# online-bricks/expected-online-bricks

LOGDIR="${TCMU_LOGDIR:-/var/log/gluster-block}"
mkdir -p "${LOGDIR}"

function printLog()
{
    local msg=${1}

    echo "[$(date -u +'%Y-%m-%d %H:%M:%S')] ${msg}" >> "${LOGDIR}/gluster-block-bricks-start.log"
}

function volume_online_brick_count()
{
        local volname=$1
        local brick_count=$2
        gluster --xml volume status "$volname" | grep '<status>' | head -n "$brick_count" | grep -c '<status>1'
}

function get_block_volnames_brick_count()
{
        gluster v info | \
        # get volume name, status, shard-status, number-of-bricks
        grep -P "(^Volume Name:|^Status:|^features.shard:|^Number of Bricks:)" | \
        # get the 'started' volumes
        sed '/^Volume Name:/{x;p;x;}' | sed -e '/./{H;$!d;}' -e 'x;/Status: Started/!d;' | \
        # get shard enabled volumes
        sed -e '/./{H;$!d;}' -e 'x;/features.shard: on/b' -e '/features.shard: yes/b' -e '/features.shard: enable/b' -e '/features.shard: true/b' -e '/features.shard: 1/b' -e d | \
        # Print volume names
        grep -P "(^Volume Name:|^Number of Bricks:)" | awk '{print $NF}'
}

function volumes_waiting_for_bricks_up()
{
        local wait_info
        local vol_down_info
        while read -r volname
        do
                read -r total_brick_count
                brick_count="$(volume_online_brick_count "$volname" "$total_brick_count")"
                if [[ $brick_count -ne $total_brick_count ]]
                then
                        vol_down_info="$volname ($((total_brick_count - brick_count))/$total_brick_count)"
                        if [[ -z "$wait_info" ]]
                        then
                                wait_info="$vol_down_info"
                        else
                                wait_info="$wait_info, $vol_down_info"
                        fi
                fi
        done < <(get_block_volnames_brick_count)
        if [[ ! -z "$wait_info" ]]; then echo "$wait_info"; fi
}

function check_dependent_commands_exist()
{
        declare -a arr=("ps" "sed" "grep" "awk" "head" "echo")
        for i in "${arr[@]}"
        do
                if ! command -v "$i" > /dev/null 2>&1
                then
                        printLog "ERROR: \'$i\' command is not found"
                        exit 1
                fi
        done

}
function check_glusterd_running()
{
# Explanation of why ps command is used this way:
# https://stackoverflow.com/questions/9117507/linux-unix-command-to-determine-if-process-is-running
        if ! ps cax | grep -w '[g]lusterd' > /dev/null 2>&1
        then
                printLog "ERROR: Glusterd is not running"
                exit 1
        fi
}

function wait_for_bricks_up()
{
        local endtime=$1
        local now

        while [[ ! -z "$(volumes_waiting_for_bricks_up)" ]]
        do
                now=$(date +%s%N)
                if [[ $now -ge $endtime ]]
                then
                        printLog "WARNING: Timeout Expired, bricks of volumes:\"$(volumes_waiting_for_bricks_up)\" are yet to come online"
                        exit 1
                fi
                sleep 1
        done
}

if [[ "$#" -ne 1 ]]
then
        echo "Usage $0 <timeout-in-seconds>"
        exit 1
fi

case $1 in
    ''|*[!0-9]*) echo "Usage $0 <timeout-in-seconds>"; exit 1 ;;
    *) ;;
esac

check_dependent_commands_exist
check_glusterd_running
timeout=$1
start=$(date +%s%N)
#Convert timeout to Nano Seconds as 'start' is in Nano seconds
endtime=$(( timeout*1000000000 + start ))
wait_for_bricks_up $endtime
now=$(date +%s%N)
printLog "INFO: All bricks for Block Hosting Volumes came online in $(((now-start)/1000000000)) seconds"
exit 0
