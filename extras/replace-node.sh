#!/bin/bash

#************************************************************************#
#                                                                        #
#  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>              #
#  This file is part of gluster-block.                                   #
#                                                                        #
#  This file is licensed to you under your choice of the GNU Lesser      #
#  General Public License, version 3 or any later version (LGPLv3 or     #
#  later), or the GNU General Public License, version 2 (GPLv2), in all  #
#  cases as published by the Free Software Foundation.                   #
#                                                                        #
#************************************************************************#
#  How to run this script:                                               #
#  ----------------------                                                #
#  $ ./replace-node.sh ${MOUNTPOINT} ${OLDNODE} ${NEWNODE}               #
#                                                                        #
#************************************************************************#


LOGDIR="/var/log/gluster-block/"


function schedule_terminate () {

    echo -e "\nholdon! we have received signal, this program will be terminated in the next safe point ..\n"
    printLog "Terminating script in the signal handler..."
    SAFE_EXIT=1
    fdClose ${fd}
}


function getAvailableFd() {

    for i in {1..65536}; do
        if [ ! -e /proc/$$/fd/${i} ]; then
            echo ${i};
            return 0;
        fi
    done

    return 1;
}


function fdOpen() {
    local fd=${1}
    local path=${2}


    # open in read mode
    eval "exec ${fd}<${path}"
}


function fdClose() {
    local fd=${1}


    eval "exec ${fd}>&-"
}


function printLog() {
    local msg=${1}


    echo "[$(date -u +'%Y-%m-%d %I:%M:%S')] ${msg}" >> ${LOGDIR}/gluster-block-replace.log
}


function readMetaInfo() {

    while read line; do
        METADATA+=(["$(echo ${line} | cut -d: -f1)"]=$(echo ${line} | cut -d' ' -f2 | tr -d " \n"))
    done < ${1}
}


function isStrPartOfArray() {
    local args=("${@}")
    local str=${args[0]}
    local array=${args[@]:1}


    for i in ${array[@]}; do
        if [ "${str}" == "${i}" ]; then
            return 0;
        fi
    done

    return 1;
}


function isStatusValid() {
    local status=${1}


    case "${status}" in
        "CONFIGSUCCESS" | "CLEANUPINPROGRESS" | "AUTHENFORCEING" | "AUTHENFORCED" |  "AUTHENFORCEFAIL" | "AUTHCLEARENFORCED" | "AUTHCLEARENFORCEING" | "AUTHCLEARENFORCEFAIL")
            return 0;
            ;;
    esac

    return 1;
}


