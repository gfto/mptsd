#ifndef NETWORK_H
#define NETWORK_H

#include "data.h"

void	connect_output	(OUTPUT *o);
int		connect_source	(INPUT *r, int retries, int readbuflen, int *http_code);
int     mpeg_sync       (INPUT *r, channel_source source_proto);

#endif
