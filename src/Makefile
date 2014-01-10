include ../common-icc.mk

CPPFLAGS += -DPLATFORM_SHM -DCHAN_SHM -DNTIME
#CPPFLAGS += -DSTEAL_HALF
#CPPFLAGS += -DLAST_VICTIM_FIRST

CFLAGS += -pthread

INCLUDE += -Ichannel_shm

LDFLAGS += -pthread

channel_SRCS = \
channel.c

tasking_SRCS = \
PRM_task.c \
tasking.c \
tasking_internal.c \
deque_list_tl.c \
stack.c

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
lu.c \
nbody3.c \
uts.c uts_seq.c uts_shm.c \
uts_dfs.c \
brg_sha1.c \
$(channel_SRCS) \
$(tasking_SRCS) \
$(runtime_SRCS)

libtasking_SRCS = $(runtime_SRCS) $(channel_SRCS) $(tasking_SRCS)

#//// ALL PROGRAMS /////////////////////////////////////////////////////////#

PROGS = sum fibgen looptest spc bpc fib-like qsort barrier mm lu nbody3
PROGS += uts-seq uts-par

#//// BENCHMARK PROGRAMS ///////////////////////////////////////////////////#

spc_SRCS 		= spc.c $(libtasking_SRCS)
bpc_SRCS 		= bpc.c $(libtasking_SRCS)
fib_like_SRCS 	= fib-like.c $(libtasking_SRCS)
qsort_SRCS 		= qsort.c $(libtasking_SRCS)
mm_SRCS 		= mm.c $(libtasking_SRCS)
lu_SRCS 		= lu.c $(libtasking_SRCS)
nbody3_SRCS 	= nbody3.c $(libtasking_SRCS)
nbody3_LIBS 	= m
uts_seq_SRCS	= uts_seq.c uts.c brg_sha1.c
uts_seq_LIBS	= m
uts_par_SRCS	= uts_shm.c uts.c brg_sha1.c $(libtasking_SRCS)
uts_par_LIBS	= m

#//// TEST PROGRAMS ////////////////////////////////////////////////////////#

sum_SRCS = sum.c $(libtasking_SRCS)
sum_LIBS = 

fibgen_SRCS 	= fibgen.c $(libtasking_SRCS)
looptest_SRCS	= looptest.c $(libtasking_SRCS)
barrier_SRCS 	= barrier.c $(libtasking_SRCS)

VPATH += channel_shm test test/rng

test: $(PROGS)

include ../rules.mk
include ../deps.mk

libtasking.a: $(libtasking_SRCS:.c=.o)
	$(AR) rc $@ $^
	cp $@ ../lib

.PHONY: veryclean
veryclean:
	$(MAKE) clean && rm -f ../lib/*.a
