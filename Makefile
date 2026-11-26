# Makefile - shortcuts for nativemd development; CMake does the building.
#
#   make install                 system dependencies (compiler, CMake, Ninja, Python, Lua, SDL2...)
#   make build                   the engine, tools and tests into $(BUILD)
#   make test                    build, then run the tests
#   make up [INTERNAL=VERSION]   play the Sonic 1 client built against this checkout
#                                (VERSION: main (default), latest, vX.Y.Z, same = this branch;
#                                 UP_ARGS="--scale 4 --fullscreen" for s1-play options)
#   make clean                   remove $(BUILD)

BUILD    ?= build
INTERNAL ?= main

.PHONY: help install build test up clean

help:
	@sed -n '2,10p' Makefile | sed 's/^# \{0,1\}//'

install:
	tools/install_deps.sh

build:
	cmake -S . -B $(BUILD) -G Ninja
	cmake --build $(BUILD)

test: build
	ctest --test-dir $(BUILD) --output-on-failure

up:
	tools/up.sh $(INTERNAL)

clean:
	rm -rf $(BUILD)
