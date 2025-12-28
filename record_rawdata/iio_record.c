
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <stdatomic.h>

#include <pthread.h>

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <ad9361.h>
#include <iio.h>
#include "iio_utils.h"
#include "iio_record.h"
#include "utils.h"

#define BULK_OF(x) (sizeof(x)/sizeof(x[0]))

struct IIO_IQ_RecorderContext {
    struct IIO_IQ_RecorderParameter parameters;
    int32_t (*pEventCallback)(uint32_t, void *);
    void *pEventCookie;
    pthread_t thid;
    int termfd;

    struct {
        _Atomic uint32_t head;
        _Atomic uint32_t tail;
        struct {
            uint32_t storesize;
            uint8_t *pFrame;
        } chunk[128];
    } ringbuffer;

};

static void* mainThread(void *pParams){
    int ret;
    int epollfd = -1;
    bool bCont = true;
    struct IIO_IQ_RecorderContext *pCtx = 
        (struct IIO_IQ_RecorderContext *)(pParams);
    struct epoll_event evs[10];
    uint32_t i;
    struct iio_context *pIIOCtx  = NULL;
    struct iio_device  *pPhyDev = NULL, *pRxDev   = NULL;
    struct iio_channel *pRx0_Phy = NULL, *pRx0_LO = NULL;
    struct iio_channel *pRx0_I = NULL, *pRx0_Q = NULL;
    struct iio_buffer *pRxBuf = NULL;
    
    epollfd = epoll_create1(0);
    if(epollfd == -1){
        bCont = false;
        goto error_exit;
    }

    evs[0].events  = EPOLLIN;
    evs[0].data.fd = pCtx->termfd;
    ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, pCtx->termfd, &evs[0]);
    if(ret == -1){
        bCont = false;
        goto error_exit;
    }

    // create IIOCtx
    pIIOCtx = iio_create_context_from_uri(&pCtx->parameters.iio_uri[0]);
    if(pIIOCtx == NULL){
        fprintf(stderr, "failed to create iio create.\n");
        goto error_exit;
    }

    // get IIO Phu device
    pPhyDev = iio_context_find_device(pIIOCtx, &pCtx->parameters.phy_name[0]);
    if(pPhyDev == NULL){
        fprintf(stderr, "failed to get iio phy device.\n");
        goto error_exit;
    }

    // get IIO RX devices
    pRxDev = iio_context_find_device(pIIOCtx, &pCtx->parameters.rx.dev_name[0]);
    if(pRxDev == NULL){
        fprintf(stderr, "failed to get iio rx device.\n");
        goto error_exit;
    }
    {
        char tmp[128];
        fprintf(stderr, "lo: %sHz\n", intToPrefixStr(tmp, sizeof(tmp), pCtx->parameters.lo));
        fprintf(stderr, "sampling rate: %s\n", intToPrefixStr(tmp, sizeof(tmp), pCtx->parameters.samplingRate));
        fprintf(stderr, "ch: %d ch\n", pCtx->parameters.rx.ch);
        fprintf(stderr, "bandwidth: %s\n", intToPrefixStr(tmp, sizeof(tmp), pCtx->parameters.rx.bandwidth));
        fprintf(stderr, "SampleSize: %s Samples\n", intToPrefixStr(tmp, sizeof(tmp), pCtx->parameters.rx.sampleSizeForBuffer));
    }
    
    // configuration RX Devices
    {
        char chname[64] = { '\0' };
        fprintf(stderr, "* Acquiring AD9361 phy channel %d\n", pCtx->parameters.rx.ch);
        snprintf(chname, sizeof(chname), "voltage%d", pCtx->parameters.rx.ch);
        pRx0_Phy = iio_device_find_channel(pPhyDev, &chname[0], false);
        if(pRx0_Phy == NULL){
            fprintf(stderr, "not found a channel for rx rf.\n");
            goto error_exit;
        }

        ret = ad9361_set_bb_rate(pPhyDev, pCtx->parameters.samplingRate);
        if(ret < 0){
            fprintf(stderr,"FIR filiter could not be constructed. %d\n", ret);
            goto error_exit;
        }

#if 0
        ret = iio_channel_attr_write_longlong(pRx0_Phy, "filter_fir_en", 1);
        if(ret < 0){
            fprintf(stderr,"filter_fir_en could not be set. %d\n", ret);
        }
#endif
        iio_channel_attr_write(pRx0_Phy, "rf_port_select", "A_BALANCED");
        iio_channel_attr_write_longlong(pRx0_Phy, "rf_bandwidth", pCtx->parameters.rx.bandwidth);
        //ret = iio_channel_attr_write_longlong(pRx0_Phy, "sampling_frequency", pCtx->parameters.samplingRate);
        if(ret < 0){
            fprintf(stderr,"rf bandwidth could not be set. %ld %d\n", pCtx->parameters.rx.bandwidth, ret);
        }
        ret = iio_channel_attr_write_longlong(pRx0_Phy, "sampling_frequency", pCtx->parameters.samplingRate);
        if(ret < 0){
            fprintf(stderr,"sampling frequency could not be set. %ld %d\n", pCtx->parameters.samplingRate,  ret);
        }
        // finds AD9361  IIO configuration of local oscillator for RX channels
        pRx0_LO = iio_device_find_channel(pPhyDev, "altvoltage0", true);
        if(pRx0_LO == NULL){
            fprintf(stderr, "not found a channel for rx lo.\n");
            goto error_exit;
        }
        iio_channel_attr_write_longlong(pRx0_LO, "frequency", pCtx->parameters.lo);
    }                   
    
    // Initializing AD9361 IIO streaming channels
    pRx0_I = iio_device_find_channel(pRxDev, "voltage0", false);
    if(pRx0_I == NULL){
        pRx0_I = iio_device_find_channel(pRxDev, "altvoltage0", false);
    }
    if(pRx0_I == NULL){
        fprintf(stderr, "I of RX 0 channel was not found\n");
        goto error_exit;
    }

    pRx0_Q = iio_device_find_channel(pRxDev, "voltage1", false);
    if(pRx0_Q == NULL){
        pRx0_Q = iio_device_find_channel(pRxDev, "altvoltage1", false);
    }
    if(pRx0_Q == NULL){
        fprintf(stderr, "Q of RX 0 channel was not found\n");
        goto error_exit;
    }

    iio_utils_read_device_attributes(pPhyDev);
    iio_utils_read_channel_attributes(pRx0_Phy);
    iio_utils_read_channel_attributes(pRx0_LO);

    iio_utils_read_device_attributes(pRxDev);
    iio_utils_read_channel_attributes(pRx0_I);
    iio_utils_read_channel_attributes(pRx0_Q);
        
    iio_channel_enable(pRx0_I);
    iio_channel_enable(pRx0_Q);
    
    fprintf(stderr, "* Creating non-cyclic IIO buffers with %.4f MiS\n", (float)pCtx->parameters.rx.sampleSizeForBuffer/(float)(1024*1024));
    pRxBuf = iio_device_create_buffer(pRxDev, pCtx->parameters.rx.sampleSizeForBuffer, false);
    if(pRxBuf == NULL){
        fprintf(stderr, "a buffer for IQ Data could not be created.\n");
        goto error_exit;
    }
    
    fprintf(stderr, "start thread\n");
    while(bCont){
        ret = epoll_wait(epollfd, &evs[0], BULK_OF(evs), 0);
        for(i=0;i<(size_t)ret;i++){
            if(evs[i].data.fd == pCtx->termfd){
                int64_t val;
                ssize_t read_size;
                read_size = read(pCtx->termfd, &val, sizeof(val));
                (void)read_size;
                bCont = false;
                printf("recv term event\n");
            }
        }
        {
            ssize_t nbytes = iio_buffer_refill(pRxBuf);
            if(nbytes < 0){
                fprintf(stderr, "Error in refilling buffer. %ld\n", nbytes);
            }            
            const uint8_t *pNow = iio_buffer_first(pRxBuf, pRx0_I);
            const uint8_t *pEnd = iio_buffer_end(pRxBuf);

            uint32_t head = atomic_load_explicit(&pCtx->ringbuffer.head, memory_order_acquire); //消費側の値の更新を保証する.
            uint32_t tail = atomic_load_explicit(&pCtx->ringbuffer.tail, memory_order_relaxed);
            uint32_t next_tail = tail + 1 >= BULK_OF(pCtx->ringbuffer.chunk) ? 0 : tail + 1;
            if(next_tail == head){
                if(pCtx->pEventCallback != NULL){
                    pCtx->pEventCallback(IIO_IQ_RECORDER_EVENT_DROP, pCtx->pEventCookie);
                }
            } else {
                pCtx->ringbuffer.chunk[tail].storesize = (uint32_t)(pEnd - pNow);
                memcpy(pCtx->ringbuffer.chunk[tail].pFrame, pNow, pCtx->ringbuffer.chunk[tail].storesize);
                // データ書き込みを先に公開してからtail更新を公開
                atomic_store_explicit(&pCtx->ringbuffer.tail, next_tail, memory_order_release);
            }
        }
    }

 error_exit:
    
    if(pRxBuf != NULL) iio_buffer_destroy(pRxBuf);
    
    if(pRx0_Q != NULL) iio_channel_disable(pRx0_Q);
    
    if(pRx0_I != NULL) iio_channel_disable(pRx0_I);
    
    if(pIIOCtx != NULL) iio_context_destroy(pIIOCtx);
    
    if(epollfd != -1){
        close(epollfd);
    }

    if(pCtx->pEventCallback != NULL){
        pCtx->pEventCallback(IIO_IQ_RECORDER_EVENT_TERM, pCtx->pEventCookie);
    }
    
    return NULL;
}

