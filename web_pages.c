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
