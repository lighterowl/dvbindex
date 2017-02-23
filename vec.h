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

#ifndef DVBINDEX_VEC_H
#define DVBINDEX_VEC_H

/* clang-format off */

#include <stddef.h>
#include <stdlib.h>

#define VEC_INITIAL_CAP 32

#define VEC_RESIZE_IF_NEEDED(v) do {\
    if (v->size == v->cap) { \
      v->cap *= 2; \
      v->data = \
          realloc(v->data, v->cap * sizeof(v->data[0])); \
    } \
  } while(0)

#define VEC_DEFINE_PUSH(Type) \
static inline void vec_##Type##_push(vec_##Type *v, Type e) { \
  VEC_RESIZE_IF_NEEDED(v); \
  v->data[v->size++] = e; \
}

#define VEC_DEFINE_INIT(Type) \
static inline void vec_##Type##_init(vec_##Type *v) { \
  v->data = malloc(VEC_INITIAL_CAP * sizeof(v->data[0])); \
  v->size = 0; \
  v->cap = VEC_INITIAL_CAP; \
}

#define VEC_DEFINE_DESTROY(Type) \
static inline void vec_##Type##_destroy(vec_##Type *v) { \
  free(v->data); \
  v->data = 0; \
  v->size = v->cap = 0; \
}

#define VEC_DEFINE_WRITE(Type) \
static inline Type* vec_##Type##_write(vec_##Type *v) { \
  VEC_RESIZE_IF_NEEDED(v); \
  return &v->data[v->size++]; \
}

#define VEC_DEFINE_BACK(Type) \
static inline Type* vec_##Type##_back(vec_##Type *v) { \
  return v->size ? (v->data + (v->size - 1)) : 0; \
}

#define VEC_DEFINE_TYPE(Type) \
  typedef struct { \
  Type* data; \
  size_t size; \
  size_t cap; \
} vec_##Type;

#define VEC_DEFINE(Type) \
  VEC_DEFINE_TYPE(Type) \
  VEC_DEFINE_INIT(Type) \
  VEC_DEFINE_DESTROY(Type) \
  VEC_DEFINE_PUSH(Type) \
  VEC_DEFINE_WRITE(Type) \
  VEC_DEFINE_BACK(Type)

#endif
