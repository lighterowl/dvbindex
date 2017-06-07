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

#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500

#include "read.h"
#include "export.h"
#include "log.h"
#include "util.h"
#include "vec.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include <dvbpsi/descriptor.h>
#include <dvbpsi/dvbpsi.h>

#include <dvbpsi/demux.h>
#include <dvbpsi/pat.h>
#include <dvbpsi/pmt.h>
#include <dvbpsi/sdt.h>

#include <arpa/inet.h> /* ntohs */
#include <libavformat/avformat.h>

#include <ftw.h>

typedef dvbpsi_t *dvbpsi_t_p;
VEC_DEFINE(dvbpsi_t_p)

typedef dvbpsi_pmt_t *dvbpsi_pmt_t_p;
VEC_DEFINE(dvbpsi_pmt_t_p)

#define TS_PACKET_SIZE 188
#define BUF_SIZE 4096

typedef void (*dvbpsi_detach_fn)(dvbpsi_t *p_dvbpsi);
typedef void (*dvbpsi_detach_fn_w_tid)(dvbpsi_t *p_dvbpsi, uint8_t i_table_id,
                                       uint16_t i_extension);

typedef enum psi_monitor_type_ {
  PSI_MONITOR_PAT,
  PSI_MONITOR_PMT,
  PSI_MONITOR_SDT
} psi_monitor_type;

typedef struct psi_monitor_ {
  dvbpsi_t *handle;
  union {
    dvbpsi_detach_fn d;
    dvbpsi_detach_fn_w_tid d_tid;
  } detach;
  psi_monitor_type type;
  uint16_t pid;
  uint16_t extension;
  uint8_t table_id;
} psi_monitor;
VEC_DEFINE(psi_monitor)

static dvbindex_log_severity
dvbpsi_msg_level_to_dvbindex_log_severity(dvbpsi_msg_level_t level) {
  switch (level) {
  case DVBPSI_MSG_ERROR:
    return DVBIDX_LOG_SEVERITY_CRITICAL;
  case DVBPSI_MSG_WARN:
    return DVBIDX_LOG_SEVERITY_WARNING;
  case DVBPSI_MSG_DEBUG:
    return DVBIDX_LOG_SEVERITY_DEBUG;
  case DVBPSI_MSG_NONE:
    assert(0 && "DVBPSI_MSG_NONE in dvbpsi log callback");
  }
  return DVBIDX_LOG_SEVERITY__LAST;
}

static void dvbindex_log_dvbpsi_cbk(dvbpsi_t *handle,
                                    const dvbpsi_msg_level_t level,
                                    const char *msg) {
  (void)handle; /* TODO : print the handle as well */
  dvbindex_log(DVBIDX_LOG_CAT_DVBPSI,
               dvbpsi_msg_level_to_dvbindex_log_severity(level), "%s\n", msg);
}

static dvbpsi_t *create_dvbpsi_handle(void) {
  return dvbpsi_new(dvbindex_log_dvbpsi_cbk, DVBPSI_MSG_DEBUG);
}

static void psi_monitor_simple_detach_init(psi_monitor *mon,
                                           dvbpsi_detach_fn detach) {
  mon->handle = create_dvbpsi_handle();
  mon->detach.d = detach;
}

static void psi_monitor_ext_detach_init(psi_monitor *mon,
                                        dvbpsi_detach_fn_w_tid detach,
                                        uint8_t table_id, uint16_t extension) {
  mon->handle = create_dvbpsi_handle();
  mon->detach.d_tid = detach;
  mon->table_id = table_id;
  mon->extension = extension;
}

static int psi_monitor_type_is_extended_detach(psi_monitor_type t) {
  switch (t) {
  case PSI_MONITOR_PAT:
  case PSI_MONITOR_PMT:
    return 0;
  case PSI_MONITOR_SDT:
    return 1;
  }
  assert(0 && "invalid psi_monitor_type");
  return -1;
}

static void psi_monitor_destroy(psi_monitor *mon) {
  if (psi_monitor_type_is_extended_detach(mon->type)) {
    mon->detach.d_tid(mon->handle, mon->table_id, mon->extension);
    dvbpsi_DetachDemux(mon->handle);
  } else {
    mon->detach.d(mon->handle);
  }
  dvbpsi_delete(mon->handle);
}

