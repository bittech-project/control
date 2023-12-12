SHELL = /bin/bash

CONTROL_DIR = control
SERVERLIB_DIR = server

help:
	@echo "		all               : make all"
	@echo "		clean             : clean files"
	@echo ""
	@echo "		control           : make control only"
	@echo "		control_clean     : control: clean"
	@echo ""
	@echo "		serverlib         : make server shared lib only"
	@echo "		serverlib_clean   : serverlib: clean"
	@echo ""
	@echo "		cscope            : generate cscope files"
	@echo "		distclean         : clean + clean cscope files"


all clean:
	$(MAKE) -j$$(nproc) -C $(SERVERLIB_DIR) $@
	$(MAKE) -j$$(nproc) -C $(CONTROL_DIR) $@

serverlib:
	cd $(SERVERLIB_DIR) && $(MAKE) all

serverlib_clean:
	cd $(SERVERLIB_DIR) && $(MAKE) clean

control: server
	cd $(CONTROL_DIR) && $(MAKE) all

control_clean: serverlib_clean
	cd $(CONTROL_DIR) && $(MAKE) clean

cscope:
	@cscope -R -b

distclean: clean
	@rm -f cscope.out

.PHONY: all clean serverlib serverlib_clean control control_clean
.PHONY: cscope distclean
