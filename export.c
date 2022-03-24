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
#include <libavcodec/avcodec.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <dvbpsi/descriptor.h>

#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/nit.h>
#include <dvbpsi/pat.h>
#include <dvbpsi/pmt.h>
#include <dvbpsi/sdt.h>

#include <dvbpsi/dr_0a.h>
#include <dvbpsi/dr_40.h>
#include <dvbpsi/dr_41.h>
#include <dvbpsi/dr_48.h>
#include <dvbpsi/dr_56.h>
#include <dvbpsi/dr_59.h>

#include "column_ids.h"
#include "dvbstring.h"
#include "tables.h"

#define DVBINDEX_SQLITE_APPLICATION_ID 0x12F834B

/* increment this whenever the schema changes */
#define DVBINDEX_USER_VERSION 5

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

typedef int (*query_build_callback)(char *, size_t,
                                    const dvbindex_table_column_def *);

static int build_query(char *buf, int bufsize, const char *initial,
                       const dvbindex_table_def *table,
                       query_build_callback cbk) {
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
                                    const dvbindex_table_column_def *c) {
  return snprintf(buf, available, "%s %s %s", c->name, type_name(c->type),
                  c->constraints);
}

static int print_asterisk_for_insert(char *buf, size_t available,
                                     const dvbindex_table_column_def *c) {
  (void)c;
  return snprintf(buf, available, "?");
}

static int create_table(sqlite3 *db, const dvbindex_table_def *table,
                        char **error) {
  char buf[4096];
  build_query(buf, sizeof(buf), "CREATE TABLE IF NOT EXISTS %s (", table,
              print_column_into_create);
  return sqlite3_exec(db, buf, 0, 0, error);
}

