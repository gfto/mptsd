/*
 * mptsd configuration
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <regex.h>
#include <math.h>
#include <netdb.h> // for uint32_t

#include "libfuncs/log.h"
#include "libfuncs/list.h"
#include "libfuncs/server.h"

#include "libtsfuncs/tsfuncs.h"

#include "data.h"
#include "sleep.h"
#include "config.h"
#include "inidict.h"
#include "iniparser.h"

extern char *server_sig;
extern char *server_ver;
extern char *copyright;

int is_valid_url(char *url) {
	regex_t re;
	regmatch_t res[5];
	int ret;
	regcomp(&re, "^([a-z]+)://([^:/?]+):?([0-9]*)/?(.*)", REG_EXTENDED);
	ret = regexec(&re,url,5,res,0);
	regfree(&re);
	return ret == 0;
}

CONFIG *config_alloc() {
	CONFIG *conf = calloc(1, sizeof(CONFIG));
	conf->inputs = list_new("input");
	conf->nit    = list_new("nit");
	conf->output = output_new();
	return conf;
}

void config_free(CONFIG **pconf) {
	CONFIG *conf = *pconf;
	if (conf) {
		conf->output->dienow = 1;

		list_free(&conf->nit, NULL, (void (*)(void **))nit_free);
		list_free(&conf->inputs, NULL, (void (*)(void **))input_free);
		list_free(&conf->channels, NULL, (void (*)(void **))channel_free);
		output_free(&conf->output);

		FREE(conf->ident);
		FREE(conf->pidfile);
		FREE(conf->server_addr);
		FREE(conf->loghost);
		FREE(conf->logident);
		FREE(conf->global_conf);
		FREE(conf->channels_conf);
		FREE(conf->nit_conf);
		FREE(conf->epg_conf);
		FREE(conf->network_name);
		FREE(conf->provider_name);
		FREE(conf->output_filename);
		FREE(*pconf);
	}
}


int config_load_channels(CONFIG *conf) {
	int num_channels = 0, i, j;

	LOGf("CONFIG: Loading channels config %s\n", conf->channels_conf);
	dictionary *ini = iniparser_load(conf->channels_conf);
	if (!ini) {
		LOGf("CONFIG: Error loading channels config (%s)\n", conf->channels_conf);
		return 0;
	}

	//iniparser_dump(ini, stdout);
	LIST *channels = list_new("channels");

	// Parse channels file
	conf->provider_name = ini_get_string_copy(ini, NULL, "Global:provider_name");
	conf->transport_stream_id = ini_get_int(ini, 0, "Global:transport_stream_id");
	for (i=1;i<32;i++) {
		CHANNEL *channel = NULL;
		int service_id = ini_get_int(ini, 0, "Channel%d:service_id", i);
		if (!service_id)
			continue;

		int is_radio = ini_get_bool(ini, 0, "Channel%d:radio", i);

		char *id = ini_get_string(ini, NULL, "Channel%d:id", i);
		if (!id) {
			LOGf("CONFIG: Channel%d have no defined id\n", i);
			continue;
		}

		char *name = ini_get_string(ini, NULL, "Channel%d:name", i);
		if (!name) {
			LOGf("CONFIG: Channel%d have no defined name\n", i);
			continue;
		}

		int eit_mode = ini_get_int(ini, 0, "Channel%d:eit_mode", i);

		for (j=1;j<8;j++) {
			char *source = ini_get_string(ini, NULL, "Channel%d:source%d", i, j);
			if (j == 1 && !source) {
				source = ini_get_string(ini, NULL, "Channel%d:source", i);
			}
			if (source) {
				if (!is_valid_url(source)) {
					LOGf("CONFIG: Invalid url: %s\n", source);
					continue;
				}
				// Init channel
				if (channel == NULL) {
					channel = channel_new(service_id, is_radio, id, name, eit_mode, source, i);
				} else {
					chansrc_add(channel, source);
				}
			}
		}

		char *worktime = ini_get_string(ini, NULL, "Channel%d:worktime", i);
		int sh=0,sm=0,eh=0,em=0;
		if (worktime && sscanf(worktime, "%02d:%02d-%02d:%02d", &sh, &sm, &eh, &em) == 4) {
			channel->worktime_start = sh * 3600 + sm * 60;
			channel->worktime_end   = eh * 3600 + em * 60;
		}

		// Channel is initialized, add it to channels list
		if (channel) {
			num_channels++;
			list_add(channels, channel);
		}
	}
//	iniparser_dump(ini, stderr);
	iniparser_freedict(&ini);

	// Check for channel changes
	struct timeval tv;
	gettimeofday(&tv, NULL);
	unsigned int randstate = tv.tv_usec;
	int cookie = rand_r(&randstate);

	/* Save current channels config */
	LIST *old_channels = conf->channels;

	/* Switch new channels config */
	conf->channels = channels;

	/* Rewrite restreamer channels */
	LNODE *lc, *lr, *lctmp, *lrtmp;
	CHANNEL *chan;
	list_lock(conf->inputs);	// Unlocked after second list_for_each(restreamer)
	list_lock(conf->channels);
	list_for_each(conf->channels, lc, lctmp) {
		chan = lc->data;
		list_for_each(conf->inputs, lr, lrtmp) {
			if (strcmp(chan->name, ((INPUT *)lr->data)->name)==0) {
				INPUT *restr = lr->data;
				/* Mark the restreamer as valid */
				restr->cookie = cookie;
				/* Check if current source exists in new channel configuration */
				int src_found = -1;
				char *old_source = restr->channel->source;
				for (i=0; i<chan->num_src; i++) {
					if (strcmp(old_source, chan->sources[i]) == 0) {
						src_found = i;
					}
				}
				if (src_found > -1) {
					/* New configuration contains existing source, just update the reference */
					chansrc_set(chan, src_found);
					restr->channel = chan;
				} else {
					/* New configuration *DO NOT* contain existing source. Force reconnect */
					LOGf("INPUT: Source changed | Channel: %s srv_fd: %d Old:%s New:%s\n", chan->name, restr->sock, restr->channel->source, chan->source);
					/* The order is important! */
					chansrc_set(chan, chan->num_src-1); /* Set source to last one. On reconnect next source will be used. */
					restr->channel = chan;
					restr->reconnect = 1;
				}
				break;
			}
		}
	}
	list_unlock(conf->channels);

	/* Kill restreamers that serve channels that no longer exist */
	list_for_each(conf->inputs, lr, lrtmp) {
		INPUT *r = lr->data;
		/* This restreamer should no longer serve clients */
		if (r->cookie != cookie) {
			proxy_log(r, "Remove");
			/* Replace channel reference with real object and instruct free_restreamer to free it */
			r->channel = channel_new(r->channel->service_id, r->channel->radio, r->channel->id, r->channel->name, r->channel->eit_mode, r->channel->source, r->channel->index);
			r->freechannel = 1;
			r->dienow = 1;
		}
	}
	list_unlock(conf->inputs);

	/* Free old_channels */
	list_free(&old_channels, NULL, (void (*)(void **))channel_free);

	LOGf("CONFIG: %d channels loaded\n", num_channels);
	return num_channels;
}

