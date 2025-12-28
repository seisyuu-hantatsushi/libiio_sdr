#include <iio.h>
#include <stdio.h>
#include "iio_utils.h"

static int device_attribute_read_cb(struct iio_device *pDevCtx, const char *pAttr, const char *pValue, size_t len, void *pCooke){
    fprintf(stderr, "\t%s %s\n", pAttr, pValue);
    return 0;
}


int32_t iio_utils_read_device_attributes(struct iio_device *pDevCtx){
    fprintf(stderr, "device name: %s\n", iio_device_get_name(pDevCtx));
    iio_device_attr_read_all(pDevCtx, device_attribute_read_cb, NULL);
    return 0;
}

static int channel_attribute_read_cb(struct iio_channel *pChCtx, const char *pAttr, const char *pValue, size_t len, void *pCooke){
   fprintf(stderr, "\t%s %s\n", pAttr, pValue);
   return 0;
}

int32_t iio_utils_read_channel_attributes(struct iio_channel *pChCtx){
    fprintf(stderr,"%s in %s\n", iio_channel_get_id(pChCtx), iio_device_get_name(iio_channel_get_device(pChCtx)));
    iio_channel_attr_read_all(pChCtx, channel_attribute_read_cb, NULL);    
}
