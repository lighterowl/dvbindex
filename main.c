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

#include "export.h"
#include "read.h"
#include "version.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(const char *progname) {
  fprintf(stderr, "dvbindex v" DVBINDEX_VERSION_STRING "\n");
  fprintf(stderr, "Usage : %s <dbfile> [file|dir]...\n", progname);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (ffmpeg_init()) {
    fprintf(stderr,
            "ffmpeg initialization failed. This is probably caused by your\n"
            "ffmpeg version not having support for MPEG-TS compiled in.\n"
            "If ffprobe works on your TS files, please submit a bug report\n"
            "for dvbindex.\n");
    return EXIT_FAILURE;
  }

  int rv = EXIT_SUCCESS;
  db_export db;
  char *db_init_error;
  int db_init_rv = db_export_init(&db, argv[1], &db_init_error);
  if (db_init_rv != SQLITE_OK) {
    fprintf(stderr, "Could not init database %s : %s (%s)\n", argv[1],
            sqlite3_errstr(db_init_rv), db_init_error);
    rv = EXIT_FAILURE;
    goto beach;
  }

  for (int i = 2; i < argc; ++i) {
    if (read_path(&db, argv[i]) != 0) {
      rv = EXIT_FAILURE;
      break;
    }
  }

  db_export_close(&db);

beach:
  sqlite3_free(db_init_error);
  return rv;
}
