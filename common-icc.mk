# Common definitions
CC = icc
CPPFLAGS += -D_GNU_SOURCE
CFLAGS += -O2 -Wall -Wcheck $(DISABLE) -mmic $(INCLUDE)
LDFLAGS += -mmic

# icc generates lots of remarks and warnings, especially when compiling UTS
# Disable the following:
# Warning  #161: unrecognized #pragma
# Warning #2259: non-pointer conversion [...] may lose significant bits
# Warning #3180: unrecognized OpenMP #pragma
DISABLE = -wd161,2259,3180

INCLUDE = -I. -I../include
