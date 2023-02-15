.PHONY: all
all:

.PHONY: install
install: all
	install -d $(PKGDIR)
	install -m0644 $(NAME).pc $(NAME)-core.pc $(PKGDIR)
	install -m0644 $(foreach lib,$(LIB_PARSERS),$(NAME)-$(lib).pc) $(PKGDIR)
