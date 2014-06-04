#!/bin/bash

numseq=4
sequences=("576p25_blue_sky.h264"
            "576p25_pedestrian.h264"
            "720p25_rush_hour.h264"
            "1088p25_riverbed_p.h264"
            "pedestrian_area.h264"
            "ducks_take_off_2160p.h264"
            "park_joy_2160p.h264"
            "big_buck_bunny_720p24.h264"
            "big_buck_bunny_1080p24.h264"
)

seqdir=
params=
outfile=
perf=
set -- `getopt "ade:i:n:o:pst:wz:" "$@"`
while [ ! -z "$1" ]
do
  case "$1" in
    -a) params="$params $1"
	;;
    -d) params="$params $1"
	;;
    -e) params="$params $1 $2"
	shift
	;;
    -i) seqdir=$2
    shift
    ;;
	-n) numseq=$(($2-1))
	shift
	;;
    -o) outfile=$2
        params="$params $1 $2"
	shift
	;;
	-p) perf="perf stat "
	;;
    -s) params="$params $1"
	;;
    -t) params="$params $1 $2"
	shift
	;;
    -w) params="$params --static-3d"
	;;
    -z) params="$params $1 $2 $3"
	shift 
	shift
	;;
     *) break;;
  esac
  shift
done

rm md5.txt
echo $seqdir
echo $params
echo $outfile
for i in `seq 0 $numseq`;
do
    echo ${sequences[$i]}
	ionice -c3 $perf time ./h264dec -i $seqdir/${sequences[$i]} $params
	if test "$outfile" != "" ;then 
		md5sum $outfile >> md5.txt
	fi
	echo ""
done

cat md5.txt