int config_load_global(CONFIG *conf) {
	LOGf("CONFIG: Loading global config %s\n", conf->global_conf);
	dictionary *ini = iniparser_load(conf->global_conf);
	if (!ini) {
		LOGf("CONFIG: Error loading global config (%s)\n", conf->global_conf);
		return 0;
	}
	conf->network_id		= ini_get_int(ini, 0,    "Global:network_id");
	conf->timeouts.pat		= ini_get_int(ini, 100,  "Timeouts:pat");
	conf->timeouts.pmt		= ini_get_int(ini, 200,  "Timeouts:pmt");
	conf->timeouts.sdt		= ini_get_int(ini, 500,  "Timeouts:sdt");
	conf->timeouts.nit		= ini_get_int(ini, 2000, "Timeouts:nit");
	conf->timeouts.eit		= ini_get_int(ini, 1000, "Timeouts:eit");
	conf->timeouts.tdt		= ini_get_int(ini, 7500, "Timeouts:tdt");
	conf->timeouts.tot		= ini_get_int(ini, 1500, "Timeouts:tot");
	conf->timeouts.stats	= ini_get_int(ini, 0,    "Timeouts:stats");
	//iniparser_dump(ini, stderr);
	iniparser_freedict(&ini);
	return 1;
}

int config_load_nit(CONFIG *conf) {
	LOGf("CONFIG: Loading nit config %s\n", conf->nit_conf);
	dictionary *ini = iniparser_load(conf->nit_conf);
	if (!ini) {
		LOGf("CONFIG: Error loading nit config (%s)\n", conf->nit_conf);
		return 0;
	}
	conf->network_name = ini_get_string_copy(ini, NULL, "Global:network_name");
	int i;
	for (i=1;i<32;i++) {
		uint16_t ts_id = ini_get_int   (ini, 0, "Transponder%d:transport_stream_id", i);
		char *freq     = ini_get_string(ini, NULL, "Transponder%d:frequency", i);
		char *mod      = ini_get_string(ini, NULL, "Transponder%d:modulation", i);
		char *sr       = ini_get_string(ini, NULL, "Transponder%d:symbol_rate", i);
		if (ts_id && freq && mod && sr) {
			if (strlen(freq) == 9 && strlen(sr) == 8) {
				list_add(conf->nit, nit_new(ts_id, freq, mod, sr));
			}
		}
	}
	//iniparser_dump(ini, stderr);
	iniparser_freedict(&ini);
	return 1;
}

