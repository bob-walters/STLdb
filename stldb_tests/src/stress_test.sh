#!/bin/bash

# $1 = test to run
# $2 = last run date/time
# $3 - seconds between 

export CMD=$1
export stop_date=$2
export ind_filename="/tmp/$$.ind"

export now=`date '+%Y-%m-%d:%H:%M:%S'`
while [[ $now < $stop_date ]]
do
    if [ -f $ind_filename ]
    then
        rm $ind_filename
    fi

    echo "Running: $CMD"
    $CMD $ind_filename &

    echo "Started child pid: $!"
    sleep $3

    if [ -f $ind_filename ]
    then
        echo "Process started as expected.  Killing it...."
        kill -9 $!
        wait $!
        echo "Process $! terminated with kill -9"
        rm $ind_filename
		sleep 3
    fi
    export now=`date '+%Y-%m-%d:%H:%M:%S'`
done
echo "End-time has been reached."

if [ -f $ind_filename ]
then
  rm $ind_filename
fi

