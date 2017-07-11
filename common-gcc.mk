# Common definitions
CC = gcc
CPPFLAGS += -D_GNU_SOURCE
CFLAGS += -O3 -Wall -Wextra $(addprefix -Wno-,$(DISABLE)) $(INCLUDE)
CFLAGS += -Wstrict-aliasing=2

DISABLE = unused-function

INCLUDE = -I. -I../include
