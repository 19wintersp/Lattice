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
	CJSON = $(shell pkg-config --silence-errors --libs libcjson)
	ifneq ($(CJSON),)
		TOOL_PARSER = cjson
		LIB_PARSERS += cjson
		PKG_PARSERS += libcjson
	endif
endif

ifneq ($(jsonc),no)
	JSONC = $(shell pkg-config --silence-errors --libs json-c)
	ifneq ($(JSONC),)
		TOOL_PARSER = jsonc
		LIB_PARSERS += jsonc
		PKG_PARSERS += json-c
	endif
endif

ifneq ($(jansson),no)
	JANSSON = $(shell pkg-config --silence-errors --libs jansson)
	ifneq ($(JANSSON),)
		TOOL_PARSER = jansson
		LIB_PARSERS += jansson
		PKG_PARSERS += jansson
	endif
endif

ifdef tool
	ifneq ($(findstring $(tool),$(LIB_PARSERS)),)
		TOOL_PARSER = $(tool)
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
