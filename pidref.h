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
