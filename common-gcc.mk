# Common definitions
CC = gcc
CPPFLAGS += -D_GNU_SOURCE
CFLAGS += -O3 -g -Wall -Wextra $(INCLUDE) 

INCLUDE += -I. -I../include
