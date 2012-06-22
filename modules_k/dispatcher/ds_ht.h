/**
 * $Id$
 *
 * Copyright (C) 2010 Daniel-Constantin Mierla (asipto.com)
 *
 * This file is part of kamailio, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
		       
#ifndef _DS_HT_H_
#define _DS_HT_H_

#include <time.h>

#include "../../str.h"
#include "../../locking.h"

#define DS_LOAD_INIT		0
#define DS_LOAD_CONFIRMED	1

typedef struct _ds_cell
{
    unsigned int cellid;
	str callid;
	str duid;
	int dset;
	int state;
	time_t  expire;
	time_t  initexpire;
    struct _ds_cell *prev;
    struct _ds_cell *next;
} ds_cell_t;

typedef struct _ds_entry
{
	unsigned int esize;
	ds_cell_t *first;
	gen_lock_t lock;	
} ds_entry_t;

typedef struct _ds_ht
{
	unsigned int htexpire;
	unsigned int htinitexpire;
	unsigned int htsize;
	ds_entry_t *entries;
	struct _ds_ht *next;
} ds_ht_t;

ds_ht_t *ds_ht_init(unsigned int htsize, int expire, int initexpire);
int ds_ht_destroy(ds_ht_t *dsht);
int ds_add_cell(ds_ht_t *dsht, str *cid, str *did, int dset);
int ds_del_cell(ds_ht_t *dsht, str *cid);
ds_cell_t* ds_get_cell(ds_ht_t *dsht, str *cid);
int ds_unlock_cell(ds_ht_t *dsht, str *cid);

int ds_ht_dbg(ds_ht_t *dsht);
int ds_cell_free(ds_cell_t *cell);
int ds_ht_clear_slots(ds_ht_t *dsht);

#endif