static int create_insert_statement(sqlite3 *db, sqlite3_stmt **stmt,
                                   const dvbindex_table_def *table) {
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

static void drop_table(sqlite3 *db, const dvbindex_table_def *table) {
  char *sql = sqlite3_mprintf("DROP TABLE IF EXISTS %s", table->name);
  assert(sql);
  int rv = sqlite3_exec(db, sql, 0, 0, 0);
  assert(rv == SQLITE_OK);
  sqlite3_free(sql);
}

static void drop_tables(sqlite3 *db) {
  for (dvbindex_table i = 0; i < DVBINDEX_TABLE__LAST; ++i) {
    drop_table(db, table_get_def(i));
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

  for (dvbindex_table i = 0; i < DVBINDEX_TABLE__LAST; ++i) {
    rv = create_table(exp->db, table_get_def(i), error);
    if (rv != SQLITE_OK) {
      goto beach;
    }
    rv = create_insert_statement(exp->db, &exp->insert_stmts[i],
                                 table_get_def(i));
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
  for (size_t i = 0; i < ARRAY_SIZE(exp->insert_stmts); ++i) {
    sqlite3_finalize(exp->insert_stmts[i]);
  }
  sqlite3_finalize(exp->file_select);
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
  sqlite3_bind_int64(stmt, VID_STREAM_COLUMN_FILE_ROWID, file_rowid);
  sqlite3_bind_int(stmt, VID_STREAM_COLUMN_PID, stream->id);
  codec_name_to_sql(stmt, VID_STREAM_COLUMN_FMT, stream->codecpar->codec_id);
  bind_ffmpeg_int(stmt, VID_STREAM_COLUMN_WIDTH, stream->codecpar->width);
  bind_ffmpeg_int(stmt, VID_STREAM_COLUMN_HEIGHT, stream->codecpar->height);
  sqlite3_bind_double(stmt, VID_STREAM_COLUMN_FPS,
                      stream->avg_frame_rate.num /
                          (double)stream->avg_frame_rate.den);
  bind_ffmpeg_int(stmt, VID_STREAM_COLUMN_BITRATE, stream->codecpar->bit_rate);
  sqlite3_step(stmt);
}

static void export_audio_stream(sqlite3_stmt *stmt, sqlite3_int64 file_rowid,
                                const AVStream *stream) {
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, AUD_STREAM_COLUMN_FILE_ROWID, file_rowid);
  sqlite3_bind_int(stmt, AUD_STREAM_COLUMN_PID, stream->id);
  codec_name_to_sql(stmt, AUD_STREAM_COLUMN_FMT, stream->codecpar->codec_id);
  bind_ffmpeg_int(stmt, AUD_STREAM_COLUMN_CHANNELS, stream->codecpar->channels);
  bind_ffmpeg_int(stmt, AUD_STREAM_COLUMN_SAMPLE_RATE,
                  stream->codecpar->sample_rate);
  bind_ffmpeg_int(stmt, AUD_STREAM_COLUMN_BITRATE, stream->codecpar->bit_rate);
  sqlite3_step(stmt);
}

void db_export_av_streams(db_export *exp, sqlite3_int64 file_rowid,
                          unsigned int num_streams, AVStream *const *streams) {
  start_transaction(exp->db);
  for (unsigned int i = 0; i < num_streams; ++i) {
    const AVStream *s = streams[i];
    switch (s->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      export_video_stream(exp->insert_stmts[DVBINDEX_TABLE_VID_STREAMS],
                          file_rowid, s);
      break;
    case AVMEDIA_TYPE_AUDIO:
      export_audio_stream(exp->insert_stmts[DVBINDEX_TABLE_AUD_STREAMS],
                          file_rowid, s);
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
    sqlite3_bind_int64(stmt, LANG_SPEC_COLUMN_ELEM_STREAM_ROWID, es_rowid);
    const char *code = (const char *)iso639_dr->code[i].iso_639_code;
    sqlite3_bind_text(stmt, LANG_SPEC_COLUMN_LANGUAGE, code,
                      sizeof(iso639_dr->code[i].iso_639_code),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, LANG_SPEC_COLUMN_AUDIO_TYPE,
                     iso639_dr->code[i].i_audio_type);
    sqlite3_step(stmt);
  }
}

static void export_teletext_descriptor(sqlite3_stmt *stmt,
                                       dvbpsi_descriptor_t *dr,
                                       sqlite3_int64 es_rowid) {
  dvbpsi_teletext_dr_t *teletext = dvbpsi_DecodeTeletextDr(dr);
  for (uint8_t i = 0; i < teletext->i_pages_number; ++i) {
    dvbpsi_teletextpage_t *page = teletext->p_pages + i;
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, TTX_PAGE_COLUMN_ELEM_STREAM_ROWID, es_rowid);
    const char *code = (const char *)page->i_iso6392_language_code;
    sqlite3_bind_text(stmt, TTX_PAGE_COLUMN_LANGUAGE, code,
                      sizeof(page->i_iso6392_language_code), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, TTX_PAGE_COLUMN_TELETEXT_TYPE,
                     page->i_teletext_type);
    sqlite3_bind_int(stmt, TTX_PAGE_COLUMN_MAGAZINE_NUMBER,
                     page->i_teletext_magazine_number);
    sqlite3_bind_int(stmt, TTX_PAGE_COLUMN_PAGE_NUMBER,
                     page->i_teletext_page_number);
    sqlite3_step(stmt);
  }
}

static void export_subtitle_descriptor(sqlite3_stmt *stmt,
                                       dvbpsi_descriptor_t *dr,
                                       sqlite3_int64 es_rowid) {
  dvbpsi_subtitling_dr_t *subtitling = dvbpsi_DecodeSubtitlingDr(dr);
  for (uint8_t i = 0; i < subtitling->i_subtitles_number; ++i) {
    dvbpsi_subtitle_t *subtitle = subtitling->p_subtitle + i;
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, SUBTITLE_CONTENT_COLUMN_ELEM_STREAM_ROWID,
                       es_rowid);
    const char *code = (const char *)subtitle->i_iso6392_language_code;
    sqlite3_bind_text(stmt, SUBTITLE_CONTENT_COLUMN_LANGUAGE, code,
                      sizeof(subtitle->i_iso6392_language_code),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, SUBTITLE_CONTENT_COLUMN_SUBTITLE_TYPE,
                     subtitle->i_subtitling_type);
    sqlite3_bind_int(stmt, SUBTITLE_CONTENT_COLUMN_COMPOSITION_PAGE_ID,
                     subtitle->i_composition_page_id);
    sqlite3_bind_int(stmt, SUBTITLE_CONTENT_COLUMN_ANCILLARY_PAGE_ID,
                     subtitle->i_ancillary_page_id);
    sqlite3_step(stmt);
  }
}

static void export_pmt_es_descriptors(db_export *exp, sqlite3_int64 es_rowid,
                                      dvbpsi_descriptor_t *dr) {
  while (dr) {
    switch (dr->i_tag) {
    case 0x0a:
      export_iso639_descriptor(exp->insert_stmts[DVBINDEX_TABLE_LANG_SPECS], dr,
                               es_rowid);
      break;

    /* descriptors 46h and 56h have exactly the same structure, as documented
     * in EN 300 468 V1.15.1. */
    case 0x46:
    case 0x56:
      export_teletext_descriptor(exp->insert_stmts[DVBINDEX_TABLE_TTX_PAGES],
                                 dr, es_rowid);
      break;

    case 0x59:
      export_subtitle_descriptor(
          exp->insert_stmts[DVBINDEX_TABLE_SUBTITLE_CONTENTS], dr, es_rowid);
      break;
    }

    dr = dr->p_next;
  }
}

static void export_pmt_es(db_export *exp, sqlite3_int64 pmt_rowid,
                          const dvbpsi_pmt_es_t *es) {
  sqlite3_stmt *stmt = exp->insert_stmts[DVBINDEX_TABLE_ELEM_STREAMS];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, ELEM_STREAM_COLUMN_PMT_ROWID, pmt_rowid);
  sqlite3_bind_int(stmt, ELEM_STREAM_COLUMN_TYPE, es->i_type);
  sqlite3_bind_int(stmt, ELEM_STREAM_COLUMN_PID, es->i_pid);
  sqlite3_step(stmt);
  sqlite3_int64 es_rowid = sqlite3_last_insert_rowid(exp->db);
  export_pmt_es_descriptors(exp, es_rowid, es->p_first_descriptor);
}

