# Rule generation
# Project must define PROGS, SRCS, and *_SRCS
OBJS = $(SRCS:.c=.o)

define RULE_template
$(1): $$($(subst -,_,$(1))_SRCS:.c=.o) $$($(subst -,_,$(1))_PREREQS)
	$$(CC) -o $$@ $$(filter %.o,$$^) $$(LDFLAGS) $$(IMPORTS) $$($(subst -,_,$(1))_LIBS:%=-l%)
endef

# Default clean target
define RULE_clean
clean:
	rm -f $(1) $$(OBJS) $$(DEPS) *.a
endef

all: $(PROGS)

$(foreach prog,$(PROGS),$(eval $(call RULE_template,$(prog))))
$(eval $(call RULE_clean,$(PROGS)))

.PHONY: all clean
