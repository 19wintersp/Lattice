TESTS = $(subst test,exec,$(wildcard test*))

BIN = ../src/build/$(NAME)

.PHONY: test
test: build $(TESTS)

build:
	mkdir -p build/

.PHONY: clean
clean:
	@rm -rf build/ || true

T = $(subst exec,test,$@)

.PHONY: $(TESTS)
$(TESTS):
	$(BIN) $(T)/lattice < $(T)/json > build/$(T)
	@diff -w $(T)/ref build/$(T) && echo Test $(T) OK || echo Test $(T) failed!
