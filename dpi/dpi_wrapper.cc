// dpi_wrapper.cc
// Minimal DPI wrapper for Spike / sim_t integration.
// Exposes C-callable APIs for SystemVerilog DPI.

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <cstring>

#include "sim.h"        // sim_t, device_factory_sargs_t, etc.
#include "config.h"     // cfg_t
#include "spdlog_wrapper.h" // you asked to keep this

// keep the exact function you requested
extern "C" {
void dpi_set_log_level(const char* level_cstr)
{
    if (!level_cstr) return;
    std::string level(level_cstr);
    std::cout << "[DRAMSys DPI] Received log level from SystemVerilog: " << level << std::endl;
    if (level == "trace") spdlog::set_level(spdlog::level::trace);
    else if (level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (level == "info") spdlog::set_level(spdlog::level::info);
    else if (level == "warn") spdlog::set_level(spdlog::level::warn);
    else if (level == "error") spdlog::set_level(spdlog::level::err);
    else if (level == "critical") spdlog::set_level(spdlog::level::critical);
    else if (level == "off") spdlog::set_level(spdlog::level::off);
}
} // extern "C"

static sim_t *g_sim = nullptr;

extern "C" {

// Create spike/sim instance and load the provided binary (elf/bin path).
// If called multiple times without spike_delete, subsequent calls are no-op.
void spike_create(const char *filename)
{
    if (!filename) return;
    if (g_sim) return;

    // create default cfg and adjust minimal fields expected by sim_t
    cfg_t *config = new cfg_t(); // use default ctor available in your Spike version
    // set a reasonable default ISA/priv (adjust if you need RV32)
    config->isa = "RV64GC";
    config->priv = "M";
    // ensure there is at least hart 0
    config->hartids = std::vector<size_t>{0};

    // minimal debug module config
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

    // no plugin devices
    std::vector<device_factory_sargs_t> empty_factories;

    // pass the binary path as htif arg so htif will load it
    std::vector<std::string> htif_args;
    htif_args.push_back(std::string("+payload=") + filename);
    htif_args.push_back(std::string(filename));
    

    std::vector<std::pair<reg_t, abstract_mem_t*>> mems;

    // choose dram base/size — 与你的 params 保持一致
    reg_t dram_base = (reg_t)0x80000000ULL;                  // typical default
    size_t dram_size = (size_t) (512ULL * 1024 * 1024);      // 512MB example

    // create mem_t (注意构造函数签名：mem_t(reg_t size) 在你的 devices.h 中)
    mem_t *dram_mem = new mem_t(dram_size);

    // map the device at dram_base
    mems.push_back(std::make_pair(dram_base, (abstract_mem_t*)dram_mem));
    
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
        // call public dpi_reset (reset() is private in your version)
        g_sim->start();
        g_sim->dpi_reset();
        g_sim->dpi_set_pc(0x80000000);
    } catch (const std::exception &e) {
        std::cerr << "[DPI] spike_create exception: " << e.what() << std::endl;
        if (g_sim) {
            delete g_sim;
            g_sim = nullptr;
        }
    }
}

// Delete spike instance
void spike_delete()
{
    if (!g_sim) return;
    delete g_sim;
    g_sim = nullptr;
}

// Step the simulation forward by one "step"
int spike_step()
{
    if (!g_sim) return -1;
    return g_sim->dpi_step(1);
}

// Reset spike (public wrapper)
void spike_reset()
{
    if (!g_sim) return;
    g_sim->dpi_reset();
}

// Get PC for hartid
uint64_t spike_get_pc(unsigned hartid)
{
    if (!g_sim) return 0;
    return g_sim->dpi_get_pc(hartid);
}

// Read 32 general-purpose registers into the provided array (must be length 32).
// Returns number of registers written (32) or 0 on error.
int spike_get_all_gprs(unsigned hartid, uint64_t out[32])
{
    if (!g_sim || !out) return 0;
    return g_sim->dpi_get_all_gprs(hartid, out);
}

// Get CSR value via sim_t wrapper
uint64_t spike_get_csr(unsigned hartid, uint32_t csr_addr)
{
    if (!g_sim) return 0;
    return g_sim->dpi_get_csr(hartid, csr_addr);
}

// Try to write CSR (best-effort; may be absent depending on your processor_t impl)
void spike_put_csr(unsigned hartid, uint32_t csr_addr, uint64_t value)
{
    if (!g_sim) return;
    processor_t* p = g_sim->get_core_by_id(hartid);
    if (p) {
        try {
            p->put_csr((int)csr_addr, value);
        } catch (...) {
            // ignore if not provided
        }
    }
}

} // extern "C"