static void config_reset_channels_epg(CONFIG *conf) {
	LNODE *lc, *lctmp;
	list_lock(conf->channels);
	list_for_each(conf->channels, lc, lctmp) {
		CHANNEL *c = lc->data;
		channel_free_epg(c);
	}
	list_unlock(conf->channels);
	conf->epg_conf_mtime = 0;
}

static EPG_ENTRY *config_load_epg_entry(dictionary *ini, char *entry, char *channel) {
	EPG_ENTRY *e = NULL;
	time_t start = ini_get_int(ini, 0, "%s-%s:start", channel, entry);
	int duration = ini_get_int(ini, 0, "%s-%s:duration", channel, entry);
	char *event  = ini_get_string(ini, NULL, "%s-%s:event", channel, entry);
	char *sdesc  = ini_get_string(ini, NULL, "%s-%s:sdescr", channel, entry);
	char *ldesc  = ini_get_string(ini, NULL, "%s-%s:descr", channel, entry);
	char *enc    = ini_get_string(ini, NULL, "%s-%s:encoding", channel, entry);
	if (start && duration && event) {
		e = epg_new(start, duration, enc, event, sdesc, ldesc);
	}
	return e;
}

static void config_channel_init_epg(CONFIG *conf, CHANNEL *c, EPG_ENTRY *now, EPG_ENTRY *next) {
	int updated = 0;

	if (epg_changed(now, c->epg_now)) {
		if(!conf->quiet)
			LOGf("EPG  : %s | Now data changed\n", c->id);
		updated++;
	}

	if (epg_changed(next, c->epg_next)) {
		if(!conf->quiet)
			LOGf("EPG  : %s | Next data changed\n", c->id);
		updated++;
	}

	if (!updated)
		return;

	// LOGf("EPG  : %s | Version %d\n", c->epg_version);
	c->epg_version++;

	struct ts_eit *eit_now = ts_eit_alloc_init_pf(c->service_id, conf->transport_stream_id, conf->network_id, 0, 1);
	eit_now->section_header->version_number = c->epg_version;
	if (now) {
		ts_eit_add_short_event_descriptor(eit_now, now->event_id, 1, now->start, now->duration, now->event, now->short_desc);
		ts_eit_add_extended_event_descriptor(eit_now, now->event_id, 1, now->start, now->duration, now->long_desc);
	}
	ts_eit_regenerate_packets(eit_now);

	struct ts_eit *eit_next = ts_eit_alloc_init_pf(c->service_id, conf->transport_stream_id, conf->network_id, 1, 1);
	eit_next->section_header->version_number = c->epg_version;
	if (next) {
		ts_eit_add_short_event_descriptor(eit_next, next->event_id, 1, next->start, next->duration, next->event, next->short_desc);
		ts_eit_add_extended_event_descriptor(eit_next, next->event_id, 1, next->start, next->duration, next->long_desc);
	}
	ts_eit_regenerate_packets(eit_next);

	channel_free_epg(c);

	c->epg_now  = now;
	c->epg_next = next;

	if (now || next) {
		c->eit_now  = eit_now;
		c->eit_next = eit_next;
		//LOGf(" ***** NOW ******\n");
		//ts_eit_dump(eit_now);
		//LOGf(" ***** NEXT *****\n");
		//ts_eit_dump(eit_next);
	} else {
		ts_eit_free(&eit_now);
		ts_eit_free(&eit_next);
	}
}


