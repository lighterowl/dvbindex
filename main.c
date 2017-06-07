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

#define _POSIX_C_SOURCE 2

#include "export.h"
#include "log.h"
#include "read.h"
#include "version.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(const char *progname) {
  fprintf(stderr, "dvbindex v" DVBINDEX_VERSION_STRING "\n");
  fprintf(stderr, "Usage : %s dbfile [stream ...]\n", progname);
  /* clang-format off */
  static const char *usagemsg =
"Read streams and save their metadata and codec information into dbfile. Each of\n"
"the streams might be a file or a directory.\n"
"\n"
"Additional options :\n"
"   -v verbosity   Specify the logging verbosity, with 0 being the lowest and 3\n"
"                  being the highest. This can be a single number, in which case\n"
"                  all components have the same verbosity, or a comma-delimited\n"
"                  sequence of component:severity tokens. Valid components are :\n"
"                  dvbindex, ffmpeg, sqlite, dvbpsi\n";
  /* clang-format on */
  fputs(usagemsg, stderr);
}

int main(int argc, char *argv[]) {
  int opt;
  while ((opt = getopt(argc, argv, "v:")) != -1) {
    switch (opt) {
    case 'v':
      dvbindex_log_parse_severity(optarg);
      break;
    }
  }

  if ((argc - optind) < 2) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (ffmpeg_init()) {
    dvbindex_log(
        DVBIDX_LOG_CAT_DVBINDEX, DVBIDX_LOG_SEVERITY_CRITICAL,
        "ffmpeg initialization failed. This is probably caused by your\n"
        "ffmpeg version not having support for MPEG-TS compiled in.\n"
        "If ffprobe works on your TS files, please submit a bug report\n"
        "for dvbindex.\n");
    return EXIT_FAILURE;
  }

  int rv = EXIT_SUCCESS;
  db_export db;
  const char *dbfilename = argv[optind];
  char *db_init_error;
  int db_init_rv = db_export_init(&db, dbfilename, &db_init_error);
  if (db_init_rv != SQLITE_OK) {
    dvbindex_log(DVBIDX_LOG_CAT_SQLITE, DVBIDX_LOG_SEVERITY_CRITICAL,
                 "Could not init database %s : %s (%s)\n", dbfilename,
                 sqlite3_errstr(db_init_rv), db_init_error);
    rv = EXIT_FAILURE;
    goto beach;
  }

  for (int i = optind + 1; i < argc; ++i) {
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
