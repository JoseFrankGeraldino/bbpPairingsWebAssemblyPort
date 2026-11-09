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
#   make tiebreak           our tie-break tool -> build/native/tiebreak
#   make test               engine suite (test-engine) + tie-break comparison
#                           with Gacrux (test-tiebreak)
#   make coverage           instrumented builds + both suites + reports + gates
#                           (engine 90%, tie-break tool 85%)
#   make dist               dist/native-linux-x64 with licences and VERSION
#   make golden-record CONFIRM=<submodule short sha>   re-record golden files
#   make wasm               WebAssembly builds (Emscripten): Node command lines
#                           in build/wasm, browser modules in build/wasm/web
#   make test-wasm          the same suites against the WebAssembly builds
#   make dist-wasm          dist/wasm: modules, bridge, licences, VERSION
#   make golden-record-wasm CONFIRM=<sha>   re-record tests/golden-wasm

SHELL := /bin/bash
.DEFAULT_GOAL := native
.DELETE_ON_ERROR:

# Reproducible builds: the engine prints __DATE__ and __TIME__ in its version
# line (src/main.cpp); GCC and clang take both from SOURCE_DATE_EPOCH when it
# is set. The commit time of this repository makes one commit build the same
# bytes anywhere (README "Release packages").
SOURCE_DATE_EPOCH ?= $(shell git log -1 --format=%ct HEAD 2>/dev/null)
export SOURCE_DATE_EPOCH

ENGINE_DIR := third_party/bbpPairings
# version: the engine Makefile takes it from "git describe" in the submodule,
# which depends on the tags a checkout happens to have; it ends up in the
# version line of every build. We pass the fork commit instead, so the bytes
# do not depend on the checkout and the program names its exact engine source.
ENGINE_VERSION := fork-$(shell git -C $(ENGINE_DIR) rev-parse HEAD 2>/dev/null | cut -c1-12)
ENGINE_MAKE := $(MAKE) -C $(ENGINE_DIR) version=$(ENGINE_VERSION)

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

# Tie-break tool (tiebreak/, our code, MIT; rules ported from Gacrux, MIT).
# Its reference, FIDE's Gacrux (TieBreakServer), is pinned by commit and runs
# with the Python standard library only.
GACRUX_URL := https://github.com/OttoMilvang/TieBreakServer.git
GACRUX_COMMIT := 49b01e67ccb2f94945fa97b97ade66a35584b341
GACRUX_DIR ?= build/gacrux
# Generated tournaments per generator source in the comparison.
BBP_TIEBREAK_COUNT ?= 20
BBP_TIEBREAK_COVERAGE_COUNT ?= 5
TIEBREAK_COVERAGE_MIN_LINES ?= 85

OBJ_ROOT := $(CURDIR)/build/obj

NATIVE_BIN := build/native/bbpPairings
LIMITS_BIN := build/native-limits/bbpPairings
ROUNDTRIP_BIN := build/tools/trf-roundtrip

COVERAGE_BIN := build/coverage/bin/bbpPairings
COVERAGE_LIMITS_BIN := build/coverage/bin/bbpPairings-limits
COVERAGE_ROUNDTRIP_BIN := build/coverage/bin/trf-roundtrip
COVERAGE_REPORT_DIR := build/coverage/report

