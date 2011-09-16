#ifndef OUTPUT_H
#define OUTPUT_H

#include "config.h"

void output_psi_init			(CONFIG *conf, OUTPUT *output);
void output_psi_free			(OUTPUT *output);

void * output_handle_psi		(void *_config);
void * output_handle_mix		(void *_config);
void * output_handle_write		(void *_config);

#endif
