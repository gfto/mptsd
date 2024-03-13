/*
 * mptsd internal web pages
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
#if WITH_JSON == 1
#include <json-c/json.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "libfuncs/io.h"
#include "libfuncs/log.h"
#include "libfuncs/list.h"
#include "libfuncs/http_response.h"

#include "config.h"

extern CONFIG *config;

#if WITH_JSON == 1
json_object *json_object_new_string_safe(char *str) {
	if (str) {
		return json_object_new_string(str);
	} else {
		return json_object_new_null();
	}
}

json_object *json_add_traffic_stats_entry(TRAFFIC_STATS_ENTRY *entry) {
	json_object *job = json_object_new_object();
	json_object *jobtemp;

	jobtemp = json_object_new_double(entry->kpbs);
	json_object_object_add(job, "kpbs", jobtemp);

	jobtemp = json_object_new_double(entry->padding);
	json_object_object_add(job, "padding", jobtemp);

	jobtemp = json_object_new_uint64(entry->traffic);
	json_object_object_add(job, "traffic", jobtemp);

	return job;
}

json_object *json_add_traffic_stats(TRAFFIC_STATS *stats) {
	json_object *job = json_object_new_object();
	json_object *jobtemp;

	jobtemp = json_add_traffic_stats_entry(&stats->last);
	json_object_object_add(job, "last", jobtemp);

	jobtemp = json_add_traffic_stats_entry(&stats->max);
	json_object_object_add(job, "max", jobtemp);

	jobtemp = json_add_traffic_stats_entry(&stats->min);
	json_object_object_add(job, "min", jobtemp);

	return job;
}

json_object *json_add_rtp_stats(RTP_STATS *rtp_stats) {
	json_object *job = json_object_new_object();
	json_object *jobtemp;

	jobtemp = json_object_new_uint64(rtp_stats->ssrc);
	json_object_object_add(job, "ssrc", jobtemp);

	jobtemp = json_object_new_int(rtp_stats->last_sequence_number);
	json_object_object_add(job, "last_sequence_number", jobtemp);

	jobtemp = json_object_new_uint64(rtp_stats->packets_lost);
	json_object_object_add(job, "packets_lost", jobtemp);

	jobtemp = json_object_new_uint64(rtp_stats->packets_received);
	json_object_object_add(job, "packets_received", jobtemp);

	return job;
}

json_object *json_add_channel(CHANNEL *c) {
	json_object *job = json_object_new_object();
	json_object *jobtemp;
	json_object *jobtemp2;

	jobtemp = json_object_new_int(c->index);
	json_object_object_add(job, "index", jobtemp);

	jobtemp = json_object_new_string_safe(c->id);
	json_object_object_add(job, "id", jobtemp);

	jobtemp = json_object_new_string_safe(c->name);
	json_object_object_add(job, "name", jobtemp);

	jobtemp = json_object_new_int(c->service_id);
	json_object_object_add(job, "service_id", jobtemp);

	jobtemp = json_object_new_int(c->base_pid);
	json_object_object_add(job, "base_pid", jobtemp);

	jobtemp = json_object_new_int(c->pmt_pid);
	json_object_object_add(job, "pmt_pid", jobtemp);

	jobtemp = json_object_new_int(c->radio);
	json_object_object_add(job, "radio", jobtemp);

	jobtemp = json_object_new_int(c->lcn);
	json_object_object_add(job, "lcn", jobtemp);

	jobtemp = json_object_new_int(c->lcn_visible);
	json_object_object_add(job, "lcn_visible", jobtemp);

	jobtemp = json_object_new_string_safe(c->source);
	json_object_object_add(job, "source", jobtemp);

	jobtemp = json_object_new_int(c->curr_src);
	json_object_object_add(job, "curr_src", jobtemp);

	jobtemp2 = json_object_new_array();
	for (uint8_t i = 0; i < c->num_src; i++) {
		jobtemp = json_object_new_string_safe(c->sources[i]);
		json_object_array_add(jobtemp2, jobtemp);
	}
	json_object_object_add(job, "sources", jobtemp2);

	return job;
}

json_object *json_add_channels(LIST *channels) {
	json_object *jarray;
	json_object *jobtemp;
	LNODE *lc, *lctmp;
	jarray = json_object_new_array();
	list_for_each(channels, lc, lctmp) {
		CHANNEL *c = lc->data;
		jobtemp = json_add_channel(c);
		json_object_array_add(jarray, jobtemp);
	}
	return jarray;
}

json_object *json_add_input(INPUT *r) {
	json_object *job = json_object_new_object();
	json_object *jobtemp;

	jobtemp = json_object_new_string_safe(r->name);
	json_object_object_add(job, "name", jobtemp);

	jobtemp = json_object_new_uint64(r->traffic);
	json_object_object_add(job, "traffic", jobtemp);

	jobtemp = json_add_traffic_stats(&r->traffic_stats);
	json_object_object_add(job, "traffic_stats", jobtemp);

	jobtemp = json_object_new_int(r->outputed_packets);
	json_object_object_add(job, "outputed_packets", jobtemp);

	jobtemp = json_add_rtp_stats(&r->rtp_stats);
	json_object_object_add(job, "rtp_stats", jobtemp);

	jobtemp = json_add_channel(r->channel);
	json_object_object_add(job, "channel", jobtemp);

	return job;
}

json_object *json_add_inputs(LIST *inputs) {
	json_object *jarray;
	json_object *jobtemp;
	LNODE *lc, *lctmp;
	jarray = json_object_new_array();
	list_for_each(inputs, lc, lctmp) {
		INPUT *r = lc->data;
		jobtemp = json_add_input(r);
		json_object_array_add(jarray, jobtemp);
	}
	return jarray;
}

json_object *json_add_output(OUTPUT *o) {
	json_object *job = json_object_new_object();
	json_object *jobtemp;
	char in_addr_str[INET_ADDRSTRLEN];

	inet_ntop(AF_INET, &o->out_host, in_addr_str, INET_ADDRSTRLEN);
	jobtemp = json_object_new_string_safe(in_addr_str);
	json_object_object_add(job, "out_host", jobtemp);

	jobtemp = json_object_new_int(o->out_port);
	json_object_object_add(job, "out_port", jobtemp);

	jobtemp = json_object_new_uint64(o->traffic);
	json_object_object_add(job, "traffic", jobtemp);

	jobtemp = json_object_new_double(o->output_bitrate);
	json_object_object_add(job, "output_bitrate", jobtemp);

	jobtemp = json_add_traffic_stats(&o->traffic_stats);
	json_object_object_add(job, "traffic_stats", jobtemp);

	jobtemp = json_object_new_uint64(o->rtp_ssrc);
	json_object_object_add(job, "rtp_ssrc", jobtemp);

	jobtemp = json_object_new_int(o->rtp_sequence_number);
	json_object_object_add(job, "rtp_sequence_number", jobtemp);

	return job;
}

json_object *json_add_global(CONFIG *c) {
	json_object *job = json_object_new_object();
	json_object *jobtemp;
	char in_addr_str[INET_ADDRSTRLEN];

	jobtemp = json_object_new_string_safe(c->ident);
	json_object_object_add(job, "ident", jobtemp);

	jobtemp = json_object_new_int(c->network_id);
	json_object_object_add(job, "network_id", jobtemp);

	jobtemp = json_object_new_string_safe(c->network_name);
	json_object_object_add(job, "network_name", jobtemp);

	jobtemp = json_object_new_double(c->output_bitrate);
	json_object_object_add(job, "output_bitrate", jobtemp);

	inet_ntop(AF_INET, &c->output_intf, in_addr_str, INET_ADDRSTRLEN);
	jobtemp = json_object_new_string_safe(in_addr_str);
	json_object_object_add(job, "output_intf", jobtemp);

	jobtemp = json_object_new_int64(c->output_packets_per_sec);
	json_object_object_add(job, "output_packets_per_sec", jobtemp);

	jobtemp = json_object_new_int64(c->output_tmout);
	json_object_object_add(job, "output_tmout", jobtemp);

	jobtemp = json_object_new_int(c->pcr_mode);
	json_object_object_add(job, "pcr_mode", jobtemp);

	jobtemp = json_object_new_int(c->use_lcn);
	json_object_object_add(job, "use_lcn", jobtemp);

	jobtemp = json_object_new_string_safe(c->provider_name);
	json_object_object_add(job, "provider_name", jobtemp);

	jobtemp = json_object_new_int(c->transport_stream_id);
	json_object_object_add(job, "transport_stream_id", jobtemp);

	// timeouts
	json_object *job2 = json_object_new_object();

	jobtemp = json_object_new_int(c->timeouts.pat);
	json_object_object_add(job2, "pat", jobtemp);

	jobtemp = json_object_new_int(c->timeouts.pmt);
	json_object_object_add(job2, "pmt", jobtemp);

	jobtemp = json_object_new_int(c->timeouts.nit);
	json_object_object_add(job2, "nit", jobtemp);

	jobtemp = json_object_new_int(c->timeouts.sdt);
	json_object_object_add(job2, "sdt", jobtemp);

	jobtemp = json_object_new_int(c->timeouts.eit);
	json_object_object_add(job2, "eit", jobtemp);

	jobtemp = json_object_new_int(c->timeouts.tdt);
	json_object_object_add(job2, "tdt", jobtemp);

	jobtemp = json_object_new_int(c->timeouts.tot);
	json_object_object_add(job2, "tot", jobtemp);

	jobtemp = json_object_new_int(c->timeouts.stats);
	json_object_object_add(job2, "stats", jobtemp);

	json_object_object_add(job, "timeouts", job2);

	return job;
}
#endif

void cmd_index(int clientsock) {
	send_200_ok(clientsock);
	send_header_textplain(clientsock);
	fdputs(clientsock, "\nHi from mptsd.\n");
}

void cmd_reconnect(int clientsock) {
	send_200_ok(clientsock);
	send_header_textplain(clientsock);
	fdputsf(clientsock, "\nReconnecting %d inputs.\n", config->inputs->items);
}

#if WITH_JSON == 1
void cmd_status_json(int clientsock) {
	send_200_ok(clientsock);
	send_header_applicationjson(clientsock);

	json_object *job = json_object_new_object();
	json_object *jobtemp;

	jobtemp = json_add_global(config);
	json_object_object_add(job, "global", jobtemp);

	jobtemp = json_add_channels(config->channels);
	json_object_object_add(job, "channels", jobtemp);

	jobtemp = json_add_inputs(config->inputs);
	json_object_object_add(job, "inputs", jobtemp);

	jobtemp = json_add_output(config->output);
	json_object_object_add(job, "output", jobtemp);

	size_t length;
	const char *jbuf = json_object_to_json_string_length(
		job, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED, &length);
	send_header_content_length(clientsock, length);
	fdputs(clientsock, "\n");
	fdwrite(clientsock, jbuf, length);
	json_object_put(job); // delete job (free memory)
}
#else // WTIH_JSON != 1
void cmd_status_json(int clientsock) {
	send_400_bad_request(clientsock, "not supported (WTIH_JSON=0)");
}
#endif
