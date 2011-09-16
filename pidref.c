#include <stdlib.h>

#include "libfuncs/log.h"
#include "libtsfuncs/tsfuncs.h"
#include "pidref.h"

PIDREF *pidref_init(int num, uint16_t base_pid) {
	PIDREF *ref = calloc(1, sizeof(PIDREF));
	ref->num = num;
	ref->base_pid = base_pid;
	ref->entries = calloc(ref->num, sizeof(PIDREF_ENTRY));
	return ref;
}

void pidref_free(PIDREF **pref) {
	PIDREF *ref = *pref;
	if (!ref)
		return;
	FREE(ref->entries);
	FREE(*pref);
}

int pidref_add(PIDREF *ref, uint16_t org_pid, uint16_t new_pid) {
	int i;
	if (!org_pid)
		return 0;
	for (i=0;i<ref->num;i++) {
		PIDREF_ENTRY *entry = &ref->entries[i];
		if (!entry->org_pid) {
			entry->org_pid = org_pid;
			entry->new_pid = new_pid;
			return 1;
		}
	}
	return 0;
}

int pidref_del(PIDREF *ref, uint16_t org_pid) {
	int i;
	if (!org_pid)
		return 0;
	for (i=0;i<ref->num;i++) {
		PIDREF_ENTRY *entry = &ref->entries[i];
		if (entry->org_pid == org_pid) {
			entry->org_pid = 0;
			entry->new_pid = 0;
			return 1;
		}
	}
	return 0;
}

uint16_t pidref_get_new_pid(PIDREF *ref, uint16_t org_pid) {
	int i;
	if (!org_pid)
		return 0;
	for (i=0;i<ref->num;i++) {
		PIDREF_ENTRY *entry = &ref->entries[i];
		if (entry->org_pid == org_pid) {
			return entry->new_pid;
		}
	}
	return 0;
}

int pidref_change_packet_pid(uint8_t *ts_packet, uint16_t packet_pid, PIDREF *ref) {
	uint16_t new_pid = pidref_get_new_pid(ref, packet_pid);
	if (new_pid) {
		ts_packet_set_pid(ts_packet, new_pid);
		return new_pid;
	}
	return 0;
}

void pidref_dump(PIDREF *ref) {
	int i;
	LOGf("pidref->base_pid = 0x%04x\n", ref->base_pid);
	LOGf("pidref->num      = %d\n"    , ref->num);
	LOG ("pidref->entries     org_pid  new_pid\n");
	for (i=0;i<ref->num;i++) {
		PIDREF_ENTRY *entry = &ref->entries[i];
		if (entry->org_pid)
			LOGf("pidref->entry[%02d] = 0x%04x   0x%04x\n", i, entry->org_pid, entry->new_pid);
	}
}
