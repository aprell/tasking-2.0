# Common definitions
CC = icc
CPPFLAGS += -D_GNU_SOURCE 
CFLAGS += -O2 -Wall -Wcheck -mmic $(INCLUDE)
LDFLAGS += -mmic

INCLUDE = -I. -I../include
