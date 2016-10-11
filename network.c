/*
 * mptsd network routines
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
#include <pthread.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <regex.h>

#include "libfuncs/io.h"
#include "libfuncs/log.h"
#include "libfuncs/list.h"
#include "libfuncs/asyncdns.h"

#include "libtsfuncs/tsfuncs.h"

#include "data.h"
#include "config.h"

extern char *server_sig;
extern char *server_ver;
extern CONFIG *config;

int connect_udp(struct sockaddr_in send_to) {
	int sendsock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sendsock < 0) {
		LOGf("socket(SOCK_DGRAM): %s\n", strerror(errno));
		return -1;
	}
	int on = 1;
	setsockopt(sendsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	// subscribe to multicast group
	// LOGf("Using ttl %d\n", multicast_ttl);
	if (IN_MULTICAST(ntohl(send_to.sin_addr.s_addr))) {
		if (setsockopt(sendsock, IPPROTO_IP, IP_MULTICAST_TTL, &config->multicast_ttl, sizeof(config->multicast_ttl)) < 0) {
			LOGf("setsockopt(IP_MUTICAST_TTL): %s\n", strerror(errno));
			close(sendsock);
			return -1;
		}
		if (setsockopt(sendsock, IPPROTO_IP, IP_MULTICAST_IF, &config->output_intf, sizeof(config->output_intf)) < 0) {
			LOGf("setsockopt(IP_MUTICAST_IF, %s): %s\n", strerror(errno), inet_ntoa(config->output_intf));
			close(sendsock);
			return -1;
		}
	}
	int writebuflen = 1316 * 100;
	if (setsockopt(sendsock, SOL_SOCKET, SO_SNDBUF, (const char *)&writebuflen, sizeof(writebuflen)) < 0)
		log_perror("play(): setsockopt(SO_SNDBUF)", errno);

	// call connect to get errors
	if (connect(sendsock, (struct sockaddr *)&send_to, sizeof send_to)) {
		LOGf("udp_connect() error: %s\n", strerror(errno));
		close(sendsock);
		return -1;
	}
	return sendsock;
}

void connect_output(OUTPUT *o) {
	struct sockaddr_in sock;
	sock.sin_family = AF_INET;
	sock.sin_port   = htons(o->out_port);
	sock.sin_addr   = o->out_host;
	o->out_sock = connect_udp(sock);
	if (o->out_sock > -1) {
		//LOGf("OUTPUT: Connected out_fd: %i | Output: udp://%s:%d\n", o->out_sock, inet_ntoa(o->out_host), o->out_port);
	} else {
		LOGf("ERROR: Can't connect output | Output: udp://%s:%d\n", inet_ntoa(o->out_host), o->out_port);
		exit(1);
	}
}

/*
	On the last try, send no-signal to clients and exit
	otherwise wait a little bit before trying again
*/
#define DO_RECONNECT do \
{ \
	chansrc_free(&src); \
	if (retries == 0) { \
		return -1; \
	} else { \
		if (errno != EHOSTUNREACH) /* When host is unreachable there is already a delay of ~4 secs per try so no sleep is needed */ \
			usleep(PROXY_RETRY_TIMEOUT * 1000); \
		if (r->dienow) \
			return -1; \
		return 1; \
	} \
} while(0)

#define FATAL_ERROR do \
{ \
	chansrc_free(&src); \
	return -1; \
} while (0)

