/*
 * mptsd output routines header file
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
#ifndef OUTPUT_H
#define OUTPUT_H

#include "config.h"

void output_psi_init			(CONFIG *conf, OUTPUT *output);
void output_psi_free			(OUTPUT *output);

void * output_handle_psi		(void *_config);
void * output_handle_mix		(void *_config);
void * output_handle_write		(void *_config);

#endif
