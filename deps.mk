# Dependency generation
# Project must define BUILDDIR and SRCS
DEPS = $(addprefix $(BUILDDIR)/deps/,$(SRCS:.c=.d))

$(BUILDDIR)/deps/%.d: %.c
	$(CC) -MM $(CFLAGS) $(CPPFLAGS) -o $@ $<
	sed -i "s/\($*.o\)/$(BUILDDIR)\/\1 $(BUILDDIR)\/deps\/$(notdir $@)/g" $@

# Create directory if necessary
ifeq ($(wildcard $(BUILDDIR)/deps),)
  $(info $(shell mkdir -p $(BUILDDIR)/deps))
endif

# Prevent make from generating dependencies when running 'make clean'
ifneq ($(MAKECMDGOALS),clean)
  -include $(DEPS)
endif
