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
