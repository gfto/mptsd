/*
 * mptsd data
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
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libfuncs/io.h"
#include "libfuncs/log.h"
#include "libfuncs/list.h"
#include "libfuncs/asyncdns.h"

#include "libtsfuncs/tsfuncs.h"

#include "data.h"
#include "config.h"
#include "output.h"

extern CONFIG *config;

channel_source get_sproto(char *url) {
	return strncmp(url, "http", 4)==0 ? tcp_sock : udp_sock;
}

int is_rtp(char *url) {
	return strncmp(url, "rtp", 3) == 0;
}

CHANSRC *chansrc_init(char *url) {
	if (!url)
		return NULL;
	regex_t re;
	regmatch_t res[5];
	regcomp(&re, "^([a-z]+)://([^:/?]+):?([0-9]*)/?(.*)", REG_EXTENDED);
	if (regexec(&re,url,5,res,0)==0) {
		char *data = strdup(url);
		char *proto, *host, *port, *path;
		int iport;
		proto= data+res[1].rm_so; data[res[1].rm_eo]=0;
		host = data+res[2].rm_so; data[res[2].rm_eo]=0;
		port = data+res[3].rm_so; data[res[3].rm_eo]=0;
		path = data+res[4].rm_so; data[res[4].rm_eo]=0;
		iport = atoi(port);
		/* Setup */
		CHANSRC *src = calloc(1, sizeof(CHANSRC));
		src->proto = strdup(proto);
		src->sproto= get_sproto(url);
		src->host  = strdup(host);
		src->port  = iport ? iport : 80;
		src->path  = strdup(path);
		src->rtp   = strcmp(proto, "rtp") == 0;
		FREE(data);
		regfree(&re);
		return src;
	}
	regfree(&re);
	return NULL;
}

void chansrc_free(CHANSRC **purl) {
	CHANSRC *url = *purl;
	if (url) {
		FREE(url->proto);
		FREE(url->host);
		FREE(url->path);
		FREE(*purl);
	}
};

void chansrc_add(CHANNEL *c, const char *src) {
	if (c->num_src >= MAX_CHANNEL_SOURCES-1)
		return;
	c->sources[c->num_src] = strdup(src);
	if (c->num_src == 0) /* Set default source to first one */
		c->source = c->sources[c->num_src];
	c->num_src++;
}

void chansrc_next(CHANNEL *c) {
	if (c->num_src <= 1)
		return;
	// uint8_t old_src = c->curr_src;
	c->curr_src++;
	if (c->curr_src >= MAX_CHANNEL_SOURCES-1 || c->sources[c->curr_src] == NULL)
		c->curr_src = 0;
	c->source = c->sources[c->curr_src];
	// LOGf("CHAN : Switch source | Channel: %s OldSrc: %d %s NewSrc: %d %s\n", c->name, old_src, c->sources[old_src], c->curr_src, c->source);
}

void chansrc_set(CHANNEL *c, uint8_t src_id) {
	if (src_id >= MAX_CHANNEL_SOURCES-1 || c->sources[src_id] == NULL)
		return;
	// uint8_t old_src = c->curr_src;
	c->curr_src = src_id;
	c->source = c->sources[c->curr_src];
	// LOGf("CHAN : Set source    | Channel: %s OldSrc: %d %s NewSrc: %d %s\n", c->name, old_src, c->sources[old_src], c->curr_src, c->source);
}





CHANNEL *channel_new(int service_id, int is_radio, const char *id, const char *name, const char *source, int channel_index){

    if (channel_index<=0 || channel_index>=256)
    {
        
	    LOGf("CONFIG: Error channel_new invalid index %d\n", channel_index);
        return NULL;
    }
    //LOGf("CONFIG: ------------------channel_new() serviceid %d id %s name %s source %s index %d\n", service_id, id, name , source , channel_index);
    
	CHANNEL *c = calloc(1, sizeof(CHANNEL));
	c->service_id = service_id;
	c->radio = is_radio;
	c->index = channel_index;
	c->base_pid = c->index * 32; // The first pid is saved for PMT , channel_index must > 0
	c->pmt_pid = c->base_pid; // The first pid is saved for PMT
	c->id = strdup(id);
	c->name = strdup(name);
	chansrc_add(c, source);


	return c;
}

void channel_free_epg(CHANNEL *c) {
	epg_free(&c->epg_now);
	epg_free(&c->epg_next);

	ts_eit_free(&c->eit_now);
	ts_eit_free(&c->eit_next);
}

