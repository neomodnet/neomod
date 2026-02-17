# Standalone osu! difficulty/ppv2 calculator
# Requires: C++23 compiler, GLM headers

CXX ?= c++
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++23 -DBUILD_TOOLS_ONLY

SRCDIR = ../../src
INCLUDES = -I$(SRCDIR)/App/Neomod -I$(SRCDIR)/App/Neomod/DiffCalc -I$(SRCDIR)/Util

# GLM: use system headers by default, override with GLM_INCLUDE=-I/path/to/glm
# https://github.com/g-truc/glm
GLM_INCLUDE ?=
INCLUDES += $(GLM_INCLUDE)

SOURCES = \
	$(SRCDIR)/App/Neomod/DatabaseBeatmap.cpp \
	$(SRCDIR)/App/Neomod/DiffCalc/DifficultyCalculator.cpp \
	$(SRCDIR)/App/Neomod/GameRules.cpp \
	$(SRCDIR)/App/Neomod/SliderCurves.cpp \
	$(SRCDIR)/App/Neomod/Tools/DiffCalcTool.cpp \
	$(SRCDIR)/Util/SString.cpp

OBJDIR = obj
BINDIR = bin
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

TARGET = $(BINDIR)/diffcalc

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)

.PHONY: all clean
