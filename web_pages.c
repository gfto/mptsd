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