int config_load_epg(CONFIG *conf) {
	struct stat st;
	if (stat(conf->epg_conf, &st) == 0) {
		if (st.st_mtime > conf->epg_conf_mtime) {
			// Set config last change time
			conf->epg_conf_mtime = st.st_mtime;
		} else {
			// LOGf("CONFIG: EPG config not changed since last check.\n");
			return 0; // The config has not changed!
		}
	} else {
		// Config file not found!
		LOGf("CONFIG: EPG config file is not found (%s)\n", conf->epg_conf);
		config_reset_channels_epg(conf);
		return 0;
	}

	// LOGf("CONFIG: Loading EPG config %s\n", conf->epg_conf);
	dictionary *ini = iniparser_load(conf->epg_conf);
	if (!ini) {
		LOGf("CONFIG: Error parsing EPG config (%s)\n", conf->epg_conf);
		config_reset_channels_epg(conf);
		return 0;
	}

	LNODE *lc, *lctmp;
	list_lock(conf->channels);
	list_for_each(conf->channels, lc, lctmp) {
		CHANNEL *c = lc->data;
		EPG_ENTRY *enow  = config_load_epg_entry(ini, "now", c->id);
		EPG_ENTRY *enext = config_load_epg_entry(ini, "next", c->id);
		config_channel_init_epg(conf, c, enow, enext);
	}
	list_unlock(conf->channels);
	//iniparser_dump(ini, stderr);
	iniparser_freedict(&ini);

	return 1;
}

extern char *program_id;

static void show_usage(void) {
	printf("%s\n", program_id);
	puts(copyright);
	puts("");
	puts("Identification:");
	puts("\t-i ident\tServer ident.               (default: ux/mptsd)");
	puts("");
	puts("Server settings:");
	puts("\t-b addr\t\tLocal IP address to bind.   (default: 0.0.0.0)");
	puts("\t-p port\t\tPort to listen.             (default: 0)");
	puts("\t-N disable network");
	puts("\t-d pidfile\tDaemonize with pidfile");
	puts("\t-l host\t\tSyslog host                 (default: disabled)");
	puts("\t-L port\t\tSyslog port                 (default: 514)");
	puts("");
	puts("Configuration files:");
	puts("\t-g file\t\tGlobal configuration file   (default: mptsd.conf)");
	puts("\t-c file\t\tChannels configuration file (default: mptsd_channels.conf)");
	puts("\t-e file\t\tEPG configuration file      (default: mptsd_epg.conf)");
	puts("\t-n file\t\tNIT configuration file      (default: mptsd_nit.conf)");
	puts("");
	puts("Output settings:");
	puts("\t-O ip\t\tOutput udp address");
	puts("\t-P ip\t\tOutput udp port             (default: 5000)");
	puts("\t-o ip\t\tOutput interface address    (default: 0.0.0.0)");
	puts("\t-t ttl\t\tSet multicast ttl           (default: 1)");
	puts("\t-s SSRC\t\tEnables RTP                 (default: disabled)");
	puts("");
	puts("\t-B Mbps\t\tOutput bitrate in Mbps      (default: 38.00)");
	puts("\t-m mode\t\tPCR mode (modes list bellow)");
	puts("\t\t\t- Mode 0: do not touch PCRs (default)");
	puts("\t\t\t- Mode 1: move PCRs to their calculated place");
	puts("\t\t\t- Mode 2: rewrite PCRs using output bitrate");
	puts("\t\t\t- Mode 3: move PCRs and rewrite them");
	puts("");
	puts("Other settings:");
	puts("\t-q\t\tQuiet");
	puts("\t-D\t\tDebug");
	puts("");
	puts("\t-W\t\tWrite output file           (recommended to use with -N)");
	puts("\t-f\t\tThe output filename         (default: mptsd-output.ts) (use - for stdout)");
	puts("\t-E\t\tWrite input file");
	puts("");
}

