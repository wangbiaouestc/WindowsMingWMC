
#include <pthread.h>
#include "h264.h"
#include "malloc.h"

/*
* Pthread version with affinity lock for ED and MBD threads. Deprecated
*/
int av_transcode_pthread_affinity(int ifile, int ofile, int frame_width, int frame_height, h264_options *opts) {
	H264Context *h;
	pthread_t read_thr, parsenal_thr, entropy_thr, mbdec_thr, write_thr;

	h = ff_h264_decode_init(ifile, ofile, frame_width, frame_height, opts);	
	timer_start = av_gettime();

	pthread_create(&read_thr, NULL, read_thread, h);
	pthread_create(&parsenal_thr, NULL, parsenal_thread, h);
	pthread_create(&entropy_thr, NULL, entropy_IPB_thread, h);
	pthread_create(&mbdec_thr, NULL, mbdec_thread, h);
	pthread_create(&write_thr, NULL, write_thread, h);


	pthread_join(read_thr, NULL);
	pthread_join(parsenal_thr, NULL);
	pthread_join(entropy_thr, NULL);
	pthread_join(mbdec_thr, NULL);
	pthread_join(write_thr, NULL);

	/* finished ! */
	ff_h264_decode_end(h);

	return 0;
}
