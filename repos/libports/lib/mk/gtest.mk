GTEST_DIR := $(call select_from_ports,gtest)/src/lib/gtest

SRC_CC = $(GTEST_DIR)/src/gtest-all.cc

vpath %.cc += $(GTEST_DIR)

INC_DIR += $(GTEST_DIR)
INC_DIR += $(GTEST_DIR)/include
INC_DIR += $(GTEST_DIR)/include/internal

LIBS += libc libm stdcxx
