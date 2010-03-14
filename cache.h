#ifndef CACHE_H
#define CACHE_H

int tdpkg_cache_init (const char* filename);
char* tdpkg_cache_read_filename (const char* filename);
int tdpkg_cache_rebuild (void);

#endif
