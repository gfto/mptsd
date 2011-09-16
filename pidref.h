/*
 * pidref header file
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
#ifndef PIDREF_H
#define PIDREF_H

#include <netdb.h>

typedef struct {
	uint16_t org_pid;
	uint16_t new_pid;
} PIDREF_ENTRY;

typedef struct {
	uint16_t base_pid;		// From this pid on there will be rewrites
	int num;
	PIDREF_ENTRY *entries;
} PIDREF;

PIDREF *	pidref_init	(int num, uint16_t base_pid);
void		pidref_free	(PIDREF **ref);

int			pidref_add	(PIDREF *ref, uint16_t org_pid, uint16_t new_pid);
int			pidref_del	(PIDREF *ref, uint16_t org_pid);

uint16_t	pidref_get_new_pid			(PIDREF *ref, uint16_t org_pid);

int			pidref_change_packet_pid	(uint8_t *ts_packet, uint16_t packet_pid, PIDREF *ref);

void		pidref_dump	(PIDREF *ref);

#endif
