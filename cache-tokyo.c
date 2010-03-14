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
#include <glob.h>
#include <sys/stat.h>

#include <tchdb.h>

#include "cache.h"

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
      if (tchdbecode (db) != TCENOFILE)
        tc_error (-1);
    }

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
    tc_error ();
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

  if (__xstat (0, filename, &stat_buf))
    {
      fprintf (stderr, "tdpkg tokyo: can't stat %s\n", filename);
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
      fprintf (stderr, "tdpkg tokyo: can't read full file %s of size %ld\n", filename, size);
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
