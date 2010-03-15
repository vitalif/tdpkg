/*
    Copyright © 2010 Luca Bruno

    This file is part of tdpkg.

    tdpkg is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    tdpkg is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with tdpkg.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE
#include <sys/stat.h>

int
tdpkg_stat (const char* filename, struct stat* buf)
{
#ifndef __USE_FILE_OFFSET64
  struct stat64 buf64;
  int result = __xstat64 (_STAT_VER, filename, &buf64);
  buf->st_size = buf64.st_size;
  buf->st_mtime = buf64.st_mtime;
  return result;
#else
  return __xstat (_STAT_VER, filename, &buf));
#endif
}