void config_load(CONFIG *conf, int argc, char **argv) {
	int j, ttl;

	conf->multicast_ttl = 1;
	conf->output->out_port = 5000;
	conf->output->rtp_sequence_number = 1;
	conf->output->rtp_ssrc = 0;
	conf->output_bitrate = 38;
	conf->logport = 514;
	conf->server_port = 0;
	conf->server_socket = -1;
	conf->write_output_network = 1;

	while ((j = getopt(argc, argv, "i:b:p:g:c:n:e:d:t:o:O:P:l:L:B:m:s:f:qDHhEWN")) != -1) {
		switch (j) {
			case 'i':
				conf->ident = strdup(optarg);
				conf->logident = strdup(optarg);
				char *c = conf->logident;
				while (*c) {
					if (*c=='/')
						*c='-';
					c++;
				}
				break;
			case 'b':
				conf->server_addr = strdup(optarg);
				break;
			case 'p':
				conf->server_port = atoi(optarg);
				break;
			case 'd':
				conf->pidfile = strdup(optarg);
				break;
			case 'g':
				conf->global_conf = strdup(optarg);
				break;
			case 'c':
				conf->channels_conf = strdup(optarg);
				break;
			case 'n':
				conf->nit_conf = strdup(optarg);
				break;
			case 'e':
				conf->epg_conf = strdup(optarg);
				break;
			case 'o':
				if (inet_aton(optarg, &conf->output_intf) == 0) {
					fprintf(stderr, "Invalid interface address: %s\n", optarg);
					exit(1);
				}
				break;
			case 'O':
				if (inet_aton(optarg, &(conf->output->out_host)) == 0) {
					fprintf(stderr, "Invalid output address: %s\n", optarg);
					exit(1);
				}
				break;
			case 'P':
				conf->output->out_port = atoi(optarg);
				if (!conf->output->out_port || conf->output->out_port < 1024) {
					fprintf(stderr, "Invalid output port: %s\n", optarg);
					exit(1);
				}
				break;
			case 's':
				conf->output->rtp_ssrc = strtoul(optarg, NULL, 10);
				break;
			case 'm':
				conf->pcr_mode = atoi(optarg);
				if (conf->pcr_mode < 0 || conf->pcr_mode > 4)
					conf->pcr_mode = 0;
				break;
			case 't':
				ttl = atoi(optarg);
				conf->multicast_ttl = (ttl && ttl < 127) ? ttl : 1;
				break;
			case 'l':
				conf->loghost = strdup(optarg);
				conf->syslog_active = 1;
				break;
			case 'L':
				conf->logport = atoi(optarg);
				break;
			case 'B':
				conf->output_bitrate = atof(optarg);
				if (conf->output_bitrate < 2 || conf->output_bitrate > 75) {
					fprintf(stderr, "Invalid output bitrate: %.2f (valid 2-75)\n", conf->output_bitrate);
					exit(1);
				}
				break;
			case 'N':
				conf->write_output_network = 0;
				break;
			case 'W':
				conf->write_output_file = 1;
				break;
			case 'E':
				conf->write_input_file = 1;
				break;
			case 'f':
				conf->output_filename = strdup(optarg);
				break;
			case 'D':
				conf->debug = 1;
				break;
			case 'q':
				conf->quiet = 1;
				break;
			case 'H':
			case 'h':
				show_usage();
				exit(0);
				break;
		}
	}
	if (conf->write_output_network && !conf->output->out_host.s_addr) {
		fprintf(stderr, "ERROR: Output address is not set (use -O x.x.x.x)\n\n");
		show_usage();
		goto ERR;
	}
	// Set defaults
	if (!conf->ident) {
		conf->ident = strdup("ux/mptsd");
		conf->logident = strdup("ux-mptsd");
	}
	if (!conf->global_conf) {
		conf->global_conf = strdup("mptsd.conf");
	}
	if (!conf->channels_conf) {
		conf->channels_conf = strdup("mptsd_channels.conf");
	}
	if (!conf->nit_conf) {
		conf->nit_conf = strdup("mptsd_nit.conf");
	}
	if (!conf->epg_conf) {
		conf->epg_conf = strdup("mptsd_epg.conf");
	}
	if (!conf->output_filename) {
		conf->output_filename = strdup("mptsd-output.ts");
	}

	// Align bitrate to 1 packet (1316 bytes)
	conf->output_bitrate        *= 1000000; // In bytes
	conf->output_packets_per_sec = ceil(conf->output_bitrate / (1316 * 8));
	conf->output_bitrate         = conf->output_packets_per_sec * (1316 * 8);
	conf->output_tmout           = 1000000 / conf->output_packets_per_sec;

	// Open the filename if we want to write to a file
	if(conf->write_output_file) {
		output_open_file(conf->output_filename, conf->output);
	}

	if (conf->server_port)
		init_server_socket(conf->server_addr, conf->server_port, &conf->server, &conf->server_socket);

	if (!conf->quiet) {
		LOGf("Configuration:\n");
		LOGf("\tServer ident      : %s\n", conf->ident);
		LOGf("\tGlobal config     : %s\n", conf->global_conf);
		LOGf("\tChannels config   : %s\n", conf->channels_conf);
		LOGf("\tNIT config        : %s\n", conf->nit_conf);
		if (conf->write_output_network)
			LOGf("\tOutput addr       : %s://%s:%d\n", (conf->output->rtp_ssrc != 0 ? "rtp" : "udp"), inet_ntoa(conf->output->out_host), conf->output->out_port);
		else
			LOGf("\tOutput addr       : disabled\n");
		if (conf->output_intf.s_addr)
			LOGf("\tOutput iface addr : %s\n", inet_ntoa(conf->output_intf));
		LOGf("\tMulticast ttl     : %d\n", conf->multicast_ttl);
		LOGf("\tRTP SSRC          : %u\n", conf->output->rtp_ssrc);
		if (conf->syslog_active) {
			LOGf("\tSyslog host       : %s\n", conf->loghost);
			LOGf("\tSyslog port       : %d\n", conf->logport);
		} else {
			LOGf("\tSyslog            : disabled\n");
		}
		LOGf("\tOutput bitrate    : %.0f bps, %.2f Kbps, %.2f Mbps\n", conf->output_bitrate, conf->output_bitrate / 1000, conf->output_bitrate / 1000000);
		LOGf("\tOutput pkt tmout  : %ld us\n", conf->output_tmout);
		LOGf("\tPackets per second: %ld\n", conf->output_packets_per_sec);
		LOGf("\tPCR mode          : %s\n",
			conf->pcr_mode == 0 ? "Do not touch PCRs" :
			conf->pcr_mode == 1 ? "Move PCRs to their calculated place" :
			conf->pcr_mode == 2 ? "Rewrite PCRs using output bitrate" :
			conf->pcr_mode == 3 ? "Move PCRs and rewrite them" : "???"
		);
		if (conf->write_output_file)
			LOGf("\tWrite output file : %s\n", conf->output_filename);
		if (conf->write_input_file)
			LOGf("\tWrite input file(s)\n");
	} else
		LOGf("Quiet mode enabled.\n");

	pthread_t sleepthread;
	if (pthread_create(&sleepthread, NULL, &calibrate_sleep, conf) == 0) {
		pthread_join(sleepthread, NULL);
	} else {
		perror("calibrate_thread");
		exit(1);
	}
	if (!conf->output_tmout)
		exit(1);

	output_buffer_alloc(conf->output, conf->output_bitrate);

	if (!config_load_global(conf))
		goto ERR;
	if (!config_load_nit(conf))
		goto ERR;
	if (!config_load_channels(conf))
		goto ERR;

	config_load_epg(conf);

	return;

ERR:
	config_free(&conf);
	exit(1);
}
