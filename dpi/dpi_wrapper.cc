// dpi_wrapper.cc
// DPI wrapper for Spike integration.
// Exposes C-callable APIs for SystemVerilog DPI.
//
// Minimal, thread-safe, and configurable: ISA / DRAM base / DRAM size / initial PC
// can be set independently (with sensible defaults) before creating the simulator.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <optional>
#include <memory>
#include <iostream>

#include "sim.h"        // sim_t, processor_t, device / memory types (project-specific)
#include "config.h"     // cfg_t
#include "spdlog_wrapper.h"

using namespace std;

static std::mutex g_mutex;
static sim_t *g_sim = nullptr;

// Configurable defaults (can be overridden via DPI calls)
static std::string g_isa_default = "RV64GC";
static uint64_t    g_dram_base_default = 0x80000000ULL;
static size_t      g_dram_size_default = (size_t)(512ULL * 1024 * 1024); // 512MiB
static uint64_t    g_initial_pc_default = 0x80000000ULL;

// User-overrides (std::optional indicates whether the user set them)
static std::optional<std::string> g_isa_override;
static std::optional<uint64_t>    g_dram_base_override;
static std::optional<size_t>      g_dram_size_override;
static std::optional<uint64_t>    g_initial_pc_override;

extern "C" {

/* Logging level */
void dpi_set_log_level(const char* level_cstr)
{
    if (!level_cstr) return;
    std::string level(level_cstr);
    if (level == "trace") spdlog::set_level(spdlog::level::trace);
    else if (level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (level == "info")  spdlog::set_level(spdlog::level::info);
    else if (level == "warn")  spdlog::set_level(spdlog::level::warn);
    else if (level == "error") spdlog::set_level(spdlog::level::err);
    else if (level == "critical") spdlog::set_level(spdlog::level::critical);
    else if (level == "off") spdlog::set_level(spdlog::level::off);
}

/* Set the ISA string (e.g. "RV64GC" or "RV32IMC"). Call before spike_create to take effect. */
void spike_set_isa(const char* isa_cstr)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!isa_cstr) { g_isa_override.reset(); return; }
    g_isa_override = std::string(isa_cstr);
}

/* Set DRAM base address. Call before spike_create to take effect. */
void spike_set_dram_base(uint64_t base)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_dram_base_override = base;
}

/* Set DRAM size in bytes. Call before spike_create to take effect. */
void spike_set_dram_size(uint64_t size)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_dram_size_override = (size_t)size;
}

/* Set initial PC. If simulator already created, sets PC immediately; otherwise applied at create. */
void spike_set_pc(uint64_t pc)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_sim) {
        try {
            g_sim->dpi_set_pc((reg_t)pc);
        } catch (...) {
            // ignore exceptions from implementation-specific calls
        }
    } else {
        g_initial_pc_override = pc;
    }
}

/* Create Spike/sim instance and load the provided binary path.
   Multiple calls without delete are no-op. */
void spike_create(const char *filename)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_sim) return;
    if (!filename) {
        std::fprintf(stderr, "[dpi] spike_create: filename is null\n");
        return;
    }

    // prepare cfg
    cfg_t *config = new cfg_t();
    // use override or default ISA
    config->isa = strdup(g_isa_override.value_or(g_isa_default).c_str());
    // default privilege mode (can be changed via cfg_t later if needed)
    config->priv = "M";
    config->hartids = std::vector<size_t>{0};

    // debug module config (minimal reasonable defaults)
    debug_module_config_t dm_config{};
    dm_config.progbufsize = 2;
    dm_config.max_sba_data_width = 0;
    dm_config.require_authentication = false;
    dm_config.abstract_rti = 0;
    dm_config.support_hasel = true;
    dm_config.support_abstract_csr_access = true;
    dm_config.support_abstract_fpr_access = true;
    dm_config.support_haltgroups = true;
    dm_config.support_impebreak = true;

    std::vector<device_factory_sargs_t> empty_factories;

    // prepare HTIF/args so sim will load the binary
    std::vector<std::string> htif_args;
    htif_args.push_back(std::string("+payload=") + filename);
    htif_args.push_back(std::string(filename));

    // memory mapping: use override or defaults
    reg_t dram_base = (reg_t) (g_dram_base_override.value_or(g_dram_base_default));
    size_t dram_size = g_dram_size_override.value_or(g_dram_size_default);

    std::vector<std::pair<reg_t, abstract_mem_t*>> mems;
    // allocate DRAM device (project-specific mem_t constructor assumed)
    abstract_mem_t *dram_mem = nullptr;
    try {
        mem_t *m = new mem_t((reg_t)dram_size);
        dram_mem = reinterpret_cast<abstract_mem_t*>(m);
        mems.push_back(std::make_pair(dram_base, dram_mem));
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[dpi] dram allocation failed: %s\n", e.what());
        delete config;
        return;
    } catch (...) {
        std::fprintf(stderr, "[dpi] dram allocation unknown failure\n");
        delete config;
        return;
    }

    try {
        g_sim = new sim_t(config,
                          /*halted*/ false,
                          /*mems*/ mems,
                          /*plugin_device_factories*/ empty_factories,
                          /*args*/ htif_args,
                          /*dm_config*/ dm_config,
                          /*log_path*/ "dpi_spike.log",
                          /*dtb_enabled*/ false,
                          /*dtb_file*/ nullptr,
                          /*socket_enabled*/ false,
                          /*cmd_file*/ nullptr,
                          /*instruction_limit*/ std::nullopt);
        g_sim->set_debug(false);
        g_sim->start();
        g_sim->dpi_reset();

        // set initial PC: override or default
        uint64_t pc = g_initial_pc_override.value_or(g_initial_pc_default);
        try { g_sim->dpi_set_pc((reg_t)pc); } catch (...) {}
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[dpi] spike_create exception: %s\n", e.what());
        if (g_sim) { delete g_sim; g_sim = nullptr; }
    } catch (...) {
        std::fprintf(stderr, "[dpi] spike_create unknown exception\n");
        if (g_sim) { delete g_sim; g_sim = nullptr; }
    }
}

/* Delete spike instance */
void spike_delete()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return;
    delete g_sim;
    g_sim = nullptr;
}

/* Step simulation forward. Return -1 on error, otherwise simulator-specific return. */
int spike_step()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return -1;
    try {
        return g_sim->dpi_step(1);
    } catch (...) {
        return -1;
    }
}

/* Reset simulator (public wrapper). */
void spike_reset()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return;
    try { g_sim->dpi_reset(); } catch (...) {}
}

/* Get PC for hart */
uint64_t spike_get_pc(unsigned hartid)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try { return (uint64_t) g_sim->dpi_get_pc(hartid); } catch (...) { return 0; }
}

/* Read 32 GPRs into out array. Returns number written or 0 on error. */
int spike_get_all_gprs(unsigned hartid, uint64_t out[32])
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim || !out) return 0;
    try { return g_sim->dpi_get_all_gprs(hartid, out); } catch (...) { return 0; }
}

/* Read CSR via sim wrapper */
uint64_t spike_get_csr(unsigned hartid, uint32_t csr_addr)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try { return g_sim->dpi_get_csr(hartid, csr_addr); } catch (...) { return 0; }
}

/* Write CSR (best-effort). */
void spike_put_csr(unsigned hartid, uint32_t csr_addr, uint64_t value)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (p) p->put_csr((int)csr_addr, value);
    } catch (...) {
        // ignore errors from unsupported CSR writes
    }
}

} // extern "C"
