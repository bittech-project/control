#
# Basic configuration options
#
SPDK_DIR := $(CONTROL_ROOT_DIR)/spdk

SPDK_HEADER_DIR ?= $(SPDK_DIR)/include
SPDK_LIB_DIR ?= $(SPDK_DIR)/build/lib
DPDK_HEADER_DIR ?= $(SPDK_DIR)/dpdk/build/include

PKG_CONFIG_PATH = $(SPDK_LIB_DIR)/pkgconfig

# DPDK_LIB_DIR= $(SPDK_DIR)/dpdk/build/lib
# VFIO_LIB_DIR = $(SPDK_DIR)/build/libvfio-user/usr/local/lib

SPDK_CFLAGS := -I$(SPDK_HEADER_DIR) -I$(DPDK_HEADER_DIR)
SPDK_LDFLAGS := -L$(SPDK_LIB_DIR)

DPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_env_dpdk)
SPDK_EVENT_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_event spdk_event_bdev)
SPDK_DPDK_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs spdk_event spdk_event_bdev spdk_env_dpdk)
SPDK_SYS_LIB := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs --static spdk_syslibs)
