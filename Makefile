SHELL = /bin/bash

CONTROL_DIR = control

help:
	@echo "		all               : make all"
	@echo "		clean             : clean files"
	@echo ""
	@echo "		control           : make control only"
	@echo "		control_clean     : control: clean"


all clean:
	$(MAKE) -j$$(nproc) -C $(CONTROL_DIR) $@

control:
	cd $(CONTROL_DIR) && $(MAKE) all

control_clean:
	cd $(CONTROL_DIR) && $(MAKE) clean


.PHONY: all clean control control_clean
