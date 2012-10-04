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
#include <fcntl.h>
#include <stdarg.h>

#include "cache.h"

typedef int (*open_t)(const char *path, int oflag, ...);
static int _tdpkg_open (const char *path, int oflag, int mode);

/* real functions */
static open_t realopen;
static open_t realopen64;
static int (*real__fxstat)(int ver, int fd, struct stat* buf);
static int (*real__fxstat64)(int ver, int fd, struct stat64* buf);
static ssize_t (*realread)(int fildes, void *buf, size_t nbyte);
static int (*realclose)(int fd);
static int (*realrename)(const char *old, const char *new);
static int (*realunlink)(const char* pathname);

/* handle open() of dpkg/src/filesdb.c */
#define FAKE_FD 4321
static struct OpenState
{
  int fd;
  char* contents;
  size_t len;
  size_t read;
  char *fn;
} open_state;

static int cache_initialized;

/* called once library is preloaded */
void _init (void)
{
  static int initialized = 0;
  if (!initialized)
    initialized = 1;
  else
    return;

  /* use absolute path for current library */
  char *ld_preload = getenv ("LD_PRELOAD");
  if (ld_preload)
    {
      char *abspath = realpath (ld_preload, NULL);
      if (abspath)
        {
          setenv ("LD_PRELOAD", abspath, 1);
          free (abspath);
        }
    }

  memset (&open_state, '\0', sizeof (open_state));
  open_state.fd = -1;

  realopen = dlsym (RTLD_NEXT, "open");
  realopen64 = dlsym (RTLD_NEXT, "open64");
  real__fxstat = dlsym (RTLD_NEXT, "__fxstat");
  real__fxstat64 = dlsym (RTLD_NEXT, "__fxstat64");
  realread = dlsym (RTLD_NEXT, "read");
  realclose = dlsym (RTLD_NEXT, "close");
  realrename = dlsym (RTLD_NEXT, "rename");
  realunlink = dlsym (RTLD_NEXT, "unlink");

  if (!tdpkg_cache_initialize ())
    cache_initialized = 1;
  else
    fprintf (stderr, "tdpkg: cache initialization failed, no wrapping\n");
}

static int
is_list_file (const char* path)
{
  return strstr (path, "/var/lib/dpkg/info/") && strstr (path, ".list");
}

int
rename (const char *old, const char *new)
{
  int result = realrename (old, new);
  if (!result && is_list_file (new))
    {
      if (tdpkg_cache_write_filename (new))
        {
          fprintf (stderr, "tdpkg: can't update cache for file %s, no wrapping\n", new);
          tdpkg_cache_finalize ();
          cache_initialized = 0;
        }
    }
  return result;
}

int
unlink (const char* pathname)
{
  int result = realunlink (pathname);
  if (!result && is_list_file (pathname))
    {
      if (tdpkg_cache_delete_filename (pathname))
        {
          fprintf (stderr, "tdpkg: can't delete %s from cache, no wrapping\n", pathname);
          tdpkg_cache_finalize ();
          cache_initialized = 0;
        }
    }
  return result;
}

static int
_tdpkg_open (const char *path, int oflag, int mode)
{
  if (!cache_initialized)
    return realopen (path, oflag, mode);

  if (!is_list_file (path) || (oflag & O_RDONLY) != O_RDONLY)
    return realopen (path, oflag, mode);

  // sometimes dpkg calls FIGETBSZ first time, open_state.fn fixes it
  if (open_state.fd >= 0 && open_state.fn != path)
    {
#ifdef TDPKG_INFO
      fprintf (stderr, "tdpkg: nested open(%s, %d, %d) detected, no wrapping\n", path, oflag, mode);
#endif
      return realopen (path, oflag, mode);
    }

  open_state.fn = (char*)path;
  open_state.contents = tdpkg_cache_read_filename (path);
  if (!open_state.contents)
    {
#ifdef TDPKG_INFO
      fprintf (stderr, "tdpkg: file %s not up-to-date in cache, rebuild cache\n", path);
#endif
      if (tdpkg_cache_rebuild ())
        {
          fprintf (stderr, "tdpkg: can't rebuild cache, no wrapping\n");
          tdpkg_cache_finalize ();
          cache_initialized = 0;
          return realopen (path, oflag, mode);
        }

      open_state.contents = tdpkg_cache_read_filename (path);
      if (!open_state.contents)
        {
          fprintf (stderr, "tdpkg: path %s not being indexed, no wrapping\n", path);
          return realopen (path, oflag, mode);
        }
    }

  open_state.fd = FAKE_FD;
  open_state.len = strlen (open_state.contents);
  return open_state.fd;
}

int
open (const char *path, int oflag, ...)
{
  va_list ap;
  va_start (ap, oflag);
  int mode = va_arg (ap, int);
  va_end (ap);
  return _tdpkg_open (path, oflag, mode);
}

int
open64 (const char *path, int oflag, ...)
{
  va_list ap;
  va_start (ap, oflag);
  int mode = va_arg (ap, int);
  va_end (ap);
  return _tdpkg_open (path, oflag, mode);
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
#ifdef TDPKG_INFO
      fprintf (stderr, "tdpkg: nested __fxstat(%d) detected, no wrapping\n", fd);
#endif
      return real__fxstat (ver, fd, buf);
    }

  buf->st_size = open_state.len;
  buf->st_mode = S_IFREG;
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
#ifdef TDPKG_INFO
      fprintf (stderr, "tdpkg: nested __fxstat64(%d) detected, no wrapping\n", fd);
#endif
      return real__fxstat64 (ver, fd, buf);
    }

  buf->st_size = open_state.len;
  buf->st_mode = S_IFREG;
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
#ifdef TDPKG_INFO
      fprintf (stderr, "tdpkg: nested read(%d) detected, no wrapping\n", fildes);
#endif
      return realread (fildes, buf, nbyte);
    }

  if (open_state.read > open_state.len)
    {
#ifdef TDPKG_INFO
      fprintf (stderr, "tdpkg: useless read(%d) detected, returning 0\n", open_state.fd);
#endif
      return 0;
    }

  size_t nowread = (open_state.len-open_state.read) > nbyte ? nbyte : (open_state.len-open_state.read);
  memcpy (buf, open_state.contents+open_state.read, nowread);
  open_state.read += nowread;
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
#ifdef TDPKG_INFO
      fprintf (stderr, "tdpkg: close() on unknown fd %d, no wrapping\n", fd);
#endif
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
