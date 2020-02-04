GPP = g++


############## START: VKL_FPROJECT
CPP_FLAGS = -std=c++11 -fPIC -g -frounding-math
CPP_LIBS  = -ljsoncpp -lgerlumph -lpng -lCCfits -lcfitsio
EXT_LIB_DIR = common/gerlumph_lib/lib

ROOT_DIR = variability/extrinsic/gerlumph_moving_source
EXT_INC_DIR = common/gerlumph_lib/include
INC_DIR = $(ROOT_DIR)/inc
SRC_DIR = $(ROOT_DIR)/src
BIN_DIR = $(ROOT_DIR)/bin
OBJ_DIR = $(ROOT_DIR)/obj
$(shell mkdir -p $(OBJ_DIR))
$(shell mkdir -p $(BIN_DIR))


DEPS = auxiliary_functions.hpp
OBJ  = auxiliary_functions.o   moving_source.o
FULL_DEPS = $(patsubst %,$(INC_DIR)/%,$(DEPS)) #Pad names with dir
FULL_OBJ  = $(patsubst %,$(OBJ_DIR)/%,$(OBJ))  #Pad names with dir
#$(info $$OBJ is [${FULL_DEPS}])

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(FULL_DEPS)
	$(GPP) $(CPP_FLAGS) -I $(EXT_INC_DIR) -I $(INC_DIR) -c -o $@ $<

gerlumph_moving_source: $(FULL_OBJ)
	$(GPP) $(CPP_FLAGS) -I $(INC_DIR) -I $(EXT_INC_DIR) -o $(BIN_DIR)/moving_source $(FULL_OBJ) $(CPP_LIBS) -L $(EXT_LIB_DIR) -Wl,-rpath,$(EXT_LIB_DIR)
gerlumph_moving_source_clean:
	$(RM) -r $(OBJ_DIR)/* $(BIN_DIR)/*
############## END: VKL_FPROJECT
