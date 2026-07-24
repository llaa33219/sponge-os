TARGET   := vct
SRC_CC   := main.cc \
            vct_main.cc \
            args.cc \
            command_router.cc \
            commands.cc \
            init_state.cc \
            pkg_client.cc
LIBS     := base
INC_DIR  := $(PRG_DIR)/include \
            $(REP_DIR)/include
