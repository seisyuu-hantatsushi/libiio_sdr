#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdint.h>
#include <time.h>

char *intToPrefixStr(char *pBuf, uint32_t bufSize, int64_t num);
char *doubleToPrefixStr(char *pBuf, uint32_t bufSize, double d);

inline int64_t timespec_sub(const struct timespec *minuend, const struct timespec *subtranend){
    return (minuend->tv_sec - subtranend->tv_sec)*1000*1000*1000 + (minuend->tv_nsec - subtranend->tv_nsec);
}

#endif /* _UTILS_H_ */
