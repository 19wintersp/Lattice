SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c,build/%.o,$(SRCS))
BIN = build/$(NAME)

_pos = $(if $(findstring $1,$2),$(call _pos,$1,$(wordlist 2,$(words $2),$2),x $3),$3)
pos = $(words $(call _pos,$1,$2))

TOOL_PKG = $(word $(call pos,$(TOOL_PARSER),$(LIB_PARSERS)),$(PKG_PARSERS))

ifeq ($(TOOL_PARSER),cjson)
	CFLAGS += -DTOOL_CJSON
else ifeq ($(TOOL_PARSER),jsonc)
	CFLAGS += -DTOOL_JSONC
else ifeq ($(TOOL_PARSER),jansson)
	CFLAGS += -DTOOL_JANSSON
else
	CFLAGS += -DTOOL_INVALID
endif

CFLAGS += -I../include
LDFLAGS = -L../lib/build

.PHONY: all
all: build $(BIN)

build:
	mkdir -p build/

.PHONY: install
install: all
	install -d $(BINDIR)
	install -m0755 $(BIN) $(BINDIR)

.PHONY: clean
clean:
	@rm -rf build/ || true

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $(shell pkg-config --libs $(TOOL_PKG)) -o $@ $^

build/%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<
