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
#include "util.h"

#include <assert.h>
#include <libavformat/avformat.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <dvbpsi/descriptor.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/pat.h>
#include <dvbpsi/pmt.h>
#include <dvbpsi/sdt.h>

#include <dvbpsi/dr_0a.h>
#include <dvbpsi/dr_48.h>

#include "dvbstring.h"

#define DVBINDEX_SQLITE_APPLICATION_ID 0x12F834B

/* increment this whenever the schema changes */
#define DVBINDEX_USER_VERSION 2

typedef struct column_def_ {
  const char *name;
  const char *constraints;
  int type;
} column_def;

typedef enum vid_stream_col_id_ {
  VID_STREAM_FILE_ROWID = 1,
  VID_STREAM_PID,
  VID_STREAM_FMT,
  VID_STREAM_WIDTH,
  VID_STREAM_HEIGHT,
  VID_STREAM_FPS,
  VID_STREAM_BITRATE,
  VID_STREAM__LAST
} vid_stream_col_id;

static const column_def vid_streams_coldefs[] = {
    {"file_rowid", "NOT NULL", SQLITE_INTEGER},
    {"pid", "NOT NULL", SQLITE_INTEGER},
    {"fmt", "", SQLITE_TEXT},
    {"width", "", SQLITE_INTEGER},
    {"height", "", SQLITE_INTEGER},
    {"fps", "", SQLITE_FLOAT},
    {"bitrate", "", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(vid_streams_coldefs) == VID_STREAM__LAST - 1,
              vid_streams_invalid_columns);

typedef enum aud_stream_col_id_ {
  AUD_STREAM_FILE_ROWID = 1,
  AUD_STREAM_PID,
  AUD_STREAM_FMT,
  AUD_STREAM_CHANNELS,
  AUD_STREAM_SAMPLE_RATE,
  AUD_STREAM_BITRATE,
  AUD_STREAM__LAST
} aud_stream_col_id;

static const column_def aud_streams_coldefs[] = {
    {"file_rowid", "NOT NULL", SQLITE_INTEGER},
    {"pid", "NOT NULL", SQLITE_INTEGER},
    {"fmt", "", SQLITE_TEXT},
    {"channels", "", SQLITE_INTEGER},
    {"sample_rate", "", SQLITE_INTEGER},
    {"bitrate", "", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(aud_streams_coldefs) == AUD_STREAM__LAST - 1,
              aud_streams_invalid_columns);

typedef enum pat_col_id_ {
  PAT_FILE_ROWID = 1,
  PAT_TSID,
  PAT_VERSION,
  PAT__LAST
} pat_col_id;

static const column_def pats_coldefs[] = {
    {"file_rowid", "NOT NULL", SQLITE_INTEGER},
    {"tsid", "NOT NULL", SQLITE_INTEGER},
    {"version", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(pats_coldefs) == PAT__LAST - 1, pats_invalid_columns);

typedef enum pmt_col_id_ {
  PMT_PAT_ROWID = 1,
  PMT_PROGRAM_NUMBER,
  PMT_VERSION,
  PMT_PCR_PID,
  PMT__LAST
} pmt_col_id;

static const column_def pmts_coldefs[] = {
    {"pat_rowid", "NOT NULL", SQLITE_INTEGER},
    {"program_number", "NOT NULL", SQLITE_INTEGER},
    {"version", "NOT NULL", SQLITE_INTEGER},
    {"pcr_pid", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(pmts_coldefs) == PMT__LAST - 1, pmts_invalid_columns);

typedef enum elem_stream_col_id_ {
  ELEM_STREAM_PMT_ROWID = 1,
  ELEM_STREAM_TYPE,
  ELEM_STREAM_PID,
  ELEM_STREAM__LAST
} elem_stream_col_id;

static const column_def elem_streams_coldefs[] = {
    {"pmt_rowid", "NOT NULL", SQLITE_INTEGER},
    {"stream_type", "NOT NULL", SQLITE_INTEGER},
    {"pid", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(elem_streams_coldefs) == ELEM_STREAM__LAST - 1,
              elem_streams_invalid_columns);

typedef enum sdt_col_id {
  SDT_PAT_ROWID = 1,
  SDT_VERSION,
  SDT_ORIGINAL_NETWORK_ID,
  SDT__LAST
} sdt_col_id;

static const column_def sdts_coldefs[] = {
    {"pat_rowid", "NOT NULL", SQLITE_INTEGER},
    {"version", "NOT NULL", SQLITE_INTEGER},
    {"onid", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(sdts_coldefs) == SDT__LAST - 1, sdts_invalid_columns);

typedef enum service_col_id_ {
  SERVICE_SDT_ROWID = 1,
  SERVICE_PROGRAM_NUMBER,
  SERVICE_RUNNING_STATUS,
  SERVICE_SCRAMBLED,
  SERVICE_NAME,
  SERVICE_PROVIDER_NAME,
  SERVICE__LAST
} service_col_id;

static const column_def services_coldefs[] = {
    {"sdt_rowid", "NOT NULL", SQLITE_INTEGER},
    {"program_number", "NOT NULL", SQLITE_INTEGER},
    {"running_status", "NOT NULL", SQLITE_INTEGER},
    {"scrambled", "NOT NULL", SQLITE_INTEGER},
    {"name", "", SQLITE_TEXT},
    {"provider_name", "", SQLITE_TEXT}};

STATIC_ASSERT(ARRAY_SIZE(services_coldefs) == SERVICE__LAST - 1,
              services_invalid_columns);

typedef enum file_col_id_ { FILE_NAME = 1, FILE_SIZE, FILE__LAST } file_col_id;

static const column_def files_coldefs[] = {
    {"name", "NOT NULL", SQLITE_TEXT}, {"size", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(files_coldefs) == FILE__LAST - 1,
              files_invalid_columns);

typedef enum lang_spec_col_id_ {
  LANG_SPEC_ELEM_STREAM_ROWID = 1,
  LANG_SPEC_LANGUAGE,
  LANG_SPEC_AUDIO_TYPE,
  LANG_SPEC__LAST
} lang_spec_col_id;

static const column_def lang_specs_coldefs[] = {
    {"elem_stream_rowid", "NOT NULL", SQLITE_INTEGER},
    {"language", "NOT NULL", SQLITE_TEXT},
    {"audio_type", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(lang_specs_coldefs) == LANG_SPEC__LAST - 1,
              lang_specs_invalid_columns);

typedef enum ttx_stream_col_id_ {
  TTX_PAGE_ELEM_STREAM_ROWID = 1,
  TTX_PAGE_LANGUAGE,
  TTX_PAGE_TELETEXT_TYPE,
  TTX_PAGE_MAGAZINE_NUMBER,
  TTX_PAGE_PAGE_NUMBER,
  TTX_PAGE__LAST
} ttx_stream_col_id;

static const column_def ttx_pages_coldefs[] = {
    {"elem_stream_rowid", "NOT NULL", SQLITE_INTEGER},
    {"language", "NOT NULL", SQLITE_TEXT},
    {"teletext_type", "NOT NULL", SQLITE_INTEGER},
    {"magazine_number", "NOT NULL", SQLITE_INTEGER},
    {"page_number", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(ttx_pages_coldefs) == TTX_PAGE__LAST - 1,
              ttx_pages_invalid_columns);

typedef struct table_def_ {
  const char *name;
  const column_def *columns;
  size_t num_columns;
} table_def;

/* clang-format off */

#define DEFINE_TABLE(x) \
  { #x, x##_coldefs, ARRAY_SIZE(x##_coldefs) }

/* clang-format on */

static const table_def tables[] = {
    DEFINE_TABLE(aud_streams),  DEFINE_TABLE(vid_streams),
    DEFINE_TABLE(pats),         DEFINE_TABLE(pmts),
    DEFINE_TABLE(elem_streams), DEFINE_TABLE(sdts),
    DEFINE_TABLE(services),     DEFINE_TABLE(files),
    DEFINE_TABLE(lang_specs),   DEFINE_TABLE(ttx_pages)};

static void start_transaction(sqlite3 *db) {
  int rc = sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);
  assert(rc == SQLITE_OK);
}

static void end_transaction(sqlite3 *db) {
  int rc = sqlite3_exec(db, "END TRANSACTION", 0, 0, 0);
  assert(rc == SQLITE_OK);
}

static const char *type_name(int type) {
  switch (type) {
  case SQLITE_INTEGER:
    return "INT";
  case SQLITE_FLOAT:
    return "REAL";
  case SQLITE_TEXT:
    return "TEXT";
  case SQLITE_BLOB:
    return "BLOB";
  }
  assert(0 && "unknown type passed to type_name");
  return 0;
}

typedef int (*query_build_callback)(char *, size_t, const column_def *);

static int build_query(char *buf, int bufsize, const char *initial,
                       const table_def *table, query_build_callback cbk) {
  int total_printed = 0;
  int printed = snprintf(buf, bufsize, initial, table->name);
  bufsize -= printed;
  assert(bufsize > 0);
  buf += printed;
  total_printed += printed;

  assert(table->num_columns > 0);
  for (size_t i = 0; i < table->num_columns; ++i) {
    printed = cbk(buf, bufsize, &table->columns[i]);
    bufsize -= printed;
    assert(bufsize > 0);
    buf += printed;
    total_printed += printed;

    printed =
        snprintf(buf, bufsize, "%c", i + 1 == table->num_columns ? ')' : ',');
    bufsize -= printed;
    assert(bufsize > 0);
    buf += printed;
    total_printed += printed;
  }

  return total_printed;
}

static int print_column_into_create(char *buf, size_t available,
                                    const column_def *c) {
  return snprintf(buf, available, "%s %s %s", c->name, type_name(c->type),
                  c->constraints);
}

static int print_asterisk_for_insert(char *buf, size_t available,
                                     const column_def *c) {
  (void)c;
  return snprintf(buf, available, "?");
}

static int create_table(sqlite3 *db, const table_def *table, char **error) {
  char buf[4096];
  build_query(buf, sizeof(buf), "CREATE TABLE IF NOT EXISTS %s (", table,
              print_column_into_create);
  return sqlite3_exec(db, buf, 0, 0, error);
}

static int create_insert_statement(sqlite3 *db, sqlite3_stmt **stmt,
                                   const table_def *table) {
  assert(stmt);
  char buf[4096];
  const char *tail;
  (void)tail;
  int size = build_query(buf, sizeof(buf), "INSERT INTO %s VALUES (", table,
                         print_asterisk_for_insert);
  int rv = sqlite3_prepare_v2(db, buf, size + 1, stmt, &tail);
  assert(*tail == 0);
  return rv;
}

static int pragma_result_cbk(void *ctx, int cols, char **vals, char **names) {
  assert(cols == 1 && vals[0]);
  uint32_t *result = ctx;
  int rv = sscanf(vals[0], "%" SCNu32, result);
  assert(rv == 1);
  return 0;
}

static uint32_t get_pragma_id(sqlite3 *db, const char *pragma) {
  uint32_t id;
  char *sql = sqlite3_mprintf("PRAGMA %s", pragma);
  assert(sql);
  int rv = sqlite3_exec(db, sql, pragma_result_cbk, &id, 0);
  assert(rv == SQLITE_OK);
  sqlite3_free(sql);
  return id;
}

static void set_pragma_id(sqlite3 *db, const char *pragma, unsigned int value) {
  char *sql = sqlite3_mprintf("PRAGMA %s = %u", pragma, value);
  int rv = sqlite3_exec(db, sql, 0, 0, 0);
  assert(rv == SQLITE_OK);
  sqlite3_free(sql);
}

static int handle_application_id(sqlite3 *db, char **error) {
  int rv = SQLITE_OK;
  uint32_t appid = get_pragma_id(db, "application_id");
  if (appid == 0) {
    set_pragma_id(db, "application_id", DVBINDEX_SQLITE_APPLICATION_ID);
  } else if (appid != DVBINDEX_SQLITE_APPLICATION_ID) {
    rv = SQLITE_FORMAT;
    *error = sqlite3_mprintf("Application ID mismatch");
  }
  return rv;
}

static int check_user_version(sqlite3 *db) {
  uint32_t user_version = get_pragma_id(db, "user_version");
  if (user_version != DVBINDEX_USER_VERSION) {
    set_pragma_id(db, "user_version", DVBINDEX_USER_VERSION);
    return 1;
  }
  return 0;
}

static void drop_table(sqlite3 *db, const table_def *table) {
  char *sql = sqlite3_mprintf("DROP TABLE IF EXISTS %s", table->name);
  assert(sql);
  int rv = sqlite3_exec(db, sql, 0, 0, 0);
  assert(rv == SQLITE_OK);
  sqlite3_free(sql);
}

static void drop_tables(sqlite3 *db) {
  for (size_t i = 0; i < ARRAY_SIZE(tables); ++i) {
    drop_table(db, &tables[i]);
  }
}

static void setup_file_select_stmt(sqlite3 *db, sqlite3_stmt **stmt) {
  const char sql[] = "SELECT rowid FROM files WHERE name = ? AND size = ?";
  int rv = sqlite3_prepare_v2(db, sql, sizeof(sql), stmt, 0);
  assert(rv == SQLITE_OK);
}

int db_export_init(db_export *exp, const char *filename, char **error) {
  int rv = sqlite3_open_v2(filename, &exp->db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                               SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_PRIVATECACHE,
                           0);
  if (rv != SQLITE_OK) {
    goto beach;
  }

  rv = handle_application_id(exp->db, error);
  if (rv != SQLITE_OK) {
    goto beach;
  }

  if (check_user_version(exp->db) != 0) {
    drop_tables(exp->db);
  }

  sqlite3_stmt **insert_statements[ARRAY_SIZE(tables)] = {
      &exp->aud_stream_insert, &exp->vid_stream_insert,  &exp->pat_insert,
      &exp->pmt_insert,        &exp->elem_stream_insert, &exp->sdt_insert,
      &exp->service_insert,    &exp->file_insert,        &exp->lang_spec_insert,
      &exp->ttx_page_insert};

  for (size_t i = 0; i < ARRAY_SIZE(tables); ++i) {
    rv = create_table(exp->db, &tables[i], error);
    if (rv != SQLITE_OK) {
      goto beach;
    }
    rv = create_insert_statement(exp->db, insert_statements[i], &tables[i]);
    if (rv != SQLITE_OK) {
      goto beach;
    }
  }

  setup_file_select_stmt(exp->db, &exp->file_select);
  return SQLITE_OK;

beach:
  sqlite3_close_v2(exp->db);
  return rv;
}

void db_export_close(db_export *exp) {
  sqlite3_finalize(exp->ttx_page_insert);
  sqlite3_finalize(exp->lang_spec_insert);
  sqlite3_finalize(exp->file_select);
  sqlite3_finalize(exp->file_insert);
  sqlite3_finalize(exp->service_insert);
  sqlite3_finalize(exp->sdt_insert);
  sqlite3_finalize(exp->elem_stream_insert);
  sqlite3_finalize(exp->pmt_insert);
  sqlite3_finalize(exp->pat_insert);
  sqlite3_finalize(exp->aud_stream_insert);
  sqlite3_finalize(exp->vid_stream_insert);
  sqlite3_close_v2(exp->db);
}

static void codec_name_to_sql(sqlite3_stmt *stmt, int pos,
                              enum AVCodecID codec_id) {
  const AVCodecDescriptor *cd = avcodec_descriptor_get(codec_id);
  if (cd) {
    sqlite3_bind_text(stmt, pos, cd->name, -1, SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, pos);
  }
}

static void bind_ffmpeg_int(sqlite3_stmt *stmt, int pos, int64_t val) {
  if (val) {
    sqlite3_bind_int64(stmt, pos, val);
  } else {
    sqlite3_bind_null(stmt, pos);
  }
}

static void export_video_stream(sqlite3_stmt *stmt, sqlite3_int64 file_rowid,
                                const AVStream *stream) {
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, VID_STREAM_FILE_ROWID, file_rowid);
  sqlite3_bind_int(stmt, VID_STREAM_PID, stream->id);
  codec_name_to_sql(stmt, VID_STREAM_FMT, stream->codecpar->codec_id);
  bind_ffmpeg_int(stmt, VID_STREAM_WIDTH, stream->codecpar->width);
  bind_ffmpeg_int(stmt, VID_STREAM_HEIGHT, stream->codecpar->height);
  sqlite3_bind_double(stmt, VID_STREAM_FPS,
                      stream->avg_frame_rate.num /
                          (double)stream->avg_frame_rate.den);
  bind_ffmpeg_int(stmt, VID_STREAM_BITRATE, stream->codecpar->bit_rate);
  sqlite3_step(stmt);
}

static void export_audio_stream(sqlite3_stmt *stmt, sqlite3_int64 file_rowid,
                                const AVStream *stream) {
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, AUD_STREAM_FILE_ROWID, file_rowid);
  sqlite3_bind_int(stmt, AUD_STREAM_PID, stream->id);
  codec_name_to_sql(stmt, AUD_STREAM_FMT, stream->codecpar->codec_id);
  bind_ffmpeg_int(stmt, AUD_STREAM_CHANNELS, stream->codecpar->channels);
  bind_ffmpeg_int(stmt, AUD_STREAM_SAMPLE_RATE, stream->codecpar->sample_rate);
  bind_ffmpeg_int(stmt, AUD_STREAM_BITRATE, stream->codecpar->bit_rate);
  sqlite3_step(stmt);
}

void db_export_av_streams(db_export *exp, sqlite3_int64 file_rowid,
                          unsigned int num_streams, AVStream *const *streams) {
  start_transaction(exp->db);
  for (unsigned int i = 0; i < num_streams; ++i) {
    const AVStream *s = streams[i];
    switch (s->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      export_video_stream(exp->vid_stream_insert, file_rowid, s);
      break;
    case AVMEDIA_TYPE_AUDIO:
      export_audio_stream(exp->aud_stream_insert, file_rowid, s);
      break;
    default:
      break;
    }
  }
  end_transaction(exp->db);
}

static void export_iso639_descriptor(sqlite3_stmt *stmt,
                                     dvbpsi_descriptor_t *dr,
                                     sqlite3_int64 es_rowid) {
  dvbpsi_iso639_dr_t *iso639_dr = dvbpsi_DecodeISO639Dr(dr);
  for (uint8_t i = 0; i < iso639_dr->i_code_count; ++i) {
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, LANG_SPEC_ELEM_STREAM_ROWID, es_rowid);
    const char *code = (const char *)iso639_dr->code[i].iso_639_code;
    sqlite3_bind_text(stmt, LANG_SPEC_LANGUAGE, code,
                      sizeof(iso639_dr->code[i].iso_639_code),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, LANG_SPEC_AUDIO_TYPE,
                     iso639_dr->code[i].i_audio_type);
    sqlite3_step(stmt);
  }
}

static void export_pmt_es_descriptors(db_export *exp, sqlite3_int64 es_rowid,
                                      dvbpsi_descriptor_t *dr) {
  while (dr) {
    switch (dr->i_tag) {
    case 0x0a:
      export_iso639_descriptor(exp->lang_spec_insert, dr, es_rowid);
      break;
    }

    dr = dr->p_next;
  }
}

static void export_pmt_es(db_export *exp, sqlite3_int64 pmt_rowid,
                          const dvbpsi_pmt_es_t *es) {
  sqlite3_reset(exp->elem_stream_insert);
  sqlite3_bind_int64(exp->elem_stream_insert, ELEM_STREAM_PMT_ROWID, pmt_rowid);
  sqlite3_bind_int(exp->elem_stream_insert, ELEM_STREAM_TYPE, es->i_type);
  sqlite3_bind_int(exp->elem_stream_insert, ELEM_STREAM_PID, es->i_pid);
  sqlite3_step(exp->elem_stream_insert);
  sqlite3_int64 es_rowid = sqlite3_last_insert_rowid(exp->db);
  export_pmt_es_descriptors(exp, es_rowid, es->p_first_descriptor);
}

sqlite3_int64 db_export_pat(db_export *exp, sqlite3_int64 file_rowid,
                            const dvbpsi_pat_t *pat) {
  sqlite3_reset(exp->pat_insert);
  sqlite3_bind_int64(exp->pat_insert, PAT_FILE_ROWID, file_rowid);
  sqlite3_bind_int(exp->pat_insert, PAT_TSID, pat->i_ts_id);
  sqlite3_bind_int(exp->pat_insert, PAT_VERSION, pat->i_version);
  sqlite3_step(exp->pat_insert);
  return sqlite3_last_insert_rowid(exp->db);
}

void db_export_pmt(db_export *exp, sqlite3_int64 pat_rowid,
                   const dvbpsi_pmt_t *pmt) {
  start_transaction(exp->db);
  sqlite3_reset(exp->pmt_insert);
  sqlite3_bind_int64(exp->pmt_insert, PMT_PAT_ROWID, pat_rowid);
  sqlite3_bind_int(exp->pmt_insert, PMT_PROGRAM_NUMBER, pmt->i_program_number);
  sqlite3_bind_int(exp->pmt_insert, PMT_VERSION, pmt->i_version);
  sqlite3_bind_int(exp->pmt_insert, PMT_PCR_PID, pmt->i_pcr_pid);
  sqlite3_step(exp->pmt_insert);
  sqlite3_int64 pmt_rowid = sqlite3_last_insert_rowid(exp->db);
  dvbpsi_pmt_es_t *es = pmt->p_first_es;
  while (es) {
    export_pmt_es(exp, pmt_rowid, es);
    es = es->p_next;
  }
  end_transaction(exp->db);
}

static void export_sdt_descriptors(sqlite3_stmt *stmt,
                                   dvbpsi_descriptor_t *sdt_dscr) {
  while (sdt_dscr) {
    switch (sdt_dscr->i_tag) {
    case 0x48: {
      dvbpsi_service_dr_t *service_dr = dvbpsi_DecodeServiceDr(sdt_dscr);
      if (service_dr) {
        size_t outlen;
        char *str =
            dvbstring_to_utf8(service_dr->i_service_name,
                              service_dr->i_service_name_length, &outlen);
        sqlite3_bind_text64(stmt, SERVICE_NAME, str, outlen, free, SQLITE_UTF8);
        str = dvbstring_to_utf8(service_dr->i_service_provider_name,
                                service_dr->i_service_provider_name_length,
                                &outlen);
        sqlite3_bind_text64(stmt, SERVICE_PROVIDER_NAME, str, outlen, free,
                            SQLITE_UTF8);
      }
    } break;
    }

    sdt_dscr = sdt_dscr->p_next;
  }
}

static void export_sdt_service(db_export *exp, sqlite3_int64 sdt_rowid,
                               const dvbpsi_sdt_service_t *service) {
  sqlite3_reset(exp->service_insert);
  sqlite3_bind_int64(exp->service_insert, SERVICE_SDT_ROWID, sdt_rowid);
  sqlite3_bind_int(exp->service_insert, SERVICE_PROGRAM_NUMBER,
                   service->i_service_id);
  sqlite3_bind_int(exp->service_insert, SERVICE_RUNNING_STATUS,
                   service->i_running_status);
  sqlite3_bind_int(exp->service_insert, SERVICE_SCRAMBLED, service->b_free_ca);
  sqlite3_bind_null(exp->service_insert, SERVICE_NAME);
  export_sdt_descriptors(exp->service_insert, service->p_first_descriptor);
  sqlite3_step(exp->service_insert);
}

void db_export_sdt(db_export *exp, sqlite3_int64 pat_rowid,
                   const dvbpsi_sdt_t *sdt) {
  start_transaction(exp->db);
  sqlite3_reset(exp->sdt_insert);
  sqlite3_bind_int64(exp->sdt_insert, SDT_PAT_ROWID, pat_rowid);
  sqlite3_bind_int(exp->sdt_insert, SDT_VERSION, sdt->i_version);
  sqlite3_bind_int(exp->sdt_insert, SDT_ORIGINAL_NETWORK_ID, sdt->i_network_id);
  sqlite3_step(exp->sdt_insert);
  sqlite3_int64 sdt_rowid = sqlite3_last_insert_rowid(exp->db);
  dvbpsi_sdt_service_t *service = sdt->p_first_service;
  while (service) {
    export_sdt_service(exp, sdt_rowid, service);
    service = service->p_next;
  }
  end_transaction(exp->db);
}

sqlite3_int64 db_export_file(db_export *exp, const char *path, off_t size) {
  sqlite3_reset(exp->file_insert);
  sqlite3_bind_text(exp->file_insert, FILE_NAME, file_name_from_path(path), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int64(exp->file_insert, FILE_SIZE, size);
  sqlite3_step(exp->file_insert);
  return sqlite3_last_insert_rowid(exp->db);
}

int db_has_file(db_export *exp, const char *path, off_t size) {
  sqlite3_bind_text(exp->file_select, 1, file_name_from_path(path), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int64(exp->file_select, 2, size);
  int rv = sqlite3_step(exp->file_select);
  sqlite3_reset(exp->file_select);
  assert(rv == SQLITE_ROW || rv == SQLITE_DONE);
  return rv == SQLITE_ROW;
}
