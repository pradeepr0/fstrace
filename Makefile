
all: bin/intromake

bin/intromake: intromake.cxx *.hxx
	g++ -std=c++14 -Wall -o bin/intromake intromake.cxx -pthread `pkg-config fuse --cflags --libs` -lulockmgr

clean:
	rm -rf bin/* *.o *.gch