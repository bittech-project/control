include $(CONTROL_ROOT_DIR)/mk/spdk.mk

COMMON_CFLAGS = -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wmissing-declarations $(SPDK_CFLAGS)

STO_GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null)

ifneq (, $(STO_GIT_COMMIT))
COMMON_CFLAGS += -DSTO_GIT_COMMIT=$(STO_GIT_COMMIT)
endif
