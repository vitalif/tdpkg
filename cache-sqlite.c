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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <glob.h>
#include <sys/stat.h>
#include <errno.h>

#include "cache.h"
#include "util.h"

#define CACHE_FILE "/var/lib/dpkg/info/tdpkg.cache"

#define sqlite_error(ret) { fprintf (stderr, "tdpkg sqlite: %s\n", sqlite3_errmsg (db)); return ret; }
#define CREATE_TABLE_SQL "CREATE TABLE IF NOT EXISTS files (filename varchar(255) PRIMARY KEY ON CONFLICT REPLACE, contents text);"
#define READ_FILE_SQL "SELECT contents FROM files WHERE filename=?"
#define INSERT_FILE_SQL "INSERT INTO files (filename, contents) VALUES (?, ?)"
#define DELETE_FILE_SQL "DELETE FROM files WHERE filename=?"

static sqlite3* db = NULL;
static sqlite3_stmt* read_file_stmt = NULL;
static sqlite3_stmt* insert_file_stmt = NULL;
static sqlite3_stmt* delete_file_stmt = NULL;

static int
_sqlite_exec (const char* sql)
{
  char* errmsg = NULL;
  sqlite3_exec (db, sql, NULL, NULL, &errmsg);
  if (errmsg)
    {
      fprintf (stderr, "tdpkg sqlite: %s\n", errmsg);
      sqlite3_free (errmsg);
      return -1;
    }
  return 0;
}

/* returns 0 on success */
int
tdpkg_cache_initialize (void)
{
  if (sqlite3_initialize () != SQLITE_OK)
    sqlite_error (-1);

  return 0;
}

static int
_sqlite_init (void)
{
  if (db)
    return 0;

  if (sqlite3_open (CACHE_FILE, &db) != SQLITE_OK)
    {
      if (unlink (CACHE_FILE))
        {
          tdpkg_cache_finalize ();
          return -1;
        }
      if (sqlite3_open (CACHE_FILE, &db) != SQLITE_OK)
        {
          tdpkg_cache_finalize ();
          sqlite_error (-1);
        }
    }

  if (_sqlite_exec (CREATE_TABLE_SQL))
    {
      tdpkg_cache_finalize ();
      if (unlink (CACHE_FILE))
        return -1;
      if (sqlite3_open (CACHE_FILE, &db) != SQLITE_OK)
        sqlite_error (-1);
      if (_sqlite_exec (CREATE_TABLE_SQL))
        return -1;
    }

  if (sqlite3_prepare (db, READ_FILE_SQL, -1, &read_file_stmt, NULL) != SQLITE_OK)
    {
      tdpkg_cache_finalize ();
      sqlite_error (-1);
    }

  if (sqlite3_prepare (db, INSERT_FILE_SQL, -1, &insert_file_stmt, NULL) != SQLITE_OK)
    {
      tdpkg_cache_finalize ();
      sqlite_error (-1);
    }

  if (sqlite3_prepare (db, DELETE_FILE_SQL, -1, &delete_file_stmt, NULL) != SQLITE_OK)
    {
      tdpkg_cache_finalize ();
      sqlite_error (-1);
    }

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

  return 0;
}

void
tdpkg_cache_finalize (void)
{
  if (read_file_stmt)
    sqlite3_finalize (read_file_stmt);
  if (insert_file_stmt)
    sqlite3_finalize (insert_file_stmt);
  if (db)
    sqlite3_close (db);
  sqlite3_shutdown ();
}

char*
tdpkg_cache_read_filename (const char* filename)
{
  if (_sqlite_init ())
    return NULL;

  if (sqlite3_reset (read_file_stmt) != SQLITE_OK)
    sqlite_error (NULL);

  if (sqlite3_bind_text (read_file_stmt, 1, filename, -1, SQLITE_STATIC) != SQLITE_OK)
    sqlite_error (NULL);

  if (sqlite3_step (read_file_stmt) != SQLITE_ROW)
    return NULL;

  char* result = strdup ((const char*)sqlite3_column_text (read_file_stmt, 0));

  if (sqlite3_step (read_file_stmt) != SQLITE_DONE)
    {
      free (result);
      sqlite_error (NULL);
    }

  return result;
}

int
tdpkg_cache_write_filename (const char* filename)
{
  if (_sqlite_init ())
    return -1;

  struct stat stat_buf;
  if (tdpkg_stat (filename, &stat_buf))
    {
      fprintf (stderr, "tdpkg sqlite: can't stat %s: %s\n", filename, strerror (errno));
      return -1;
    }
  size_t size = stat_buf.st_size;

  FILE* file = fopen (filename, "r");
  if (!file)
    {
      fprintf (stderr, "tdpkg sqlite: can't open %s: %s\n", filename, strerror (errno));
      return -1;
    }

  char* contents = malloc (size);
  if (fread (contents, sizeof (char), size, file) < size)
    {
      // FIXME: let's handle this?
      fprintf (stderr, "tdpkg sqlite: can't read full file %s of size %u\n", filename, size);
      fclose (file);
      return -1;
    }
  fclose (file);

  if (sqlite3_reset (insert_file_stmt) != SQLITE_OK)
    sqlite_error (-1);

  if (sqlite3_bind_text (insert_file_stmt, 1, filename, -1, SQLITE_STATIC) != SQLITE_OK)
    sqlite_error (-1);

  if (sqlite3_bind_text (insert_file_stmt, 2, contents, size, SQLITE_STATIC) != SQLITE_OK)
    {
      free (contents);
      sqlite_error (-1);
    }

  if (sqlite3_step (insert_file_stmt) != SQLITE_DONE)
    {
      free (contents);
      sqlite_error (-1);
    }

  free (contents);
  return 0;
}

int
tdpkg_cache_delete_filename (const char* filename)
{
  if (_sqlite_init ())
    return -1;

  if (sqlite3_reset (delete_file_stmt) != SQLITE_OK)
    sqlite_error (-1);

  if (sqlite3_bind_text (delete_file_stmt, 1, filename, -1, SQLITE_STATIC) != SQLITE_OK)
    sqlite_error (-1);

  if (sqlite3_step (delete_file_stmt) != SQLITE_DONE)
    sqlite_error (-1);

  return 0;
}
  

int
tdpkg_cache_rebuild (void)
{
  if (_sqlite_init ())
    return -1;

  glob_t glob_list;
  if (glob ("/var/lib/dpkg/info/*.list", 0, NULL, &glob_list))
    {
      fprintf (stderr, "tdpkg sqlite: can't glob /var/lib/dpkg/info/*.list\n");
      return -1;
    }

  if (_sqlite_exec ("DELETE FROM files; BEGIN;"))
    {
      globfree (&glob_list);
      return -1;
    }

  int i;
  for (i=0; i < glob_list.gl_pathc; i++)
    {
      const char* filename = glob_list.gl_pathv[i];
      printf ("tdpkg: (Indexing list file %d...)\r", i+1);
      if (tdpkg_cache_write_filename (filename))
        {
          _sqlite_exec ("ROLLBACK;");
          globfree (&glob_list);
          return -1;
        }
    }
  globfree (&glob_list);

  if (_sqlite_exec ("COMMIT;"))
    return -1;

  printf ("tdpkg: %d list files cached succefully\n", i);
  return 0;
}
