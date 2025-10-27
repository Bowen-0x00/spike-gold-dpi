[Chinese README (中文版本)](./README_zh.md) · [English README (English version)](./README.md)

# spike-gold-dpi

## Project Overview

`spike-gold-dpi` is a modification of Spike (the RISC-V ISA simulator) that exposes a DPI-C interface so a SystemVerilog RTL simulation can **drive Spike per-clock** and sample Spike’s architectural state at each clock (or at whatever step semantics you define). The goal is to enable convenient difftest-style comparisons during RTL development: if architectural state (PC / instruction / GPR / CSR) diverges from Spike, the simulation stops (optionally) and reports which instruction and which registers differ—making bug localization much faster.

Intended audience: CPU / RISC-V RTL developers who want fast, precise ISA-level comparisons in development and CI.



## Key Features

* **DPI-C interface**: create / destroy Spike instances from SystemVerilog.
* **`step()`**: advance the Spike model at each clock (or at a chosen synchronization point).
* **Snapshot reads**: read PC, general-purpose registers (x0..x31), floating registers, CSRs, etc.
* **Configurable comparators**: choose which registers / CSR sets to check.
* **Diff reporting**: prints the failing PC, disassembly (if ELF / symbols are available), and lists which registers / CSRs differ and their values. Can be configured to stop simulation on mismatch (useful for CI).
* **Helper scripts**: generate golden traces, compare snapshots, and produce friendlier diff reports.



## Quick Usage Summary

1. Build / install the modified Spike in this repository (see Build & Dependencies).
2. In your SystemVerilog testbench `import "DPI-C"` and call the functions provided by this project.
3. After reset, create a Spike instance with `spike_create("<elf>", hartid)`.
4. On each clock edge call `spike_step(ctx)` (or follow your chosen policy), then call `spike_get_gpr`, `spike_get_pc`, `spike_get_csr` to read Spike's state.
5. Compare Spike’s state with the DUT (RTL) snapshot: if they differ, print detailed info and (optionally) stop the simulation.
6. At the end of simulation call `spike_delete(ctx)`.



## Build & Dependencies

This project requires the same dependencies as upstream Spike.

### Example build steps

```bash
mkdir build && cd build
../configure --prefix=$RISCV
make -j$(nproc)
cd ../dpi
make
# optional: make install
```

After building, a shared library will be produced under `dpi/`—for example `dpi/libspike_dpi.so` (platform-specific name). Link or load that shared library into your RTL simulator when you run your RTL tests so the SV testbench can call the DPI functions.



## Limitations & Notes

* **Spike is a functional ISA model**, not a cycle-accurate microarchitectural model. This project compares **architectural state** (PC, registers, CSRs) to find functional/ISA-level bugs; it is **not** intended to detect microarchitectural or timing-specific issues.
* When performing per-clock alignment with a cycle-accurate RTL, you must decide the meaning of `spike_step()` in your testbench (e.g., step-per-instruction when the RTL retires an instruction vs. step every cycle and let Spike advance to the next retire event). The framework supports different step modes; pick the one that matches your test strategy.
* For disassembly/symbolic diagnostics you should provide an ELF with symbols (or pass an objdump/map) so the helper scripts can show human-readable instruction info.
