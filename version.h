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

#ifndef DVBINDEX_VERSION_H
#define DVBINDEX_VERSION_H

#define DVBINDEX_VERSION_MAJOR 0
#define DVBINDEX_VERSION_MINOR 0
#define DVBINDEX_VERSION_PATCH 2

#define DVBINDEX_VERSION                                                       \
  (DVBINDEX_VERSION_MAJOR << 16 | DVBINDEX_VERSION_MINOR << 8 |                \
   DVBINDEX_VERSION_PATCH)

#define xstr(s) str(s)
#define str(s) #s

#define DVBINDEX_VERSION_STRING                                                \
  xstr(DVBINDEX_VERSION_MAJOR) "." xstr(DVBINDEX_VERSION_MINOR) "." xstr(      \
      DVBINDEX_VERSION_PATCH)

#endif