void channel_free(CHANNEL **pc) {
	CHANNEL *c = *pc;
	if (c) {
		channel_free_epg(c);
		FREE(c->id);
		FREE(c->name);
		int i;
		for (i=c->num_src-1; i>=0; i--) {
			FREE(c->sources[i]);
		}
		c->source = NULL;
		FREE(*pc);
	}
}


EPG_ENTRY *epg_new(time_t start, int duration, char *encoding, char *event, char *short_desc, char *long_desc) {
	EPG_ENTRY *e;
	if (!event)
		return NULL;
	e             = calloc(1, sizeof(EPG_ENTRY));
	e->event_id   = (start / 60) &~ 0xffff0000;
	e->start      = start;
	e->duration   = duration;
	if (encoding && strcmp(encoding, "iso-8859-5")==0) {
		e->event      = init_dvb_string_iso_8859_5(event);
		e->short_desc = init_dvb_string_iso_8859_5(short_desc);
		e->long_desc  = init_dvb_string_iso_8859_5(long_desc);
	} else {	// Default is utf-8
		e->event      = init_dvb_string_utf8(event);
		e->short_desc = init_dvb_string_utf8(short_desc);
		e->long_desc  = init_dvb_string_utf8(long_desc);
	}
	return e;
}

void epg_free(EPG_ENTRY **pe) {
	EPG_ENTRY *e = *pe;
	if (e) {
		FREE(e->event);
		FREE(e->short_desc);
		FREE(e->long_desc);
		FREE(*pe);
	}
}


// Return 1 if they are different
// Return 0 if they are the same
int epg_changed(EPG_ENTRY *a, EPG_ENTRY *b) {
	if (!a && b) return 1;
	if (!b && a) return 1;
	if (!a && !b) return 0;
	if (a->event_id != b->event_id) return 1;
	if (a->start != b->start) return 1;
	if (a->duration != b->duration) return 1;
	if (xstrcmp(a->event, b->event) != 0) return 1;
	if (xstrcmp(a->short_desc, b->short_desc) != 0) return 1;
	if (xstrcmp(a->long_desc, b->long_desc) != 0) return 1;
	return 0;
}

void input_stream_alloc(INPUT *input) {
	input->stream.pidref = pidref_init(64, input->channel->base_pid);
	input->stream.pat = ts_pat_alloc();
	input->stream.pmt = ts_pmt_alloc();
	input->stream.last_pat = ts_pat_alloc();
	input->stream.last_pmt = ts_pmt_alloc();
}

void input_stream_free(INPUT *input) {
	ts_pmt_free(&input->stream.pmt);
	ts_pmt_free(&input->stream.pmt_rewritten);
	ts_pmt_free(&input->stream.last_pmt);
	ts_pat_free(&input->stream.pat);
	ts_pat_free(&input->stream.pat_rewritten);
	ts_pat_free(&input->stream.last_pat);
	pidref_free(&input->stream.pidref);
	input->stream.nit_pid    = 0;
	input->stream.pmt_pid    = 0;
	input->stream.pcr_pid    = 0;
	input->stream.input_pcr  = 0;
}

void input_stream_reset(INPUT *input) {
	input_stream_free(input);
	input_stream_alloc(input);
}

INPUT * input_new(const char *name, CHANNEL *channel) {
	char *tmp;
	INPUT *r = calloc(1, sizeof(INPUT));

	r->name = strdup(name);
	r->sock = -1;
	r->channel = channel;

	if (config->write_input_file) {
		asprintf(&tmp, "mptsd-input-%s.ts", channel->id);
		r->ifd = open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0644);
		FREE(tmp);
	}

	r->buf = cbuf_init(1428 * 1316 * 4, channel->id); // ~ 40000 x 188

	input_stream_alloc(r);

	return r;
}

void input_free(INPUT **pinput) {
	INPUT *r = *pinput;
	if (!r)
		return;
	if (r->sock > -1)
		shutdown_fd(&(r->sock));
	if (r->freechannel)
		channel_free(&r->channel);
	if (r->ifd)
		close(r->ifd);

	input_stream_free(r);

	cbuf_free(&r->buf);

	FREE(r->name);
	FREE(*pinput);
}



OUTPUT *output_new() {
	OUTPUT *o = calloc(1, sizeof(OUTPUT));
	o->obuf_ms = 100;

	o->psibuf = cbuf_init(50 * 1316, "psi");
	if (!o->psibuf) {
		LOGf("ERROR: Can't allocate PSI input buffer\n");
		exit(1);
	}
	cbuf_poison(o->psibuf, 'Y');

	return o;
}