static void pat_monitor_init(psi_monitor *mon) {
  psi_monitor_simple_detach_init(mon, dvbpsi_pat_detach);
  mon->pid = 0;
  mon->type = PSI_MONITOR_PAT;
}

static void pmt_monitor_init(psi_monitor *mon, uint16_t pid) {
  psi_monitor_simple_detach_init(mon, dvbpsi_pmt_detach);
  mon->pid = pid;
  mon->type = PSI_MONITOR_PMT;
}

#define SDT_PID 0x11
#define SDT_CURRENT_TABLE_ID 0x42

static void sdt_monitor_init(psi_monitor *mon, uint8_t table_id,
                             uint16_t tsid) {
  psi_monitor_ext_detach_init(mon, dvbpsi_sdt_detach, table_id, tsid);
  mon->pid = SDT_PID;
  mon->type = PSI_MONITOR_SDT;
}

typedef dvbpsi_sdt_t *dvbpsi_sdt_t_p;
VEC_DEFINE(dvbpsi_sdt_t_p)

struct ts_file_read_ctx_;

typedef struct {
  db_export *db;
  const struct ts_file_read_ctx_ *file_ctx;
  sqlite3_int64 file_rowid;
  vec_psi_monitor psi_monitors;
  sqlite3_int64 pat_rowid;
  dvbpsi_pat_t *current_pat;
  vec_dvbpsi_pmt_t_p current_pmts;
  vec_dvbpsi_sdt_t_p current_sdts;
  int has_file_rowid;
} psi_parse_state;

typedef struct dvbpsi_read_state_ {
  uint8_t buf[TS_PACKET_SIZE];
  size_t buf_fill;
  off_t last_pos;
} dvbpsi_read_state;

typedef struct ts_file_read_ctx_ {
  FILE *file;
  const char *file_name;
  off_t file_size;
  psi_parse_state dvbpsi_parse;
  dvbpsi_read_state dvbpsi_state;
} ts_file_read_ctx;

static int pat_is_same(const dvbpsi_pat_t *p1, const dvbpsi_pat_t *p2) {
  return p1->b_current_next == p2->b_current_next &&
         p1->i_ts_id == p2->i_ts_id && p1->i_version == p2->i_version;
}

static void psi_destroy_pmt(psi_parse_state *handles) {
  vec_psi_monitor new_monitors;
  vec_psi_monitor_init(&new_monitors);
  for (size_t i = 0; i < handles->psi_monitors.size; ++i) {
    psi_monitor *p = &handles->psi_monitors.data[i];
    if (p->type == PSI_MONITOR_PMT) {
      psi_monitor_destroy(p);
    } else {
      psi_monitor *np = vec_psi_monitor_write(&new_monitors);
      *np = *p;
    }
  }
  vec_psi_monitor_destroy(&handles->psi_monitors);
  handles->psi_monitors = new_monitors;
}

static void psi_destroy_pmt_data(psi_parse_state *handles) {
  for (size_t i = 0; i < handles->current_pmts.size; ++i) {
    dvbpsi_pmt_t *p = handles->current_pmts.data[i];
    dvbpsi_pmt_delete(p);
  }
}

static int psi_seek_sdt_for_onid(vec_dvbpsi_sdt_t_p *sdts,
                                 uint16_t network_id) {
  for (size_t i = 0; i < sdts->size; ++i) {
    if (sdts->data[i]->i_network_id == network_id)
      return i;
  }
  return -1;
}

static int should_discard_sdt(const dvbpsi_sdt_t *p1, const dvbpsi_sdt_t *p2) {
  return p1->i_version == p2->i_version &&
         p1->b_current_next == p2->b_current_next;
}

static void psi_sdt_cbk(void *p_cb_data, dvbpsi_sdt_t *p_new_sdt) {
  psi_parse_state *state = p_cb_data;
  int sdt_idx =
      psi_seek_sdt_for_onid(&state->current_sdts, p_new_sdt->i_network_id);
  if (sdt_idx != -1 &&
      should_discard_sdt(state->current_sdts.data[sdt_idx], p_new_sdt)) {
    dvbpsi_sdt_delete(p_new_sdt);
  } else {
    if (sdt_idx != -1) {
      state->current_sdts.data[sdt_idx] = p_new_sdt;
    } else {
      vec_dvbpsi_sdt_t_p_push(&state->current_sdts, p_new_sdt);
    }
    db_export_sdt(state->db, state->pat_rowid, p_new_sdt);
  }
}

