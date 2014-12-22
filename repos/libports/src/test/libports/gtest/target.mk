TARGET = gtest

GTEST_DIR := $(call select_from_ports,gtest)/src/lib/gtest

INC_DIR += $(GTEST_DIR)

SRC_CC = $(GTEST_DIR)/src/gtest_main.cc
SRC_CC += $(GTEST_DIR)/test/gtest_all_test.cc

LIBS = config_args stdcxx gtest
