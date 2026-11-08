# Build entry point for bbpPairingsWebAssemblyPort (our code, MIT).
#
# The engine (third_party/bbpPairings, our fork, Apache 2.0) is always built
# with its own Makefile, unchanged. We only pass variables that Makefile
# already exposes:
#   OBJ=...           out-of-tree object directory, so nothing is ever written
#                     inside the submodule
#   optimize=no       no -O3 / -flto (helper and coverage builds)
#   CXX="g++ --coverage"   instrumented build
# The engine's compile flags are never restated here.
#
# Main targets (Linux: WSL2 Ubuntu locally, ubuntu-latest in CI):
#   make native             release build -> build/native/bbpPairings
#   make test               black-box suite against ENGINE (default: native)
#   make coverage           instrumented build + suite + report + 90% gate
#   make dist               dist/native-linux-x64 with licences and VERSION
#   make golden-record CONFIRM=<submodule short sha>   re-record golden files

SHELL := /bin/bash
.DEFAULT_GOAL := native
.DELETE_ON_ERROR:

ENGINE_DIR := third_party/bbpPairings
ENGINE_MAKE := $(MAKE) -C $(ENGINE_DIR)

# Pin information (see README.md "Pinned engine").
ENGINE_FORK_URL := https://github.com/JoseFrankGeraldino/bbpPairings
ENGINE_BRANCH := master
# Fork tag on the unmodified upstream baseline (8f9e3c5); the pinned commit
# may be later. VERSION records the tag at the pinned commit, if any.
ENGINE_BASELINE_TAG := v6.0.0-lessader.1
UPSTREAM_URL := https://github.com/BieremaBoyzProgramming/bbpPairings
UPSTREAM_BASE := v6.0.0+5 (upstream master 8f9e3c5, TRF-26 fixes)

PYTHON ?= python3
JOBS ?= $(shell nproc 2>/dev/null || echo 2)
GCOVR ?= gcovr
COVERAGE_MIN_LINES ?= 90

# Number of generated tournaments in the suite. CI uses the default (300);
# override locally, e.g. BBP_GENERATED_COUNT=20 make test.
BBP_GENERATED_COUNT ?= 300
export BBP_GENERATED_COUNT

OBJ_ROOT := $(CURDIR)/build/obj

NATIVE_BIN := build/native/bbpPairings
LIMITS_BIN := build/native-limits/bbpPairings
ROUNDTRIP_BIN := build/tools/trf-roundtrip

COVERAGE_BIN := build/coverage/bin/bbpPairings
COVERAGE_LIMITS_BIN := build/coverage/bin/bbpPairings-limits
COVERAGE_ROUNDTRIP_BIN := build/coverage/bin/trf-roundtrip
COVERAGE_REPORT_DIR := build/coverage/report

ENGINE ?= $(NATIVE_BIN)
ENGINE_LIMITS ?= $(LIMITS_BIN)
ROUNDTRIP ?= $(ROUNDTRIP_BIN)

DIST_DIR := dist/native-linux-x64

# _GLIBCXX_NO_ASSERTIONS: since GCC 15, libstdc++ turns on _GLIBCXX_ASSERTIONS
# by default when not optimizing. The release build (-O3) never has them, so
# the unoptimized builds opt out to behave like the release build (no effect on
# older GCC).
NOASSERT := -D_GLIBCXX_NO_ASSERTIONS

# Test-only configuration with small compile-time limits (upstream Makefile
# variables), so that "this build only supports ..." errors are reachable with
# small inputs. The generator and checker need wider types (compile-time
# asserts in tournament/generator.h), so they are left out
# (engine_comparison=no); optimize=no because that configuration hits a GCC 15
# LTO false positive (-Wstrict-overflow inside libstdc++) at -O3 -flto.
# Never packaged.
LIMITS_VARS := engine_comparison=no optimize=no \
	max_players=40 max_points=99 max_rating=255 max_rounds=9

