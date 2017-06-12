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

#ifndef DVBINDEX_EXPORT_H
#define DVBINDEX_EXPORT_H

#include "tables.h"
#include <sqlite3.h>
#include <sys/types.h>

typedef struct AVStream AVStream;
typedef struct dvbpsi_pat_s dvbpsi_pat_t;
typedef struct dvbpsi_pmt_s dvbpsi_pmt_t;
typedef struct dvbpsi_sdt_s dvbpsi_sdt_t;

typedef struct db_export_ {
  sqlite3 *db;
  sqlite3_stmt *insert_stmts[DVBINDEX_TABLE__LAST];
  sqlite3_stmt *file_select;
} db_export;

int db_export_init(db_export *exp, const char *filename, char **error);
void db_export_av_streams(db_export *exp, sqlite3_int64 file_rowid,
                          unsigned int num_streams, AVStream *const *streams);
sqlite3_int64 db_export_pat(db_export *exp, sqlite3_int64 file_rowid,
                            const dvbpsi_pat_t *pat);
void db_export_pmt(db_export *exp, sqlite3_int64 pat_rowid,
                   const dvbpsi_pmt_t *pmt);
void db_export_sdt(db_export *exp, sqlite3_int64 pat_rowid,
                   const dvbpsi_sdt_t *sdt);
int db_has_file(db_export *exp, const char *path, off_t size);
sqlite3_int64 db_export_file(db_export *exp, const char *path, off_t size);
void db_export_close(db_export *exp);

#endif
