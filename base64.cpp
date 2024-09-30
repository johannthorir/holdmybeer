// Copyright (c) 2014 Jóhann Þórir Jóhannsson. All rights reserved.

#include <stdlib.h>
#include "base64.h"

/* Private prototypes */
/* static int is_base64(char c); */
static char encode(unsigned char u);
static unsigned char decode(char c);


/**
 **
 **/
void base64_encode(
        const unsigned char *src,
        size_t src_len,
        std::ostream &oss)
{
    unsigned int i;

    if(!src)
        return;

    if(!src_len)
        src_len = strlen((char *)src);

    for(i=0; i<src_len; i+=3)
    {
        unsigned char b1 = src[i];
        unsigned char b2 = (i+1 < src_len) ? src[i+1] : 0;
        unsigned char b3 = (i+2 < src_len) ? src[i+2] : 0;
        unsigned char b4 = b1 >> 2;
        unsigned char b5 = ((b1 & 0x3) << 4) | (b2 >> 4);
        unsigned char b6 = ((b2 & 0xf) << 2) | (b3 >> 6);
        unsigned char b7 = b3 & 0x3f;

        oss << encode(b4) << encode(b5);

        if(i+1 < src_len)
            oss << encode(b6);
        else
            oss << '=';

        if(i+2 < src_len)
            oss << encode(b7);
        else
            oss << '=';
    }
}

char *base64_encode(
        const unsigned char *src,
        size_t src_len,
        char *dest,
        size_t max_dest_len
    )
{
    unsigned int i;
    char *p = dest;

    if(!src)
        return NULL;

    if(!src_len)
        src_len = strlen((char *)src);

    if(src_len * 4/3 + 5 > max_dest_len)
        return NULL;

    for(i=0; i<src_len; i+=3)
    {
        unsigned char b1 = src[i];
        unsigned char b2 = (i+1 < src_len) ? src[i+1] : 0;
        unsigned char b3 = (i+2 < src_len) ? src[i+2] : 0;
        unsigned char b4 = b1 >> 2;
        unsigned char b5 = ((b1 & 0x3) << 4) | (b2 >> 4);
        unsigned char b6 = ((b2 & 0xf) << 2) | (b3 >> 6);
        unsigned char b7 = b3 & 0x3f;

        *p++ = encode(b4);
        *p++ = encode(b5);

        if(i+1 < src_len)
            *p++ = encode(b6);
        else
            *p++ = '=';

        if(i+2 < src_len)
            *p++ = encode(b7);
        else
            *p++ = '=';
    }

    *p = '\0';

    return dest;
}



/**
 **   Note that this is not POSIX in that non-base64 characters must
 **   be stripped.
 **/
int base64_decode(
        const char *src,
        size_t src_len,
        unsigned char *dest,
        size_t dest_max_len
    )
{
    unsigned char *p = dest;
    unsigned int k;

    if(!src)   return 0;
    if(!*src)  return 0;
    if(!dest)  return 0;

    for(k=0; k < src_len; k += 4)
    {
        char c1 = src[k];
        char c2 = (k+1 < src_len) ? src[k+1] : 'A';
        char c3 = (k+2 < src_len) ? src[k+2] : 'A';
        char c4 = (k+3 < src_len) ? src[k+3] : 'A';

        unsigned char b1 = decode(c1);
        unsigned char b2 = decode(c2);
        unsigned char b3 = decode(c3);
        unsigned char b4 = decode(c4);

        *p++ = ((b1 << 2) | (b2 >> 4));

        if((p - dest) == (int)dest_max_len)
            return 0;

        if(c3 != '=')
        {
            *p++ = (((b2 & 0xf) << 4) | (b3 >> 2));
            if((p - dest) == (int)dest_max_len)
                return 0;
        }

        if(c4 != '=')
        {
            *p++ = (((b3 & 0x3) <<6 ) | b4);
            if((p - dest) == (int)dest_max_len)
                return 0;
        }
    }

    return(p - dest);
}


/**
 ** Base64 encode one byte
 **/
static char encode(unsigned char u)
{
    if(u < 26)  return 'A' + u;
    if(u < 52)  return 'a' + (u-26);
    if(u < 62)  return '0' + (u-52);
    if(u == 62) return '+';

    return '/';
}


/**
 ** Decode a base64 character
 **/
static unsigned char decode(char c)
{
    if(c >= 'A' && c <= 'Z') return(c - 'A');
    if(c >= 'a' && c <= 'z') return(c - 'a' + 26);
    if(c >= '0' && c <= '9') return(c - '0' + 52);
    if(c == '+')             return 62;

    return 63;
}