.PHONY: native native-limits tools test coverage coverage-build coverage-run \
	coverage-report dist golden-record clean check-submodule

check-submodule:
	@test -f $(ENGINE_DIR)/Makefile || { \
		echo "Submodule missing: run 'git submodule update --init'" >&2; exit 1; }

# --- native release build ------------------------------------------------------

native: check-submodule
	$(ENGINE_MAKE) -j$(JOBS) OBJ=$(OBJ_ROOT)/native $(OBJ_ROOT)/native/bbpPairings.exe
	install -D -m 755 $(OBJ_ROOT)/native/bbpPairings.exe $(NATIVE_BIN)

native-limits: check-submodule
	$(ENGINE_MAKE) -j$(JOBS) OBJ=$(OBJ_ROOT)/native-limits $(LIMITS_VARS) \
		CXX="g++ $(NOASSERT)" $(OBJ_ROOT)/native-limits/bbpPairings.exe
	install -D -m 755 $(OBJ_ROOT)/native-limits/bbpPairings.exe $(LIMITS_BIN)

# --- white-box helper ------------------------------------------------------------
# trf-roundtrip links the engine's own objects (all except main.o), built by the
# engine Makefile with optimize=no (no LTO), plus tests/roundtrip/trf_roundtrip.cpp.
# Only -std, the include path and NDEBUG (to match the engine objects) are given.

HELPER_CXXFLAGS := -std=c++20 -DNDEBUG -I$(ENGINE_DIR)/src
# Expanded by the shell when the recipe runs (the objects do not exist yet
# when make expands the recipe).
ENGINE_LIB_OBJS = $$(find $(1) -name '*.o' ! -name main.o | sort)

tools: check-submodule
	$(ENGINE_MAKE) -j$(JOBS) OBJ=$(OBJ_ROOT)/plain optimize=no CXX="g++ $(NOASSERT)" \
		$(OBJ_ROOT)/plain/bbpPairings.exe
	mkdir -p $(dir $(ROUNDTRIP_BIN))
	g++ $(NOASSERT) $(HELPER_CXXFLAGS) -o $(ROUNDTRIP_BIN) tests/roundtrip/trf_roundtrip.cpp \
		$(call ENGINE_LIB_OBJS,$(OBJ_ROOT)/plain)

# --- test suite -----------------------------------------------------------------
# ENGINE can be any executable that behaves like the bbpPairings CLI (in E2 a
# Node runner script for the WebAssembly build). ENGINE_LIMITS is the same
# engine built with LIMITS_VARS (limits group); ENGINE_LIMITS= skips that
# group, and the runner says so.

TEST_DEPS := tools
ifeq ($(ENGINE),$(NATIVE_BIN))
TEST_DEPS += native
endif
ifeq ($(ENGINE_LIMITS),$(LIMITS_BIN))
TEST_DEPS += native-limits
endif

test: $(TEST_DEPS)
	$(PYTHON) tests/run_tests.py --engine $(ENGINE) \
		--engine-limits "$(ENGINE_LIMITS)" --roundtrip $(ROUNDTRIP)

# --- coverage -------------------------------------------------------------------
# Instrumented builds through the engine's own Makefile: optimize=no (drops -O3
# and -flto) and CXX="g++ --coverage". Everything else (warnings, -std=c++20,
# -DNDEBUG) is the engine's release configuration (plus LIMITS_VARS for the
# limits build). Objects and .gcda files stay in build/obj/coverage*.

COV_CXX := g++ --coverage $(NOASSERT)

