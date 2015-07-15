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

CC           = g++
CFLAGS       = -c -Wall -std=c++11 $(GTK_CFLAGS)
DEBUG_CFLAGS = -g -O0 -DDEBUG -fstack-protector
LIBS         = $(GTK_LIBS) -lnotify -lpthread -lasound -lSourcePlaylist -lSourceSongcast -lSourceUpnpAv -lohMediaPlayer -lohMediaPlayerTestUtils -lWebAppFramework -lConfigUi -lohNetCore -lohNetmon  -lCodecAac -lCodecAacBase -lCodecAifc -lCodecAlac -lCodecAlacBase -lCodecPcm -lCodecAiff -lCodecAiffBase -lCodecVorbis -llibOgg -lCodecAdts -lCodecFlac -lCodecWav -lohPipeline -lssl -lcrypto -ldl
INCLUDES     = -I../dependencies/Linux-armhf/ohMediaPlayer/include -I../dependencies/Linux-armhf/ohNetmon/include -I../dependencies/Linux-armhf/openssl/include -I../dependencies/Linux-armhf/ohNetGenerated-Linux-armhf-Release/include/ohnet/OpenHome/Net/Core

TARGET    = litepipe-test-app

REL_OBJ_DIR   = objs
DEBUG_OBJ_DIR = debug-objs/
OBJ_DIR       = $(REL_OBJ_DIR)

INSTALL      = install
RESOURCEDIR  = ../dependencies/Linux-armhf/ohMediaPlayer/res

PREFIX = /usr/local

# The directory to install the application to
BINDIR = $(PREFIX)/bin

# The directory to install the resource files to.
RESDIR = $(PREFIX)/share/litepipe-test-app

# If 'DEBUG=0 is specified on the command line build a debug biuld.

LIBS += -L../dependencies/Linux-armhf/ohMediaPlayer/lib -L../dependencies/Linux-armhf/ohNetmon/lib -L../dependencies/Linux-armhf/openssl/lib

ifdef DEBUG
OBJ_DIR   = $(DEBUG_OBJ_DIR)
CFLAGS   += $(DEBUG_CFLAGS)
LIBS     += -L../dependencies/Linux-armhf/ohNet-Linux-armhf-Debug/lib
INCLUDES += -I../dependencies/Linux-armhf/ohNet-Linux-armhf-Debug/include/ohnet
else
LIBS     += -L../dependencies/Linux-armhf/ohNet-Linux-armhf-Release/lib
INCLUDES += -I../dependencies/Linux-armhf/ohNet-Linux-armhf-Release/include/ohnet
endif

ifdef NVWA_DIR
# New/Delete leak checker, if available.
CFLAGS   += -DUSE_NVWA
INCLUDES += -I$(NVWA_DIR)
HEADERS  += $(wildcard $(NVWA_DIR)/*.h)
endif

.PHONY: default all clean build install uninstall

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
	rm -f $(TARGET)
ifdef NVWA_DIR
	rm $(NVWA_DIR)/*.o
endif

install:
	$(INSTALL) -d $(BINDIR) $(RESDIR) $(RESDIR)/res
	$(INSTALL) -m 755 $(TARGET) $(BINDIR)
	$(INSTALL) -m 644 OpenHome-48x48.png $(RESDIR)
	cp -R $(RESOURCEDIR)  $(RESDIR)

uninstall:
	rm $(BINDIR)/$(TARGET)
	rm -rf $(RESDIR)
