/*
 * mptsd data header file
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
#ifndef DATA_H
#define DATA_H

/* How much to wait for connection to be established with channel source (miliseconds) */
#define PROXY_CONNECT_TIMEOUT 1000

/* Seconds to sleep between retries (miliseconds) */
#define PROXY_RETRY_TIMEOUT 1000

/* 7 * 188 */
#define FRAME_PACKET_SIZE 1316

#define RTP_HEADER_SIZE 12

#include "libfuncs/libfuncs.h"

#include "libtsfuncs/tsdata.h"

#include "pidref.h"

typedef enum { udp_sock, tcp_sock } channel_source;

typedef struct {
	channel_source sproto;
	char *proto;
	char *host;
	char *path;
	unsigned int port;
	unsigned int rtp;
} CHANSRC;

#define MAX_CHANNEL_SOURCES 8

typedef struct {
	uint16_t	event_id;
	time_t		start;
	int			duration;
	char *		event;
	char *		short_desc;
	char *		long_desc;
} EPG_ENTRY;

typedef struct {
	/* Config */
	int			base_pid;
	int			service_id;
	int			pmt_pid;
	int			radio;
	int			lcn;
	int			lcn_visible;
	char *		id;
	char *		name;
	/* Sources */
	char *		source; /* Full source url */
	char *		sources[MAX_CHANNEL_SOURCES];
	uint8_t		num_src;
	uint8_t		curr_src;
	int			worktime_start;
	int			worktime_end;

	/* EPG */
	uint8_t			epg_version:5;

	EPG_ENTRY *		epg_now;
	EPG_ENTRY *		epg_next;

	struct ts_eit *	eit_now;
	struct ts_eit *	eit_next;
} CHANNEL;

typedef struct {
	uint64_t pcr;
	uint64_t last_pcr;
	int bytes;
	int ts_packets_per_output_bitrate;
} PCR;

typedef struct {
	PIDREF *pidref;			/* Rewritten pids list */

	uint16_t nit_pid;		/* Pid of the NIT, default 0x10 */
	uint16_t pmt_pid;		/* Pid of the original PMT, used to replace PMT */
	uint16_t pcr_pid;		/* PCR pid */

	struct ts_pat *pat;		/* The PAT */
	struct ts_pmt *pmt;		/* The PMT */

	uint64_t input_pcr;		// Latest PCR entered into input buffer

	uint8_t pid_pat_cont:4;
	uint8_t pid_pmt_cont:4;
	struct ts_pat *pat_rewritten;	/* The rewritten PAT */
	struct ts_pmt *pmt_rewritten;	/* The rewritten PMT */

	struct ts_pat *last_pat;		/* The last incoming PAT */
	struct ts_pmt *last_pmt;		/* The last incoming PMT */
} INPUT_STREAM;

typedef struct {
	char  *name;
	CHANNEL *channel;
	int sock;				/* Server socket */
	struct sockaddr_in src_sockname;
	int reconnect:1,		/* Set to 1 to force proxy reconnect */
	    connected:1,		/* It's set to 1 when proxy is connected and serving clients */
	    insert_eit:1,		/* When set to 1 input adds EIT table into stream (if there is info) */
	    working:1,			/* Set to 1 if the input is in worktime */
	    dienow:1,			/* Stop serving clients and exit now */
	    freechannel:1;		/* Free channel data on object free (this is used in chanconf) */
	int cookie;				/* Used in chanconf to determine if the restreamer is alrady checked */
	int ifd;

	pthread_t thread;

	uint16_t output_pcr_pid;
	uint64_t output_last_pcr;			// The PCR before latest outputed
	uint64_t output_pcr;				// Latest outputed PCR
	int output_pcr_packets_needed;		// Based on selected output rate how much packets should be between output_pcr and output_last_pcr
	int outputed_packets;				// How much packets have been sent. This is reset on every PCR and incremented on every sent packet (every input and output padding)

	int disabled;			/* Input is disabled, no data is fed to output buffers */
	int input_ready;		/* Set to 1 from INPUT thread when input is ready to be mixed in output */

	CBUF *buf;				// Input buffer */

	INPUT_STREAM stream;
} INPUT;

