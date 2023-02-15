export NAME = lattice

export MAJOR = 0
export MINOR = 1
export PATCH = 0
export VERSION = $(MAJOR).$(MINOR).$(PATCH)

export PREFIX ?= /usr/local
export BINDIR ?= $(PREFIX)/bin
export LIBDIR ?= $(PREFIX)/lib
export INCDIR ?= $(PREFIX)/include
export MANDIR ?= $(PREFIX)/share/man
export PKGDIR ?= $(LIBDIR)/pkgconfig

export LIB_PARSERS =
export PKG_PARSERS =
export TOOL_PARSER = no

ifneq ($(cjson),no)
	CJSON_CFLAGS = $(shell pkg-config --silence-errors --cflags libcjson)
	ifneq ($(CJSON_CFLAGS),)
		TOOL_PARSER = cjson
		LIB_PARSERS += cjson
		PKG_PARSERS += libcjson
	endif
endif

ifneq ($(jsonc),no)
	JSONC_CFLAGS = $(shell pkg-config --silence-errors --cflags json-c)
	ifneq ($(JSONC_CFLAGS),)
		TOOL_PARSER = jsonc
		LIB_PARSERS += jsonc
		PKG_PARSERS += json-c
	endif
endif

ifneq ($(jansson),no)
	JANSSON_CFLAGS = $(shell pkg-config --silence-errors --cflags jansson)
	ifneq ($(JANSSON_CFLAGS),)
		TOOL_PARSER = jansson
		LIB_PARSERS += jansson
		PKG_PARSERS += jansson
	endif
endif

export CFLAGS = -Wall -Wextra

.PHONY: all
all: src lib

.PHONY: src
src: lib
	@$(MAKE) --no-print-directory -C src

.PHONY: lib
lib:
	@$(MAKE) --no-print-directory -C lib

.PHONY: test
test: src
	@$(MAKE) --no-print-directory -C test

.PHONY: install
install: all
	@$(MAKE) --no-print-directory -C src install
	@$(MAKE) --no-print-directory -C lib install
	@$(MAKE) --no-print-directory -C include install
	@$(MAKE) --no-print-directory -C man install
	@$(MAKE) --no-print-directory -C pc install

.PHONY: clean
clean:
	@$(MAKE) --no-print-directory -C src clean
	@$(MAKE) --no-print-directory -C lib clean
	@$(MAKE) --no-print-directory -C man clean
	@$(MAKE) --no-print-directory -C test clean
