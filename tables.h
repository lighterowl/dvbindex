/* dvbindex - a program for indexing DVB streams
Copyright (C) 2017 Daniel Kamil Kozar

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#ifndef DVBINDEX_TABLES_H
#define DVBINDEX_TABLES_H

#include <stddef.h>

typedef struct dvbindex_table_column_def_ {
  const char *name;
  const char *constraints;
  int type;
} dvbindex_table_column_def;

typedef struct dvbindex_table_def_ {
  const char *name;
  const dvbindex_table_column_def *columns;
  size_t num_columns;
} dvbindex_table_def;

typedef enum dvbindex_table_ {
  DVBINDEX_TABLE_AUD_STREAMS,
  DVBINDEX_TABLE_VID_STREAMS,
  DVBINDEX_TABLE_PATS,
  DVBINDEX_TABLE_PMTS,
  DVBINDEX_TABLE_ELEM_STREAMS,
  DVBINDEX_TABLE_SDTS,
  DVBINDEX_TABLE_SERVICES,
  DVBINDEX_TABLE_FILES,
  DVBINDEX_TABLE_LANG_SPECS,
  DVBINDEX_TABLE_TTX_PAGES,
  DVBINDEX_TABLE_SUBTITLE_CONTENTS,
  DVBINDEX_TABLE_NETWORKS,
  DVBINDEX_TABLE_TRANSPORT_STREAMS,
  DVBINDEX_TABLE_TS_SERVICES,
  DVBINDEX_TABLE__LAST
} dvbindex_table;

const dvbindex_table_def* table_get_def(dvbindex_table t);

#endif
