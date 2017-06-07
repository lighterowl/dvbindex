#include "log.h"
#include <stdio.h>

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

void dvbindex_log_set_severity(dvbindex_log_cat cat,
                               dvbindex_log_severity severity) {
  max_severity[cat] = severity;
}
