#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <iio.h>

int main(int argc, char **argv){
    char *backend = NULL;
    struct iio_scan_context *pScanCtx = NULL;
    struct iio_context_info **ppInfo;
    int ret;
    uint32_t i,num_of_contexts;
    
    if(argc > 1){
        backend = argv[1];
    }

    pScanCtx = iio_create_scan_context(backend, 0);
    if(pScanCtx == NULL){
        fprintf(stderr, "failed to create scan context. %s(%d)\n", strerror(errno), errno);
        return 1;
    }

    ret = iio_scan_context_get_info_list(pScanCtx, &ppInfo);
    if(ret < 0){
        fprintf(stderr, "failed to get results of scan context. %s(%d)\n", strerror(-1*ret), -1*ret);
        goto error_exit;
    }
    num_of_contexts = (uint32_t)ret;
    fprintf(stderr, "number of counts found: %u\n", num_of_contexts);

    for (i = 0; i < num_of_contexts; i++) {
        fprintf(stdout, "\t%u: %s [%s]\n",
                i, iio_context_info_get_description(ppInfo[i]),
                iio_context_info_get_uri(ppInfo[i]));
    }
    
 error_exit:
    iio_scan_context_destroy(pScanCtx);
    return 0;
}
