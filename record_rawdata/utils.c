#include <stdio.h>
#include "utils.h"

char *intToPrefixStr(char *pBuf, uint32_t bufSize, int64_t num){

    if(num < 1000){
        snprintf(pBuf, bufSize, "%ld", num);
    } else if(num < 1000*1000){
        snprintf(pBuf, bufSize, "%.3fK", (double)num/1000.0);
    } else if(num < 1000*1000*1000){
        snprintf(pBuf, bufSize, "%.3fM", (double)num/(1000.0*1000.0));
    } else {
        snprintf(pBuf, bufSize, "%.3fG", (double)num/(1000.0*1000.0*1000.0));
    }
    
    return pBuf;
}

char *doubleToPrefixStr(char *pBuf, uint32_t bufSize, double d){

    if(d < 1000.0){
        snprintf(pBuf, bufSize, "%.3f", d);
    } else if(d < 1000.0*1000.0){
        snprintf(pBuf, bufSize, "%.3fK", d/1000.0);
    } else if(d < 1000*1000*1000){
        snprintf(pBuf, bufSize, "%.3fM", d/(1000.0*1000.0));
    } else {
        snprintf(pBuf, bufSize, "%.3fG", d/(1000.0*1000.0*1000.0));
    }
    
    return pBuf;
}
