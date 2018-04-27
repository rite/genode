TARGET  := sculpt_manager
SRC_CC  := $(notdir $(wildcard $(PRG_DIR)/*.cc))
SRC_CC  += $(addprefix runtime/,$(notdir $(wildcard $(PRG_DIR)/runtime/*.cc)))
LIBS    += base
INC_DIR += $(PRG_DIR) $(REP_DIR)/src/app/depot_deploy
