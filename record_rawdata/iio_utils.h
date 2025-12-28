#ifndef _IIO_UTILS_H_
#define _IIO_UTILS_H_

#include <stdint.h>
#include <iio.h>

int32_t iio_utils_read_device_attributes(struct iio_device *pDevCtx);
int32_t iio_utils_read_channel_attributes(struct iio_channel *pChCtx);

#endif /* _IIO_UTILS_H_ */
