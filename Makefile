all: SilServ.out SilCli_gnuplot.out
	

SilCli_gnuplot.out: obj/SilCli_gnuplot.o
	gcc -Wall -Wextra -o SilCli_gnuplot.out obj/SilCli_gnuplot.o -lzmq

SilServ.out: obj/SilServ.o obj/SilShared.o
	gcc -Wall -Wextra -o SilServ.out obj/SilServ.o obj/SilShared.o -lzmq -lrt

module: mod/SilPi.c
	$(MAKE) -C `pwd`/mod

obj/%.o: src/%.c
	gcc -Wall -Wextra -c -o $@ $<

clean:
	rm -rf *.out obj/* mod/*.o mod/*.ko mod/.SilMod*.cmd mod/*.mod.c mod/*.mod.o mod/*.cmd mod/.tmp_versions mod/modules.order mod/Module.symvers
