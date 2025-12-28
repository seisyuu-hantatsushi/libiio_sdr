#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>

#include <getopt.h>
#include <errno.h>

#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/eventfd.h>

#include <jansson.h>

#include "iio_record.h"

#define BULK_OF(x) (sizeof(x)/sizeof(x[0]))

struct AppContext{
    int termfd;
    struct IIO_IQ_RecorderParameter recorderParameter;
    char params_filepath[PATH_MAX];
    char output_filepath[PATH_MAX];
};

static const struct option long_options[] = {
    {"help",    no_argument,       0, 'h'},
    {"iio_uri", required_argument, 0, 'i'},
    {"params",  required_argument, 0, 'p'},
    {"output",  required_argument, 0, 'o'},
    {"lo",      required_argument, 0,  1 },
    {0, 0, 0, 0}
};

static int parse_arguments(struct AppContext *pCtx, int argc, char **argv){
    int c = 0;
    
    for(;;){
        int option_index = 0;
        c = getopt_long(argc, argv, "hi:p:o:", long_options, &option_index);
        if(c == -1){
            break;
        }
        
        switch(c){
        case 'i':
            strncpy(pCtx->recorderParameter.iio_uri, optarg, sizeof(pCtx->recorderParameter.iio_uri) - 1);
            break;
        case 'p':
            strncpy(&pCtx->params_filepath[0], optarg, sizeof(pCtx->params_filepath) - 1);
            break;
        case 'o':
            strncpy(&pCtx->output_filepath[0], optarg, sizeof(pCtx->output_filepath) - 1);
            break;
        case 'h':
            return -1;
        }
    }
    
    return 0;
}

static int32_t read_parameter(struct IIO_IQ_RecorderParameter *pRecorderParameter, const char *pFilePath){
    int ret = 0;
    json_error_t err;
    json_t *pRoot = json_load_file(pFilePath, JSON_DECODE_ANY, &err);
    const json_t *pObject;

    if(pRoot == NULL){
        fprintf(stderr, "%s:%d,%d\n", err.source, err.line, err.column);
        fprintf(stderr, "%s\n", &err.text[0]);
        return -1;
    }

    pObject = json_object_get(pRoot, "phy");
    if(json_is_string(pObject)){
        strncpy(&pRecorderParameter->phy_name[0],
                json_string_value(pObject),
                sizeof(pRecorderParameter->phy_name)-1);
    } else {
        fprintf(stderr, "not found phy name in parameter file.");
        ret = -1;
        goto error_exit;
    }

    pObject = json_object_get(pRoot, "lo");
    if(json_is_integer(pObject)){
        pRecorderParameter->lo = json_integer_value(pObject);
    } else {
        fprintf(stderr, "not found lo in parameter file.");
        ret = -1;
        goto error_exit;
    }

    pObject = json_object_get(pRoot, "SampleRate");
    if(json_is_integer(pObject)){
        pRecorderParameter->samplingRate = json_integer_value(pObject);
    } else {
        ret = -1;
        goto error_exit;            
    }

    {
        const json_t *pRXObject;
        pRXObject = json_object_get(pRoot, "rx");
        if(json_is_object(pRXObject)){        
            pObject = json_object_get(pRXObject, "ch");
            if(json_is_integer(pObject)){
                pRecorderParameter->rx.ch = json_integer_value(pObject);
            } else {
                ret = -1;
                goto error_exit;            
            }

            pObject = json_object_get(pRXObject, "devname");
            if(json_is_string(pObject)){
                strncpy(&pRecorderParameter->rx.dev_name[0],
                        json_string_value(pObject),
                        sizeof(pRecorderParameter->rx.dev_name)-1);
            } else {
                ret = -1;
                goto error_exit;            
            }
            
            pObject = json_object_get(pRXObject, "bandwidth");
            if(json_is_integer(pObject)){
                pRecorderParameter->rx.bandwidth = json_integer_value(pObject);
            } else {
                ret = -1;
                goto error_exit;
            }            

            pObject = json_object_get(pRXObject, "SampleSizeForBuffer");
            if(json_is_integer(pObject)){
                pRecorderParameter->rx.sampleSizeForBuffer = json_integer_value(pObject);
            } else {
                ret = -1;
                goto error_exit;
            }

        } else {
            ret = -1;
            goto error_exit;            
        }
    }
    
 error_exit:
    json_decref(pRoot);
    return ret;
}

static int32_t RecorderEvent(uint32_t event, void *pCookie){
    struct AppContext *pCtx = (struct AppContext *)pCookie;
    uint64_t val = 1;
    ssize_t ret;
    switch(event){
    case IIO_IQ_RECORDER_EVENT_TERM:
        ret = write(pCtx->termfd, &val, sizeof(val));
        if(ret < 0){
            fprintf(stderr, "failed to write event. %s(%d)\n", strerror(errno), errno);
        }
        break;
    case IIO_IQ_RECORDER_EVENT_DROP:
        fprintf(stderr, "IQ Data is dropped\n");
        ret = write(pCtx->termfd, &val, sizeof(val));
        if(ret < 0){
            fprintf(stderr, "failed to write event. %s(%d)\n", strerror(errno), errno);
        }
        break;
    default:
        break;
    }
    return 0;
}

