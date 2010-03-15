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

#ifndef CACHE_H
#define CACHE_H

int tdpkg_cache_initialize (void);
void tdpkg_cache_finalize (void);
char* tdpkg_cache_read_filename (const char* filename);
int tdpkg_cache_write_filename (const char* filename);
int tdpkg_cache_delete_filename (const char* filename);
int tdpkg_cache_rebuild (void);

#endif
