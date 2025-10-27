
[Chinese README (中文版本)](./README_zh.md) · [English README (English version)](./README.md)
## spike-gold-dpi

### 项目简介

`spike-gold-dpi` 是在 Spike（RISC-V ISA 模拟器）基础上做的改造：通过暴露 DPI-C 接口，让 SystemVerilog 的 RTL 仿真可以**按时钟驱动 Spike**，并在每个时钟（或按你定义的步进语义）读取 Spike 的架构状态。目标是在 RTL 开发时方便做对比测试（difftest）：一旦架构状态（PC/指令/GPR/CSR）与 Spike 不一致，就停止仿真并报告是哪条指令、哪些寄存器不一致，方便定位错误。

适用对象：处理器/RISC-V RTL 开发人员，期望在提交/CI 时对设计做快速、精确的 ISA 级对比验证。

### 主要功能

* DPI-C 接口：SystemVerilog 端可以创建/销毁 Spike 实例。
* `step()`：每个时钟（或你选择的点）调用，前进 Spike 模型。
* 快照读取：读取 PC、通用寄存器（x0..x31）、浮点寄存器、CSR 等。
* 可配置的比较器：支持只比较需要关注的寄存器/CSR 集合。
* 差异报告：输出出错的 PC、反汇编（若 ELF 可用），并列出不同的寄存器/CSR 及其值；可配置为在差异发生时结束仿真（非常适合 CI）。
* 辅助脚本：用于生成 golden trace、比较并输出更友好的差异报告。

### 使用概要

1. 编译/安装本仓库中修改过的 Spike（见下文）。
2. 在 SystemVerilog 测试平台中 `import "DPI-C"` 并调用本项目暴露的函数。
3. 复位后使用 `spike_create("<elf>", hartid)` 创建模型实例。
4. 每个时钟边沿调用 `spike_step(ctx)`（或按你的策略），然后调用 `spike_get_gpr` / `spike_get_pc` / `spike_get_csr` 读取状态。
5. 将 Spike 的状态与 DUT（RTL）的状态做比较：若不一致则打印详细信息并（可选）停止仿真。
6. 仿真结束调用 `spike_delete(ctx)`。

### 构建与依赖

需要与上游 Spike 相同的依赖：

构建示例：

```bash
mkdir build && cd build
../configure --prefix=$RISCV
make -j$(nproc)
cd ../dpi
make
# 可选：make install
```

会在dpi/下生成 `libspike_dpi.so`（或平台对应的共享库），供仿真器链接。

### 限制与注意事项

- Spike 是功能级 ISA 模型，不是 cycle-accurate 模型。本方案对比的是**架构状态**（PC、寄存器、CSR），用于查找功能性/ISA 级别的错误，而非微结构或时序细节。
