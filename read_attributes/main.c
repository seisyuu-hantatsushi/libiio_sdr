
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include <iio.h>

static const struct option long_options[] = {
    {"help",    no_argument,       0, 'h'},
    {"iio_uri", required_argument, 0, 'i'},
    {0, 0, 0, 0}
};

struct AppContext {
    char iio_uri[256];
};

static int parse_arguments(struct AppContext *pCtx, int argc, char **argv){
    int c = 0;
    
    for(;;){
        int option_index = 0;
        c = getopt_long(argc, argv, "hi:", long_options, &option_index);
        if(c == -1){
            break;
        }
        
        switch(c){
        case 'i':
            strncpy(pCtx->iio_uri, optarg, sizeof(pCtx->iio_uri) - 1);
            break;
        case 'h':
            return -1;
        }
    }
    
    return 0;
}

static int device_attribute_read_cb(struct iio_device *pDevCtx, const char *pAttr, const char *pValue, size_t len, void *pCooke){
    printf("\t\t%s %s\n", pAttr, pValue);
    return 0;
}

static int32_t show_device_attributes(struct iio_context *pIIOCtx, struct iio_device *pDevCtx){
    printf("\tdevice attributes\n");
    iio_device_attr_read_all(pDevCtx, device_attribute_read_cb, NULL);
    return 0;
}

static int channel_attribute_read_cb(struct iio_channel *pChCtx, const char *pAttr, const char *pValue, size_t len, void *pCooke){
    printf("\t\t\t %s %s\n", pAttr, pValue);
    return 0;
}

static int32_t show_channel_attributes(struct iio_channel *pChCtx){
    printf("\t\t channel attributes\n");
    iio_channel_attr_read_all(pChCtx, channel_attribute_read_cb, NULL);
    return 0;
}

int main(int argc, char **argv){
    int ret;
    struct AppContext appCtx = { 0 };
    struct iio_context *pIIOCtx  = NULL;
    struct iio_device *pDevCtx = NULL;
    
    uint32_t i,count = 0;
    
    ret = parse_arguments(&appCtx, argc, argv);
    if(ret < 0){
        fprintf(stderr, "invalid argiments");
        return 1;
    }

    // create IIOCtx
    pIIOCtx = iio_create_context_from_uri(&appCtx.iio_uri[0]);
    if(pIIOCtx == NULL){
        fprintf(stderr, "failed to create iio create.\n");
        goto error_exit;
    }

    printf("context description:\n %s\n", iio_context_get_description(pIIOCtx));

    count = iio_context_get_attrs_count(pIIOCtx);
    printf("count of context attribute: %u\n", count);
    for(i=0;i<count;i++){
        const char *pName;
        const char *pValue;
        printf("\t");
        iio_context_get_attr(pIIOCtx, i, &pName, &pValue);
        printf("name:%s, value:%s\n", pName, pValue);
    }

    count = iio_context_get_devices_count(pIIOCtx);
    printf("count of devices: %u\n", count);

    for(i=0;i<count;i++){
        uint32_t j,channel_count;
        const char *pDevName = NULL;
        printf("\t");
        pDevCtx=iio_context_get_device(pIIOCtx, i);
        pDevName = iio_device_get_name(pDevCtx);
        printf("device %d %s\n",i, pDevName);
        show_device_attributes(pIIOCtx, pDevCtx);

        channel_count = iio_device_get_channels_count(pDevCtx);
        printf("\t a number of channels: %d\n", channel_count);
        for(j=0;j<channel_count;j++){
            struct iio_channel *pChCtx = iio_device_get_channel(pDevCtx, j);
            const char *pName =  iio_channel_get_name(pChCtx);
            const char *pId   =  iio_channel_get_id(pChCtx);
            printf("\t\t channel name: %s id %s %s %s\n",
                   pName, pId,
                   iio_channel_is_output(pChCtx)?"output":"input",
                   iio_channel_is_enabled(pChCtx)?"enable":"disable");
            show_channel_attributes(pChCtx);
        }

    }
 
    
    
 error_exit:
    
    if(pIIOCtx != NULL) iio_context_destroy(pIIOCtx);

    return ret;
}
