SHELL = /bin/bash

CONTROL_DIR = control

help:
	@echo "		all               : make all"
	@echo "		clean             : clean files"
	@echo ""
	@echo "		control           : make control only"
	@echo "		control_clean     : control: clean"
	@echo ""
	@echo "		cscope            : generate cscope files"
	@echo "		distclean         : clean + clean cscope files"


all clean:
	$(MAKE) -j$$(nproc) -C $(CONTROL_DIR) $@

control:
	cd $(CONTROL_DIR) && $(MAKE) all

control_clean:
	cd $(CONTROL_DIR) && $(MAKE) clean

cscope:
	@cscope -R -b

distclean: clean
	@rm -f cscope.out

.PHONY: all clean control control_clean
.PHONY: cscope distclean
