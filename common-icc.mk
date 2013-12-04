# Common definitions
CC = icc
CPPFLAGS += -D_GNU_SOURCE 
CFLAGS += -O3 -g -Wall -Wcheck -mmic $(INCLUDE)
LDFLAGS += -mmic

INCLUDE = -I. -I../include