function getValidHosts() {
    eval "declare -A metadata="${1#*=}

    local validHostsArr=()
    local commonKeys=(VOLUME GBID SIZE HA ENTRYCREATE PASSWORD)


    for index in ${!metadata[@]}; do
        isStrPartOfArray "${index}" "${commonKeys[@]}"
        ret=$?
        if [[ ${ret} -ne 0 ]]; then
            isStatusValid ${metadata["${index}"]}
            ret=$?
            if [[ ${ret} -ne 1 ]]; then
                validHostsArr+=("${index}")
            fi
        fi
    done

    unset metadata

    echo "${validHostsArr[@]}"  # return
}


function runCreate() {
    eval "declare -A metadata="${1#*=}
    local block_name=${2}
    local old_node=${3}
    local new_node=${4}

    local iqn_prefix="iqn.2016-12.org.gluster-block"
    local gbid=${metadata["GBID"]}
    local hosts=( $(getValidHosts "$(declare -p metadata)") )

    local subcmd1="\$(targetcli /backstores/user:glfs ls | grep ' ${block_name} ' -c)"

    local subcmd2="targetcli set global auto_add_default_portal=false auto_enable_tpgt=false loglevel_file=info logfile=${LOGFILE}\n
                   targetcli /backstores/user:glfs create ${block_name} ${metadata["SIZE"]} ${metadata["VOLUME"]}@${new_node}/block-store/${gbid} ${gbid}\n
                   targetcli /backstores/user:glfs/${block_name} set attribute cmd_time_out=0\n
                   targetcli /iscsi create ${iqn_prefix}:${gbid}\n"


    for index in ${!hosts[@]}; do
        NODE=${hosts[$index]}
        if [[ "${NODE}" == "${old_node}" ]]; then
            NODE=${new_node}
        fi

        no=$(expr ${index} + 1)

        if [[ ${no} -ne 1 ]]; then
            subcmd2+="targetcli /iscsi/${iqn_prefix}:${metadata["GBID"]} create tpg${no}\n"
        fi

        if [[ "${NODE}" == "${new_node}" ]]; then
            subcmd2+="targetcli /iscsi/${iqn_prefix}:${gbid}/tpg${no}/luns create /backstores/user:glfs/${block_name}\n
                      targetcli /iscsi/${iqn_prefix}:${gbid}/tpg${no}/portals create ${NODE}\n
                      targetcli /iscsi/${iqn_prefix}:${gbid}/tpg${no} enable\n
                      targetcli /iscsi/${iqn_prefix}:${gbid}/tpg${no} set attribute generate_node_acls=1 demo_mode_write_protect=0\n"
        else
            subcmd2+="targetcli /iscsi/${iqn_prefix}:${gbid}/tpg${no}/luns create /backstores/user:glfs/${block_name}\n
                      targetcli /iscsi/${iqn_prefix}:${gbid}/tpg${no}/portals create ${NODE}\n
                      targetcli /iscsi/${iqn_prefix}:${gbid}/tpg${no} set attribute tpg_enabled_sendtargets=0 generate_node_acls=1 demo_mode_write_protect=0\n"
        fi

        if [[ "x${metadata["PASSWORD"]}" != "x" ]]; then
            subcmd2+="targetcli /iscsi/${iqn_prefix}:${gbid}/tpg${no} set auth userid=${gbid} password=${metadata["PASSWORD"]}\n"
        fi
    done

    subcmd2+="targetcli / saveconfig"


    cmd="if [[ ${subcmd1} -eq 0 ]]; then ${subcmd2}; else echo "skipped"; fi"

    unset metadata

    IFS=' '
    eval $(printf "${cmd}")  # exec
}


function runReplace() {
    eval "declare -A metadata="${1#*=}
    local old_node=${2}
    local new_node=${3}

    local iqn_prefix="iqn.2016-12.org.gluster-block"
    local gbid=${metadata["GBID"]}
    local tpgno=$(targetcli /iscsi/${iqn_prefix}:${gbid} ls | grep -e tpg -e " ${old_node}:" | grep -B1 " ${old_node}:" | grep -o "tpg[0-9]*")

    local subcmd="targetcli /iscsi/${iqn_prefix}:${gbid}/${tpgno}/portals create ${new_node};
                  targetcli /iscsi/${iqn_prefix}:${gbid}/${tpgno}/portals delete ip_address=${old_node} ip_port=3260;
                  targetcli / saveconfig"


    cmd="if [[ "x" != "${tpgno}x" ]]; then ${subcmd}; else echo "skipped"; fi"
    unset metadata

    IFS=' '
    eval $(printf "${cmd}")  # exec
}


function runDelete() {
    eval "declare -A metadata="${1#*=}
    local block_name=${2}

    local iqn_prefix="iqn.2016-12.org.gluster-block"
    local gbid=${metadata["GBID"]}

    local subcmd1="\$(targetcli /backstores/user:glfs ls | grep ' ${block_name} ' | grep '/${gbid} ' -c)"

    local subcmd2="targetcli /backstores/user:glfs delete ${block_name};
                   targetcli /iscsi delete ${iqn_prefix}:${gbid};
                   targetcli / saveconfig"


    cmd="if [[ ${subcmd1} -gt 0 ]]; then ${subcmd2}; else echo "skipped"; fi"
    unset metadata

    IFS=' '
    eval $(printf "${cmd}")  # exec
}


function parseCreateOutput() {
    eval "declare -A metadata="${1#*=}
    local block_name=${2}
    local new_node=${3}

    local output=()


    output="$(cat /tmp/gb_create)"
    if [[ "${output}" == "skipped" ]]; then
        printLog "WARNING: create: creating ${block_name} on ${new_node} skipped"
        return 1;
    fi

    output=("$(cat /tmp/gb_create | awk '/Created user-backed storage/{flag=1} /Created TPG 1/{flag=0} flag')")

    local i=1;
    while [ $i -le ${metadata["HA"]} ]; do
        output+=("$(cat /tmp/gb_create | awk "/Created TPG $i/{flag=1} /Created TPG $(expr $i + 1)/{flag=0} flag")")
        i=`expr $i + 1`
    done


    for index in ${!output[@]}; do
        if [[ ${index} -eq 0 ]]; then
            if [[ ${output["${index}"]} != *"Created user-backed storage object ${block_name} size ${metadata["SIZE"]}."* ]]; then
                printLog "ERROR: create: create user-backed storage object for ${block_name} with size ${metadata["SIZE"]} failed"
                return 1;
            fi

            if [[ ${output["${index}"]} != *"Parameter cmd_time_out is now '0'."* ]]; then
                printLog "ERROR: create: setting cmd_time_out=0 for ${block_name} failed"
                return 1;
            fi

            if [[ ${output["${index}"]} != *"Created target iqn.2016-12.org.gluster-block:${metadata["GBID"]}."* ]]; then
                printLog "ERROR: create: create target for ${block_name} failed"
                return 1;
            fi
        else
            if [[ ${output["${index}"]} != *"Created TPG ${index}."* ]]; then
                printLog "ERROR: create: create TPG ${index} failed for  ${block_name}"
                return 1;
            fi

            if [[ ${output["${index}"]} != *"Created LUN 0."* ]]; then
                printLog "ERROR: create: creation of LUN 0 for ${block_name} on tpg ${index} failed"
                return 1;
            fi

            if [[ ${output["${index}"]} != *"Using default IP port 3260"* ]]; then
                printLog "ERROR: create: using default portal failed for ${block_name} on tpg ${index}"
                return 1;
            fi

            if [[ ${output["${index}"]} != *"Created network portal"* ]]; then
                printLog "ERROR: create: creation of portal failed for ${block_name} on tpg ${index}"
                return 1;
            fi

            if [[ ${output["${index}"]} != *"Parameter generate_node_acls is now '1'."* ]]; then
                printLog "ERROR: create: setting generate_node_acls=1 failed for ${block_name} on tpg ${index}"
                return 1;
            fi

            if [[ ${output["${index}"]} != *"Parameter demo_mode_write_protect is now '0'."* ]]; then
                printLog "ERROR: create: setting demo_mode_write_protect=0 failed for ${block_name} on tpg ${index}"
                return 1;
            fi

            if [[ ${output["${index}"]} == *"${new_node}"* ]]; then
                if [[ ${output["${index}"]} != *"The TPGT has been enabled."* ]]; then
                    printLog "ERROR: create: enabling TPGT failed for ${block_name} on tpg ${index} & portal ${new_node}"
                    return 1;
                fi
            else
                if [[ ${output["${index}"]} != *"Parameter tpg_enabled_sendtargets is now '0'."* ]]; then
                    printLog "ERROR: create: setting tpg_enabled_sendtargets=0 failed for ${block_name} on tpg ${index}"
                    return 1;
                fi
            fi
        fi
    done

    unset metadata

    return 0;
}


function parseReplaceOutput() {
    local output=${1}
    local block_name=${2}
    local old_node=${3}
    local new_node=${4}


    if [[ "${output}" == "skipped" ]]; then
        printLog "WARNING: replace: replacing network portal ${old_node}:3260 for ${block_name} skipped"
        return 1;
    fi

    if [[ ${output} != *"Deleted network portal ${old_node}:3260"* ]]; then
        printLog "ERROR: replace: deleting network portal ${old_node}:3260 for ${block_name} failed"
        return -1;
    fi

    if [[ ${output} != *"Using default IP port 3260"* ]]; then
        printLog "ERROR: replace: using default portal for ${block_name} failed"
        return -1;
    fi

    if [[ ${output} != *"Created network portal ${new_node}:3260."* ]]; then
        printLog "ERROR: replace: creation of portal ${new_node} for ${block_name} failed"
        return -1;
    fi

    return 0;
}


function parseDeleteOutput() {
    local output=${1}
    local block_name=${2}
    local gbid=${3}

    local iqn_prefix="iqn.2016-12.org.gluster-block"


    if [[ "${output}" == "skipped" ]]; then
        printLog "WARNING: delete: deleting storage object ${block_name} skipped"
        return 1;
    fi

    if [[ ${output} != *"Deleted storage object ${block_name}."* ]]; then
        printLog "ERROR: delete: deleting storage object ${block_name} failed"
        return -1;
    fi

    if [[ ${output} != *"Deleted Target ${iqn_prefix}:${gbid}."* ]]; then
        printLog "ERROR: delete: deleting target ${iqn_prefix}:${gbid} of ${block_name} failed"
        return -1;
    fi

    return 0;
}


function readMetaInfo() {

    while read line; do
        METADATA+=(["$(echo ${line} | cut -d: -f1)"]=$(echo ${line} | cut -d' ' -f2 | tr -d " \n"))
    done < ${1}
}


function takeLockInit() {
    local fd=${1}


    # ignore signals till trap setting
    trap '' INT TERM QUIT

    flock -x ${fd}
    if [[ ${?} -ne 0 ]]; then
      printLog "failed to take lock"
      exit 1;
    fi
}


function releaseLockExit() {
    local fd=${1}


    flock -u ${fd}
    if [[ ${?} -ne 0 ]]; then
      printLog "failed to release lock"
      exit 1;
    fi

    # trap open for very short duration
    trap schedule_terminate INT TERM QUIT
    sleep 1;
}


function usage() {

cat <<USAGE
Usage:
  ./replace-node.sh \${MOUNTPOINT} \${OLDNODE} \${NEWNODE}
USAGE
}


# start main

if [[ ${#} -ne 3 ]]; then
    usage
    exit 1;
fi

MOUNTPOINT=${1}
OLDNODE=${2}
NEWNODE=${3}

DIR=${MOUNTPOINT}/block-meta/

if [[ ! -d "${DIR}" ]]; then
    echo "Fuse mount point, does not have directory '${DIR}'"
    usage
    exit 1;
fi

mkdir -p ${LOGDIR}

printLog "Starting replace node job, mount-point=${MOUNTPOINT} old-node=${OLDNODE} new-node=${NEWNODE}"

# Fetch an available fd
fd=$(getAvailableFd)
if [[ ${?} -ne 0 ]]; then
    printLog "no free fd's available on the node"
    exit 1;
fi
fdOpen ${fd} ${DIR}/meta.lock

SAFE_EXIT=0
for BLOCKNAME in $(ls ${DIR}); do
    declare -A METADATA

    # handle last signal (aka safe point)
    if [[ ${SAFE_EXIT} -eq 1 ]]; then
        echo "exiting in a safe point :)"
        printLog "exiting in a safe point..."
        exit 1;
    fi

    takeLockInit ${fd}

    if [[ ${BLOCKNAME} == "meta.lock" ]]; then
        continue;
    fi

    readMetaInfo "${DIR}/${BLOCKNAME}"
    HOSTS=( $(getValidHosts "$(declare -p METADATA)") )

    echo "blockname: ${BLOCKNAME}"

    skip=0;
    if [[ $(echo "${!METADATA[@]} " | grep -c " ${OLDNODE} ") -eq 0 ]]; then
        echo "${OLDNODE} not configured for this block."
        echo "-------------------------------------"
        printLog "INFO: ${BLOCKNAME} not configured on ${OLDNODE}, skipping replace..."
        skip=1;
    fi
    if [[ skip -eq 1 ]]; then
        # unset variables
        unset HOSTS
        unset METADATA

        releaseLockExit ${fd}
        continue;
    fi

    ssh root@${NEWNODE} "LOGFILE=${LOGDIR}/gluster-block-configshell.log; $(declare -f isStrPartOfArray isStatusValid getValidHosts runCreate); runCreate '$(declare -p METADATA)' ${BLOCKNAME} ${OLDNODE} ${NEWNODE}" > /tmp/gb_create

    parseCreateOutput "$(declare -p METADATA)" ${BLOCKNAME} ${NEWNODE}
    ret=$?
    echo "" > /tmp/gb_create  # clear
    if [[ ${ret} -eq 0 ]]; then
        echo "${OLDNODE}: CLEANUPSUCCESS" >> "${DIR}/${BLOCKNAME}"
        echo "${NEWNODE}: CONFIGSUCCESS" >> "${DIR}/${BLOCKNAME}"

        echo "create on '${NEWNODE}' success"
        printLog "INFO: create of ${BLOCKNAME} on ${NEWNODE} success"
    elif [[ ${ret} -eq 1 ]]; then
        echo "create on '${NEWNODE}' skipped"
        printLog "WARNING: create of ${BLOCKNAME} on ${NEWNODE} skipped"
    else
        echo "create on '${NEWNODE}' failed"
        printLog "ERROR: create of ${BLOCKNAME} on ${NEWNODE} failed"
        printLog "INFO: skipping replace and delete for ${BLOCKNAME}"

        echo "-------------------------------------"

        # unset variables
        unset HOSTS
        unset METADATA

        releaseLockExit ${fd}
        continue;
    fi

    for NODE in ${HOSTS[@]}; do
        if [[ "${NODE}" != "${OLDNODE}" && "${NODE}" != "${NEWNODE}" ]]; then
            REPLACEOUT=$(ssh root@${NODE} "$(declare -f runReplace); runReplace '$(declare -p METADATA)' ${OLDNODE} ${NEWNODE}")
            parseReplaceOutput "${REPLACEOUT}" ${BLOCKNAME} ${OLDNODE} ${NEWNODE}
            ret=$?
            if [[ ${ret} -eq 0 ]]; then
                echo "replace on '${NODE}' success"
                printLog "INFO: replace of ${BLOCKNAME} on ${NODE} success"
            elif [[ ${ret} -eq 1 ]]; then
                echo "replace on '${NODE}' skipped"
                printLog "WARNING: replace of ${BLOCKNAME} on ${NODE} skipped"
            else
                echo "replace on '${NODE}' failed"
                printLog "ERROR: replace of ${BLOCKNAME} on ${NODE} failed"
            fi
            unset REPLACEOUT
        fi
    done

    DELETEOUT=$(ssh root@${OLDNODE} "$(declare -f runDelete); runDelete '$(declare -p METADATA)' ${BLOCKNAME}")
    parseDeleteOutput "${DELETEOUT}" ${BLOCKNAME} ${METADATA["GBID"]}
    ret=$?
    if [[ ${ret} -eq 0 ]]; then
        echo "delete on '${OLDNODE}' success"
        printLog "INFO: delete of ${BLOCKNAME} on ${OLDNODE} success"
    elif [[ ${ret} -eq 1 ]]; then
        echo "delete on '${OLDNODE}' skipped"
        printLog "WARNING: delete of ${BLOCKNAME} on ${OLDNODE} skipped"
    else
        echo "delete on '${OLDNODE}' failed"
        printLog "ERROR: delete of ${BLOCKNAME} on ${OLDNODE} failed"
    fi
    unset DELETEOUT

    echo "-------------------------------------"

    # unset variables
    unset HOSTS
    unset METADATA

    releaseLockExit ${fd}
done

printLog "Finished replace node job, mount-point=${MOUNTPOINT} old-node=${OLDNODE} new-node=${NEWNODE}"

fdClose ${fd}
