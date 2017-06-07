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

#include "log.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define min(x, y) (((x) < (y)) ? (x) : (y))

static dvbindex_log_severity max_severity[DVBIDX_LOG_CAT__LAST] = {
    /* dvbindex */ DVBIDX_LOG_SEVERITY_INFO,
    /* ffmpeg */ DVBIDX_LOG_SEVERITY_CRITICAL,
    /* dvbpsi */ DVBIDX_LOG_SEVERITY_INFO,
    /* sqlite */ DVBIDX_LOG_SEVERITY_WARNING};
static const char *const cat_names[DVBIDX_LOG_CAT__LAST] = {
    "dvbindex", "ffmpeg", "dvbpsi", "sqlite"};
static const char *const sever_names[DVBIDX_LOG_SEVERITY__LAST] = {
    "CRI", "WRN", "INF", "DBG"};

void dvbindex_vlog(dvbindex_log_cat cat, dvbindex_log_severity severity,
                   const char *fmt, va_list args) {
  if (severity <= max_severity[cat]) {
    fprintf(stderr, "[%s] [%s] ", cat_names[cat], sever_names[severity]);
    vfprintf(stderr, fmt, args);
  }
}

void dvbindex_log(dvbindex_log_cat cat, dvbindex_log_severity severity,
                  const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  dvbindex_vlog(cat, severity, fmt, args);
  va_end(args);
}

static const char *strnpchr(const char *start, const char *end, int ch) {
  while (start != end) {
    if (*start == ch) {
      return start;
    }
    ++start;
  }
  return 0;
}

static void parse_component_severity_token(const char *start, const char *end) {
  const char *colon = strnpchr(start, end, ':');
  const char *sever_start = start;
  dvbindex_log_cat cat = DVBIDX_LOG_CAT__LAST;
  if (colon) {
    size_t param_cat_name_len = end - start;
    for (size_t i = 0; i < ARRAY_SIZE(cat_names); ++i) {
      size_t cat_name_len = strlen(cat_names[i]);
      if (strncmp(cat_names[i], start, min(cat_name_len, param_cat_name_len)) ==
          0) {
        cat = (dvbindex_log_cat)i;
        break;
      }
    }
    sever_start = colon + 1;
  }
  dvbindex_log_severity sever = strtol(sever_start, 0, 10);
  if (cat == DVBIDX_LOG_CAT__LAST) {
    for (cat = 0; cat < DVBIDX_LOG_CAT__LAST; ++cat) {
      max_severity[cat] = sever;
    }
  } else {
    max_severity[cat] = sever;
  }
}

void dvbindex_log_parse_severity(const char *start) {
  const char *end;
  while ((end = strchr(start, ',')) != 0) {
    parse_component_severity_token(start, end);
    start = end + 1;
  }
  parse_component_severity_token(start, strchr(start, '\0'));
}
