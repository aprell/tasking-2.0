# Common definitions
CC = gcc
CPPFLAGS += -D_GNU_SOURCE
CFLAGS += -O3 -Wall -Wextra $(INCLUDE)

INCLUDE = -I. -I../include
