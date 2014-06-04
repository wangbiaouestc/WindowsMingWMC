#! /bin/bash

workers=(1 4 8 12 16 20 24 28 32)
cpus=(0 3 7 15 15 23 23 31 31)
nodes=(0 0 0 1 1 2 2 3 3)

confs=( "1 1 1" "1 2 2" "2 3 4" "2 4 5" "3 5 8" "3 6 10" "4 7 12" "4 8 15" "5 8 17"		#small
		"1 1 1" "1 2 2" "2 3 4" "2 4 5" "3 5 7" "3 6 9" "4 7 12" "4 8 13" "5 10 15")    #large



#confsmall=("1 1 1" "1 2 2" "2 3 4" "2 4 5" "3 5 8" "3 6 10" "4 7 12" "4 8 15" "5 8 17")
# "7 10 21" "8 12 25" "10 15 29" "11 17 32")
#conflarge=("1 1 1" "1 2 2" "2 3 4" "2 4 5" "3 5 7" "3 6 9" "4 7 12" "4 8 13" "5 10 15")
#"5 12 21" "6 15 25" "7 17 30" "8 19 36")


configs=9

average_ompss_2d=0
average_ompss_3d=0
average_pthread=0
average_serial=0

iterations_low=4
iterations_high=8

nframes=10000  # max frames limit for debug purpose
inputs=("14" "10")
inputs_vebose=("Big Bug Bunny 1920x1080 10000 frames" "Park Joy 3840x2160 2500 frames")
osargs=("-z 8 8" "-z 12 12 --static-3d")

time_stamp=`date +%Y.%m.%d_%H.%M.%S`
outputdir="/home/stefan.hauser/ffmpeg_smp/ppopp_results/rx600s5-1t/$time_stamp"
ompss_2d="$outputdir/ompss_2d.txt"
ompss_3d="$outputdir/ompss_3d.txt"
pthread="$outputdir/pthread.txt"
serial="$outputdir/serial.txt"

#executes the experiments for a single conf $1=confnum $2 iterations $3 input_idx
function execute_single_conf {
	conf=$1
	iter=$2
	iidx=$3

	average_ompss_2d=0
	average_ompss_3d=0
	average_pthread=0

	echo "Workers: " ${workers[$conf]} | tee -a $ompss_2d $ompss_3d $pthread $serial

	cd build-ss
	for ((i=1;i<=$iter;i+=1)); do
	    # OMPSS
	    #export CSS_NUM_CPUS=$worker
	    NX_PES=${workers[$conf]} numactl --interleave=0-${nodes[$conf]} time -p ./ffmpeg -i ${inputs[$iidx]} -n $nframes -e $((${workers[$conf]}+1)) ${osargs[0]} 2> output
		runtime=$(cat output | grep real | sed s/^.*l.//g)
	    average_ompss_2d=$(echo "$average_ompss_2d + $runtime"|bc)
	    echo -n $runtime " " >> $ompss_2d
	done

	for ((i=1;i<=$iter;i+=1)); do
		NX_PES=${workers[$conf]} numactl --interleave=0-${nodes[$conf]} time -p ./ffmpeg -i ${inputs[$iidx]} -n $nframes -e $((${workers[$conf]}+1)) ${osargs[1]} 2> output
		runtime=$(cat output | grep real | sed s/^.*l.//g)
	    average_ompss_3d=$(echo "$average_ompss_3d + $runtime"|bc)
	    echo -n $runtime " " >> $ompss_3d
	done
	cd ..

	cd build
	for ((i=1;i<=$iter;i+=1)); do
		# Pthreads
	    numactl --physcpubind=0-$((${cpus[$conf]})) time -p ./ffmpeg -i ${inputs[$iidx]} -n $nframes -t ${confs[$(($conf + $iidx * $configs))]} 2> output
		runtime=$(cat output | grep real | sed s/^.*l.//g)
	    average_pthread=$(echo "$average_pthread + $runtime"|bc)
	    echo -n $runtime " " >> $pthread
	done
	cd ..

	echo "" | tee -a $pthread $ompss_2d $ompss_3d
	average_ompss_2d=$(echo "scale=5;$average_ompss_2d/$iter"|bc)
	average_ompss_3d=$(echo "scale=5;$average_ompss_3d/$iter"|bc)
	average_pthread=$(echo "scale=5;$average_pthread/$iter"|bc)

	echo "time: " $average_ompss_2d >> $ompss_2d
	echo "time: " $average_ompss_3d >> $ompss_3d
	echo "time: " $average_pthread >> $pthread
	echo "time: " $average_serial >> $serial
}


mkdir $outputdir

echo "Processing inputs ..."

echo "h264dec Benchmark" | tee $ompss_2d $ompss_3d $pthread $serial

for n in 0 1; do
	echo "Input: ${inputs_vebose[$n]}" | tee -a $ompss_2d $ompss_3d $pthread $serial
	echo "" | tee -a $ompss_2d $ompss_3d $pthread $serial

	# Serial
	cd build
	numactl --physcpubind=0 time -p ./ffmpeg -i ${inputs[$n]} -n $nframes -s 2> output
	runtime=$(cat output | grep real | sed s/^.*l.//g)
	average_serial=$runtime
	cd ..

	execute_single_conf 0 1 $n

	#Parallel
	for ((confidx=1;confidx<=4;confidx+=1)); do
		execute_single_conf $confidx $iterations_low $n		
	done

	for ((confidx=5;confidx<=$(($configs-1));confidx+=1)); do
		execute_single_conf $confidx $iterations_high $n		
	done

	echo "-------------------" | tee -a $ompss_2d $ompss_3d $pthread $serial
done

echo "FINISHED"

rm build/output build-ss/output

