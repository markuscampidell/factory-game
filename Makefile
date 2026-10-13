# Makefile - incremental build of factory_game.exe (build.bat runs this
# with mingw32-make). Every .cpp is compiled to its own object file in
# build/<mode>/; next time only the .cpp files that changed - or that
# include a header that changed (g++ -MMD writes those dependencies) - are
# compiled again, then everything is linked. `-j` uses all CPU cores.
#
#   mingw32-make -j8                 optimized game, no console window
#   mingw32-make -j8 MODE=debug      console window + debug info
#   mingw32-make objects             just compile (no link)

MODE ?= release
TARGET ?= factory_game.exe

SRC := $(wildcard src/core/*.cpp src/sim/*.cpp src/game/*.cpp)
OBJDIR := build/$(MODE)
OBJ := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRC))

ifeq ($(MODE),debug)
  CXXFLAGS ?= -std=c++17 -O0 -g
  LDFLAGS ?=
else
  CXXFLAGS ?= -std=c++17 -O2 -DNDEBUG
  LDFLAGS ?= -mwindows
endif
CPPFLAGS := -Ithird_party -MMD -MP
# raylib's DLL (the fully static link doesn't work with MSYS2's raylib)
LIBS ?= -lraylib -lopengl32 -lgdi32 -lwinmm -static-libgcc -static-libstdc++

# make the folder an object file goes into (cmd.exe on Windows, sh elsewhere)
ifeq ($(OS),Windows_NT)
  # cmd.exe even if some other sh.exe is on the PATH
  SHELL := cmd.exe
  # (- : two parallel jobs may create the same folder at once - that's fine)
  mkdir_for = -@if not exist "$(subst /,\,$(patsubst %/,%,$(dir $1)))" mkdir "$(subst /,\,$(patsubst %/,%,$(dir $1)))" 2>nul
else
  mkdir_for = @mkdir -p "$(dir $1)"
endif

.PHONY: all objects
all: $(TARGET)
objects: $(OBJ)

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(OBJDIR)/%.o: %.cpp
	$(call mkdir_for,$@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

-include $(OBJ:.o=.d)