IIO_IQ_Recorder IIO_IQ_RecorderCreate(const struct IIO_IQ_RecorderParameter* pParameter,
                                      int32_t (*pEventCallback)(uint32_t event, void *pCookie),
                                      void *pCallbackCookie){
    int32_t ret;
    uint32_t i;
    struct IIO_IQ_RecorderContext *pCtx = NULL;

    pCtx = (struct IIO_IQ_RecorderContext *)malloc(sizeof(struct IIO_IQ_RecorderContext));
    memset(pCtx, 0x00, sizeof(struct IIO_IQ_RecorderContext));

    pCtx->parameters = *pParameter;
    pCtx->pEventCallback  = pEventCallback;
    pCtx->pEventCookie = pCallbackCookie;

    pCtx->termfd = -1;
    pCtx->termfd = eventfd(0,0);
    if(pCtx->termfd == -1){
        goto error_exit;
    }

    for(i=0;i<BULK_OF(pCtx->ringbuffer.chunk);i++){
        pCtx->ringbuffer.chunk[i].storesize = 0;
        pCtx->ringbuffer.chunk[i].pFrame = (uint8_t *)malloc(pCtx->parameters.rx.sampleSizeForBuffer*sizeof(uint16_t)*2);
    }
                                                  
    ret = pthread_create(&pCtx->thid, NULL, mainThread, pCtx);
    if(ret != 0){
        goto error_exit;
    }

    return pCtx;

 error_exit:

    for(i=0;i<BULK_OF(pCtx->ringbuffer.chunk);i++){
        if (pCtx->ringbuffer.chunk[i].pFrame != NULL) free(pCtx->ringbuffer.chunk[i].pFrame);
    }
    
    if(pCtx->termfd != -1){
        close(pCtx->termfd);
    }
    free(pCtx);
    return NULL;
}

