# Build the interpreter:  make  ->  ./ruby
CC      ?= cc
TARGET  ?= 33
CFLAGS  ?= -O2 -std=gnu11 -DRUBY_TARGET=$(TARGET) -MMD -MP -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function
LDFLAGS ?=
LDLIBS  ?= -lm

SRCS = core.c alloc.c parse.c eval.c init.c object.c enum.c misc.c struct.c \
       string.c array.c hash.c numeric.c regexp.c enumerator.c main.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

ruby: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# -MMD -MP records each object's real header dependencies, so editing a
# header rebuilds everything that includes it rather than silently not.
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) ruby

.PHONY: clean
