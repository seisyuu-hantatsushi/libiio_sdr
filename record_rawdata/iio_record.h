#ifndef _IIO_IQ_RECORDER_H_
#define _IIO_IQ_RECORDER_H_

#include <stdint.h>
#include <sys/types.h>

#define IIO_IQ_RECORDER_EVENT_TERM (0U)
#define IIO_IQ_RECORDER_EVENT_DROP (1U)

struct IIO_IQ_RecorderParameter {
    char iio_uri[256];
    char phy_name[64];
    uint64_t lo;
    uint64_t samplingRate;
    struct {
        char dev_name[64];
        uint32_t ch;
        uint64_t bandwidth;
        uint32_t sampleSizeForBuffer;
    } rx;
};

typedef struct IIO_IQ_RecorderContext* IIO_IQ_Recorder;

IIO_IQ_Recorder IIO_IQ_RecorderCreate(const struct IIO_IQ_RecorderParameter* pParameter,
                                      int32_t (*pEventCallback)(uint32_t event, void *pCookie),
                                      void *pCallbackCookie);

void IIO_IQ_RecorderDestory(IIO_IQ_Recorder recorder);

int32_t IIO_IQ_RecorderReadData(IIO_IQ_Recorder recorder, uint8_t *pData, uint32_t size);

#endif /* _IIO_RECORDER_H_ */
