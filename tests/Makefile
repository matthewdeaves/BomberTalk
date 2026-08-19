# Host unit tests for the BomberTalk portable core.
#
# Native gcc, no cross toolchain, no dependencies. Compiles the pure core
# units with -DBT_HOST_TEST (which swaps the Toolbox types for host shims in
# host_mac_types.h) and links each test_*.c into its own executable.
#
#   make check   # build + run all tests, non-zero exit on any failure
#   make clean
#
# test_png2pict.py is the odd one out: the asset pipeline's PICT writer is
# Python, so its test is too. It is skipped when python3 or ImageMagick is
# missing rather than failing the suite.
#
# See notes/deepening-and-testability.md.

CC     ?= cc
CFLAGS  = -std=c89 -Wall -Wextra -Wno-unused-function \
          -DBT_HOST_TEST -I. -I../include

# Each test and the core sources it exercises.
BINS = test_pixfmt test_netcoord test_movement test_tilemap_parse \
       test_collision test_raycast test_wincond test_net_wire

all: check

test_pixfmt: test_pixfmt.c ../src/pixfmt.c
	$(CC) $(CFLAGS) -o $@ test_pixfmt.c ../src/pixfmt.c

test_netcoord: test_netcoord.c ../src/netcoord.c
	$(CC) $(CFLAGS) -o $@ test_netcoord.c ../src/netcoord.c

test_movement: test_movement.c ../src/movement.c
	$(CC) $(CFLAGS) -o $@ test_movement.c ../src/movement.c

test_tilemap_parse: test_tilemap_parse.c ../src/tilemap_parse.c
	$(CC) $(CFLAGS) -o $@ test_tilemap_parse.c ../src/tilemap_parse.c

test_collision: test_collision.c ../src/collision.c
	$(CC) $(CFLAGS) -o $@ test_collision.c ../src/collision.c

test_raycast: test_raycast.c ../src/raycast.c
	$(CC) $(CFLAGS) -o $@ test_raycast.c ../src/raycast.c

test_wincond: test_wincond.c ../src/wincond.c
	$(CC) $(CFLAGS) -o $@ test_wincond.c ../src/wincond.c

test_net_wire: test_net_wire.c ../src/net_wire.c
	$(CC) $(CFLAGS) -o $@ test_net_wire.c ../src/net_wire.c

check: $(BINS)
	@fail=0; \
	for t in $(BINS); do \
	  ./$$t || fail=1; \
	done; \
	if command -v python3 >/dev/null 2>&1 && command -v magick >/dev/null 2>&1; then \
	  python3 test_png2pict.py || fail=1; \
	else \
	  echo "skipping test_png2pict.py (needs python3 + ImageMagick)"; \
	fi; \
	if [ $$fail -eq 0 ]; then \
	  echo "ALL HOST TESTS PASSED"; \
	else \
	  echo "HOST TESTS FAILED"; exit 1; \
	fi

clean:
	rm -f $(BINS)

.PHONY: all check clean
