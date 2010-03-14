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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include "cache.h"

/* real functions */
static int (*realopen)(const char *path, int oflag, ...);
static int (*real__fxstat)(int ver, int fd, struct stat* buf);
static int (*real__fxstat64)(int ver, int fd, struct stat64* buf);
static ssize_t (*realread)(int fildes, void *buf, size_t nbyte);
static int (*realclose)(int fd);
static FILE* (*realfopen)(const char *path, const char *mode);
static FILE* (*realfdopen)(int fd, const char *mode);
static int (*realfclose)(FILE* fp);

/* state */
static struct FopenState
{
  FILE* file;
  int putc_called;
} fopen_state;

#define FAKE_FD 1234
static struct OpenState
{
  int fd;
  char* contents;
  size_t len;
  size_t read;
} open_state;

static FILE* ignore_file;
static int cache_initialized;

/* called once library is preloaded */
void _init (void)
{
  static int initialized = 0;
  if (!initialized)
    initialized = 1;
  else
    return;

  memset (&fopen_state, '\0', sizeof (fopen_state));
  memset (&open_state, '\0', sizeof (open_state));
  open_state.fd = -1;

  realopen = dlsym (RTLD_NEXT, "open");
  real__fxstat = dlsym (RTLD_NEXT, "__fxstat");
  real__fxstat64 = dlsym (RTLD_NEXT, "__fxstat64");
  realread = dlsym (RTLD_NEXT, "read");
  realclose = dlsym (RTLD_NEXT, "close");
  realfopen = dlsym (RTLD_NEXT, "fopen");
  realfdopen = dlsym (RTLD_NEXT, "fdopen");
  realfclose = dlsym (RTLD_NEXT, "fclose");

  const char* cache_filename = "cache.db";
  if (!tdpkg_cache_init (cache_filename))
    cache_initialized = 1;
  else
    fprintf (stderr, "tdpkg: cache at %s initialization failed, no wrapping\n", cache_filename);
}

static int
is_list_file (const char* path)
{
  return strstr (path, "/var/lib/dpkg/info/") && strstr (path, ".list");
}

FILE*
fopen (const char *path, const char *mode)
{
  if (!cache_initialized)
    return realfopen (path, mode);

  if (!is_list_file (path))
    return realfopen (path, mode);

  if (fopen_state.file)
    {
      fprintf (stderr, "tdpkg: multiple fopen(%s, %s) without fclose() detected, no wrapping\n", path, mode);
      return realfopen (path, mode);
    }

  printf ("tpkdg: fopen(%s, %s)\n", path, mode);
  fopen_state.file = realfopen (path, mode);
  return fopen_state.file;
}

FILE*
fdopen (int fd, const char *mode)
{
  if (!cache_initialized)
    return realfdopen (fd, mode);

  ignore_file = realfdopen (fd, mode);
  return ignore_file;
}

int
fclose (FILE *fp)
{
  if (!cache_initialized)
    return realfclose (fp);

  if (fp == ignore_file)
    {
      printf ("tdpkg: ignored %p\n", ignore_file);
      ignore_file = NULL;
      return realfclose (fp);
    }

  if (fopen_state.file != fp)
    {
      fprintf (stderr, "tdpkg: fclose() to unknown file %p detected, no wrapping\n", fp);
      return realfclose (fp);
    }

  printf ("tdpkg: fclose()\n");
  int result = realfclose (fopen_state.file);
  fopen_state.file = NULL;

  return result;
}

int
open (const char *path, int oflag, int mode)
{
  if (!cache_initialized)
    return realopen (path, oflag, mode);

  if (!is_list_file (path))
    return realopen (path, oflag, mode);

  if (open_state.fd >= 0)
    {
      fprintf (stderr, "tdpkg: multiple open(%s, %d, %d) without close() detected, no wrapping\n", path, oflag, mode);
      return realopen (path, oflag, mode);
    }

  open_state.contents = tdpkg_cache_read_filename (path);
  if (!open_state.contents)
    {
      fprintf (stderr, "tdpkg: file %s not up-to-date in cache, rebuild cache\n", path);
      cache_initialized = 0;
      if (tdpkg_cache_rebuild ())
        {
          fprintf (stderr, "tdpkg: can't rebuild cache, no wrapping\n");
          return realopen (path, oflag, mode);
        }
      cache_initialized = 1;

      open_state.contents = tdpkg_cache_read_filename (path);
      if (!open_state.contents)
        {
          fprintf (stderr, "tdpkg: path %s not being indexed, no wrapping\n", path);
          return realopen (path, oflag, mode);
        }
    }

  printf ("tdpkg: open(%s, %d, %d)\n", path, oflag, mode);
  open_state.fd = FAKE_FD;
  open_state.len = strlen (open_state.contents);

  return open_state.fd;
}

int
__fxstat (int ver, int fd, struct stat* buf)
{
  if (!cache_initialized)
    return real__fxstat (ver, fd, buf);

  if (open_state.fd < 0)
    return real__fxstat (ver, fd, buf);

  if (open_state.fd != fd)
    {
      fprintf (stderr, "tdpkg: __fxstat() to unknown fd %d\n", fd);
      return real__fxstat (ver, fd, buf);
    }

  printf ("tdpkg: __fxstat(%d) is %ld\n", fd, open_state.len);
  buf->st_size = open_state.len;
  return 0;
}

int
__fxstat64 (int ver, int fd, struct stat64* buf)
{
  if (!cache_initialized)
    return real__fxstat64 (ver, fd, buf);

  if (open_state.fd < 0)
    return real__fxstat64 (ver, fd, buf);

  if (open_state.fd != fd)
    {
      fprintf (stderr, "tdpkg: __fxstat64() to unknown fd %d\n", fd);
      return real__fxstat64 (ver, fd, buf);
    }

  printf ("tdpkg: __fxstat64(%d) is %ld\n", fd, open_state.len);
  buf->st_size = open_state.len;
  return 0;
}

ssize_t
read (int fildes, void *buf, size_t nbyte)
{
  if (!cache_initialized)
    return realread (fildes, buf, nbyte);

  if (open_state.fd < 0)
    return realread (fildes, buf, nbyte);

  if (open_state.fd != fildes)
    {
      fprintf (stderr, "tdpkg: read() to unknown fd %d detected, no wrapping\n", fildes);
      return realread (fildes, buf, nbyte);
    }

  if (open_state.read > open_state.len)
    {
      fprintf (stderr, "tdpkg: read() already done on %d, returning 0\n", open_state.fd);
      return 0;
    }

  size_t nowread = (open_state.len-open_state.read) > nbyte ? nbyte : (open_state.len-open_state.read);
  memcpy (buf, open_state.contents+open_state.read, nowread);
  open_state.read += nowread;
  printf ("tdpkg: read(%d, %p, %ld) -> %ld\n", fildes, buf, nbyte, nowread);
  return nowread;
}

int
close (int fd)
{
  if (!cache_initialized)
    return realclose (fd);

  if (open_state.fd < 0)
    return realclose (fd);

  if (open_state.fd != fd)
    {
      fprintf (stderr, "tdpkg: close() on unknown fd %d, no wrapping\n", fd);
      return realclose (fd);
    }

  open_state.fd = -1;
  if (open_state.contents)
    free (open_state.contents);
  open_state.contents = NULL;
  open_state.len = 0;
  open_state.read = 0;
  return 0;
}
