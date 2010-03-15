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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glob.h>
#include <sys/stat.h>
#include <errno.h>

#include <tchdb.h>

#include "cache.h"
#include "util.h"

#define CACHE_FILE "/var/lib/dpkg/info/tdpkg.cache"

static TCHDB* db = NULL;
static int in_transaction = 0;
static int is_write = 0;

#define tc_error(ret) { fprintf (stderr, "tdpkg tokio: %s\n", tchdberrmsg (tchdbecode (db))); return ret; }

static int
_tokyo_init (int write)
{
  if (db && is_write >= write)
    return 0;

  if (db)
    tdpkg_cache_finalize ();

  db = tchdbnew ();
  int flags = HDBOREADER | HDBOLCKNB;
  if (write)
    flags |= HDBOWRITER | HDBOCREAT;

  if (!tchdbopen (db, CACHE_FILE, flags))
    {
      int ecode = tchdbecode (db);
      if (ecode == TCEMETA || ecode == TCEREAD)
        {
          tchdbdel (db);
          db = NULL;
          if (unlink (CACHE_FILE))
            return -1;
          db = tchdbnew ();
          if (!tchdbopen (db, CACHE_FILE, flags))
            {
              if (tchdbecode (db) != TCENOFILE)
                tc_error (-1);
            }
        }
      else if (ecode != TCENOFILE)
        tc_error (-1);
    }
  if (write && !tchdbsync (db))
    tc_error (-1);

  /* ensure cache consistency with the file system */
  struct stat stat_buf;
  if (tdpkg_stat (CACHE_FILE, &stat_buf))
    {
      if (tdpkg_cache_rebuild ())
        {
          tdpkg_cache_finalize ();
          return -1;
        }
      return 0;
    }
  time_t db_time = stat_buf.st_mtime;

  glob_t glob_list;
  if (glob ("/var/lib/dpkg/info/*.list", 0, NULL, &glob_list))
    {
      fprintf (stderr, "tdpkg sqlite: can't glob /var/lib/dpkg/info/*.list\n");
      tdpkg_cache_finalize ();
      return -1;
    }

  int i;
  for (i=0; i < glob_list.gl_pathc; i++)
    {
      const char* filename = glob_list.gl_pathv[i];
      struct stat stat_buf;

      /* we don't use fstat because it's been wrapped */
      if (tdpkg_stat (filename, &stat_buf))
        {
          fprintf (stderr, "tdpkg sqlite: can't stat %s: %s\n", filename, strerror (errno));
          tdpkg_cache_finalize ();
          return -1;
        }

      /* list file more recent than cache */
      if (stat_buf.st_mtime > db_time)
        {
          if (tdpkg_cache_rebuild ())
            {
              globfree (&glob_list);
              tdpkg_cache_finalize ();
              return -1;
            }
          break;
        }
    }
  globfree (&glob_list);

  is_write = write;
  return 0;
}


int
tdpkg_cache_initialize (void)
{
  return 0;
}

void
tdpkg_cache_finalize (void)
{
  if (db && !tchdbclose (db))
    {
      if (tchdbecode (db) != TCENOFILE)
        tc_error ();
    }
  if (db)
    tchdbdel (db);
  db = NULL;
}

char*
tdpkg_cache_read_filename (const char* filename)
{
  if (_tokyo_init (0))
    return NULL;

  return tchdbget2 (db, filename);
}

int
tdpkg_cache_write_filename (const char* filename)
{
  if (_tokyo_init (1))
    return -1;

  struct stat stat_buf;
  if (tdpkg_stat (filename, &stat_buf))
    {
      fprintf (stderr, "tdpkg tokyo: can't stat %s: %s\n", filename, strerror (errno));
      return -1;
    }
  size_t size = stat_buf.st_size;

  FILE* file = fopen (filename, "r");
  if (!file)
    {
      fprintf (stderr, "tdpkg tokyo: can't open %s\n", filename);
      return -1;
    }

  char* contents = malloc (size+1);
  if (fread (contents, sizeof (char), size, file) < size)
    {
      // FIXME: let's handle this?
      fprintf (stderr, "tdpkg tokyo: can't read full file %s of size %u\n", filename, size);
      fclose (file);
      return -1;
    }
  fclose (file);
  contents[size] = '\0';

  if (!tchdbputasync2 (db, filename, contents))
    {
      free (contents);
      tc_error (-1);
    }
  free (contents);

  if (!in_transaction && !tchdbsync (db))
    tc_error (-1);
  return 0;
}

int
tdpkg_cache_delete_filename (const char* filename)
{
  if (!tchdbout2 (db, filename))
    tc_error (-1);
  if (!tchdbsync (db))
    tc_error (-1);
  return 0;
}

int
tdpkg_cache_rebuild (void)
{
  if (_tokyo_init (1))
    return -1;

  glob_t glob_list;
  if (glob ("/var/lib/dpkg/info/*.list", 0, NULL, &glob_list))
    {
      fprintf (stderr, "tdpkg sqlite: can't glob /var/lib/dpkg/info/*.list\n");
      return -1;
    }

  if (!tchdbvanish (db))
    tc_error (-1);

  in_transaction = 1;
  int i;
  for (i=0; i < glob_list.gl_pathc; i++)
    {
      const char* filename = glob_list.gl_pathv[i];
      printf ("tdpkg: (Indexing list file %d...)\r", i+1);
      if (tdpkg_cache_write_filename (filename))
        {
          globfree (&glob_list);
          in_transaction = 0;
          tchdbvanish (db);
          return -1;
        }
    }
  globfree (&glob_list);
  in_transaction = 0;

  if (!tchdbsync (db))
    tc_error (-1);

  printf ("tdpkg: %d list files cached succefully\n", i);
  return 0;
}
