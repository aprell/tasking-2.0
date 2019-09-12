include ../common-gcc.mk

BUILDDIR := build
LIBDIR := ../lib

CPPFLAGS += -DNTIME
CPPFLAGS += -DSTEAL=adaptive
CPPFLAGS += -DSTEAL_EARLY
CPPFLAGS += -DSTEAL_EARLY_THRESHOLD=0
CPPFLAGS += -DSPLIT=adaptive
CPPFLAGS += -DMAXSTEAL=1
CPPFLAGS += -DCPUFREQ=$(cpu_freq_ghz)
#CPPFLAGS += -DCPUFREQ=1.05263 # Xeon Phi 5110P
#CPPFLAGS += -DCHANNEL_CACHE=100
CPPFLAGS += -DLAZY_FUTURES

INCLUDE += -Ichannel_shm
CFLAGS += -pthread
LDFLAGS += -pthread

ifeq ($(SANITIZE),1)
  SANITIZE := -fsanitize=address,undefined
  CFLAGS += $(SANITIZE)
  LDFLAGS += $(SANITIZE)
endif

# Profile with coz run --- ./prog args
ifeq ($(USE_COZ),1)
  COZ_ROOT := $(HOME)/github/coz
  CPPFLAGS += -DUSE_COZ
  CFLAGS += -g
  INCLUDE += -I$(COZ_ROOT)/include
  IMPORTS += -ldl
endif

tasking_SRCS := \
channel.c \
deque.c \
runtime.c \
tasking.c \
tasking_internal.c

SRCS := \
barrier.c \
bpc.c \
brg_sha1.c \
cilksort.c \
fib.c \
fibgen.c \
fib-like.c \
getoptions.c \
loopsched.c \
lu.c \
mm.c \
mm_dac.c \
nbody3.c \
nqueens.c \
qsort.c \
spc.c \
task_example.c \
test_async.c \
uts.c \
uts_dfs.c \
uts_seq.c \
uts_shm.c \
$(tasking_SRCS)

#//// TEST & BENCHMARK PROGRAMS ////////////////////////////////////////////#

PROGS := barrier bpc cilksort fib fibgen fib-like loopsched lu mm mm_dac
PROGS += nbody3 nqueens qsort spc task_example test_async uts-par uts-seq

barrier_SRCS 	  := barrier.c $(tasking_SRCS)
bpc_SRCS 		  := bpc.c $(tasking_SRCS)
cilksort_SRCS     := cilksort.c getoptions.c $(tasking_SRCS)
fib_SRCS 	      := fib.c $(tasking_SRCS)
fibgen_SRCS 	  := fibgen.c $(tasking_SRCS)
fib_like_SRCS 	  := fib-like.c $(tasking_SRCS)
loopsched_SRCS    := loopsched.c $(tasking_SRCS)
lu_SRCS 		  := lu.c $(tasking_SRCS)
mm_SRCS 		  := mm.c $(tasking_SRCS)
mm_dac_SRCS 	  := mm_dac.c $(tasking_SRCS)
nbody3_SRCS 	  := nbody3.c $(tasking_SRCS)
nqueens_SRCS      := nqueens.c $(tasking_SRCS)
qsort_SRCS 		  := qsort.c $(tasking_SRCS)
spc_SRCS 		  := spc.c $(tasking_SRCS)
task_example_SRCS := task_example.c $(tasking_SRCS)
test_async_SRCS   := test_async.c $(tasking_SRCS)
uts_par_SRCS	  := uts_shm.c uts.c brg_sha1.c $(tasking_SRCS)
uts_seq_SRCS	  := uts_seq.c uts.c brg_sha1.c

nbody3_LIBS 	  := m
uts_par_LIBS	  := m
uts_seq_LIBS	  := m

#///////////////////////////////////////////////////////////////////////////#

VPATH += channel_shm test test/rng

test: $(PROGS)

include ../rules.mk
include ../deps.mk

libtasking: $(LIBDIR)/libtasking.a

$(LIBDIR)/libtasking.a: $(BUILDDIR)/libtasking.a
	@mkdir -p $(LIBDIR)
	@cp $< $@

$(BUILDDIR)/libtasking.a: $(addprefix $(BUILDDIR)/,$(tasking_SRCS:.c=.o))
	$(AR) rc $@ $^

.PHONY: veryclean
veryclean:
	$(MAKE) clean
	rm -rf $(BUILDDIR) $(LIBDIR)

#//// EXPERIMENTAL /////////////////////////////////////////////////////////#

# Determine maximum CPU clock frequency in GHz
cpu_freq := /sys/devices/system/cpu/cpu0/cpufreq
ifeq ($(wildcard $(cpu_freq)),)
  $(warning Directory $(cpu_freq) does not exist)
endif
cpu_freq_khz := $(shell cat $(cpu_freq)/scaling_max_freq)
cpu_freq_ghz := $(shell echo "scale=5; $(cpu_freq_khz)/10^6" | bc)