sqlite3_int64 db_export_pat(db_export *exp, sqlite3_int64 file_rowid,
                            const dvbpsi_pat_t *pat) {
  sqlite3_stmt *stmt = exp->insert_stmts[DVBINDEX_TABLE_PATS];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, PAT_COLUMN_FILE_ROWID, file_rowid);
  sqlite3_bind_int(stmt, PAT_COLUMN_TSID, pat->i_ts_id);
  sqlite3_bind_int(stmt, PAT_COLUMN_VERSION, pat->i_version);
  sqlite3_step(stmt);
  return sqlite3_last_insert_rowid(exp->db);
}

void db_export_pmt(db_export *exp, sqlite3_int64 pat_rowid,
                   const dvbpsi_pmt_t *pmt) {
  start_transaction(exp->db);
  sqlite3_stmt *stmt = exp->insert_stmts[DVBINDEX_TABLE_PMTS];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, PMT_COLUMN_PAT_ROWID, pat_rowid);
  sqlite3_bind_int(stmt, PMT_COLUMN_PROGRAM_NUMBER, pmt->i_program_number);
  sqlite3_bind_int(stmt, PMT_COLUMN_VERSION, pmt->i_version);
  sqlite3_bind_int(stmt, PMT_COLUMN_PCR_PID, pmt->i_pcr_pid);
  sqlite3_step(stmt);
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
        sqlite3_bind_text64(stmt, SERVICE_COLUMN_NAME, str, outlen, free,
                            SQLITE_UTF8);
        str = dvbstring_to_utf8(service_dr->i_service_provider_name,
                                service_dr->i_service_provider_name_length,
                                &outlen);
        sqlite3_bind_text64(stmt, SERVICE_COLUMN_PROVIDER_NAME, str, outlen,
                            free, SQLITE_UTF8);
      }
    } break;
    }

    sdt_dscr = sdt_dscr->p_next;
  }
}

static void export_sdt_service(db_export *exp, sqlite3_int64 sdt_rowid,
                               const dvbpsi_sdt_service_t *service) {
  sqlite3_stmt *stmt = exp->insert_stmts[DVBINDEX_TABLE_SERVICES];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, SERVICE_COLUMN_SDT_ROWID, sdt_rowid);
  sqlite3_bind_int(stmt, SERVICE_COLUMN_PROGRAM_NUMBER, service->i_service_id);
  sqlite3_bind_int(stmt, SERVICE_COLUMN_RUNNING_STATUS,
                   service->i_running_status);
  sqlite3_bind_int(stmt, SERVICE_COLUMN_SCRAMBLED, service->b_free_ca);
  sqlite3_bind_null(stmt, SERVICE_COLUMN_NAME);
  export_sdt_descriptors(stmt, service->p_first_descriptor);
  sqlite3_step(stmt);
}

void db_export_sdt(db_export *exp, sqlite3_int64 pat_rowid,
                   const dvbpsi_sdt_t *sdt) {
  sqlite3_stmt *stmt = exp->insert_stmts[DVBINDEX_TABLE_SDTS];
  start_transaction(exp->db);
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, SDT_COLUMN_PAT_ROWID, pat_rowid);
  sqlite3_bind_int(stmt, SDT_COLUMN_VERSION, sdt->i_version);
  sqlite3_bind_int(stmt, SDT_COLUMN_ORIGINAL_NETWORK_ID, sdt->i_network_id);
  sqlite3_step(stmt);
  sqlite3_int64 sdt_rowid = sqlite3_last_insert_rowid(exp->db);
  dvbpsi_sdt_service_t *service = sdt->p_first_service;
  while (service) {
    export_sdt_service(exp, sdt_rowid, service);
    service = service->p_next;
  }
  end_transaction(exp->db);
}

