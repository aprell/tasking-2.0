BUILDDIR := build
LIBDIR := lib

ifeq ($(CC),gcc)
  include gcc.mk
else ifeq ($(CC),icc)
  include icc.mk
else
  include gcc.mk
endif

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
CPPFLAGS += -DBACKOFF=wait_cond

INCLUDE += -Iinclude -Isrc -Isrc/channel_shm
CFLAGS += -pthread
LDFLAGS += -pthread

ifeq ($(SANITIZE),1)
  CFLAGS += -fsanitize=address,undefined
  LDFLAGS += -fsanitize=address,undefined
endif

# Profile with coz run --- ./prog args
ifeq ($(USE_COZ),1)
  $(warning Please update COZ_ROOT)
  COZ_ROOT := path/to/coz
  CPPFLAGS += -DUSE_COZ
  CFLAGS += -g
  INCLUDE += -I$(COZ_ROOT)/include
  LDLIBS += -ldl
endif

#//// SOURCE FILES /////////////////////////////////////////////////////////#

tasking_SRCS := \
  channel.c \
  deque.c \
  runtime.c \
  tasking.c

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
  uts_seq.c \
  uts_shm.c \
  $(tasking_SRCS)

#//// TEST & BENCHMARK PROGRAMS ////////////////////////////////////////////#

PROGS := \
  barrier \
  bpc \
  cilksort \
  fib \
  fibgen \
  fib-like \
  loopsched \
  lu \
  mm \
  mm_dac \
  nbody3 \
  nqueens \
  qsort \
  spc \
  task_example \
  test_async \
  uts-par \
  uts-seq

barrier_SRCS      := barrier.c $(tasking_SRCS)
bpc_SRCS          := bpc.c $(tasking_SRCS)
cilksort_SRCS     := cilksort.c getoptions.c $(tasking_SRCS)
fib_SRCS          := fib.c $(tasking_SRCS)
fibgen_SRCS       := fibgen.c $(tasking_SRCS)
fib_like_SRCS     := fib-like.c $(tasking_SRCS)
loopsched_SRCS    := loopsched.c $(tasking_SRCS)
lu_SRCS           := lu.c $(tasking_SRCS)
mm_SRCS           := mm.c $(tasking_SRCS)
mm_dac_SRCS       := mm_dac.c $(tasking_SRCS)
nbody3_SRCS       := nbody3.c $(tasking_SRCS)
nqueens_SRCS      := nqueens.c $(tasking_SRCS)
qsort_SRCS        := qsort.c $(tasking_SRCS)
spc_SRCS          := spc.c $(tasking_SRCS)
task_example_SRCS := task_example.c $(tasking_SRCS)
test_async_SRCS   := test_async.c $(tasking_SRCS)
uts_par_SRCS      := uts_shm.c uts.c brg_sha1.c $(tasking_SRCS)
uts_seq_SRCS      := uts_seq.c uts.c brg_sha1.c

nbody3_LIBS  := m
uts_par_LIBS := m
uts_seq_LIBS := m

#///////////////////////////////////////////////////////////////////////////#

VPATH += src src/channel_shm test test/rng

include rules.mk
include deps.mk

all:: libtasking

test: $(PROGS)

libtasking: $(LIBDIR)/libtasking.a

$(LIBDIR)/libtasking.a: $(BUILDDIR)/libtasking.a
	mkdir -p $(LIBDIR)
	ln -sf ../$< $@

$(BUILDDIR)/libtasking.a: $(addprefix $(BUILDDIR)/,$(tasking_SRCS:.c=.o))
	$(AR) rc $@ $^

clean::
	rm -rf $(BUILDDIR) $(LIBDIR)

help:
	@echo
	@echo "Usage:"
	@echo "  make (all)         Build libtasking.a and all test programs"
	@echo "  make libtasking    Build libtasking.a"
	@echo "  make test          Build all test programs"
	@echo "  make <prog>        Build test program <prog>"
	@echo "  make clean         Remove all build artifacts"
	@echo

.PHONY: all test libtasking clean help

#//// EXPERIMENTAL /////////////////////////////////////////////////////////#

# Determine maximum CPU clock frequency in GHz
cpu_freq := /sys/devices/system/cpu/cpu0/cpufreq
ifeq ($(wildcard $(cpu_freq)),)
  $(warning Directory $(cpu_freq) does not exist)
endif
cpu_freq_khz := $(shell cat $(cpu_freq)/scaling_max_freq)
cpu_freq_ghz := $(shell echo "scale=5; $(cpu_freq_khz)/10^6" | bc)
