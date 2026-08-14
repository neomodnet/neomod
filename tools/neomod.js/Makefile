CXX = em++
CXXFLAGS = -DBUILD_TOOLS_ONLY -Os -std=c++23 -ffunction-sections -fdata-sections -fno-exceptions -fno-unwind-tables -flto=thin
LDFLAGS = -Wl,--gc-sections -lembind -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 -s SINGLE_FILE=0 -s ALLOW_MEMORY_GROWTH=1 -s IMPORTED_MEMORY=1

SRCDIR = ../../src
INCLUDES = -I$(SRCDIR)/App/Neomod -I$(SRCDIR)/App/Neomod/DiffCalc -I$(SRCDIR)/Util -I$(SRCDIR)/../libraries

# GLM: use system headers by default, override with GLM_INCLUDE=-I/path/to/glm
# https://github.com/g-truc/glm
PKG_CONFIG ?= pkg-config
GLM_INCLUDE ?= $(shell $(PKG_CONFIG) --cflags glm)
INCLUDES += $(GLM_INCLUDE)

SOURCES = \
	NeomodJs.cpp \
	$(SRCDIR)/App/Neomod/DatabaseBeatmap.cpp \
	$(SRCDIR)/App/Neomod/DiffCalc/DifficultyCalculator.cpp \
	$(SRCDIR)/App/Neomod/GameRules.cpp \
	$(SRCDIR)/App/Neomod/Replay.cpp \
	$(SRCDIR)/App/Neomod/SliderCurves.cpp \
	$(SRCDIR)/Util/Parsing.cpp \
	$(SRCDIR)/Util/SString.cpp \
	$(SRCDIR)/Util/Vectors.cpp

neomod.js: $(SOURCES)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) $^ -o bin/$@

clean:
	rm -rf bin

.PHONY: clean