static dvbpsi_pmt_t_p *get_program_pmt(vec_dvbpsi_pmt_t_p *pmts,
                                       uint16_t pgmno) {
  for (size_t i = 0; i < pmts->size; ++i) {
    if (pmts->data[i]->i_program_number == pgmno) {
      return &pmts->data[i];
    }
  }
  dvbpsi_pmt_t_p *rv = vec_dvbpsi_pmt_t_p_write(pmts);
  *rv = 0;
  return rv;
}

static int should_discard_pmt(const dvbpsi_pmt_t *curpmt,
                              const dvbpsi_pmt_t *newpmt) {
  return curpmt->i_version == newpmt->i_version &&
         curpmt->b_current_next == newpmt->b_current_next;
}

static void psi_pmt_cbk(void *p_cb_data, dvbpsi_pmt_t *p_new_pmt) {
  psi_parse_state *ctx = p_cb_data;
  dvbpsi_pmt_t_p *pmt =
      get_program_pmt(&ctx->current_pmts, p_new_pmt->i_program_number);

  if (*pmt && should_discard_pmt(*pmt, p_new_pmt)) {
    dvbpsi_pmt_delete(p_new_pmt);
  } else {
    if (*pmt) {
      dvbpsi_pmt_delete(*pmt);
    }
    *pmt = p_new_pmt;
    db_export_pmt(ctx->db, ctx->pat_rowid, p_new_pmt);
  }
}

static void psi_push_new_pmt(psi_parse_state *handles,
                             const struct dvbpsi_pat_program_s *program) {
  psi_monitor *p = vec_psi_monitor_write(&handles->psi_monitors);
  pmt_monitor_init(p, program->i_pid);
  dvbpsi_pmt_attach(p->handle, program->i_number, psi_pmt_cbk, handles);
}

static void psi_sdt_demux_cbk(dvbpsi_t *handle, uint8_t table_id, uint16_t tsid,
                              void *p_cb_data) {
  psi_parse_state *handles = p_cb_data;
  if (table_id == SDT_CURRENT_TABLE_ID &&
      tsid == handles->current_pat->i_ts_id) {
    /* dvbpsi will return an error if there's already a callback associated
     * with the same table_id/tsid combination, which suits us just fine. */
    dvbpsi_sdt_attach(handle, table_id, tsid, psi_sdt_cbk, handles);
  }
}

static void ensure_file_has_rowid(psi_parse_state *handles) {
  if (!handles->has_file_rowid) {
    handles->file_rowid =
        db_export_file(handles->db, handles->file_ctx->file_name,
                       handles->file_ctx->file_size);
    handles->has_file_rowid = 1;
  }
}

static void psi_new_pat_received(psi_parse_state *handles,
                                 dvbpsi_pat_t *new_pat) {
  struct dvbpsi_pat_program_s *program = new_pat->p_first_program;
  psi_destroy_pmt(handles);
  while (program) {
    psi_push_new_pmt(handles, program);
    program = program->p_next;
  }
  dvbpsi_pat_delete(handles->current_pat);
  ensure_file_has_rowid(handles);
  handles->current_pat = new_pat;
  handles->pat_rowid = db_export_pat(handles->db, handles->file_rowid, new_pat);
  psi_monitor *sdt_mon = vec_psi_monitor_write(&handles->psi_monitors);
  sdt_monitor_init(sdt_mon, SDT_CURRENT_TABLE_ID, new_pat->i_ts_id);
  dvbpsi_AttachDemux(sdt_mon->handle, psi_sdt_demux_cbk, handles);
}

static void psi_pat_cbk(void *p_cb_data, dvbpsi_pat_t *p_new_pat) {
  psi_parse_state *handles = p_cb_data;
  if (!handles->current_pat || !pat_is_same(handles->current_pat, p_new_pat)) {
    psi_new_pat_received(handles, p_new_pat);
  } else {
    dvbpsi_pat_delete(p_new_pat);
  }
}