int main(int argc, char **argv){
    int ret;
    sigset_t sigmask;
    int epollfd = -1, sigfd = -1;
    struct epoll_event evs[3];
    bool bCnt = true;
    struct AppContext appCtx = { '\0' };
    IIO_IQ_Recorder recorder = NULL;
    uint32_t recvBufSize = 32*1024*4;
    uint8_t *pRecvBuffer = NULL;
    FILE *outfp = NULL;
    
    appCtx.termfd = -1;

    pRecvBuffer = (uint8_t *)malloc(recvBufSize);
    if(pRecvBuffer == NULL){
        fprintf(stderr, "receive buffer could not be allocated.\n");
        ret = 1;
        goto error_exit;
    }
    
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGINT);

    ret = sigprocmask(SIG_BLOCK, &sigmask, NULL);
    if(ret != 0){
        fprintf(stderr, "failed to signal mask %s(%d)\n", strerror(errno), errno);
        ret = 1;
        goto error_exit;
    }

    epollfd = epoll_create1(0);
    if(epollfd == -1){
        fprintf(stderr, "failed to create epoll fd %s(%d)\n", strerror(errno), errno);
        ret = 1;
        goto error_exit;
    }

    sigfd = signalfd(-1, &sigmask, 0);
    if(sigfd == -1){
        fprintf(stderr, "failed to create signal fd %s(%d)\n", strerror(errno), errno);
        ret = 1;
        goto error_exit;
    }

    evs[0].events  = EPOLLIN;
    evs[0].data.fd = sigfd;
    ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, sigfd, &evs[0]);
    if(ret == -1){
        fprintf(stderr, "failed to add sigfd to epoll fd %s(%d)\n", strerror(errno), errno);
        ret = 1;
        goto error_exit;
    }

    appCtx.termfd = eventfd(0,0);
    if(appCtx.termfd == -1){
        fprintf(stderr, "failed to create event fd for terminate %s(%d)\n", strerror(errno), errno);
        ret = 1;
        goto error_exit;
    }

    evs[0].events = EPOLLIN;
    evs[0].data.fd = appCtx.termfd;
    ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, appCtx.termfd, &evs[0]);
    if(ret == -1){
        fprintf(stderr, "failed to add termfd to epoll fd %s(%d)\n", strerror(errno), errno);
        ret = 1;
        goto error_exit;
    }

    ret = parse_arguments(&appCtx, argc, argv);
    if(ret < 0){
        ret = 1;
        goto error_exit;
    }
    
    if(appCtx.recorderParameter.iio_uri[0] == '\0'){
        fprintf(stderr, "You need to specify iio_uri.\n");
        ret = 1;
        goto error_exit;
    }

    if(appCtx.params_filepath[0] == '\0'){
        fprintf(stderr, "You need to specify parameter file.\n");
        ret = 1;
        goto error_exit;
    }

    ret = read_parameter(&appCtx.recorderParameter, &appCtx.params_filepath[0]);
    if(ret < 0){
        fprintf(stderr,"error in parameter file.\n");
        goto error_exit;
    }

    if(appCtx.output_filepath[0] != '\0'){
        fprintf(stderr, "output file: %s\n", &appCtx.output_filepath[0]);
        outfp = fopen(appCtx.output_filepath, "w");
        if(outfp == NULL){
            fprintf(stderr,"failed to open output file.\n");
            goto error_exit;
        }
    }

    fprintf(stderr, "iio_uri: %s\n", appCtx.recorderParameter.iio_uri);

    recorder = IIO_IQ_RecorderCreate(&appCtx.recorderParameter, RecorderEvent, &appCtx);
    if(recorder == NULL){
        goto error_exit;
    }

    while(bCnt){
        size_t i;
        int32_t read_size;
        
        ret = epoll_wait(epollfd, &evs[0], BULK_OF(evs), 0);
        if(ret == -1){
            fprintf(stderr, "failed to wait epoll fd %s(%d)\n", strerror(errno), errno);
            ret = 1;
            goto error_exit;
        }

        read_size = IIO_IQ_RecorderReadData(recorder, pRecvBuffer, recvBufSize);
        if(read_size > 0 && outfp != NULL){
            const uint16_t *pIQData = (const uint16_t*)pRecvBuffer;
            for(i=0;i<read_size/sizeof(uint16_t);i++){
                float f = (float)pIQData[i]/32768.0f;
                fwrite(&f, sizeof(f), 1, outfp);
            }
        }
        
        for(i=0;i<(size_t)ret;i++){
            if(evs[i].data.fd == sigfd){
                ssize_t readsize;
                struct signalfd_siginfo siginfo = { 0 };
                readsize = read(sigfd, &siginfo, sizeof(siginfo));
                if(readsize > 0){
                    switch(siginfo.ssi_signo){
                    case SIGINT:
                    case SIGTERM:
                        fprintf(stderr, "recv TERM or INT signal\n");
                        bCnt = 0;
                        break;
                    default:
                        break;
                    }
                }
            } else if(evs[i].data.fd == appCtx.termfd){
                int64_t val;
                ssize_t read_size;
                read_size = read(appCtx.termfd, &val, sizeof(val));
                (void)read_size;
                bCnt = 0;
            }
        }
    }

    ret = 0;
    
 error_exit:

    if(pRecvBuffer != NULL){
        free(pRecvBuffer);
    }

    if(outfp != NULL){
        fclose(outfp);
    }
    
    if(recorder != NULL){
        IIO_IQ_RecorderDestory(recorder);
    }
    
    if(appCtx.termfd != -1){
        close(appCtx.termfd);
    }
    
    if(sigfd != -1){
        close(sigfd);
    }
    
    if(epollfd != -1){
        close(epollfd);
    }

    return ret;
}

