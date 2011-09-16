#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>
#include <math.h>

#include "libfuncs/log.h"

#include "config.h"

void * calibrate_sleep(void *_config) {
	struct timeval tv1, tv2;
	unsigned long diff = 0, loops = 0;
	CONFIG *conf = _config;

	if (!conf->quiet) {
		LOGf("\tCalibrating sleep timeout...\n");
		LOGf("\tRequest timeout   : %ld us\n", conf->output_tmout);
	}

	do {
		gettimeofday(&tv1, NULL);
		usleep(1);
		gettimeofday(&tv2, NULL);
		diff += timeval_diff_usec(&tv1, &tv2) - 1;
	} while (loops++ != 3000);

	conf->usleep_overhead = diff / loops;
	conf->output_tmout -= conf->usleep_overhead;

	if (!conf->quiet) {
		LOGf("\tusleep(1) overhead: %ld us\n", conf->usleep_overhead);
		LOGf("\tOutput pkt tmout  : %ld us\n", conf->output_tmout);
	}

	if (conf->output_tmout < 0) {
		LOGf("usleep overhead is to much!! Disabling output rate control.\n");
		conf->output_tmout = 0;
	}

	pthread_exit(0);
}