static void psi_destroy_sdt_data(vec_dvbpsi_sdt_t_p *sdts) {
  for (size_t i = 0; i < sdts->size; ++i) {
    dvbpsi_sdt_delete(sdts->data[i]);
  }
}

static void psi_handle_vec_init(psi_parse_state *handles, db_export *db) {
  vec_psi_monitor_init(&handles->psi_monitors);
  psi_monitor *m = vec_psi_monitor_write(&handles->psi_monitors);
  pat_monitor_init(m);
  dvbpsi_pat_attach(m->handle, psi_pat_cbk, handles);
  handles->current_pat = 0;
  handles->db = db;
  handles->has_file_rowid = 0;
  vec_dvbpsi_pmt_t_p_init(&handles->current_pmts);
  vec_dvbpsi_sdt_t_p_init(&handles->current_sdts);
}

static void psi_handle_vec_destroy(psi_parse_state *handles) {
  if (handles->current_pat) {
    dvbpsi_pat_delete(handles->current_pat);
  }
  psi_destroy_pmt_data(handles);
  vec_dvbpsi_pmt_t_p_destroy(&handles->current_pmts);
  psi_destroy_sdt_data(&handles->current_sdts);
  vec_dvbpsi_sdt_t_p_destroy(&handles->current_sdts);
  for (size_t i = 0; i < handles->psi_monitors.size; ++i) {
    psi_monitor_destroy(&handles->psi_monitors.data[i]);
  }
  vec_psi_monitor_destroy(&handles->psi_monitors);
}

static uint16_t ts_extract_pid(const uint8_t *buf) {
  uint16_t rv;
  memcpy(&rv, buf + 1, sizeof(rv));
  return htons(rv) & 0x1fff;
}

static void psi_handle_vec_push_packet(psi_parse_state *handles, uint8_t *buf) {
  for (size_t i = 0; i < handles->psi_monitors.size; ++i) {
    psi_monitor *pm = &handles->psi_monitors.data[i];
    if (buf[0] == 0x47 && ts_extract_pid(buf) == pm->pid) {
      dvbpsi_packet_push(pm->handle, buf);
    }
  }
}

static int ts_file_read_ctx_init(ts_file_read_ctx *ctx, const char *filename,
                                 db_export *db) {
  FILE *f = fopen(filename, "rb");
  if (!f) {
    return errno;
  }
  ctx->file = f;
  ctx->file_name = filename;
  ctx->dvbpsi_state.buf_fill = 0;
  ctx->dvbpsi_state.last_pos = 0;
  fseeko(f, 0, SEEK_END);
  ctx->file_size = ftello(f);
  fseeko(f, 0, SEEK_SET);
  psi_handle_vec_init(&ctx->dvbpsi_parse, db);
  ctx->dvbpsi_parse.file_ctx = ctx;
  return 0;
}

static void ts_file_read_ctx_destroy(ts_file_read_ctx *ctx) {
  psi_handle_vec_destroy(&ctx->dvbpsi_parse);
  fclose(ctx->file);
}

static AVInputFormat *mpegts_format;

static dvbindex_log_severity ffmpeg_to_dvbindex_severity(int severity) {
  if (severity <= AV_LOG_FATAL) {
    return DVBIDX_LOG_SEVERITY_CRITICAL;
  }
  if (severity <= AV_LOG_WARNING) {
    return DVBIDX_LOG_SEVERITY_WARNING;
  }
  if (severity == AV_LOG_INFO) {
    return DVBIDX_LOG_SEVERITY_INFO;
  }
  if (severity >= AV_LOG_VERBOSE) {
    return DVBIDX_LOG_SEVERITY_DEBUG;
  }
  assert(0);
  return DVBIDX_LOG_SEVERITY__LAST;
}

static void ffmpeg_log_callback(void *p, int severity, const char *fmt,
                                va_list args) {
  (void)p;
  dvbindex_vlog(DVBIDX_LOG_CAT_FFMPEG, ffmpeg_to_dvbindex_severity(severity),
                fmt, args);
}

int ffmpeg_init(void) {
  /* register codecs and formats and other lavf/lavc components*/
  av_register_all();

  av_log_set_callback(ffmpeg_log_callback);

  /* this saves an av_find_input_format call when doing open_input */
  mpegts_format = av_find_input_format("mpegts");
  return mpegts_format ? 0 : 1;
}

