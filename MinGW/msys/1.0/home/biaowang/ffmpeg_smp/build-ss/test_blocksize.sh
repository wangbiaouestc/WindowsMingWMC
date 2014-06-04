#!/bin/bash

params=""
ofile=""
#default thread number
cores=1 
iter=3
blockstart=1

blocksize=("1 1" "2 2" "4 4" "6 6" "8 8" "10 10" "12 12" "16 16")

set -- `getopt "i:t:r:bo:" "$@"`
while [ ! -z "$1" ]
do
  case "$1" in
    -i) params="$params $1 $2"
	shift ;;
    -t) cores=$2
	shift ;;
    -r) iter=$2
	shift ;;
    -b) blockstart=0
	shift ;;
    -o) ofile=$2
	shift ;;
    *) 
      break ;;
  esac
  shift
done

if [ -z $ofile ] 
then
    echo "No file specified"
    exit
elif [ -f $ofile ] 
then
    rm $ofile
fi


for n in `seq 1 $cores`; do
    for bidx in `seq $blockstart 7`; do
        avgtime=0.0
        time=""
        for i in `seq 1 $iter`; do 
            curtime=$((NX_PES=$((n)) time -p ./ffmpeg $params -z ${blocksize[$bidx]} -e $n -n 10) 2>&1 | grep real | cut -d' ' -f 2)
            avgtime=`echo "scale=2; $avgtime+$curtime" | bc -l`
            time="$time $curtime"
        done
        avgtime=`echo "scale=2; $avgtime / $iter" | bc -l`
        echo "$n ${blocksize[$bidx]} $time $avgtime" >> $ofile
    done
done

