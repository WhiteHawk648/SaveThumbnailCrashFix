ZIG ?= /home/gavin/.local/share/mise/installs/zig/0.16.0/bin/zig
CXX = $(ZIG) c++
TARGET = x86-windows-gnu

CXXFLAGS = -target $(TARGET) -O2 -Wall -Wextra -shared -s
INCLUDES = -I./include

OUT = SaveThumbnailCrashFix.dll
SRC = src/main.cpp

all: $(OUT)

$(OUT): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) -o $(OUT)

clean:
	rm -f $(OUT) SaveThumbnailCrashFix.lib SaveThumbnailCrashFix.pdb

.PHONY: all clean
