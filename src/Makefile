include ../common.mk

CPPFLAGS += -DPLATFORM_SHM -DCHAN_SHM

CFLAGS += -pthread

INCLUDE += -Ichannel_shm

LDFLAGS += -pthread

channel_SRCS = \
channel.c

tasking_SRCS = \
PRM_task.c \
tasking.c \
tasking_internal.c

runtime_SRCS = \
runtime.c

SRCS = \
sum.c \
fibgen.c \
looptest.c \
spc.c \
bpc.c \
fib-like.c \
qsort.c \
barrier.c \
mm.c \
nbody3.c \
$(channel_SRCS) \
$(tasking_SRCS) \
$(runtime_SRCS)

libtasking_SRCS = $(runtime_SRCS) $(channel_SRCS) $(tasking_SRCS) 

#//// ALL PROGRAMS /////////////////////////////////////////////////////////#

PROGS = sum fibgen looptest spc bpc fib-like qsort barrier mm nbody3

#//// BENCHMARK PROGRAMS ///////////////////////////////////////////////////#

spc_SRCS 		= spc.c $(libtasking_SRCS)
bpc_SRCS 		= bpc.c $(libtasking_SRCS)
fib_like_SRCS 	= fib-like.c $(libtasking_SRCS)
qsort_SRCS 		= qsort.c $(libtasking_SRCS)
mm_SRCS 		= mm.c $(libtasking_SRCS)
nbody3_SRCS 	= nbody3.c $(libtasking_SRCS)
nbody3_LIBS	    = m

#//// TEST PROGRAMS ////////////////////////////////////////////////////////#

sum_SRCS = sum.c $(libtasking_SRCS)
sum_LIBS = 

fibgen_SRCS 	= fibgen.c $(libtasking_SRCS)
looptest_SRCS	= looptest.c $(libtasking_SRCS)
barrier_SRCS 	= barrier.c $(libtasking_SRCS)

VPATH += channel_shm test

test: $(PROGS)

include ../rules.mk
include ../deps.mk

libtasking.a: $(libtasking_SRCS:.c=.o)
	$(AR) rc $@ $^
	cp $@ ../lib

.PHONY: veryclean
veryclean:
	$(MAKE) clean && rm -f ../lib/*.a
