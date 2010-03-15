#ifndef CACHE_H
#define CACHE_H

int tdpkg_cache_initialize (void);
void tdpkg_cache_finalize (void);
char* tdpkg_cache_read_filename (const char* filename);
int tdpkg_cache_write_filename (const char* filename);
int tdpkg_cache_delete_filename (const char* filename);
int tdpkg_cache_rebuild (void);

#endif