typedef enum {
	obuf_empty    = 0,		// Buffer is empty and can be used by mix thread
	obuf_filling  = 1,		// Buffer is being filled by mix thread
	obuf_full     = 2,		// Buffer is filled and can be used by write thread
	obuf_emptying = 3,		// Buffer is being emptyed by write thread
} OBUF_STATUS;

typedef struct {
	uint8_t *	buf;
	int			size;		// Output buffer size (must be size % 1316 == 0)
	int			written;
	OBUF_STATUS	status;
} OBUF;

typedef struct {
	struct in_addr out_host;
	int out_port;
	int out_sock;			/* The udp socket */
	int ofd;
	int dienow;				/* Instruct output to die */

	pthread_t psi_thread;
	pthread_t mix_thread;
	pthread_t write_thread;

	CBUF *psibuf;			// Input buffer */

	unsigned int obuf_ms;	// How much miliseconds of data output buffer holds
	OBUF obuf[2];			// Output buffers
	double output_bitrate;	// Output bitrate (bps)

	uint64_t traffic;

	uint64_t traffic_period;
	uint64_t padding_period;

	uint8_t pid_pat_cont:4;
	uint8_t pid_nit_cont:4;
	uint8_t pid_sdt_cont:4;
	uint8_t pid_eit_cont:4;
	uint8_t pid_tdt_cont:4;

	struct ts_pat *pat;
	struct timeval pat_ts;

	struct ts_sdt *sdt;
	struct timeval sdt_ts;

	struct ts_nit *nit;
	struct timeval nit_ts;

	struct ts_tdt *tdt;
	struct timeval tdt_ts;

	struct ts_tdt *tot;
	struct timeval tot_ts;

	struct timeval eit_ts;

	uint64_t last_org_pcr[8193];	// Last PCR value indexed by PID x
	uint64_t last_pcr[8193];		// Last PCR value indexed by PID x
	uint64_t last_traffic[8193];	// Last traffic when PCR with PID x was seen
} OUTPUT;

typedef struct {
	char		*freq;
	char		*modulation;
	char		*symbol_rate;
	uint16_t	ts_id;
	uint32_t	_freq;
	uint8_t		_modulation;
	uint32_t	_symbol_rate;
} NIT;

EPG_ENTRY *	epg_new			(time_t start, int duration, char *encoding, char *event, char *short_desc, char *long_desc);
void		epg_free		(EPG_ENTRY **e);
int			epg_changed		(EPG_ENTRY *a, EPG_ENTRY *b);

CHANNEL *	channel_new		(int service_id, int is_radio, char *id, char *name, char *source, int lcn, int is_lcn_visible);
void		channel_free	(CHANNEL **c);
void		channel_free_epg(CHANNEL *c);

channel_source get_sproto(char *url);
int is_rtp(char *url);

CHANSRC *	chansrc_init	(char *url);
void		chansrc_free	(CHANSRC **url);
void		chansrc_add		(CHANNEL *c, char *src);
void		chansrc_next	(CHANNEL *c);
void		chansrc_set		(CHANNEL *c, uint8_t src_id);

INPUT *		input_new		(const char *name, CHANNEL *channel);
void		input_free		(INPUT **input);

void		input_stream_reset	(INPUT *input);

OUTPUT *	output_new			();
void		output_free			(OUTPUT **output);
void		output_open_file	(OUTPUT *o);
void		output_buffer_alloc	(OUTPUT *o, double output_bitrate);
void		obuf_reset			(OBUF *ob);

NIT *		nit_new			(uint16_t ts_id, char *freq, char *modulation, char *symbol_rate);
void		nit_free		(NIT **nit);


void		proxy_log		(INPUT *r, char *msg);
void		proxy_close		(LIST *inputs, INPUT **input);

#endif
