# Common definitions
CC = gcc
CPPFLAGS += -D_GNU_SOURCE
CFLAGS += -O3 -Wall -Wextra $(addprefix -Wno-,$(DISABLE)) $(INCLUDE)

DISABLE = unused-function

INCLUDE = -I. -I../include
