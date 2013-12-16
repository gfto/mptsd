/*
 * mptsd main
 * Copyright (C) 2010-2011 Unix Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include "libfuncs/libfuncs.h"

#include "libtsfuncs/tsfuncs.h"

#include "iniparser.h"
#include "data.h"
#include "config.h"
#include "network.h"
#include "input.h"
#include "output.h"
#include "web_server.h"
#include "udp_server.h"

#define PROGRAM_NAME "ux-mptsd"

const char *program_id = PROGRAM_NAME " " GIT_VER " build " BUILD_ID;

char *server_sig = PROGRAM_NAME;
char *server_ver = GIT_VER;
char *copyright  = "Copyright (C) 2010-2011 Unix Solutions Ltd.";

CONFIG *config;
int keep_going = 1;
int rcvsig = 0;

void spawn_input_threads(CONFIG *conf) {
	LNODE *lc, *lctmp;
	LNODE *lr, *lrtmp;
	int spawned = 0;
	list_for_each(conf->channels, lc, lctmp) {
		CHANNEL *c = lc->data;
		int restreamer_active = 0;
		list_lock(conf->inputs);
		list_for_each(conf->inputs, lr, lrtmp) {
			INPUT *r = lr->data;
			if (xstrcmp(r->name, c->name)==0) {
				restreamer_active = 1;
				break;
			}
		}
		list_unlock(conf->inputs);
		if (!restreamer_active) {
			INPUT *nr = input_new(c->name, c);
			if (nr) {
				list_add(conf->inputs, nr);
				LOGf("SPAWN : %s thread.\n", c->name);
				if (pthread_create(&nr->thread, NULL, &input_stream, nr) == 0) {
					spawned++;
					pthread_detach(nr->thread);
				} else {
					LOGf("ERROR: Can't create proxy for %s\n", c->name);
				}
			} else {
				LOGf("ERROR: Error creating proxy for %s\n", c->name);
			}
		}
	}
	LOGf("INPUT : %d thread%s spawned.\n", spawned, spawned > 1 ? "s" : "");
}

void spawn_output_threads(CONFIG *conf) {
	if (pthread_create(&conf->output->psi_thread, NULL, &output_handle_psi, conf) == 0) {
		pthread_detach(conf->output->psi_thread);
	} else {
		LOGf("ERROR: Can't spawn PSI output thread: %s\n", strerror(errno));
		exit(1);
	}

	if (pthread_create(&conf->output->mix_thread, NULL, &output_handle_mix, conf) == 0) {
		pthread_detach(conf->output->mix_thread);
	} else {
		LOGf("ERROR: Can't spawn MIX output thread: %s\n", strerror(errno));
		exit(1);
	}

	if (pthread_create(&conf->output->write_thread, NULL, &output_handle_write, conf) == 0) {
		pthread_detach(conf->output->write_thread);
	} else {
		LOGf("ERROR: Can't spawn WRITE output thread: %s\n", strerror(errno));
		exit(1);
	}
}

void kill_threads(CONFIG *conf) {
	int loops = 0;
	conf->output->dienow = 1;
	while (conf->inputs->items || conf->output->dienow < 4) {
		usleep(50000);
		if (loops++ > 60) // 3 seconds
			exit(0);
	}
}

/*
void do_reconnect(CONFIG *conf) {
	LNODE *l, *tmp;
	list_lock(conf->inputs);
	list_for_each(conf->inputs, l, tmp) {
		INPUT *r = l->data;
		r->reconnect = 1;
	}
	list_unlock(conf->inputs);
}

void do_reconf(CONFIG *conf) {
//	load_channels_config();
	spawn_input_threads(conf);
}
*/

void signal_quit(int sig) {
	rcvsig = sig;
	keep_going = 0;
}

void init_signals() {
	signal(SIGCHLD, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

//	signal(SIGHUP , do_reconf);
//	signal(SIGUSR1, do_reconnect);

	signal(SIGINT , signal_quit);
	signal(SIGTERM, signal_quit);
}

int main(int argc, char **argv) {
	set_http_response_server_ident(server_sig, server_ver);
	ts_set_log_func(LOG);

	config = config_alloc();
	config_load(config, argc, argv);

	output_psi_init(config, config->output);

	daemonize(config->pidfile);
	web_server_start(config);
	udp_server_start(config);
	log_init(config->logident, config->syslog_active, config->pidfile == NULL, config->loghost, config->logport);
	init_signals(config);

	LOGf("INIT  : %s %s (%s)\n" , server_sig, server_ver, config->ident);

	connect_output(config->output);
	spawn_input_threads(config);
	spawn_output_threads(config);

	do { usleep(50000); } while(keep_going);

	kill_threads(config);
	web_server_stop(config);
	udp_server_stop(config);

	LOGf("SHUTDOWN: Signal %d | %s %s (%s)\n", rcvsig, server_sig, server_ver, config->ident);
	config_free(&config);

	log_close();

	exit(0);
}
