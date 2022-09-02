#
# Basic configuration options
#
SPDK_DIR ?= /root/gleb/SPDK/spdk

SPDK_HEADER_DIR=$(SPDK_DIR)/include
SPDK_LIB_DIR=$(SPDK_DIR)/build/lib

PKG_CONFIG_PATH = $(SPDK_LIB_DIR)/pkgconfig

COMMON_CFLAGS+=-I$(SPDK_HEADER_DIR)
COMMON_CFLAGS+=-L$(SPDK_LIB_DIR)
COMMON_CFLAGS+=-L$(DPDK_LIB_DIR)
COMMON_CFLAGS+=-L$(VFIO_LIB_DIR)

DPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_env_dpdk)
SPDK_EVENT_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_event spdk_event_bdev)
SPDK_DPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_event spdk_event_bdev spdk_env_dpdk)
SYS_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs --static spdk_syslibs)
