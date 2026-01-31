ROOT := ../..
PROTO_DIR := ./proto

CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

INCLUDES := -I. -I$(ROOT) -I$(PROTO_DIR)

PB_SRCS := $(ROOT)/pb_common.c \
           $(ROOT)/pb_encode.c \
           $(ROOT)/pb_decode.c

PROTO_SRCS := $(PROTO_DIR)/core/Contract.pb.c \
              $(PROTO_DIR)/core/Tron.pb.c \
              $(PROTO_DIR)/google/protobuf/any.pb.c

SRCS := transaction_trigger_demo.c \
        transaction_trigger_encode.c \
        transaction_trigger_decode.c \
        $(PB_SRCS) \
        $(PROTO_SRCS)

OBJS := $(SRCS:.c=.o)

TARGET := transaction_trigger_demo

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