TIEBREAK_BIN := build/native/tiebreak
COVERAGE_TIEBREAK_BIN := build/coverage/bin/tiebreak
COVERAGE_TIEBREAK_REPORT_DIR := build/coverage/tiebreak-report
TIEBREAK_SOURCES := $(sort $(wildcard tiebreak/*.cpp))
TIEBREAK_HEADERS := $(sort $(wildcard tiebreak/*.h))

ENGINE ?= $(NATIVE_BIN)
ENGINE_LIMITS ?= $(LIMITS_BIN)
ROUNDTRIP ?= $(ROUNDTRIP_BIN)
TIEBREAK ?= $(TIEBREAK_BIN)

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

.PHONY: native native-limits tools tiebreak gacrux test test-engine \
	test-tiebreak coverage coverage-build coverage-run coverage-report dist \
	golden-record clean check-submodule wasm wasm-toolchain wasm-node \
	wasm-limits wasm-tools wasm-tiebreak wasm-web test-wasm \
	test-wasm-engine test-wasm-tiebreak test-wasm-bridge dist-wasm \
	golden-record-wasm package test-package emsdk

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

# --- tie-break tool -------------------------------------------------------------
# Our own C++ (tiebreak/), so the flags are ours: the engine Makefile's warning
# set, which the C++ rules (documentation-repo bbpPairings/AGENTS.md) require
# all C++ in this repository to pass.

TIEBREAK_WARNINGS := -std=c++20 -ftabstop=2 -Werror -Wfatal-errors -pedantic \
	-pedantic-errors -Wall -Walloca -Wcast-qual -Wctor-dtor-privacy \
	-Wdisabled-optimization -Wdouble-promotion -Wextra -Wextra-semi -Wformat=2 \
	-Winvalid-pch -Wmismatched-tags -Wmissing-braces -Wmissing-declarations \
	-Wmissing-include-dirs -Woverloaded-virtual -Wpacked -Wpointer-arith \
	-Wredundant-decls -Wsign-promo -Wstrict-overflow=2 -Wsuggest-override \
	-Wswitch-default -Wundef -Wunknown-pragmas -Wunused-macros -Wvla \
	-Wzero-as-null-pointer-constant

tiebreak: $(TIEBREAK_BIN)

$(TIEBREAK_BIN): $(TIEBREAK_SOURCES) $(TIEBREAK_HEADERS)
	mkdir -p $(dir $@)
	g++ $(TIEBREAK_WARNINGS) -O2 -DNDEBUG -o $@ $(TIEBREAK_SOURCES)

# The reference implementation at the pinned commit (CI checks it out into
# GACRUX_DIR beforehand; locally it is cloned on first use).
gacrux:
	@if [ ! -d $(GACRUX_DIR)/.git ]; then \
		git clone -q $(GACRUX_URL) $(GACRUX_DIR); fi
	@if [ "$$(git -C $(GACRUX_DIR) rev-parse HEAD)" != "$(GACRUX_COMMIT)" ]; then \
		git -C $(GACRUX_DIR) fetch -q origin $(GACRUX_COMMIT) \
		&& git -C $(GACRUX_DIR) checkout -q --detach $(GACRUX_COMMIT); fi
	@test "$$(git -C $(GACRUX_DIR) rev-parse HEAD)" = "$(GACRUX_COMMIT)" \
		&& test -z "$$(git -C $(GACRUX_DIR) status --porcelain)" \
		|| { echo "$(GACRUX_DIR) is not Gacrux at $(GACRUX_COMMIT)" >&2; exit 1; }

# The pinned Emscripten SDK (see "WebAssembly" below). emsdk install skips a
# version already installed.
emsdk:
	@if [ ! -d $(EMSDK_DIR)/.git ]; then \
		git clone -q $(EMSDK_URL) $(EMSDK_DIR); fi
	@if [ "$$(git -C $(EMSDK_DIR) rev-parse HEAD)" != "$(EMSDK_COMMIT)" ]; then \
		git -C $(EMSDK_DIR) fetch -q origin $(EMSDK_COMMIT) \
		&& git -C $(EMSDK_DIR) checkout -q --detach $(EMSDK_COMMIT); fi
	@test "$$(git -C $(EMSDK_DIR) rev-parse HEAD)" = "$(EMSDK_COMMIT)" \
		|| { echo "$(EMSDK_DIR) is not emsdk at $(EMSDK_COMMIT)" >&2; exit 1; }
	$(EMSDK_DIR)/emsdk install $(EMSDK_VERSION)
	$(EMSDK_DIR)/emsdk activate $(EMSDK_VERSION)

# --- test suite -----------------------------------------------------------------
# ENGINE can be any executable that behaves like the bbpPairings CLI (in E2 a
# Node runner script for the WebAssembly build). ENGINE_LIMITS is the same
# engine built with LIMITS_VARS (limits group); ENGINE_LIMITS= skips that
# group, and the runner says so. TIEBREAK is any executable with our
# tie-break tool's command line; ENGINE generates its test tournaments.

TEST_DEPS := tools
ifeq ($(ENGINE),$(NATIVE_BIN))
TEST_DEPS += native
endif
ifeq ($(ENGINE_LIMITS),$(LIMITS_BIN))
TEST_DEPS += native-limits
endif
TIEBREAK_TEST_DEPS := gacrux
ifeq ($(ENGINE),$(NATIVE_BIN))
TIEBREAK_TEST_DEPS += native
endif
ifeq ($(TIEBREAK),$(TIEBREAK_BIN))
TIEBREAK_TEST_DEPS += tiebreak
endif

test: test-engine test-tiebreak

test-engine: $(TEST_DEPS)
	$(PYTHON) tests/run_tests.py --engine $(ENGINE) \
		--engine-limits "$(ENGINE_LIMITS)" --roundtrip $(ROUNDTRIP)

test-tiebreak: $(TIEBREAK_TEST_DEPS)
	$(PYTHON) tests/tiebreak_compare.py --tool $(TIEBREAK) \
		--gacrux $(GACRUX_DIR)/gacrux --engine $(ENGINE) \
		--count $(BBP_TIEBREAK_COUNT)

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
	mkdir -p $(OBJ_ROOT)/coverage-tiebreak
	for source in $(TIEBREAK_SOURCES); do \
		$(COV_CXX) $(TIEBREAK_WARNINGS) -O0 -DNDEBUG -c $$source \
			-o $(OBJ_ROOT)/coverage-tiebreak/$$(basename $$source .cpp).o || exit 1; \
	done
	$(COV_CXX) -o $(COVERAGE_TIEBREAK_BIN) $(OBJ_ROOT)/coverage-tiebreak/*.o

coverage-run: coverage-build gacrux
	find $(OBJ_ROOT)/coverage $(OBJ_ROOT)/coverage-limits \
		$(OBJ_ROOT)/coverage-tiebreak build/coverage/bin -name '*.gcda' -delete
	$(PYTHON) tests/run_tests.py --instrumented --engine $(COVERAGE_BIN) \
		--engine-limits $(COVERAGE_LIMITS_BIN) --roundtrip $(COVERAGE_ROUNDTRIP_BIN)
	$(PYTHON) tests/tiebreak_compare.py --tool $(COVERAGE_TIEBREAK_BIN) \
		--gacrux $(GACRUX_DIR)/gacrux --engine $(COVERAGE_BIN) \
		--count $(BBP_TIEBREAK_COVERAGE_COUNT)

# Report and gate only. Fails when no coverage data exists (e.g. after deleting
# the .gcda files) or when line coverage is below COVERAGE_MIN_LINES (engine)
# or TIEBREAK_COVERAGE_MIN_LINES (tie-break tool). Data of both instrumented
# engine builds (same sources) is merged.
coverage-report:
	$(PYTHON) tests/coverage_gate.py --gcovr $(GCOVR) \
		--min-lines $(COVERAGE_MIN_LINES) --engine-dir $(ENGINE_DIR) \
		--object-dir $(OBJ_ROOT)/coverage --object-dir $(OBJ_ROOT)/coverage-limits \
		--report-dir $(COVERAGE_REPORT_DIR)
	$(PYTHON) tests/coverage_gate.py --gcovr $(GCOVR) \
		--min-lines $(TIEBREAK_COVERAGE_MIN_LINES) --source-dir tiebreak \
		--title "tie-break tool coverage (tiebreak/)" \
		--object-dir $(OBJ_ROOT)/coverage-tiebreak \
		--report-dir $(COVERAGE_TIEBREAK_REPORT_DIR)

coverage: coverage-run
	$(MAKE) --no-print-directory coverage-report

# --- packaging ------------------------------------------------------------------

dist: native tiebreak
	rm -rf $(DIST_DIR)
	mkdir -p $(DIST_DIR)
	install -m 755 $(NATIVE_BIN) $(DIST_DIR)/bbpPairings
	install -m 755 $(TIEBREAK_BIN) $(DIST_DIR)/tiebreak
	install -m 644 $(ENGINE_DIR)/LICENSE.txt $(ENGINE_DIR)/Apache-2.0.txt NOTICE \
		$(DIST_DIR)/
	install -m 644 LICENSE $(DIST_DIR)/LICENSE-tiebreak.txt
	install -m 644 tiebreak/GACRUX-LICENSE.txt $(DIST_DIR)/GACRUX-LICENSE.txt
	{ \
		echo "engine_fork=$(ENGINE_FORK_URL)"; \
		echo "engine_branch=$(ENGINE_BRANCH)"; \
		echo "engine_commit=$$(git -C $(ENGINE_DIR) rev-parse HEAD)"; \
		echo "engine_tag=$$(git -C $(ENGINE_DIR) tag --points-at HEAD | head -1 | grep . || echo none)"; \
		echo "engine_baseline_tag=$(ENGINE_BASELINE_TAG) (8f9e3c5, unmodified upstream master)"; \
		echo "upstream=$(UPSTREAM_URL)"; \
		echo "upstream_base=$(UPSTREAM_BASE)"; \
		echo "port_repo_commit=$$(git rev-parse HEAD)$$(git diff --quiet HEAD 2>/dev/null || echo -dirty)"; \
		echo "tiebreak_reference=Gacrux $(GACRUX_URL) $(GACRUX_COMMIT)"; \
		echo "target=native-linux-x64"; \
		echo "requires=$$(objdump -T $(NATIVE_BIN) $(TIEBREAK_BIN) | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1) $$(objdump -T $(NATIVE_BIN) $(TIEBREAK_BIN) | grep -o 'GLIBCXX_[0-9.]*' | sort -uV | tail -1) (glibc and GCC libstdc++.so.6, linked dynamically; not musl)"; \
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

golden-record-wasm: wasm-node wasm-limits wasm-tools
	@actual=$$(git -C $(ENGINE_DIR) rev-parse --short=7 HEAD); \
	if [ "$(CONFIRM)" != "$$actual" ]; then \
		echo "Refusing to record golden files: pass CONFIRM=$$actual (the submodule commit) after review." >&2; \
		exit 1; \
	fi
	$(PYTHON) tests/run_tests.py --record --target wasm --engine $(WASM_BIN) \
		--engine-limits $(WASM_LIMITS_BIN) --roundtrip $(WASM_ROUNDTRIP_BIN)

# --- WebAssembly (E2) -------------------------------------------------------------
# The engine is built by its own Makefile with Emscripten: COMP=clang selects
# its clang warning set, CXX="em++ -fwasm-exceptions" the compiler (Wasm
# exception handling: the engine reports every error by an exception, which
# Emscripten aborts on by default), and optional_ldflags replaces the link
# flags (the default ones carry a bare "-s", strip for GNU ld, which emcc reads
# as a setting). The compile flags are the engine's own.
#
# The toolchain is pinned: the generator's random distributions come from
# Emscripten's libc++, so tests/golden-wasm holds for this version only.
# em++ is taken from PATH, else from EMSDK_ENV.

EMSDK_VERSION := 7.0.0
EMSDK_ENV ?= $(HOME)/emsdk/emsdk_env.sh
# make emsdk installs that version from emsdk's 7.0.0 release commit into
# EMSDK_DIR (CI, and any pipeline building from this repository, with
# EMSDK_ENV=<absolute EMSDK_DIR>/emsdk_env.sh).
EMSDK_URL := https://github.com/emscripten-core/emsdk.git
EMSDK_COMMIT := 1693ce7359fc9694f198694f902a2cc061239b86
EMSDK_DIR ?= build/emsdk
WASM_ENV := $(if $(shell command -v em++ 2>/dev/null),,source $(EMSDK_ENV) > /dev/null &&)
WASM_CXX := em++ -fwasm-exceptions
# Node command lines with the bbpPairings and tie-break CLI contract: real
# files relative to the working directory, the program's exit code.
WASM_NODE_LDFLAGS := -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sNODERAWFS=1
# Browser (and Web Worker) ES modules: an in-memory file system, main called
# by the bridge (bridge/bbp-bridge.ts) on a fresh instance per run.
WASM_WEB_LDFLAGS := -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_ES6=1 \
	-sINVOKE_RUN=0 -sEXIT_RUNTIME=1 -sFORCE_FILESYSTEM=1 \
	-sENVIRONMENT=web,worker,node -sEXPORTED_RUNTIME_METHODS=callMain,FS

WASM_BIN := build/wasm/bbpPairings
WASM_LIMITS_BIN := build/wasm-limits/bbpPairings
WASM_ROUNDTRIP_BIN := build/wasm/trf-roundtrip
WASM_TIEBREAK_BIN := build/wasm/tiebreak
WASM_WEB_DIR := build/wasm/web
DIST_WASM_DIR := dist/wasm

# A Node command line: the Emscripten loader with a node shebang, next to its
# .wasm. Run as the file itself, the program sees its own path as argv[0], as
# a native executable does. $(1) = loader, $(2) = target.
define wasm_node_install
	mkdir -p $(dir $(2))
	{ echo '#!/usr/bin/env node'; cat $(1); } > $(2)
	chmod 755 $(2)
	install -m 644 $(basename $(1)).wasm $(dir $(2))
endef

wasm: wasm-node wasm-limits wasm-tools wasm-tiebreak wasm-web

wasm-toolchain:
	@$(WASM_ENV) version=$$(em++ --version | head -1); \
	case "$$version" in *" $(EMSDK_VERSION) "*) ;; \
		*) echo "Emscripten $(EMSDK_VERSION) required, found: $$version" >&2; exit 1;; esac

wasm-node: check-submodule wasm-toolchain
	$(WASM_ENV) $(ENGINE_MAKE) -j$(JOBS) OBJ=$(OBJ_ROOT)/wasm COMP=clang \
		CXX="$(WASM_CXX)" optional_ldflags="$(WASM_NODE_LDFLAGS)" \
		$(OBJ_ROOT)/wasm/bbpPairings.exe
	$(call wasm_node_install,$(OBJ_ROOT)/wasm/bbpPairings.exe,$(WASM_BIN))

wasm-limits: check-submodule wasm-toolchain
	$(WASM_ENV) $(ENGINE_MAKE) -j$(JOBS) OBJ=$(OBJ_ROOT)/wasm-limits COMP=clang \
		$(LIMITS_VARS) CXX="$(WASM_CXX)" optional_ldflags="$(WASM_NODE_LDFLAGS)" \
		$(OBJ_ROOT)/wasm-limits/bbpPairings.exe
	$(call wasm_node_install,$(OBJ_ROOT)/wasm-limits/bbpPairings.exe,$(WASM_LIMITS_BIN))

# The round-trip helper on the WebAssembly engine objects (optimize=no, as
# for the native helper).
wasm-tools: check-submodule wasm-toolchain
	$(WASM_ENV) $(ENGINE_MAKE) -j$(JOBS) OBJ=$(OBJ_ROOT)/wasm-plain COMP=clang \
		optimize=no CXX="$(WASM_CXX)" optional_ldflags="$(WASM_NODE_LDFLAGS)" \
		$(OBJ_ROOT)/wasm-plain/bbpPairings.exe
	mkdir -p $(OBJ_ROOT)/wasm-tools
	$(WASM_ENV) $(WASM_CXX) $(HELPER_CXXFLAGS) $(WASM_NODE_LDFLAGS) \
		-o $(OBJ_ROOT)/wasm-tools/trf-roundtrip.js tests/roundtrip/trf_roundtrip.cpp \
		$(call ENGINE_LIB_OBJS,$(OBJ_ROOT)/wasm-plain)
	$(call wasm_node_install,$(OBJ_ROOT)/wasm-tools/trf-roundtrip.js,$(WASM_ROUNDTRIP_BIN))

wasm-tiebreak: wasm-toolchain $(TIEBREAK_SOURCES) $(TIEBREAK_HEADERS)
	mkdir -p $(OBJ_ROOT)/wasm-tiebreak
	$(WASM_ENV) $(WASM_CXX) $(TIEBREAK_WARNINGS) -O2 -DNDEBUG $(WASM_NODE_LDFLAGS) \
		-o $(OBJ_ROOT)/wasm-tiebreak/tiebreak.js $(TIEBREAK_SOURCES)
	$(call wasm_node_install,$(OBJ_ROOT)/wasm-tiebreak/tiebreak.js,$(WASM_TIEBREAK_BIN))

wasm-web: check-submodule wasm-toolchain $(TIEBREAK_SOURCES) $(TIEBREAK_HEADERS)
	$(WASM_ENV) $(ENGINE_MAKE) -j$(JOBS) OBJ=$(OBJ_ROOT)/wasm-web COMP=clang \
		CXX="$(WASM_CXX)" \
		optional_ldflags="$(WASM_WEB_LDFLAGS) -sEXPORT_NAME=createBbpPairings" \
		$(OBJ_ROOT)/wasm-web/bbpPairings.exe
	mkdir -p $(WASM_WEB_DIR) $(OBJ_ROOT)/wasm-web-tiebreak
	install -m 644 $(OBJ_ROOT)/wasm-web/bbpPairings.exe $(WASM_WEB_DIR)/bbpPairings.mjs
	install -m 644 $(OBJ_ROOT)/wasm-web/bbpPairings.wasm $(WASM_WEB_DIR)/
	$(WASM_ENV) $(WASM_CXX) $(TIEBREAK_WARNINGS) -O2 -DNDEBUG $(WASM_WEB_LDFLAGS) \
		-sEXPORT_NAME=createTiebreak \
		-o $(OBJ_ROOT)/wasm-web-tiebreak/tiebreak.mjs $(TIEBREAK_SOURCES)
	install -m 644 $(OBJ_ROOT)/wasm-web-tiebreak/tiebreak.mjs \
		$(OBJ_ROOT)/wasm-web-tiebreak/tiebreak.wasm $(WASM_WEB_DIR)/

# The engine suite (golden overlay tests/golden-wasm for the cases listed in
# run_tests.py TARGET_DIFFERENCES), the Gacrux comparison of the tie-break
# tool (tournaments generated by the WebAssembly engine), and the bridge.
test-wasm: test-wasm-engine test-wasm-tiebreak test-wasm-bridge

test-wasm-engine: wasm-node wasm-limits wasm-tools
	$(PYTHON) tests/run_tests.py --target wasm --engine $(WASM_BIN) \
		--engine-limits $(WASM_LIMITS_BIN) --roundtrip $(WASM_ROUNDTRIP_BIN)

test-wasm-tiebreak: wasm-node wasm-tiebreak gacrux
	$(PYTHON) tests/tiebreak_compare.py --tool $(WASM_TIEBREAK_BIN) \
		--gacrux $(GACRUX_DIR)/gacrux --engine $(WASM_BIN) \
		--count $(BBP_TIEBREAK_COUNT)

test-wasm-bridge: wasm-web wasm-node native tiebreak
	$(WASM_ENV) node tests/bridge/bridge_test.ts --modules $(WASM_WEB_DIR) \
		--engine $(NATIVE_BIN) --wasm-engine $(WASM_BIN) --tiebreak $(TIEBREAK_BIN)

dist-wasm: wasm-web
	rm -rf $(DIST_WASM_DIR)
	mkdir -p $(DIST_WASM_DIR)
	install -m 644 $(WASM_WEB_DIR)/bbpPairings.mjs $(WASM_WEB_DIR)/bbpPairings.wasm \
		$(WASM_WEB_DIR)/tiebreak.mjs $(WASM_WEB_DIR)/tiebreak.wasm \
		bridge/bbp-bridge.ts bridge/bbp-worker.ts $(DIST_WASM_DIR)/
	install -m 644 $(ENGINE_DIR)/LICENSE.txt $(ENGINE_DIR)/Apache-2.0.txt NOTICE \
		$(DIST_WASM_DIR)/
	install -m 644 LICENSE $(DIST_WASM_DIR)/LICENSE-tiebreak.txt
	install -m 644 tiebreak/GACRUX-LICENSE.txt $(DIST_WASM_DIR)/GACRUX-LICENSE.txt
	@# The Emscripten runtime in the .mjs loaders and the libraries compiled
	@# into the .wasm files (THIRD_PARTY_NOTICES.md, "Emscripten").
	$(WASM_ENV) root=$$(dirname "$$(command -v emcc)"); \
	install -m 644 $$root/LICENSE $(DIST_WASM_DIR)/EMSCRIPTEN-LICENSE.txt && \
	install -m 644 $$root/system/lib/libcxx/LICENSE.TXT $(DIST_WASM_DIR)/LIBCXX-LICENSE.txt && \
	install -m 644 $$root/system/lib/libcxxabi/LICENSE.TXT $(DIST_WASM_DIR)/LIBCXXABI-LICENSE.txt && \
	install -m 644 $$root/system/lib/libc/musl/COPYRIGHT $(DIST_WASM_DIR)/MUSL-COPYRIGHT.txt
	{ \
		echo "engine_fork=$(ENGINE_FORK_URL)"; \
		echo "engine_commit=$$(git -C $(ENGINE_DIR) rev-parse HEAD)"; \
		echo "port_repo_commit=$$(git rev-parse HEAD)$$(git diff --quiet HEAD 2>/dev/null || echo -dirty)"; \
		echo "tiebreak_reference=Gacrux $(GACRUX_URL) $(GACRUX_COMMIT)"; \
		echo "emscripten=$(EMSDK_VERSION)"; \
		echo "target=wasm32-emscripten (ES modules, web/worker/node)"; \
	} > $(DIST_WASM_DIR)/VERSION
	@echo "dist: $(DIST_WASM_DIR)"; ls -l $(DIST_WASM_DIR)

# --- release packages -------------------------------------------------------------
# packaging/make_packages.py turns dist/ into the archives the main app and the
# downloads consume (README "Release packages"): the npm package of the
# WebAssembly build and the native Linux archive, plus SHA256SUMS. Every entry
# carries the commit time, so one commit gives the same bytes. Nothing is
# published from here: consumers build the packages from a pinned commit of
# this repository with these targets (README "Release packages").

PACKAGE_VERSION := 0.1.0
PACKAGES_DIR := dist/packages
PACKAGE_TEST_DIR := build/package-test
PACKAGE_ARGS = --native $(DIST_DIR) --wasm $(DIST_WASM_DIR) \
	--version $(PACKAGE_VERSION) --mtime $(SOURCE_DATE_EPOCH)
PACKAGED_NATIVE := $(PACKAGE_TEST_DIR)/native/bbppairings-native-linux-x64-$(PACKAGE_VERSION)
PACKAGED_WASM := $(PACKAGE_TEST_DIR)/wasm/package

package: dist dist-wasm
	rm -rf $(PACKAGES_DIR)
	$(WASM_ENV) $(PYTHON) packaging/make_packages.py $(PACKAGE_ARGS) --out $(PACKAGES_DIR)

# The shipped files themselves are tested: the archive checks, a second build
# of the archives (same bytes), the engine suite and the Gacrux comparison on
# the extracted native binaries, and the bridge test on the extracted npm
# package (its JavaScript bridge, its worker loading the modules next to it).
test-package: package native-limits tools gacrux wasm-node
	$(PYTHON) tests/package_check.py --packages $(PACKAGES_DIR) \
		--version $(PACKAGE_VERSION) --native $(DIST_DIR) --wasm $(DIST_WASM_DIR) \
		--extract $(PACKAGE_TEST_DIR)
	$(WASM_ENV) $(PYTHON) packaging/make_packages.py $(PACKAGE_ARGS) \
		--out $(PACKAGE_TEST_DIR)/again > /dev/null
	for file in $(PACKAGES_DIR)/*; do \
		cmp "$$file" $(PACKAGE_TEST_DIR)/again/$$(basename "$$file") || exit 1; \
	done
	$(PYTHON) tests/run_tests.py --engine $(PACKAGED_NATIVE)/bbpPairings \
		--engine-limits $(LIMITS_BIN) --roundtrip $(ROUNDTRIP_BIN)
	$(PYTHON) tests/tiebreak_compare.py --tool $(PACKAGED_NATIVE)/tiebreak \
		--gacrux $(GACRUX_DIR)/gacrux --engine $(PACKAGED_NATIVE)/bbpPairings \
		--count $(BBP_TIEBREAK_COUNT)
	$(WASM_ENV) node tests/bridge/bridge_test.ts --modules $(PACKAGED_WASM) \
		--bridge $(PACKAGED_WASM) --engine $(PACKAGED_NATIVE)/bbpPairings \
		--wasm-engine $(WASM_BIN) --tiebreak $(PACKAGED_NATIVE)/tiebreak

clean:
	rm -rf build dist
