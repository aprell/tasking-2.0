include ../../common-gcc.mk

BUILDDIR := build

CPPFLAGS += -DCHANNEL_CACHE=100

CFLAGS += -pthread -fsanitize=address

INCLUDE = -I. -I..

LDFLAGS += -pthread -fsanitize=address

PROGS = fib shift throughput
SRCS = channel.c fib.c shift.c throughput.c

fib_SRCS = fib.c channel.c
shift_SRCS = shift.c channel.c
throughput_SRCS = throughput.c channel.c

VPATH += test

test: $(PROGS)

.PHONY: veryclean
veryclean:
	$(MAKE) clean
	rm -rf build

include ../../rules.mk
include ../../deps.mk