static void push_to_dvbpsi(ts_file_read_ctx *ctx, uint8_t *buf, size_t size) {
  if (ctx->dvbpsi_state.buf_fill != 0) {
    /* incomplete packet leftover from previous call. */
    const size_t needed = TS_PACKET_SIZE - ctx->dvbpsi_state.buf_fill;
    if (size >= needed) {
      memcpy(ctx->dvbpsi_state.buf + ctx->dvbpsi_state.buf_fill, buf, needed);
      size -= needed;
      buf += needed;
      ctx->dvbpsi_state.buf_fill = 0;
      psi_handle_vec_push_packet(&ctx->dvbpsi_parse, ctx->dvbpsi_state.buf);
    }
  }

  /* submit as much as possible */
  while (size >= TS_PACKET_SIZE) {
    psi_handle_vec_push_packet(&ctx->dvbpsi_parse, buf);
    buf += TS_PACKET_SIZE;
    size -= TS_PACKET_SIZE;
  }

  /* save for next call if an incomplete packet is available. */
  if (size != 0) {
    memcpy(ctx->dvbpsi_state.buf, buf, size);
    ctx->dvbpsi_state.buf_fill = size;
  }
}

static int read_packet(void *opaque, uint8_t *buf, int buf_size) {
  /* really a wrapper for fread() which also synchronizes dvbpsi decoders. */
  ts_file_read_ctx *ctx = opaque;
  FILE *io = ctx->file;
  size_t readsize = fread(buf, 1, (size_t)buf_size, io);

  /* any packets submitted to dvbpsi must be only submitted sequentially. */
  off_t newpos = ftello(ctx->file);
  if (newpos > ctx->dvbpsi_state.last_pos) {
    push_to_dvbpsi(ctx, buf, readsize);
    ctx->dvbpsi_state.last_pos = newpos;
  }

  return (int)readsize;
}

static off_t seek_destination(off_t end, off_t cur, int64_t offset,
                              int whence) {
  switch (whence) {
  case SEEK_CUR:
    return cur + offset;

  case SEEK_SET:
    return offset;

  case SEEK_END:
    return end + offset;
  }

  assert(0);
  return -1;
}

static void feed_dvbpsi_while_seeking(ts_file_read_ctx *ctx, off_t cur,
                                      off_t dst) {
  /* called when forwarding the file into areas not yet read by dvbpsi. make
   * sure to read all the data in between the current position and the seek
   * destination, as all data sent to dvbpsi must be delivered in file
   * order. */
  uint8_t buf[BUF_SIZE];
  assert(dst > cur);
  size_t to_read = (size_t)(dst - cur);
  while (to_read) {
    size_t readsize = fread(buf, 1, FFMIN(BUF_SIZE, to_read), ctx->file);
    push_to_dvbpsi(ctx, buf, readsize);
    to_read -= readsize;
  }

  ctx->dvbpsi_state.last_pos += to_read;
}

static int64_t seek_packet(void *opaque, int64_t offset, int whence) {
  /* not specifying a seek function results in ffmpeg not being able to estimate
   * the bitrate and length of the file. that's probably because it does not
   * know the file's size, which is delivered via AVSEEK_SIZE. */
  ts_file_read_ctx *ctx = opaque;
  off_t cur = ftello(ctx->file);
  switch (whence) {
  case SEEK_CUR:
  case SEEK_SET:
  case SEEK_END: {
    off_t dst = seek_destination(ctx->file_size, cur, offset, whence);
    if (dst > ctx->dvbpsi_state.last_pos && dst > cur) {
      feed_dvbpsi_while_seeking(ctx, cur, dst);
    }
    fseeko(ctx->file, offset, whence);
    return offset;
  }

  case AVSEEK_SIZE:
    return ctx->file_size;

  default:
    return -1;
  }
}

