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
#ifndef DVBINDEX_COLUMN_IDS_H
#define DVBINDEX_COLUMN_IDS_H

typedef enum vid_stream_col_id_ {
  VID_STREAM_COLUMN_FILE_ROWID = 1,
  VID_STREAM_COLUMN_PID,
  VID_STREAM_COLUMN_FMT,
  VID_STREAM_COLUMN_WIDTH,
  VID_STREAM_COLUMN_HEIGHT,
  VID_STREAM_COLUMN_FPS,
  VID_STREAM_COLUMN_BITRATE,
  VID_STREAM_COLUMN__LAST
} vid_stream_col_id;

typedef enum aud_stream_col_id_ {
  AUD_STREAM_COLUMN_FILE_ROWID = 1,
  AUD_STREAM_COLUMN_PID,
  AUD_STREAM_COLUMN_FMT,
  AUD_STREAM_COLUMN_CHANNELS,
  AUD_STREAM_COLUMN_SAMPLE_RATE,
  AUD_STREAM_COLUMN_BITRATE,
  AUD_STREAM_COLUMN__LAST
} aud_stream_col_id;

typedef enum pat_col_id_ {
  PAT_COLUMN_FILE_ROWID = 1,
  PAT_COLUMN_TSID,
  PAT_COLUMN_VERSION,
  PAT_COLUMN__LAST
} pat_col_id;

typedef enum pmt_col_id_ {
  PMT_COLUMN_PAT_ROWID = 1,
  PMT_COLUMN_PROGRAM_NUMBER,
  PMT_COLUMN_VERSION,
  PMT_COLUMN_PCR_PID,
  PMT_COLUMN__LAST
} pmt_col_id;

typedef enum elem_stream_col_id_ {
  ELEM_STREAM_COLUMN_PMT_ROWID = 1,
  ELEM_STREAM_COLUMN_TYPE,
  ELEM_STREAM_COLUMN_PID,
  ELEM_STREAM_COLUMN__LAST
} elem_stream_col_id;

typedef enum sdt_col_id {
  SDT_COLUMN_PAT_ROWID = 1,
  SDT_COLUMN_VERSION,
  SDT_COLUMN_ORIGINAL_NETWORK_ID,
  SDT_COLUMN__LAST
} sdt_col_id;

typedef enum service_col_id_ {
  SERVICE_COLUMN_SDT_ROWID = 1,
  SERVICE_COLUMN_PROGRAM_NUMBER,
  SERVICE_COLUMN_RUNNING_STATUS,
  SERVICE_COLUMN_SCRAMBLED,
  SERVICE_COLUMN_NAME,
  SERVICE_COLUMN_PROVIDER_NAME,
  SERVICE_COLUMN__LAST
} service_col_id;

typedef enum file_col_id_ {
  FILE_COLUMN_NAME = 1,
  FILE_COLUMN_SIZE,
  FILE_COLUMN__LAST
} file_col_id;

typedef enum lang_spec_col_id_ {
  LANG_SPEC_COLUMN_ELEM_STREAM_ROWID = 1,
  LANG_SPEC_COLUMN_LANGUAGE,
  LANG_SPEC_COLUMN_AUDIO_TYPE,
  LANG_SPEC_COLUMN__LAST
} lang_spec_col_id;

typedef enum ttx_stream_col_id_ {
  TTX_PAGE_COLUMN_ELEM_STREAM_ROWID = 1,
  TTX_PAGE_COLUMN_LANGUAGE,
  TTX_PAGE_COLUMN_TELETEXT_TYPE,
  TTX_PAGE_COLUMN_MAGAZINE_NUMBER,
  TTX_PAGE_COLUMN_PAGE_NUMBER,
  TTX_PAGE_COLUMN__LAST
} ttx_stream_col_id;

typedef enum subtitle_content_col_id_ {
  SUBTITLE_CONTENT_COLUMN_ELEM_STREAM_ROWID = 1,
  SUBTITLE_CONTENT_COLUMN_LANGUAGE,
  SUBTITLE_CONTENT_COLUMN_SUBTITLE_TYPE,
  SUBTITLE_CONTENT_COLUMN_COMPOSITION_PAGE_ID,
  SUBTITLE_CONTENT_COLUMN_ANCILLARY_PAGE_ID,
  SUBTITLE_CONTENT_COLUMN__LAST
} subtitle_content_col_id;

typedef enum network_col_id_ {
  NETWORK_COLUMN_FILE_ROWID = 1,
  NETWORK_COLUMN_NETWORK_ID,
  NETWORK_COLUMN_NETWORK_NAME,
  NETWORK_COLUMN__LAST
} network_col_id;

typedef enum transport_stream_col_id_ {
  TRANSPORT_STREAM_COLUMN_NETWORK_ROWID = 1,
  TRANSPORT_STREAM_COLUMN_TSID,
  TRANSPORT_STREAM_COLUMN_ONID,
  TRANSPORT_STREAM_COLUMN__LAST
} transport_stream_col_id;

typedef enum ts_service_col_id_ {
  TS_SERVICE_COLUMN_TS_ROWID = 1,
  TS_SERVICE_COLUMN_SERVICE_ID,
  TS_SERVICE_COLUMN_SERVICE_TYPE,
  TS_SERVICE_COLUMN__LAST
} ts_service_col_id;

#endif
