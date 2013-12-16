/*
 * mptsd internal web server
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
#include <regex.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "libfuncs/libfuncs.h"

#include "libfuncs/io.h"
#include "libfuncs/log.h"

#include "libtsfuncs/tsfuncs.h"

#include "data.h"
#include "config.h"
#include "network.h"

#include "udp_server.h"

extern int keep_going;

#define NEXT_CLIENT { FREE(path); FREE(buf); pthread_exit(0); }
#define SHUTDOWN_CLIENT { FREE(path); FREE(buf); shutdown_fd(&clientsock); pthread_exit(0); }
#define BUFSIZE 1316

void *udp_server_thread(void *data) {
	CONFIG *conf = data;

	struct sockaddr_in client;					/* remote address */
	socklen_t clientlen = sizeof(client);		/* length of addresses */
	int clientsock; 							/* #bytes received */
	char buf[FRAME_PACKET_SIZE];				/* receive buffer */

	while (keep_going) {


		clientsock = recvfrom(conf->udp_server_socket,(char*)buf, FRAME_PACKET_SIZE, 0, (struct sockaddr *)&client, &clientlen);

		if (clientsock < 0) {
			if (conf->udp_server_socket > -1)	// The server_socket is closed on exit, so do not report errors
				LOGf("ERROR : Failed to accept client fd: %i err: %s\n", clientsock, strerror(errno));
			if (errno==EMFILE || errno==ENFILE)
				break;
		} else {

		//	LOGf("received %d bytes from %s with port %d \n", clientsock, inet_ntoa(client.sin_addr), ntohs( client.sin_port ) );

		// TODO  Process buffer and add to output_mix

		}

	}

	pthread_exit(0);


}

void udp_server_start(CONFIG *conf) {
	if (conf->udp_server_port)
		udp_server_init(conf->udp_server_addr, conf->udp_server_port, &conf->udp_server, &conf->udp_server_socket);

	if (conf->udp_server_socket > -1) {
		LOG("UDP_SERVER: Started \n");
		pthread_create(&conf->udp_server_thread, NULL, &udp_server_thread, conf);
		}
}

void udp_server_stop(CONFIG *conf) {
	if (conf->udp_server_socket > -1) {
		shutdown_fd(&conf->udp_server_socket);
		pthread_join(conf->udp_server_thread, NULL);
	}
}


void udp_server_init(char *bind_addr, int bind_port, struct sockaddr_in *server, int *server_socket) {

	LOG("UDP_SERVER: Init.\n");

	char *binded;

	struct hostent *host_ptr;
	*server_socket = socket(PF_INET, SOCK_DGRAM, 0);

	if (*server_socket == -1) {
		perror("socket(server_socket)");
		exit(1);
	}

	int j = 1;
	if (setsockopt(*server_socket, SOL_SOCKET, SO_REUSEADDR,(const char *) &j, sizeof(j))<0) {
		perror("setsockopt(SO_REUSEADDR)");
		exit(1);
	}

	memset(server, 0, sizeof(struct sockaddr_in));
	if (!bind_addr) {
		binded = "*";
		server->sin_addr.s_addr = htonl(INADDR_ANY);
	} else {
		host_ptr = gethostbyname(bind_addr);
		if (!host_ptr) {
			fprintf(stderr,"Error can't resolve bind address: %s\n", bind_addr);
			exit(1);
		}
		memcpy(&server->sin_addr, host_ptr->h_addr, sizeof(server->sin_addr));
		binded = inet_ntoa(server->sin_addr);
	}

	/* Bind to server socket */
	LOGf ("UDP_SERVER: Bind to : %s:%i\n", binded, bind_port);

	server->sin_family = AF_INET;
	server->sin_port   = htons(bind_port);

	if (bind(*server_socket, (struct sockaddr *)server, sizeof(struct sockaddr_in)) < 0) {
		perror("bind(server_socket)");
		exit(1);
	}

	LOG("UDP_SERVER: UDP server Initialized.\n");

}