sqlite3_int64 db_export_file(db_export *exp, const char *path, off_t size) {
  sqlite3_stmt *stmt = exp->insert_stmts[DVBINDEX_TABLE_FILES];
  sqlite3_reset(stmt);
  sqlite3_bind_text(stmt, FILE_COLUMN_NAME, file_name_from_path(path), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, FILE_COLUMN_SIZE, size);
  sqlite3_step(stmt);
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

static void export_nit_descriptors(sqlite3_stmt *stmt,
                                   dvbpsi_descriptor_t *dr) {
  while (dr) {
    switch (dr->i_tag) {
    case 0x40: {
      dvbpsi_network_name_dr_t *netname = dvbpsi_DecodeNetworkNameDr(dr);
      if (netname) {
        size_t outlen;
        char *utf8 = dvbstring_to_utf8(netname->i_name_byte,
                                       netname->i_name_length, &outlen);
        sqlite3_bind_text(stmt, NETWORK_COLUMN_NETWORK_NAME, utf8, outlen,
                          free);
      }
      break;
    }
    }

    dr = dr->p_next;
  }
}

static void export_nit_ts_descriptors(db_export *exp,
                                      sqlite3_int64 nit_ts_rowid,
                                      dvbpsi_descriptor_t *dr) {
  while (dr) {
    switch (dr->i_tag) {
    case 0x41: {
      dvbpsi_service_list_dr_t *slist = dvbpsi_DecodeServiceListDr(dr);
      if (slist) {
        sqlite3_stmt *stmt = exp->insert_stmts[DVBINDEX_TABLE_TS_SERVICES];
        for (unsigned int i = 0; i < slist->i_service_count; ++i) {
          sqlite3_reset(stmt);
          sqlite3_bind_int64(stmt, TS_SERVICE_COLUMN_TS_ROWID, nit_ts_rowid);
          sqlite3_bind_int(stmt, TS_SERVICE_COLUMN_SERVICE_ID,
                           slist->i_service[i].i_service_id);
          sqlite3_bind_int(stmt, TS_SERVICE_COLUMN_SERVICE_TYPE,
                           slist->i_service[i].i_service_type);
          sqlite3_step(stmt);
        }
      }
    }
    }

    dr = dr->p_next;
  }
}

static void export_nit_transport_streams(db_export *exp,
                                         sqlite3_int64 nit_rowid,
                                         dvbpsi_nit_ts_t *ts) {
  while (ts) {
    sqlite3_stmt *stmt = exp->insert_stmts[DVBINDEX_TABLE_TRANSPORT_STREAMS];
    sqlite3_reset(stmt);
    sqlite3_bind_int64(stmt, TRANSPORT_STREAM_COLUMN_NETWORK_ROWID, nit_rowid);
    sqlite3_bind_int(stmt, TRANSPORT_STREAM_COLUMN_TSID, ts->i_ts_id);
    sqlite3_bind_int(stmt, TRANSPORT_STREAM_COLUMN_ONID, ts->i_orig_network_id);
    sqlite3_step(stmt);
    sqlite3_int64 nit_ts_rowid = sqlite3_last_insert_rowid(exp->db);
    export_nit_ts_descriptors(exp, nit_ts_rowid, ts->p_first_descriptor);
    ts = ts->p_next;
  }
}

void db_export_nit(db_export *exp, sqlite3_int64 file_rowid,
                   const dvbpsi_nit_t *nit) {
  start_transaction(exp->db);
  sqlite3_stmt *stmt = exp->insert_stmts[DVBINDEX_TABLE_NETWORKS];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, NETWORK_COLUMN_FILE_ROWID, file_rowid);
  sqlite3_bind_int(stmt, NETWORK_COLUMN_NETWORK_ID, nit->i_network_id);
  export_nit_descriptors(stmt, nit->p_first_descriptor);
  sqlite3_step(stmt);
  sqlite3_int64 nit_rowid = sqlite3_last_insert_rowid(exp->db);
  export_nit_transport_streams(exp, nit_rowid, nit->p_first_ts);
  end_transaction(exp->db);
}