void IIO_IQ_RecorderDestory(IIO_IQ_Recorder recorder){
    uint64_t val = 1;
    struct IIO_IQ_RecorderContext *pCtx = (struct IIO_IQ_RecorderContext *)recorder;

    write(pCtx->termfd, &val, sizeof(val));

    pthread_join(pCtx->thid, NULL);
    
    free(pCtx);
    
    return;
}

int32_t IIO_IQ_RecorderReadData(IIO_IQ_Recorder recorder, uint8_t *pBuf, uint32_t bufsize){
    int32_t readsize = 0;
    struct IIO_IQ_RecorderContext *pCtx = (struct IIO_IQ_RecorderContext *)recorder;
    if(bufsize < 4){
        return -1;
    }

    {
        uint32_t head = atomic_load_explicit(&pCtx->ringbuffer.head, memory_order_relaxed);
        uint32_t tail = atomic_load_explicit(&pCtx->ringbuffer.tail, memory_order_acquire); //この時点より別スレッドでの値の更新を保証する.
        uint32_t next_head;

        if(head == tail){
            return 0;
        }

        if(bufsize < pCtx->ringbuffer.chunk[head].storesize){
            fprintf(stderr, "not enough receive buffer. %u < %u\n", bufsize, pCtx->ringbuffer.chunk[head].storesize);
            return -1;
        }
        
        memcpy(pBuf,
               pCtx->ringbuffer.chunk[head].pFrame,
               pCtx->ringbuffer.chunk[head].storesize);
        readsize = (int32_t)(pCtx->ringbuffer.chunk[head].storesize);
        next_head = head+1 >= BULK_OF(pCtx->ringbuffer.chunk) ? 0 : head+1;
        atomic_store_explicit(&pCtx->ringbuffer.head, next_head, memory_order_release); //releaseを宣言して,これ以前の値の更新を保証する.
    }
    
    return readsize;
}
