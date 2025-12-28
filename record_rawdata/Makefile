TARGET=record_iqdata

DEBUG_TARGET   = $(TARGET)_debug
RELEASE_TARGET = $(TARGET)

LDFLAGS_OPT = `pkg-config --libs libiio` -ljansson

C_OBJS = main.o iio_record.o utils.o

include ../Makefile.common
