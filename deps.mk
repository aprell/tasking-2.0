# Dependency generation
# Project must define SRCS
DEPS = $(addprefix deps/,$(SRCS:.c=.d))

deps/%.d: %.c
	$(CC) -MM $(CFLAGS) $(CPPFLAGS) -o $@ $<
	sed -i "s/\($*.o\)/\1 deps\/$(notdir $@)/g" $@

# Prevent make from generating dependencies when running 'make clean' or
# 'make veryclean'
ifneq ($(MAKECMDGOALS),clean)
  ifneq ($(MAKECMDGOALS),veryclean)
    -include $(DEPS)
  endif
endif
