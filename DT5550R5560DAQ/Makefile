COMMON_SRC = ftdi_ft60x.cpp ni_usb3_class.cpp ni_usb3_wrapper.cpp R5560_SDKLib.cpp circular_buffer.c
CFLAGS     = -Ilinux-x86_64 -std=c++11
LFLAGS     = -Llinux-x86_64 
LIBS       = -l:libftd3xx.so
CERNROOTINC   = $(shell root-config --cflags)
CERNROOTLIB   = $(shell root-config --glibs)

all: libni_usb3.so test init parsadjust scaler osc0 osc1 osc2 spestart spestop speshow list cpack #$(TARGETS)

libni_usb3.so:
	g++ -o $@ $(CFLAGS) $(COMMON_SRC) $(LFLAGS) $(LIBS) -lzmq -shared -fPIC  -g -fpermissive 

test:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB) #-l:libni_usb3.so 

init:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

parsadjust:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

scaler:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

osc0:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

osc1:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

osc2:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

spestart:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

spestop:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

speshow:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

list:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB)

cpack:
	g++ $(CERNROOTINC) -o $@ $@.cpp -L. ./libni_usb3.so $(CERNROOTLIB) -fpermissive 

#$(TARGETS):
#	g++ -o $@ $(CFLAGS) $(LFLAGS) $@.cpp $(COMMON_SRC) $(LIBS)

clean:
	-rm -rf libni_usb3.so test init parsadjust scaler osc0 osc1 osc2 spestart spestop speshow list cpack

# PREFIX is environment variable, but if it is not set, then set default value
ifeq ($(PREFIX),)
    PREFIX := /usr/local
endif

install: libni_usb3.so
	install -d $(DESTDIR)$(PREFIX)/lib/
	install -m 644 libni_usb3.so $(DESTDIR)$(PREFIX)/lib/
	install -d $(DESTDIR)$(PREFIX)/include/
	install -m 644 ni_usb3_wrapper.h $(DESTDIR)$(PREFIX)/include/
	ldconfig 
