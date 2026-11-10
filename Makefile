# vms-hass - Linux build: the agent on the stub platform, and the tests.
#
# OpenVMS builds with build_vms.com from the same sources (DECISIONS.md D2).
# -std=c99 -pedantic is deliberate: VSI C is the target compiler, so C11 must
# not creep in.  Do not weaken these flags.

CC      ?= cc
CFLAGS  ?= -std=c99 -pedantic -Wall -Wextra -Wshadow -O2 -g
# glibc hides getaddrinfo, localtime_r and friends under strict -std=c99.
CFLAGS  += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
# Includes are by bare name: VSI C does not resolve "../" in #include paths.
CFLAGS  += -Isrc/core -Isrc/plat
LDLIBS  ?= -lssl -lcrypto -lm

CORE = src/core/ha_buf.c src/core/ha_fmt.c src/core/ha_text.c src/core/ha_json.c src/core/ha_mqtt.c \
       src/core/ha_conf.c src/core/ha_model.c src/core/ha_sess.c src/core/ha_agent.c
NET  = src/plat/ha_net_ossl.c
STUB = src/plat/stub/ha_plat_stub.c
HDRS = $(wildcard src/core/*.h src/plat/*.h)

B = build
SAN = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all

all: $(B)/hass_agent $(B)/test_core

$(B):
	mkdir -p $(B)

$(B)/hass_agent: $(CORE) $(NET) $(STUB) src/main/ha_main.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS)

$(B)/hass_agent-asan: $(CORE) $(NET) $(STUB) src/main/ha_main.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(filter %.c,$^) $(LDLIBS)

$(B)/test_core: $(CORE) tests/unit/test_core.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) -lm

$(B)/test_core-asan: $(CORE) tests/unit/test_core.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(filter %.c,$^) -lm

$(B)/fuzz_core: $(CORE) tests/unit/fuzz_core.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(filter %.c,$^) -lm

# The core as OpenVMS VAX compiles it (DECISIONS.md D11): ha_u64/ha_i64 as double,
# and ha_snprintf in place of the C library's.  No core file may name a 64-bit
# integer type, which VAX C does not have.
WIDE = -DHA_WIDE_DOUBLE -DHA_NO_SNPRINTF

$(B)/test_core-wide: $(CORE) tests/unit/test_core.c $(HDRS) | $(B)
	@if grep -nE '\b(u?int64_t|long long|__int64)\b' src/core/*.c src/core/*.h | \
	    grep -v 'src/core/ha_port.h' | grep -v '^src/core/ha_json.c:.*long long)v'; then \
	    echo "64-bit integer types in src/core (see ha_port.h)"; exit 1; fi
	@if grep -nE '[0-9]U?LL\b|\b(u?int64_t|long long)\b' tests/unit/test_core.c; then \
	    echo "64-bit integer types in the unit tests (they run on VAX too)"; exit 1; fi
	$(CC) $(CFLAGS) $(WIDE) $(SAN) -o $@ $(filter %.c,$^) -lm

test: $(B)/test_core $(B)/test_core-asan $(B)/test_core-wide
	$(B)/test_core && $(B)/test_core-asan && $(B)/test_core-wide

test-wide: $(B)/test_core-wide
	$(B)/test_core-wide

fuzz: $(B)/fuzz_core
	$(B)/fuzz_core 300000

integration: $(B)/hass_agent-asan
	tests/integration/run.sh

clean:
	rm -rf $(B)/*.o $(B)/hass_agent* $(B)/test_core* $(B)/fuzz_core

.PHONY: all test test-wide fuzz integration clean