static int read_ts_file(db_export *db, const char *filename) {
  ts_file_read_ctx ctx;
  int ret = ts_file_read_ctx_init(&ctx, filename, db);
  if (ret != 0) {
    return AVERROR(ret);
  }

  if (db_has_file(db, filename, ctx.file_size)) {
    dvbindex_log(DVBIDX_LOG_CAT_DVBINDEX, DVBIDX_LOG_SEVERITY_INFO,
                 "%s [%lld] already in database, skipping\n",
                 file_name_from_path(filename), (long long int)ctx.file_size);
    return 0;
  }

  /* ffmpeg is used as the main reading driver of the files that we read. dvbpsi
   * is invoked indirectly via the callbacks invoked from within ffmpeg, and
   * makes sure that the PSI decoders receive the same data that ffmpeg does. */
  AVFormatContext *fmt_ctx;
  AVIOContext *avio_ctx;

  if (!(fmt_ctx = avformat_alloc_context())) {
    return AVERROR(ENOMEM);
  }

  uint8_t *avio_ctx_buffer = av_malloc(BUF_SIZE);
  if (!avio_ctx_buffer) {
    ret = AVERROR(ENOMEM);
    goto beach;
  }

  avio_ctx = avio_alloc_context(avio_ctx_buffer, BUF_SIZE, 0, &ctx, read_packet,
                                0, seek_packet);
  if (!avio_ctx) {
    ret = AVERROR(ENOMEM);
    goto beach;
  }
  fmt_ctx->pb = avio_ctx;

  /* restrict the possible input formats to mpegts only. */
  ret = avformat_open_input(&fmt_ctx, 0, mpegts_format, 0);
  if (ret < 0) {
    goto beach2;
  }

  /* this calls our own I/O callbacks. */
  ret = avformat_find_stream_info(fmt_ctx, 0);
  if (ret < 0) {
    goto beach2;
  }

  /* ffmpeg is not really required to read the file until the end, since it can
   * jump over parts it doesn't really care about. ensure that all the PSI data
   * is submitted, though. */
  {
    fseeko(ctx.file, ctx.dvbpsi_state.last_pos, SEEK_SET);
    size_t readsize;
    uint8_t buf[BUF_SIZE];
    do {
      readsize = fread(buf, 1, BUF_SIZE, ctx.file);
      push_to_dvbpsi(&ctx, buf, readsize);
    } while (readsize);
  }

  /* it is possible that we got here without a PAT, which means that the file
   * won't have a database rowid. but ffmpeg might've registered some streams
   * even without a PAT, and we need a valid rowid to insert streams. */
  ensure_file_has_rowid(&ctx.dvbpsi_parse);
  db_export_av_streams(db, ctx.dvbpsi_parse.file_rowid, fmt_ctx->nb_streams,
                       fmt_ctx->streams);

  dvbindex_log(DVBIDX_LOG_CAT_DVBINDEX, DVBIDX_LOG_SEVERITY_INFO, "Saved %s\n",
               file_name_from_path(filename));

  ret = 0;

beach2:
  /* the internal buffer could have changed, and be != avio_ctx_buffer */
  av_freep(&avio_ctx->buffer);
  av_freep(&avio_ctx);

beach:
  avformat_close_input(&fmt_ctx);
  ts_file_read_ctx_destroy(&ctx);

  return ret;
}

/* no other way to pass this to the nftw() callback, sadly. */
static db_export *g_db;

static int nftw_cbk(const char *fpath, const struct stat *sb, int typeflag,
                    struct FTW *ftwbuf) {
  if (typeflag == FTW_F) {
    int rv = read_ts_file(g_db, fpath);
    const char *name = file_name_from_path(fpath);
    switch (rv) {
    case 0:
      break;

    case AVERROR(ENOMEM):
      /* don't process any more files, don't print, try to exit cleanly. */
      return ENOMEM;

    case AVERROR_EOF:
      dvbindex_log(DVBIDX_LOG_CAT_DVBINDEX, DVBIDX_LOG_SEVERITY_INFO,
                   "%s does not look like a MPEG-TS\n", name);
      break;

    default:
      dvbindex_log(DVBIDX_LOG_CAT_DVBINDEX, DVBIDX_LOG_SEVERITY_CRITICAL,
                   "Error while reading %s : %s\n", name, av_err2str(rv));
    }
  }
  return 0;
}

int read_path(db_export *db, const char *path) {
  g_db = db;
  /* 20 is taken from nftw's manpage. */
  return nftw(path, nftw_cbk, 20, FTW_PHYS);
}
