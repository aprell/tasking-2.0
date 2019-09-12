# Rule generation
# Project must define BUILDDIR, PROGS, SRCS, and *_SRCS
OBJS = $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))
PROGS_ = $(addprefix $(BUILDDIR)/,$(PROGS))

# NOTE: I don't recall what *_PREREQS was used for. Delete?
define RULE_template
$(1): $(BUILDDIR)/$(1)
	@ln -sf $$^ $$@

$(BUILDDIR)/$(1): $$(addprefix $(BUILDDIR)/,$$($(subst -,_,$(1))_SRCS:.c=.o)) $$($(subst -,_,$(1))_PREREQS)
	$$(CC) -o $$@ $$(filter %.o,$$^) $$(LDFLAGS) $$(IMPORTS) $$($(subst -,_,$(1))_LIBS:%=-l%)
endef

# Default clean target
define RULE_clean
clean:
	rm -f $(1) $$(PROGS_) $$(OBJS) $$(DEPS) *.a
endef

all: $(PROGS)

$(BUILDDIR)/%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(foreach prog,$(PROGS),$(eval $(call RULE_template,$(prog))))
$(eval $(call RULE_clean,$(PROGS)))

.PHONY: all clean
