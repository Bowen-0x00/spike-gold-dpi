CXX = g++
CXXFLAGS = -fPIC -O2 -std=c++17 -I$(TOP) -I$(TOP)/riscv -I$(TOP)/fesvr -I$(TOP)/build -I$(TOP)/fdt -I/home/host/Projects/spdlog/include
LDFLAGS = -shared -pthread -Wl,-soname,libspike_dpi.so

TOP = $(shell pwd)/..
BUILD_DIR = $(TOP)/build

# list all spike object files
SPIKE_OBJS = $(shell find $(BUILD_DIR) -type f -name '*.o')

LIBSPIKE = libspike.a
TARGET = libspike_dpi.so
WRAPPER = dpi_wrapper.cc

all: prepare $(TARGET)

prepare:
	@if [ -z "$(SPIKE_OBJS)" ]; then \
	  echo "No spike .o files found in $(BUILD_DIR). Build spike first."; exit 1; \
	fi
	@echo "Creating $(LIBSPIKE) with spike objects..."
	@rm -f $(LIBSPIKE)
	@ar rcs $(LIBSPIKE) $(SPIKE_OBJS)

$(TARGET): $(WRAPPER) $(LIBSPIKE)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(WRAPPER) $(LIBSPIKE) -ldl -lrt -lm

clean:
	rm -f $(LIBSPIKE) $(TARGET)
