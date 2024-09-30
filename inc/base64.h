#ifndef BASE64_H
#define BASE64_H

#include <string.h>
#include <ostream>

void  base64_encode(const unsigned char *src,  size_t src_len,  std::ostream &oss);
char *base64_encode(const unsigned char *src,  size_t src_len,  char *dest,  size_t max_dest_len);
int   base64_decode(const char *src, size_t src_len, unsigned char *dest, size_t dest_max_len  );

#endif