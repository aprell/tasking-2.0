include ../../common-gcc.mk

CPPFLAGS += -DCHANNEL_CACHE=100

CFLAGS += -pthread -fsanitize=address

INCLUDE = -I. -I..

LDFLAGS += -pthread -fsanitize=address

PROGS = fib shift throughput utest
SRCS = channel.c fib.c shift.c throughput.c utest.c

fib_SRCS = fib.c channel.c
shift_SRCS = shift.c channel.c
throughput_SRCS = throughput.c channel.c
utest_SRCS = utest.c channel.c

VPATH += test

test: $(PROGS)

include ../../rules.mk
include ../../deps.mk