coverage-build: check-submodule
	$(ENGINE_MAKE) -j$(JOBS) OBJ=$(OBJ_ROOT)/coverage optimize=no CXX="$(COV_CXX)" \
		$(OBJ_ROOT)/coverage/bbpPairings.exe
	$(ENGINE_MAKE) -j$(JOBS) OBJ=$(OBJ_ROOT)/coverage-limits $(LIMITS_VARS) \
		CXX="$(COV_CXX)" $(OBJ_ROOT)/coverage-limits/bbpPairings.exe
	install -D -m 755 $(OBJ_ROOT)/coverage/bbpPairings.exe $(COVERAGE_BIN)
	install -D -m 755 $(OBJ_ROOT)/coverage-limits/bbpPairings.exe $(COVERAGE_LIMITS_BIN)
	$(COV_CXX) $(HELPER_CXXFLAGS) -o $(COVERAGE_ROUNDTRIP_BIN) \
		tests/roundtrip/trf_roundtrip.cpp $(call ENGINE_LIB_OBJS,$(OBJ_ROOT)/coverage)

coverage-run: coverage-build
	find $(OBJ_ROOT)/coverage $(OBJ_ROOT)/coverage-limits build/coverage/bin \
		-name '*.gcda' -delete
	$(PYTHON) tests/run_tests.py --instrumented --engine $(COVERAGE_BIN) \
		--engine-limits $(COVERAGE_LIMITS_BIN) --roundtrip $(COVERAGE_ROUNDTRIP_BIN)

# Report and gate only. Fails when no coverage data exists (e.g. after deleting
# the .gcda files) or when line coverage is below COVERAGE_MIN_LINES. Data of
# both instrumented builds (same sources) is merged.
coverage-report:
	$(PYTHON) tests/coverage_gate.py --gcovr $(GCOVR) \
		--min-lines $(COVERAGE_MIN_LINES) --engine-dir $(ENGINE_DIR) \
		--object-dir $(OBJ_ROOT)/coverage --object-dir $(OBJ_ROOT)/coverage-limits \
		--report-dir $(COVERAGE_REPORT_DIR)

coverage: coverage-run
	$(MAKE) --no-print-directory coverage-report

# --- packaging ------------------------------------------------------------------

dist: native
	rm -rf $(DIST_DIR)
	mkdir -p $(DIST_DIR)
	install -m 755 $(NATIVE_BIN) $(DIST_DIR)/bbpPairings
	install -m 644 $(ENGINE_DIR)/LICENSE.txt $(ENGINE_DIR)/Apache-2.0.txt NOTICE \
		$(DIST_DIR)/
	{ \
		echo "engine_fork=$(ENGINE_FORK_URL)"; \
		echo "engine_branch=$(ENGINE_BRANCH)"; \
		echo "engine_commit=$$(git -C $(ENGINE_DIR) rev-parse HEAD)"; \
		echo "engine_tag=$$(git -C $(ENGINE_DIR) tag --points-at HEAD | head -1 | grep . || echo none)"; \
		echo "engine_baseline_tag=$(ENGINE_BASELINE_TAG) (8f9e3c5, unmodified upstream master)"; \
		echo "upstream=$(UPSTREAM_URL)"; \
		echo "upstream_base=$(UPSTREAM_BASE)"; \
		echo "port_repo_commit=$$(git rev-parse HEAD)$$(git diff --quiet HEAD 2>/dev/null || echo -dirty)"; \
		echo "target=native-linux-x64"; \
	} > $(DIST_DIR)/VERSION
	@echo "dist: $(DIST_DIR)"; ls -l $(DIST_DIR)

# --- golden files ---------------------------------------------------------------
# Golden files are recorded from the native release build of the pinned commit
# only, and change only by a reviewed decision (a pin bump).

golden-record: native native-limits tools
	@actual=$$(git -C $(ENGINE_DIR) rev-parse --short=7 HEAD); \
	if [ "$(CONFIRM)" != "$$actual" ]; then \
		echo "Refusing to record golden files: pass CONFIRM=$$actual (the submodule commit) after review." >&2; \
		exit 1; \
	fi
	$(PYTHON) tests/run_tests.py --record --engine $(NATIVE_BIN) \
		--engine-limits $(LIMITS_BIN) --roundtrip $(ROUNDTRIP_BIN)

clean:
	rm -rf build dist
