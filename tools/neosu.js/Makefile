CXX = emcc
CXXFLAGS = -O3 -std=c++23 -DBUILD_TOOLS_ONLY
LDFLAGS = -lembind -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 -s SINGLE_FILE=0 -s ALLOW_MEMORY_GROWTH=1 -s IMPORTED_MEMORY=1

SRCDIR = ../../src
INCLUDES = -I$(SRCDIR)/App/Neomod -I$(SRCDIR)/App/Neomod/DiffCalc -I$(SRCDIR)/Util

# GLM: use system headers by default, override with GLM_INCLUDE=-I/path/to/glm
# https://github.com/g-truc/glm
GLM_INCLUDE ?=
INCLUDES += $(GLM_INCLUDE)

SOURCES = \
	NeomodJs.cpp \
	$(SRCDIR)/App/Neomod/DatabaseBeatmap.cpp \
	$(SRCDIR)/App/Neomod/DiffCalc/DifficultyCalculator.cpp \
	$(SRCDIR)/App/Neomod/GameRules.cpp \
	$(SRCDIR)/App/Neomod/Replay.cpp \
	$(SRCDIR)/App/Neomod/SliderCurves.cpp \
	$(SRCDIR)/Util/SString.cpp

neomod.js: $(SOURCES)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) $^ -o bin/$@

clean:
	rm -rf bin

.PHONY: clean
