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
#include <sqlite3.h>
#include <glob.h>
#include <sys/stat.h>

#include "cache.h"

#define sqlite_error(ret) { fprintf (stderr, "tdpkg sqlite: %s\n", sqlite3_errmsg (db)); return ret; }
#define READ_FILE_SQL "SELECT contents FROM files WHERE filename=?"
#define INSERT_FILE_SQL "INSERT INTO files (filename, contents) VALUES (?, ?)"

static sqlite3* db;
static sqlite3_stmt* read_file_stmt;
static sqlite3_stmt* insert_file_stmt;

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
tdpkg_cache_init (const char* filename)
{
  if (sqlite3_initialize () != SQLITE_OK)
    sqlite_error (-1);

  if (sqlite3_open (filename, &db) != SQLITE_OK)
    sqlite_error (-1);

  if (_sqlite_exec ("CREATE TABLE IF NOT EXISTS files (filename varchar(255) primary key, contents text);"))
    return -1;

  if (sqlite3_prepare (db, READ_FILE_SQL, -1, &read_file_stmt, NULL) != SQLITE_OK)
    sqlite_error (-1);

  if (sqlite3_prepare (db, INSERT_FILE_SQL, -1, &insert_file_stmt, NULL) != SQLITE_OK)
    sqlite_error (-1);

  return 0;
}

char*
tdpkg_cache_read_filename (const char* filename)
{
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
tdpkg_cache_rebuild (void)
{
  glob_t glob_list;
  if (glob ("/var/lib/dpkg/info/*.list", 0, NULL, &glob_list))
    {
      fprintf (stderr, "tdpkg sqlite: can't glob /var/lib/dpkg/info/*.list\n");
      return -1;
    }

  if (_sqlite_exec ("DELETE FROM files; BEGIN;"))
    return -1;

  int i;
  for (i=0; i < glob_list.gl_pathc; i++)
    {
      const char* filename = glob_list.gl_pathv[i];
      struct stat stat_buf;

      /* we don't use fstat because it's been wrapped */
      if (__xstat (1, filename, &stat_buf))
        {
          fprintf (stderr, "tdpkg sqlite: can't stat %s\n", filename);
          return -1;
        }
      size_t size = stat_buf.st_size;

      FILE* file = fopen (filename, "r");
      if (!file)
        {
          fprintf (stderr, "tdpkg sqlite: can't open %s\n", filename);
          return -1;
        }

      printf ("tdpkg: (Indexing list file %d...)\r", i+1);

      char* contents = malloc (size);
      if (fread (contents, sizeof (char), size, file) < size)
        {
          // FIXME: let's handle this?
          _sqlite_exec ("ROLLBACK;");
          fclose (file);
          fprintf (stderr, "tdpkg sqlite: can't read full file %s of size %ld\n", filename, size);
        }
      fclose (file);

      if (sqlite3_reset (insert_file_stmt) != SQLITE_OK)
        {
          _sqlite_exec ("ROLLBACK;");
          sqlite_error (-1);
        }

      if (sqlite3_bind_text (insert_file_stmt, 1, filename, -1, SQLITE_STATIC) != SQLITE_OK)
        {
          _sqlite_exec ("ROLLBACK;");
          sqlite_error (-1);
        }

      if (sqlite3_bind_text (insert_file_stmt, 2, contents, size, SQLITE_STATIC) != SQLITE_OK)
        {
          _sqlite_exec ("ROLLBACK;");
          free (contents);
          sqlite_error (-1);
        }

      if (sqlite3_step (insert_file_stmt) != SQLITE_DONE)
        {
          _sqlite_exec ("ROLLBACK;");
          free (contents);
          sqlite_error (-1);
        }

      free (contents);
    }

  if (_sqlite_exec ("COMMIT;"))
    return -1;

  printf ("tdpkg: %d list files inserted succefully\n", i);

  return 0;
}
