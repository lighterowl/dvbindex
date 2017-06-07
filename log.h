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

#ifndef DVBINDEX_LOG_H
#define DVBINDEX_LOG_H

#include <stdarg.h>

typedef enum dvbindex_log_cat_ {
  DVBIDX_LOG_CAT_DVBINDEX,
  DVBIDX_LOG_CAT_FFMPEG,
  DVBIDX_LOG_CAT_DVBPSI,
  DVBIDX_LOG_CAT_SQLITE,
  DVBIDX_LOG_CAT__LAST
} dvbindex_log_cat;

typedef enum dvbindex_log_severity_ {
  DVBIDX_LOG_SEVERITY_CRITICAL,
  DVBIDX_LOG_SEVERITY_WARNING,
  DVBIDX_LOG_SEVERITY_INFO,
  DVBIDX_LOG_SEVERITY_DEBUG,
  DVBIDX_LOG_SEVERITY__LAST
} dvbindex_log_severity;

void dvbindex_vlog(dvbindex_log_cat cat, dvbindex_log_severity severity,
                   const char *fmt, va_list args);
void dvbindex_vlog_ctx(dvbindex_log_cat cat, dvbindex_log_severity severity,
                       void *ctx, const char *fmt, va_list args);
void dvbindex_log(dvbindex_log_cat cat, dvbindex_log_severity severity,
                  const char *fmt, ...);
void dvbindex_log_parse_severity(const char *string);

#endif
