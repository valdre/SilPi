### Simone Valdre' - 28/09/2022. Distributed under GPL-3.0-or-later licence

all: SilServ.out SilCli_gnuplot.out SilCli_root.out
	

SilCli_gnuplot.out: obj/SilCli_gnuplot.o
	gcc -Wall -Wextra -o $@ $^ -lzmq

SilCli_root.out: src/SilCli_root.cpp SilCli_rootDict.cxx
	g++ -Wall -Wextra -o $@ $^ -lzmq `root-config --cflags --glibs`

SilServ.out: obj/SilServ.o obj/SilShared.o
	gcc -Wall -Wextra -o $@ $^ -lzmq -lrt

module: mod/SilPi.c
	$(MAKE) -C `pwd`/mod

obj/%.o: src/%.c
	gcc -Wall -Wextra -c -o $@ $^

SilCli_rootDict.cxx: include/SilCli_root.h include/SilCli_rootLinkDef.h
	rootcling -f $@ -c $^

clean:
	rm -rf *.out *.pcm *.cxx *Dict.* *.d AutoDict* obj/* mod/*.o mod/*.ko mod/.SilMod*.cmd mod/*.mod.c mod/*.mod.o mod/*.cmd mod/.tmp_versions mod/modules.order mod/Module.symvers
