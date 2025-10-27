find ../build -name '*.o' > all_objs.txt
ar rcs libspike.a $(cat all_objs.txt)
g++ -fPIC -O2 -std=c++17 -shared -pthread -Wl,-soname,libspike_dpi.so     -o libspike_dpi.so dpi_wrapper.cc libspike.a -ldl -lrt -lm -I../ -I../fesvr -I../riscv