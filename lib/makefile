CORE_SRCS = lattice.c
EXTRA_SRCS = $(foreach lib,$(LIB_PARSERS),lattice-$(lib).c)

CORE_OBJS = $(patsubst %.c,build/%.o,$(CORE_SRCS))
EXTRA_OBJS = $(patsubst %.c,build/%.o,$(EXTRA_SRCS))

SO_SUFFIX = .so.$(MINOR).$(PATCH)
SO_NAME = $(NAME)$(MAJOR)

MAIN_SO = build/lib$(SO_NAME)$(SO_SUFFIX)
CORE_SO = build/lib$(SO_NAME)-core$(SO_SUFFIX)
EXTRA_SO = $(foreach lib,$(LIB_PARSERS),build/lib$(SO_NAME)-$(lib)$(SO_SUFFIX))

CORE_LDFLAGS =
EXTRA_LDFLAGS = $(shell pkg-config --libs $(PKG_PARSERS))

CFLAGS += -I../include -DVERSION_STR=\"$(VERSION)\"
CFLAGS += -DMAJOR=$(MAJOR) -DMINOR=$(MINOR) -DPATCH=$(PATCH)

_pos = $(if $(findstring $1,$2),$(call _pos,$1,$(wordlist 2,$(words $2),$2),x $3),$3)
pos = $(words $(call _pos,$1,$2))

.PHONY: all
all: build $(MAIN_SO) $(CORE_SO) $(EXTRA_SO)

build:
	mkdir -p build/

.PHONY: install
install: all
	install -d $(LIBDIR)
	for bso in $(MAIN_SO) $(CORE_SO) $(EXTRA_SO) ; do \
		install -m0755 $$bso $(LIBDIR) ;\
		so=$${bso#build/} ;\
		ln -sf $(LIBDIR)/$$so $(LIBDIR)/$${so%.$(PATCH)} ;\
		ln -sf $(LIBDIR)/$$so $(LIBDIR)/$${so%.$(MINOR).$(PATCH)} ;\
	done

.PHONY: clean
clean:
	@rm -rf build/ || true

$(MAIN_SO): $(CORE_OBJS) $(EXTRA_OBJS)
	$(CC) -shared $(CORE_LDFLAGS) $(EXTRA_LDFLAGS) -o $@ $^

$(CORE_SO): $(CORE_OBJS)
	$(CC) -shared $(CORE_LDFLAGS) -o $@ $^

SO_LIB = $(patsubst build/lattice-%.o,%,$<)
SO_PKG = $(word $(call pos,$(SO_LIB),$(LIB_PARSERS)),$(PKG_PARSERS))

build/lib$(SO_NAME)-%$(SO_SUFFIX): build/lattice-%.o $(CORE_OBJS)
	$(CC) -shared $(shell pkg-config --libs $(SO_PKG)) -o $@ $^

build/lattice.o: lattice.c
	$(CC) -c $(CFLAGS) -o $@ $<

OBJ_LIB = $(patsubst lattice-%.c,%,$<)
OBJ_PKG = $(word $(call pos,$(OBJ_LIB),$(LIB_PARSERS)),$(PKG_PARSERS))

build/lattice-%.o: lattice-%.c
	$(CC) -c $(CFLAGS) $(shell pkg-config --cflags $(OBJ_PKG)) -o $@ $<
