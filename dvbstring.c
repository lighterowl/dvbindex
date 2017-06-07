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

#include "dvbstring.h"
#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static const char *iso8859_table[] = {"ISO88591",
                                      "ISO88592",
                                      "ISO88593",
                                      "ISO88594",
                                      "ISO88595",
                                      "ISO88596",
                                      "ISO88597",
                                      "ISO88598",
                                      "ISO88599",
                                      "ISO885910",
                                      "ISO885911",
                                      0 /* reserved */
                                      ,
                                      "ISO885913",
                                      "ISO885914",
                                      "ISO885915"};

STATIC_ASSERT(ARRAY_SIZE(iso8859_table) == 15, iso8859_table_invalid_size);

#define ISO_6937 "ISO6937"
#define KSX_1001 "KSX1001-2004"

static const char *get_extended_8859_encoding(const uint8_t *str, size_t *len,
                                              const uint8_t **start) {
  if (*len < 3) {
    /* invalid : EN 300 468 specifies that 0x10 must be followed by two extra
     * bytes. */
    return 0;
  }
  if (str[1] != 0) {
    /* reserved */
    return 0;
  }
  if (str[2] >= 0x01 && str[2] <= 0x0F) {
    *len -= 3;
    *start = str + 3;
    return iso8859_table[str[2] - 1];
  }
  /* all other values are reserved. */
  return 0;
}

static const char *get_encoding(const uint8_t *str, size_t *len,
                                const uint8_t **start) {
  if (str[0] >= 0x20) {
    *start = str;
    return ISO_6937;
  }
  if (str[0] >= 0x01 && str[0] <= 0x0B) {
    *len -= 1;
    *start = str + 1;
    return iso8859_table[str[0] + 3];
  }
  if (str[0] == 0x10) {
    return get_extended_8859_encoding(str, len, start);
  }

  *len -= 1;
  *start = str + 1;

  switch (str[0]) {
  /* 0x0C - 0x0F : reserved */
  case 0x11:
    /* EN 300 468 V1.15.1 specifies this as the "Basic Multilingual Plane" of
     * ISO/IEC 10646. 10646 contains both UCS-2 and UTF-16, but UTF-16 can
     * encode code points beyond the BMP, so I guess UCS-2 should be used here.
     */
    return "UCS2";
  case 0x12:
    return KSX_1001;
  case 0x13:
    return "GB2312";
  case 0x14:
    return "BIG5";
  case 0x15:
    return "UTF-8";
  /* 0x16 - 0x1E : reserved */
  case 0x1F:
    /* private encoding specified by encoding_type_id. unsupported. */
    return 0;
  }
  return 0;
}

#include "ksx1001.c"

static int ksx1001_code_point_cmp(const void *a, const void *b) {
  const uint16_t(*pA)[2] = a;
  const uint16_t(*pB)[2] = b;
  return (*pA)[0] - (*pB)[0];
}

static uint16_t ksx1001_get_code_point(uint16_t ksx_char) {
  uint16_t search[2] = {ksx_char, 0};
  uint16_t(*found)[2] =
      bsearch(&search, ksx1001_to_code_point, ARRAY_SIZE(ksx1001_to_code_point),
              sizeof(*ksx1001_to_code_point), ksx1001_code_point_cmp);
  return found ? (*found)[1] : 0;
}

static unsigned int code_point_utf8_length(uint16_t cp) {
  if (cp <= 0x7F) {
    return 1;
  }
  if (cp <= 0x07FF) {
    return 2;
  }
  return 3;
}

static void code_point_to_utf8(char *utf8, uint16_t cp) {
  unsigned int bytes = code_point_utf8_length(cp);
  switch (bytes) {
  case 1:
    *utf8 = (char)cp;
    break;

  case 2:
    *utf8 = (char)((cp >> 6) | 0xC0);
    *(utf8 + 1) = (char)((cp & 0x3F) | 0x80);
    break;

  case 3:
    *utf8 = (char)((cp >> 12) | 0xE0);
    *(utf8 + 1) = (char)(((cp >> 6) & 0x3F) | 0x80);
    *(utf8 + 2) = (char)((cp & 0x3F) | 0x80);
  }
}

