default:
	$(CXX) $(CPPFLAGS) $(CFLAGS) -DUSE_LIBUV -std=c++11 -O3 -Wall uWScat.cpp -o uWScat -luWS -luv -lssl -lz

.PHONY: clean
clean:
	rm -f uWScat

