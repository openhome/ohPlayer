# Makefile
 
#
# Command Line Options:
#   USE_LIBAVCODEC=0:
#            Use the platform libavcodecs for MP3/AAC instead of the embedded,
#            license restricted variants. 
#   DEBUG=0: Debug build.
#            Glibc mtrace will be enabled. MALLOC_TRACE must be defined in the
#            environment to activate.
#   NVWA_DIR=<source location>:
#            Integrate a cross platform new/delete leak detector.
#            Downloadable from http://wyw.dcweb.cn/leakage.htm
#                     

.PHONY: default all clean ubuntu raspbian ubuntu-install ubuntu-uninstall raspbian-install raspbian-uninstall

all: ubuntu raspbian 

clean:
	-$(MAKE) -f Makefile.ubuntu clean
	$(MAKE) -f Makefile.raspbian clean

ubuntu:
	$(MAKE) -f Makefile.ubuntu
    
raspbian:
	$(MAKE) -f Makefile.raspbian

ubuntu-install:
	$(MAKE) -f Makefile.ubuntu install

ubuntu-uninstall:
	$(MAKE) -f Makefile.ubuntu uninstall

raspbian-install:
	$(MAKE) -f Makefile.raspbian install

raspbian-uninstall:
	$(MAKE) -f Makefile.raspbian uninstall

