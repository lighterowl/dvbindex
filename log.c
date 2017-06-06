#include "log.h"
#include <stdio.h>

static dvbindex_log_severity max_severity[DVBIDX_LOG_CAT__LAST];
static const char *const cat_names[DVBIDX_LOG_CAT__LAST] = {
    "dvbindex", "ffmpeg", "dvbpsi", "sqlite"};

void dvbindex_vlog(dvbindex_log_cat cat, dvbindex_log_severity severity,
                   const char *fmt, va_list args) {
  if (severity <= max_severity[cat]) {
    fprintf(stderr, "[%s] ", cat_names[cat]);
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

void dvbindex_log_set_severity(dvbindex_log_cat cat,
                               dvbindex_log_severity severity) {
  max_severity[cat] = severity;
}