static size_t ksx1001_to_utf8_iconv(iconv_t cd, char **inbuf,
                                    size_t *inbytesleft, char **outbuf,
                                    size_t *outbytesleft) {
  if (inbuf == 0 || inbytesleft == 0 || outbuf == 0 || outbytesleft == 0) {
    return 0;
  }

  while (*inbytesleft > 2) {
    uint16_t inchar;
    memcpy(&inchar, *inbuf, sizeof(inchar));
    uint16_t cp = ksx1001_get_code_point(inchar);
    if (cp == 0) {
      errno = EILSEQ;
      return (size_t)-1;
    }
    unsigned int needed_bytes = code_point_utf8_length(cp);
    if (needed_bytes > *outbytesleft) {
      errno = E2BIG;
      return (size_t)-1;
    }

    code_point_to_utf8(*outbuf, cp);

    *outbytesleft -= needed_bytes;
    *outbuf += needed_bytes;
    *inbytesleft -= 2;
    *inbuf += 2;
  }
  if (*inbytesleft == 1) {
    errno = EINVAL;
    return (size_t)-1;
  }
  return 0;
}

typedef size_t (*iconv_fn_ptr_t)(iconv_t, char **, size_t *, char **, size_t *);

typedef struct iconv_conv_state_ {
  iconv_fn_ptr_t iconv_fn;
  iconv_t cd;
  char *inpos;
  size_t inleft;
  char *out;
  char *outpos;
  size_t outsize;
  size_t outleft;
  int is_iso6937;
} iconv_conv_state;

#define ICONV_BUF_SIZE 4096

static int iconv_conv_state_init(iconv_conv_state *state, const uint8_t *start,
                                 size_t len, const char *encoding) {
  if (strcmp(encoding, KSX_1001) != 0) {
    state->iconv_fn = iconv;
    state->cd = iconv_open("UTF-8", encoding);
    if (state->cd == (iconv_t)-1) {
      return errno;
    }
  } else {
    state->iconv_fn = ksx1001_to_utf8_iconv;
    state->cd = (iconv_t)-1;
  }
  state->inpos = (char *)start; /* needed due to iconv's interface */
  state->inleft = len;
  state->out = malloc(ICONV_BUF_SIZE);
  state->outpos = state->out;
  state->outsize = state->outleft = ICONV_BUF_SIZE;
  state->is_iso6937 = strcmp(encoding, ISO_6937) == 0 ? 1 : 0;
  return 0;
}

static void enlarge_iconv_buf(iconv_conv_state *state) {
  state->outleft += ICONV_BUF_SIZE;
  state->outsize += ICONV_BUF_SIZE;
  ptrdiff_t outrelpos = state->outpos - state->out;
  state->out = realloc(state->out, state->outsize);
  state->outpos = state->out + outrelpos;
}

#undef ICONV_BUF_SIZE

static void fix_iso6937_euro_sign(iconv_conv_state *state) {
  const static unsigned char utf8_euro[] = {0xe2, 0x82, 0xac};
  const static size_t utf8_euro_len = sizeof(utf8_euro);
  while (state->outleft < utf8_euro_len) {
    enlarge_iconv_buf(state);
  }
  memcpy(state->outpos, utf8_euro, utf8_euro_len);
  state->inpos += 1;
  state->inleft -= 1;
  state->outpos += utf8_euro_len;
  state->outleft -= utf8_euro_len;
  /* reset state to restart conversion cleanly at next character. */
  iconv(state->cd, 0, 0, 0, 0);
}

static int handle_iconv_error(int iconv_errno, iconv_conv_state *state) {
  int rv = 0;
  if (iconv_errno == E2BIG) {
    enlarge_iconv_buf(state);
  } else if ((iconv_errno == EILSEQ || iconv_errno == EINVAL) &&
             state->is_iso6937 && (*(unsigned char *)state->inpos) == 0xA4) {
    /* DVB's incarnation of ISO 6937 uses 0xA4 for the Euro sign. it is not
     * supported by iconv, so we need to replace it ourselves. */
    fix_iso6937_euro_sign(state);
  } else {
    rv = 1;
  }
  return rv;
}

static void do_conv(iconv_conv_state *state) {
  while (state->inleft != 0) {
    size_t iconv_rv = state->iconv_fn(state->cd, &state->inpos, &state->inleft,
                                      &state->outpos, &state->outleft);
    if (iconv_rv == (size_t)-1 && handle_iconv_error(errno, state) != 0) {
      free(state->out);
      state->out = 0;
      return;
    }
  }
}

char *dvbstring_to_utf8(const uint8_t *str, size_t len, size_t *outlen) {
  if (len == 0 || str == 0) {
    return 0;
  }
  const uint8_t *start;
  const char *encoding = get_encoding(str, &len, &start);
  if (encoding == 0) {
    return 0;
  }

  iconv_conv_state state;
  if (iconv_conv_state_init(&state, start, len, encoding) != 0) {
    return 0;
  }

  do_conv(&state);
  if (state.cd != (iconv_t)-1) {
    iconv_close(state.cd);
  }
  *outlen = state.outpos - state.out;
  return state.out;
}
