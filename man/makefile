ROFFS = $(wildcard $(NAME).*)
GZIPS = $(patsubst %,build/%.gz,$(ROFFS))

.PHONY: all
all: build $(GZIPS)

build:
	mkdir -p build/

.PHONY: install
install: all
	for roff in $(ROFFS) ; do \
		man=man$${roff#$(NAME).} ;\
		install -d $(MANDIR)/$$man ;\
		install -m0644 build/$$roff.gz $(MANDIR)/$$man ;\
	done

.PHONY: clean
clean:
	@rm -rf build/ || true

build/%.gz: %
	gzip -c $< > $@
