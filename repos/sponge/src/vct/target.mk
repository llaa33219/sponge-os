TARGET   := vct
SRC_CC   := main.cc \
            vct_main.cc \
            args.cc \
            command_router.cc \
            commands.cc \
            init_state.cc \
            notifier_reporter.cc
LIBS     := base sponge_backend_client
INC_DIR  := $(PRG_DIR)/include \
            $(REP_DIR)/include
