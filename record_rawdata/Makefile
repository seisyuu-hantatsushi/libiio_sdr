TARGET=record_iqdata

DEBUG_TARGET   = $(TARGET)_debug
RELEASE_TARGET = $(TARGET)

LDFLAGS_OPT = -liio -ljansson -lad9361

C_OBJS = main.o iio_utils.o utils.o iio_record.o 

include ../Makefile.common