/*
	Returns:
		-1 = exit thread
		 1 = retry
		 0 = connected ok
*/
int connect_source(INPUT *r, int retries, int readbuflen, int *http_code) {
	CHANSRC *src = chansrc_init(r->channel->source);
	if (!src) {
		LOGf("ERR  : Can't parse channel source | Channel: %s Source: %s\n", r->channel->name, r->channel->source);
		FATAL_ERROR;
	}
	r->connected = 0;
	r->reconnect = 0;

	int active = 1;
	int dret = async_resolve_host(src->host, src->port, &(r->src_sockname), 5000, &active);
	if (dret != 0) {
		if (dret == 1)
			proxy_log(r, "Can't resolve host");
		if (dret == 2)
			proxy_log(r, "Timeout resolving host");
		DO_RECONNECT;
	}

	proxy_log(r, "Connecting");

	char buf[1024];
	*http_code = 0;
	if (src->sproto == tcp_sock) {
		r->sock = socket(PF_INET, SOCK_STREAM, 0);
		if (r->sock < 0) {
			log_perror("play(): Could not create SOCK_STREAM socket.", errno);
			FATAL_ERROR;
		}
		//proxy_log(r, "Add");
		if (do_connect(r->sock, (struct sockaddr *)&(r->src_sockname), sizeof(r->src_sockname), PROXY_CONNECT_TIMEOUT) < 0) {
			LOGf("ERR  : Error connecting to %s srv_fd: %i err: %s\n", r->channel->source, r->sock, strerror(errno));
			DO_RECONNECT;
		}

		snprintf(buf,sizeof(buf)-1, "GET /%s HTTP/1.0\nHost: %s:%u\nX-Smart-Client: yes\nUser-Agent: %s %s (%s)\n\n",
		         src->path, src->host, src->port, server_sig, server_ver, config->ident);
		buf[sizeof(buf)-1] = 0;
		fdwrite(r->sock, buf, strlen(buf));

		char xresponse[128];
		memset(xresponse, 0, sizeof(xresponse));
		memset(buf, 0, sizeof(buf));
		regmatch_t res[4];
		while (fdgetline(r->sock,buf,sizeof(buf)-1)) {
			if (buf[0] == '\n' || buf[0] == '\r')
				break;
			if (strstr(buf,"HTTP/1.") != NULL) {
				regex_t http_response;
				regcomp(&http_response, "^HTTP/1.[0-1] (([0-9]{3}) .*)", REG_EXTENDED);
				if (regexec(&http_response,buf,3,res,0) != REG_NOMATCH) {
					char codestr[4];
					if ((unsigned int)res[1].rm_eo-res[1].rm_so < sizeof(xresponse)) {
						strncpy(xresponse, &buf[res[1].rm_so], res[1].rm_eo-res[1].rm_so);
						xresponse[res[1].rm_eo-res[1].rm_so] = '\0';
						chomp(xresponse);
						strncpy(codestr, &buf[res[2].rm_so], res[2].rm_eo-res[2].rm_so);
						codestr[3] = 0;
						*http_code = atoi(codestr);
					}
				}
				regfree(&http_response);
			}
			if (*http_code == 504) { // Extract extra error code
				if (strstr(buf, "X-ErrorCode: ") != NULL) {
					*http_code = atoi(buf+13);
					break;
				}
			}
		}
		if (*http_code == 0) { // No valid HTTP response, retry
			LOGf("DEBUG: Server returned not valid HTTP code | srv_fd: %i\n", r->sock);
			DO_RECONNECT;
		}
		if (*http_code == 504) { // No signal, exit
			LOGf("ERR  : Get no-signal for %s from %s on srv_fd: %i\n", r->channel->name, r->channel->source, r->sock);
			FATAL_ERROR;
		}
		if (*http_code > 300) { // Unhandled or error codes, exit
			LOGf("ERR  : Get code %i for %s from %s on srv_fd: %i exiting.\n", *http_code, r->channel->name, r->channel->source, r->sock);
			FATAL_ERROR;
		}
		// connected ok, continue
	} else {

		char multicast = IN_MULTICAST(ntohl(r->src_sockname.sin_addr.s_addr));
		
		//if (!IN_MULTICAST(ntohl(r->src_sockname.sin_addr.s_addr))) {
		//	LOGf("ERR  : %s is not multicast address\n", r->channel->source);
		//	FATAL_ERROR;
		//}
		struct ip_mreq mreq;
		struct sockaddr_in receiving_from;

		r->sock = socket(PF_INET, SOCK_DGRAM, 0);
		if (r->sock < 0) {
			log_perror("play(): Could not create SOCK_DGRAM socket.", errno);
			FATAL_ERROR;
		}
		// LOGf("CONN : Listening on multicast socket %s srv_fd: %i retries left: %i\n", r->channel->source, r->sock, retries);
		int on = 1;
		setsockopt(r->sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		
		if (multicast)  {
    		// subscribe to multicast group
    		memcpy(&mreq.imr_multiaddr, &(r->src_sockname.sin_addr), sizeof(struct in_addr));
    		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    		if (setsockopt(r->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
    			LOGf("ERR  : Failed to add IP membership on %s srv_fd: %i\n", r->channel->source, r->sock);
    			FATAL_ERROR;
    		}
		}
		// bind to the socket so data can be read
		memset(&receiving_from, 0, sizeof(receiving_from));
		receiving_from.sin_family = AF_INET;
		receiving_from.sin_addr   = r->src_sockname.sin_addr;
		receiving_from.sin_port   = htons(src->port);
		if (bind(r->sock, (struct sockaddr *) &receiving_from, sizeof(receiving_from)) < 0) {
			LOGf("ERR  : Failed to bind to %s srv_fd: %i\n", r->channel->source, r->sock);
			FATAL_ERROR;
		}
	}

	if (setsockopt(r->sock, SOL_SOCKET, SO_RCVBUF, (const char *)&readbuflen, sizeof(readbuflen)) < 0)
		log_perror("play(): setsockopt(SO_RCVBUF)", errno);

	r->connected = 1;

//	proxy_log(r, "Connected");
	chansrc_free(&src);
	return 0;
}

/*         Start: 3 seconds on connect */
/* In connection: Max UDP timeout == 3 seconds (read) + 2 seconds (connect) == 5 seconds */
#define UDP_READ_RETRIES 3
#define UDP_READ_TIMEOUT 1000

/*         Start: 1/4 seconds on connect */
/* In connection: Max TCP timeout == 5 seconds (read) + 2 seconds (connect)             == 7 seconds */
/* In connection: Max TCP timeout == 5 seconds (read) + 8 seconds (connect, host unrch) == 13 seconds */
#define TCP_READ_RETRIES 5
#define TCP_READ_TIMEOUT 1000

/*
	Returns:
		0 = synced ok
		1 = not synced, reconnect
*/
int mpeg_sync(INPUT *r, channel_source source_proto) {
	time_t sync_start = time(NULL);
	unsigned int sync_packets = 0;
	unsigned int read_bytes = 0;
	char syncframe[188];

	int _timeout = TCP_READ_TIMEOUT;
	int _retries = TCP_READ_RETRIES;
	if (source_proto == udp_sock) {
		_timeout = UDP_READ_TIMEOUT;
		_retries = UDP_READ_RETRIES;
	}
	do {
		if (r->dienow)
			return 1;
resync:
		if (fdread_ex(r->sock, syncframe, 1, _timeout, _retries, 1) != 1) {
			proxy_log(r, "mpeg_sync fdread() timeoutA");
			return 1; // reconnect
		}
		// LOGf("DEBUG:     Read 0x%02x Offset %u Sync: %u\n", (uint8_t)syncframe[0], read_bytes, sync_packets);
		read_bytes++;
		if (syncframe[0] == 0x47) {
			ssize_t rdsz = fdread_ex(r->sock, syncframe, 188-1, _timeout, _retries, 1);
			if (rdsz != 188-1) {
				proxy_log(r, "mpeg_sync fdread() timeoutB");
				return 1; // reconnect
			}
			read_bytes += 188-1;
			if (++sync_packets == 7) // sync 7 packets
				break;
			goto resync;
		} else {
			sync_packets = 0;
		}
		if (read_bytes > FRAME_PACKET_SIZE) { // Can't sync in 1316 bytes
			proxy_log(r, "mpeg_sync can't sync after 1316 bytes");
			return 1; // reconnect
		}
		if (sync_start+2 <= time(NULL)) { // Do not sync in two seconds
			proxy_log(r, "mpeg_sync can't sync in 2 seconds");
			return 1; // reconnect
		}
	} while (1);
	if (read_bytes-FRAME_PACKET_SIZE != 0)
		LOGf("INPUT : [%-12s] TS synced after %u bytes\n", r->channel->id, read_bytes-FRAME_PACKET_SIZE);
	return 0;
}
