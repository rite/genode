#
# \brief  Build config for Genodes core process
# \author Stefan Kalkowski
# \author Josef Söntgen
# \author Martin Stein
# \date   2014-02-25
#

# add include paths
ifeq ($(filter-out $(SPECS),wand_quad),)
INC_DIR += $(REP_DIR)/src/core/spec/wand_quad
endif

ifeq ($(filter-out $(SPECS),apalis_imx6),)
INC_DIR += $(REP_DIR)/src/core/spec/apalis_imx6
endif

# add C++ sources
SRC_CC += platform_services.cc

NR_OF_CPUS = 4

# include less specific configuration
include $(REP_DIR)/lib/mk/spec/cortex_a9/core-hw.inc
