SRC_CC   = env.cc cap_sel_alloc.cc main_thread.cc context_area.cc \
           reload_parent_cap.cc
LIBS     = ipc heap lock log_console
INC_DIR += $(BASE_DIR)/src/base/env

vpath env.cc               $(BASE_DIR)/src/base/env
vpath cap_sel_alloc.cc      $(REP_DIR)/src/base/env
vpath main_thread.cc        $(REP_DIR)/src/base/env
vpath context_area.cc      $(BASE_DIR)/src/base/env
vpath reload_parent_cap.cc $(BASE_DIR)/src/base/env
