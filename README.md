[中文版本 (Chinese README)](./README_zh.md) · [English version (English README)](./README.md)
## spike-gold-dpi

### Project Introduction

`spike-gold-dpi` is a modification of Spike (the RISC-V ISA simulator). By exposing a DPI-C interface, it allows a SystemVerilog RTL simulation to **drive Spike in lockstep**, and to read Spike's architectural state at every clock cycle (or according to your defined stepping semantics). The goal is to facilitate differential testing (difftest) during RTL development: as soon as an architectural state (PC/instruction/GPR/CSR) mismatches with Spike, the simulation stops and reports which instruction and registers are inconsistent, making it easier to locate errors.

This tool is intended for processor/RISC-V RTL developers who want to perform fast and accurate ISA-level differential verification on their designs during commits or in a continuous integration (CI) environment.

### Main Features

*   **DPI-C Interface**: Create and destroy Spike instances from the SystemVerilog side.
*   **`step()`**: Advance the Spike model with each clock cycle (or at a point of your choosing) by calling this function.
*   **Snapshot Reading**: Read the PC, general-purpose registers (x0-x31), floating-point registers, and CSRs.
*   **Configurable Comparator**: Supports comparing only the set of registers/CSRs that you are interested in.
*   **Difference Reporting**: Outputs the PC and disassembly (if an ELF file is available) of the failing instruction, and lists the differing registers/CSRs and their values. It can be configured to terminate the simulation upon a mismatch, which is ideal for CI.
*   **Helper Scripts**: Used to generate golden traces and to compare and output more user-friendly difference reports.

### Usage Overview

1.  Compile and install the modified Spike from this repository (see below).
2.  In your SystemVerilog testbench, `import "DPI-C"` and call the functions exposed by this project.
3.  After reset, create a model instance using `spike_create("<elf>", hartid)`.
4.  On each clock edge (or according to your strategy), call `spike_step(ctx)`, and then call `spike_get_gpr` / `spike_get_pc` / `spike_get_csr` to read the state.
5.  Compare the state from Spike with the state of the DUT (your RTL). If they do not match, print detailed information and (optionally) stop the simulation.
6.  Call `spike_delete(ctx)` at the end of the simulation.

### Build and Dependencies

The dependencies are the same as for the upstream Spike.

Build example:
```bash
mkdir build && cd build
../configure --prefix=$RISCV
make -j$(nproc)
cd ../dpi
make
# Optional: make install
```

This will generate `libspike_dpi.so` (or the corresponding shared library for your platform) in the `dpi/` directory for your simulator to link against.

### Limitations and Notes

*   Spike is a functional ISA model, not a cycle-accurate model. This solution compares the **architectural state** (PC, registers, CSRs) to find functional/ISA-level bugs, not microarchitectural or timing details.