compile spike first
 ```bash
 ../configure --prefix=/opt/riscv/ CPPFLAGS="-I/home/host/Projects/spdlog/include"
 make -j
 ```


Then build the library

```bash
cd dpi
make
```

Then run the RTL simulation


```bash
TOP=spike_tb make sim SPIKE_LOG_LEVEL=debug
```