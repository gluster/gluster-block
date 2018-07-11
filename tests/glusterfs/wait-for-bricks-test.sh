#!/bin/bash -x

#This test-script tests that wait-for-bricks.sh works as expected in different cases
exit_code=0
source_path="../../extras"

function test_success {
        if ! "$@" ;
        then
                exit_code=1
        fi
}

function test_failure {
        if "$@" ;
        then
                exit_code=1
        fi
}

#Should fail when glusterd is not running
test_failure $source_path/wait-for-bricks.sh 10

#Should succeed when gluster --mode=scriptd is running but no volumes exist
glusterd
test_success $source_path/wait-for-bricks.sh 10

#Set brick-multiplexing off, so that we can kill bricks.
gluster volume set all cluster.brick-multiplex off

#Block hosting volumes are identified with features.shard being present for now
#Should succeed when volumes that are not of interest are present
gluster --mode=script volume create vol localhost.localdomain:/brick1 force
test_success $source_path/wait-for-bricks.sh 10

gluster --mode=script volume start vol
test_success $source_path/wait-for-bricks.sh 10

gluster --mode=script volume create vol2 replica 3 localhost.localdomain:/brick{2..4} force
test_success $source_path/wait-for-bricks.sh 10

gluster --mode=script volume start vol2
test_success $source_path/wait-for-bricks.sh 10

#Checking for the case when only the volume with replicate is available
gluster --mode=script volume stop vol
test_success $source_path/wait-for-bricks.sh 10
gluster --mode=script volume start vol

#Checking for the case where bhv is present but stopped
gluster --mode=script volume create bhv1 replica 3 localhost.localdomain:/brick{5..7} force
gluster --mode=script volume set bhv1 features.shard on
test_success $source_path/wait-for-bricks.sh 10

#Start bhv1 so that wait-for-bricks considers this volume
gluster --mode=script volume start bhv1
test_success $source_path/wait-for-bricks.sh 10

#When a brick in bhv1 is down, script should fail
kill -9 "$(gluster --mode=script --xml volume status bhv1 | grep -oPm1 "(?<=<pid>)[^<]+")"
test_failure $source_path/wait-for-bricks.sh 10

gluster --mode=script volume start bhv1 force
#Kill a brick from non block hosting volume and the script should succeed
kill -9 "$(gluster --mode=script --xml volume status vol2 | grep -oPm1 "(?<=<pid>)[^<]+")"
test_success $source_path/wait-for-bricks.sh 10

#Create one more block hosting volume to test that brick down in any one bhv leads to failure
gluster --mode=script volume create bhv2 replica 3 localhost.localdomain:/brick{8..10} force
gluster --mode=script volume set bhv2 features.shard on
test_success $source_path/wait-for-bricks.sh 10

gluster --mode=script volume start bhv2
test_success $source_path/wait-for-bricks.sh 10

kill -9 "$(gluster --mode=script --xml volume status bhv2 | grep -oPm1 "(?<=<pid>)[^<]+")"
test_failure $source_path/wait-for-bricks.sh 10

#Brick in each Block hosting volume is down
kill -9 "$(gluster --mode=script --xml volume status bhv1 | grep -oPm1 "(?<=<pid>)[^<]+")"
test_failure $source_path/wait-for-bricks.sh 10

#1 Brick is down in 1 Block hosting volume and on the other all bricks are up
gluster --mode=script v start bhv2 force
test_failure $source_path/wait-for-bricks.sh 10

gluster --mode=script volume start bhv1 force
test_success $source_path/wait-for-bricks.sh 10

gluster --mode=script v stop bhv1
gluster --mode=script v stop bhv2
gluster --mode=script v stop vol
gluster --mode=script v stop vol2
gluster --mode=script v delete bhv1
gluster --mode=script v delete bhv2
gluster --mode=script v delete vol
gluster --mode=script v delete vol2
killall -9 glusterd
exit $exit_code
