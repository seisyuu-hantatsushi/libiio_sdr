#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <errno.h>

#include <iio.h>

/* helper macros */
#define MHZ(x) ((long long)(x*1000000.0 + .5))
#define GHZ(x) ((long long)(x*1000000000.0 + .5))

struct AppContext{
  struct iio_context *pIIOCtx;
  char iio_uri[256];
};

static const struct option long_options[] = {
  {"help",    no_argument,      0, 'h'},
  {"iio_uri", required_argument, 0, 'i'},
  {0, 0, 0, 0}
};

int parse_arguments(struct AppContext *pCtx, int argc, char **argv){
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

int main(int argc, char **argv){
  int ret, err;
  struct AppContext appCtx = { '\0' };
  unsigned int num_of_device_in_context = 0;
  
  // Stream configurations
  //struct stream_cfg rxcfg = { 0 };

  // RX stream config
  //rxcfg.bw_hz = MHZ(2);   // 2 MHz rf bandwidth
  //rxcfg.fs_hz = MHZ(2.4);   // 2.4 MS/s rx sample rate
  //rxcfg.lo_hz = MHZ(77.8); // 77.8 MHz rf frequency
  //rxcfg.rfport = "A_BALANCED"; // port A (select for rf freq.)
  
  ret = parse_arguments(&appCtx, argc, argv);
  if(ret < 0){
    return 1;
  }

  if(appCtx.iio_uri[0] == '\0'){
    printf("You need to specify iio_uri.\n");
  }
  
  printf("iio_uri: %s\n", appCtx.iio_uri);
  
  appCtx.pIIOCtx = iio_create_context_from_uri(appCtx.iio_uri);

  if(appCtx.pIIOCtx == NULL){
    int last_error = errno;
    printf("failed to create iioctx. %s(%d)\n",
	   strerror(last_error),last_error);
    return 1;
  }
  printf("obtained iioctx\n");
  num_of_device_in_context = iio_context_get_devices_count(appCtx.pIIOCtx);
  printf("num_of_device_in_context = %u\n", num_of_device_in_context);
  
  if(appCtx.pIIOCtx != NULL){
    iio_context_destroy(appCtx.pIIOCtx);
  }
  
  return 0;
}
