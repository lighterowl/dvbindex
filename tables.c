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

#include "tables.h"
#include "column_ids.h"
#include "util.h"
#include <assert.h>
#include <sqlite3.h>

static const dvbindex_table_column_def vid_streams_coldefs[] = {
    {"file_rowid", "NOT NULL", SQLITE_INTEGER},
    {"pid", "NOT NULL", SQLITE_INTEGER},
    {"fmt", "", SQLITE_TEXT},
    {"width", "", SQLITE_INTEGER},
    {"height", "", SQLITE_INTEGER},
    {"fps", "", SQLITE_FLOAT},
    {"bitrate", "", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(vid_streams_coldefs) == VID_STREAM_COLUMN__LAST - 1,
              vid_streams_invalid_columns);

static const dvbindex_table_column_def aud_streams_coldefs[] = {
    {"file_rowid", "NOT NULL", SQLITE_INTEGER},
    {"pid", "NOT NULL", SQLITE_INTEGER},
    {"fmt", "", SQLITE_TEXT},
    {"channels", "", SQLITE_INTEGER},
    {"sample_rate", "", SQLITE_INTEGER},
    {"bitrate", "", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(aud_streams_coldefs) == AUD_STREAM_COLUMN__LAST - 1,
              aud_streams_invalid_columns);

static const dvbindex_table_column_def pats_coldefs[] = {
    {"file_rowid", "NOT NULL", SQLITE_INTEGER},
    {"tsid", "NOT NULL", SQLITE_INTEGER},
    {"version", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(pats_coldefs) == PAT_COLUMN__LAST - 1,
              pats_invalid_columns);

static const dvbindex_table_column_def pmts_coldefs[] = {
    {"pat_rowid", "NOT NULL", SQLITE_INTEGER},
    {"program_number", "NOT NULL", SQLITE_INTEGER},
    {"version", "NOT NULL", SQLITE_INTEGER},
    {"pcr_pid", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(pmts_coldefs) == PMT_COLUMN__LAST - 1,
              pmts_invalid_columns);

static const dvbindex_table_column_def elem_streams_coldefs[] = {
    {"pmt_rowid", "NOT NULL", SQLITE_INTEGER},
    {"stream_type", "NOT NULL", SQLITE_INTEGER},
    {"pid", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(elem_streams_coldefs) == ELEM_STREAM_COLUMN__LAST - 1,
              elem_streams_invalid_columns);

static const dvbindex_table_column_def sdts_coldefs[] = {
    {"pat_rowid", "NOT NULL", SQLITE_INTEGER},
    {"version", "NOT NULL", SQLITE_INTEGER},
    {"onid", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(sdts_coldefs) == SDT_COLUMN__LAST - 1,
              sdts_invalid_columns);

static const dvbindex_table_column_def services_coldefs[] = {
    {"sdt_rowid", "NOT NULL", SQLITE_INTEGER},
    {"program_number", "NOT NULL", SQLITE_INTEGER},
    {"running_status", "NOT NULL", SQLITE_INTEGER},
    {"scrambled", "NOT NULL", SQLITE_INTEGER},
    {"name", "", SQLITE_TEXT},
    {"provider_name", "", SQLITE_TEXT}};

STATIC_ASSERT(ARRAY_SIZE(services_coldefs) == SERVICE_COLUMN__LAST - 1,
              services_invalid_columns);

static const dvbindex_table_column_def files_coldefs[] = {
    {"name", "NOT NULL", SQLITE_TEXT}, {"size", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(files_coldefs) == FILE_COLUMN__LAST - 1,
              files_invalid_columns);

static const dvbindex_table_column_def lang_specs_coldefs[] = {
    {"elem_stream_rowid", "NOT NULL", SQLITE_INTEGER},
    {"language", "NOT NULL", SQLITE_TEXT},
    {"audio_type", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(lang_specs_coldefs) == LANG_SPEC_COLUMN__LAST - 1,
              lang_specs_invalid_columns);

static const dvbindex_table_column_def ttx_pages_coldefs[] = {
    {"elem_stream_rowid", "NOT NULL", SQLITE_INTEGER},
    {"language", "NOT NULL", SQLITE_TEXT},
    {"teletext_type", "NOT NULL", SQLITE_INTEGER},
    {"magazine_number", "NOT NULL", SQLITE_INTEGER},
    {"page_number", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(ttx_pages_coldefs) == TTX_PAGE_COLUMN__LAST - 1,
              ttx_pages_invalid_columns);

static const dvbindex_table_column_def subtitle_contents_coldefs[] = {
    {"elem_stream_rowid", "NOT NULL", SQLITE_INTEGER},
    {"language", "NOT NULL", SQLITE_TEXT},
    {"subtitling_type", "NOT NULL", SQLITE_INTEGER},
    {"composition_page_id", "NOT NULL", SQLITE_INTEGER},
    {"ancillary_page_id", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(subtitle_contents_coldefs) ==
                  SUBTITLE_CONTENT_COLUMN__LAST - 1,
              subtitle_contents_invalid_columns);

static const dvbindex_table_column_def networks_coldefs[] = {
    {"file_rowid", "NOT NULL", SQLITE_INTEGER},
    {"network_id", "NOT NULL", SQLITE_INTEGER},
    {"network_name", "", SQLITE_TEXT}};

STATIC_ASSERT(ARRAY_SIZE(networks_coldefs) == NETWORK_COLUMN__LAST - 1,
              networks_invalid_coldefs);

static const dvbindex_table_column_def transport_streams_coldefs[] = {
    {"network_rowid", "NOT NULL", SQLITE_INTEGER},
    {"tsid", "NOT NULL", SQLITE_INTEGER},
    {"onid", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(transport_streams_coldefs) ==
                  TRANSPORT_STREAM_COLUMN__LAST - 1,
              transport_streams_invalid_coldefs);

static const dvbindex_table_column_def ts_services_coldefs[] = {
    {"ts_rowid", "NOT NULL", SQLITE_INTEGER},
    {"service_id", "NOT NULL", SQLITE_INTEGER},
    {"service_type", "NOT NULL", SQLITE_INTEGER}};

STATIC_ASSERT(ARRAY_SIZE(ts_services_coldefs) == TS_SERVICE_COLUMN__LAST - 1,
              ts_services_invalid_coldefs);

/* clang-format off */

#define DEFINE_TABLE(x) \
  { #x, x##_coldefs, ARRAY_SIZE(x##_coldefs) }

/* clang-format on */

const dvbindex_table_def *table_get_def(dvbindex_table t) {
  static const dvbindex_table_def tables[] = {DEFINE_TABLE(aud_streams),
                                              DEFINE_TABLE(vid_streams),
                                              DEFINE_TABLE(pats),
                                              DEFINE_TABLE(pmts),
                                              DEFINE_TABLE(elem_streams),
                                              DEFINE_TABLE(sdts),
                                              DEFINE_TABLE(services),
                                              DEFINE_TABLE(files),
                                              DEFINE_TABLE(lang_specs),
                                              DEFINE_TABLE(ttx_pages),
                                              DEFINE_TABLE(subtitle_contents),
                                              DEFINE_TABLE(networks),
                                              DEFINE_TABLE(transport_streams),
                                              DEFINE_TABLE(ts_services)};
  STATIC_ASSERT(ARRAY_SIZE(tables) == DVBINDEX_TABLE__LAST,
                not_all_tables_defined);
  assert(t < DVBINDEX_TABLE__LAST);
  return tables + t;
}
