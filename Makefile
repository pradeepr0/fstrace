
all: bin/intromake

bin/intromake: intromake.cxx *.hxx
	g++ -std=gnu++0x -o bin/intromake intromake.cxx -pthread `pkg-config fuse --cflags --libs` -lulockmgr

clean:
	rm -rf bin/* *.o *.gch