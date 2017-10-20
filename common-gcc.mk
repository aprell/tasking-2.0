# Common definitions
CC = gcc
CPPFLAGS += -D_GNU_SOURCE
CFLAGS += -O3 -Wall -Wextra -Wstrict-aliasing=2
CFLAGS += $(addprefix -Wno-,$(DISABLE))
CFLAGS += $(INCLUDE) $(SANITIZE)
LDFLAGS += $(SANITIZE)

DISABLE = unused-function

INCLUDE = -I. -I../include

SANITIZE := -fsanitize=address,undefined
