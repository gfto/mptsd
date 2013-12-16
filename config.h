/*
 * mptsd configuration header file
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
#ifndef CONFIG_H
#define CONFIG_H

#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "libfuncs/list.h"

#include "data.h"

typedef struct {
	char			*ident;
	char			*pidfile;

	int				syslog_active;
	char			*logident;
	char			*loghost;
	int				logport;

	struct sockaddr_in		server;
	char			*server_addr;
	int				server_port;
	int				server_socket;
	pthread_t		server_thread;

	struct sockaddr_in		udp_server;
	char			*udp_server_addr;
	int				udp_server_port;
	int				udp_server_socket;
	pthread_t		udp_server_thread;

	int				multicast_ttl;

	struct in_addr	output_intf;

	char			*global_conf;
	char			*channels_conf;
	char			*nit_conf;
	char			*epg_conf;

	int				debug;
	int				quiet;

	int				write_input_file;
	int				write_output_file;
	int				pcr_mode;			// 0 - do not touch PCRs
										// 1 - move PCRs to their calculated place
										// 2 - rewrite PCRs using output bitrate
										// 3 - move PCRs and rewrite them

	uint16_t		network_id;			// For NIT && SDT
	uint16_t		transport_stream_id;// For NIT
	char			*frequency;			// For NIT
	char			*modulation;		// For NIT
	char			*symbol_rate;		// For NIT

	char 			*network_name;		// For NIT
	char 			*provider_name;		// For SDT

	double			output_bitrate;		// Output bitrate (bps)
	long			output_tmout;		// Smooth interval miliseconds
	long			usleep_overhead;		// How much more usecs uslep(1) takes
	long			output_packets_per_sec;	// How much packets should be sent in one second

	time_t			epg_conf_mtime;		// Last change time of epg config

	LIST *			channels;			// List of channels
	LIST *			inputs;				// Input threads
	LIST *			nit;
	OUTPUT *		output;				// Output

	struct {
		unsigned int	pat;			// DVB section id 0x00 (program_association_section)
		unsigned int	pmt;			// DVB section id 0x02 (program_map_section)
		unsigned int	nit;			// DVB section id 0x40 (network_information_section - actual_network)
		unsigned int	sdt;			// DVB section id 0x42 (service_description_section - actual_transport_stream)
		unsigned int	eit;			// DVB section id 0x4e (event_information_section - actual_transport_stream, present/following)
		unsigned int	tdt;			// DVB section id 0x70 (time_date_section)
		unsigned int	tot;			// DVB section id 0x73 (time_offset_section)
		unsigned int	stats;			// Local
	} timeouts;
} CONFIG;


CONFIG *	config_alloc	();
void		config_free		(CONFIG **conf);
void		config_load		(CONFIG *conf, int argc, char **argv);

int			config_load_global		(CONFIG *conf);
int			config_load_channels		(CONFIG *conf);
int			config_load_nit			(CONFIG *conf);
int			config_load_epg			(CONFIG *conf);

#endif
