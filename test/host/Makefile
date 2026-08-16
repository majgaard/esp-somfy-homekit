# Host unit tests. No ESP-IDF and no hardware needed.
#
#   make -C test/host run    plain build
#   make -C test/host asan   with AddressSanitizer and UBSan
#
# The sanitizer target is separate because ASan binaries hang under some
# sandboxed shells. Run it from a normal terminal.

ROOT := ../..
CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g \
          -I$(ROOT)/components/somfy_rts/include \
          -I$(ROOT)/components/shade_ctl/include \
          -I.

SRC := main.c test_somfy_rts.c test_shade_ctl.c \
       $(ROOT)/components/somfy_rts/somfy_rts.c \
       $(ROOT)/components/shade_ctl/shade_ctl.c

DEPS := $(SRC) test_util.h

.PHONY: run asan clean
run: build/tests
	./build/tests

asan: build/tests_asan
	./build/tests_asan

build/tests: $(DEPS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $@

build/tests_asan: $(DEPS)
	@mkdir -p build
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(SRC) -o $@

clean:
	rm -rf build
