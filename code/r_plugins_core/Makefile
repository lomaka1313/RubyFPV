CPPFLAGS=-Wall -O0 -D _GNU_SOURCE
CFLAGS := $(CFLAGS) 
RELEASE_DIR = ../../

all: plugin_core

core_plugins_utils.o: ../public/utils/core_plugins_utils.c
	gcc -c -o $@ $< $(CPPFLAGS)

%.o: %.cpp
	g++ $(CFLAGS) -c -o $@ $< $(CPPFLAGS)  


plugin_core: core_plugins_utils.o plugin_core.o
	gcc plugin_core.o core_plugins_utils.o -shared -Wl,-soname,plugin_core.so.1 -o plugin_core.so.1.0.1 -lc
	$(info Copy plugin_core)
	$(info ----------------------------------------------------)

clean:
	rm -f plugin_core.so.* *.o
