
all: bin/fstrace

bin/fstrace: fstrace.cxx *.hxx
	g++ -std=c++14 -Wall -o bin/fstrace fstrace.cxx -pthread `pkg-config fuse --cflags --libs` -lulockmgr

clean:
	rm -rf bin/* *.o *.gch