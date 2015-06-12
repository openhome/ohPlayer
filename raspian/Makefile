# Makefile
 
#
# Command Line Options:
#   DEBUG=0: Debug build.
#            Glibc mtrace will be enabled. MALLOC_TRACE must be defined in the
#            environment to activate.
#   NVWA_DIR=<source location>:
#            Integrate a cross platform new/delete leak detector.
#            Downloadable from http://wyw.dcweb.cn/leakage.htm
#                     

# GTK Specifics
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0)

TARGET       = LitePipeTestApp
CC           = g++
CFLAGS       = -c -Wall -std=c++0x $(GTK_CFLAGS)
DEBUG_CFLAGS = -g -O0 -DDEBUG -fstack-protector
LIBS         = $(GTK_LIBS) -lnotify
INCLUDES     =

REL_TARGET    = LitePipeTestApp
DEBUG_TARGET  = LitePipeTestAppDebug
TARGET        = $(REL_TARGET)

REL_OBJ_DIR   = objs
DEBUG_OBJ_DIR = debug-objs/
OBJ_DIR       = $(REL_OBJ_DIR)

# If 'DEBUG=0 is specified on the command line build a debug biuld.
ifdef DEBUG
OBJ_DIR  = $(DEBUG_OBJ_DIR)
CFLAGS  += $(DEBUG_CFLAGS)
TARGET   = $(DEBUG_TARGET)
endif

ifdef NVWA_DIR
# New/Delete leak checker, if available.
CFLAGS   += -DUSE_NVWA
INCLUDES += -I$(NVWA_DIR)
HEADERS  += $(wildcard $(NVWA_DIR)/*.h)
endif

.PHONY: default all clean build

default: build $(TARGET)
all: default

OBJECTS  = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(wildcard *.cpp))
HEADERS  = $(wildcard *.h)

ifdef NVWA_DIR
# Include the new/delete leak checker in debug builds.
OBJECTS += $(NVWA_DIR)/debug_new.o
endif

$(OBJ_DIR)/%.o: %.cpp $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

build:
	@mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(REL_OBJ_DIR) $(DEBUG_OBJ_DIR)
	rm -f $(REL_TARGET) $(DEBUG_TARGET)
ifdef NVWA_DIR
	rm $(NVWA_DIR)/*.o
endif