void output_open_file(OUTPUT *o) {
	o->ofd = open("mptsd-output.ts", O_CREAT | O_WRONLY | O_TRUNC, 0644);
}

void obuf_reset(OBUF *ob) {
	int i;
	memset(ob->buf, 0xff, ob->size);
	for (i=0; i<ob->size; i+=188) {
		ob->buf[i+0] = 0x47;
		ob->buf[i+1] = 0x1f;
		ob->buf[i+2] = 0xff;
		ob->buf[i+3] = 0x00;
	}
	ob->written = 0;
	ob->status  = obuf_empty;
}

void output_buffer_alloc(OUTPUT *o, double output_bitrate) {
	if (!output_bitrate) {
		LOGf("No output bitrate, can't determine buffer!\n");
		exit(1);
	}

	o->output_bitrate = output_bitrate;

	long pps  = ceil((double)output_bitrate / (FRAME_PACKET_SIZE * 8));	// Packets per second
	long ppms = ceil((double)pps / ((double)1000 / o->obuf_ms));		// Packets per o->buffer_ms miliseconds
	long obuf_size = ppms * 1316;

	o->obuf[0].size   = obuf_size;
	o->obuf[0].status = obuf_empty;
	o->obuf[0].buf    = malloc(o->obuf[0].size);
	obuf_reset(&o->obuf[0]);

	o->obuf[1].size   = obuf_size;
	o->obuf[1].status = obuf_empty;
	o->obuf[1].buf    = malloc(o->obuf[0].size);
	obuf_reset(&o->obuf[1]);

	LOGf("\tOutput buf size   : %ld * 2 = %ld\n", obuf_size, obuf_size * 2);
	LOGf("\tOutput buf packets: %ld (188 bytes)\n", obuf_size / 188);
	LOGf("\tOutput buf frames : %ld (1316 bytes)\n", obuf_size / 1316);
	LOGf("\tOutput buf ms     : %u ms\n", o->obuf_ms);
}

void output_free(OUTPUT **po) {
	OUTPUT *o = *po;
	if (!o)
		return;
	if (o->out_sock > -1)
		shutdown_fd(&(o->out_sock));
	if (o->ofd)
		close(o->ofd);
	cbuf_free(&o->psibuf);
	FREE(o->obuf[0].buf);
	FREE(o->obuf[1].buf);
	output_psi_free(o);
	FREE(*po);
}


NIT *nit_new(uint16_t ts_id, char *freq, char *mod, char *symbol_rate) {
	char tmp[9];
	unsigned i, pos;

	if (strlen(freq) != 9 || strlen(symbol_rate) != 8)
		return NULL;
	NIT *n = calloc(1, sizeof(NIT));
	n->freq        = strdup(freq);
	n->modulation  = strdup(mod);
	n->symbol_rate = strdup(symbol_rate);
	n->ts_id       = ts_id;

	n->_modulation =
		strcmp(mod, "16-QAM") == 0  ? 0x01 :
		strcmp(mod, "32-QAM") == 0  ? 0x02 :
		strcmp(mod, "64-QAM") == 0  ? 0x03 :
		strcmp(mod, "128-QAM") == 0 ? 0x04 :
		strcmp(mod, "256-QAM") == 0 ? 0x05 : 0x00;

	memset(tmp, 0, sizeof(tmp));
	pos = 0;
	for (i=0;i<strlen(freq);i++) {
		if (isdigit(freq[i])) {
			tmp[pos] = freq[i];
			pos++;
		}
	}
	n->_freq = strtol(tmp, NULL, 16);

	memset(tmp, 0, sizeof(tmp));
	pos = 0;
	for (i=0;i<strlen(symbol_rate);i++) {
		if (isdigit(symbol_rate[i])) {
			tmp[pos] = symbol_rate[i];
			pos++;
		}
	}
	n->_symbol_rate = strtol(tmp, NULL, 16);

	return n;
}

void nit_free(NIT **pn) {
	NIT *n = *pn;
	if (n) {
		FREE(n->freq);
		FREE(n->modulation);
		FREE(n->symbol_rate);
		FREE(*pn);
	}
}

void proxy_log(INPUT *r, char *msg) {
	LOGf("INPUT : [%-12s] %s fd: %d src: %s\n", r->channel->id, msg, r->sock, r->channel->source);
}

void proxy_close(LIST *inputs, INPUT **input) {
	proxy_log(*input, "Stop");
	// If there are no clients left, no "Timeout" messages will be logged
	list_del_entry(inputs, *input);
	input_free(input);
}
