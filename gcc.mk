# Common definitions
CPPFLAGS += -D_GNU_SOURCE
CFLAGS += -O3 -Wall -Wextra -Wstrict-aliasing=2
CFLAGS += $(addprefix -Wno-,$(DISABLE))
CFLAGS += $(INCLUDE)

DISABLE = unused-function
