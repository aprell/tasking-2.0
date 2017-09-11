include ../common-gcc.mk

CPPFLAGS += -DPLATFORM_SHM -DCHAN_SHM -DNTIME
CPPFLAGS += -DMANAGER_ID=1
CPPFLAGS += -DSTEAL_ADAPTIVE
CPPFLAGS += -DSPLIT=adaptive
CPPFLAGS += -DOPTIMIZE_BARRIER
CPPFLAGS += -DSTEAL_BACKOFF
CPPFLAGS += -DCPUFREQ=$(cpu_freq_ghz)
#CPPFLAGS += -DCPUFREQ=1.05263 # Xeon Phi 5110P
#CPPFLAGS += -DLAZY_FUTURES

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
cilksort.c getoptions.c \
nqueens.c \
loopsched.c \
futures_api.c \
task_example.c \
test_async.c \
$(channel_SRCS) \
$(tasking_SRCS) \
$(runtime_SRCS)

libtasking_SRCS = $(runtime_SRCS) $(channel_SRCS) $(tasking_SRCS)

#//// ALL PROGRAMS /////////////////////////////////////////////////////////#

PROGS = sum fibgen looptest spc bpc fib-like qsort barrier mm lu nbody3
PROGS += uts-seq uts-par cilksort nqueens loopsched futures_api task_example
PROGS += test_async

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
cilksort_SRCS   = cilksort.c getoptions.c $(libtasking_SRCS)
nqueens_SRCS    = nqueens.c $(libtasking_SRCS)
loopsched_SRCS  = loopsched.c $(libtasking_SRCS)

#//// TEST PROGRAMS ////////////////////////////////////////////////////////#

sum_SRCS = sum.c $(libtasking_SRCS)
sum_LIBS =

fibgen_SRCS 	  = fibgen.c $(libtasking_SRCS)
looptest_SRCS	  = looptest.c $(libtasking_SRCS)
barrier_SRCS 	  = barrier.c $(libtasking_SRCS)
futures_api_SRCS  = futures_api.c $(libtasking_SRCS)
task_example_SRCS = task_example.c $(libtasking_SRCS)
test_async_SRCS   = test_async.c $(libtasking_SRCS)

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

#//// EXPERIMENTAL /////////////////////////////////////////////////////////#

# Determine maximum CPU clock frequency in GHz
cpu_freq := /sys/devices/system/cpu/cpu0/cpufreq
ifeq ($(wildcard $(cpu_freq)),)
  $(warning Directory $(cpu_freq) does not exist)
endif
cpu_freq_khz := $(shell cat $(cpu_freq)/scaling_max_freq)
cpu_freq_ghz := $(shell echo "scale=5; $(cpu_freq_khz)/10^6" | bc)
