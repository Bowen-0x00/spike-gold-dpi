// dpi_wrapper.cc
// DPI wrapper for Spike integration.
// Exposes C-callable APIs for SystemVerilog DPI.

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
#include <algorithm>

#include "sim.h"        // sim_t, processor_t, device / memory types
#include "config.h"     // cfg_t
#include "spdlog_wrapper.h"

using namespace std;

static std::mutex g_mutex;
static sim_t *g_sim = nullptr;

// Defaults and overrides
static std::string g_isa_default = "RV64GC";
static uint64_t    g_dram_base_default = 0x80000000ULL;
static size_t      g_dram_size_default = (size_t)(512ULL * 1024 * 1024); // 512MiB
static uint64_t    g_initial_pc_default = 0x80000000ULL;

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

/* Set ISA/DRAM/PC overrides (call before spike_create to take effect) */
void spike_set_isa(const char* isa_cstr)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!isa_cstr) { g_isa_override.reset(); return; }
    g_isa_override = std::string(isa_cstr);
}

void spike_set_dram_base(uint64_t base)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_dram_base_override = base;
}

void spike_set_dram_size(uint64_t size)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_dram_size_override = (size_t)size;
}

void spike_set_pc(uint64_t pc)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_sim) {
        try { g_sim->dpi_set_pc((reg_t)pc); } catch (...) {}
    } else {
        g_initial_pc_override = pc;
    }
}

/* Create Spike instance and load ELF */
void spike_create(const char *filename)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_sim) return;
    if (!filename) {
        std::fprintf(stderr, "[dpi] spike_create: filename is null\n");
        return;
    }

    cfg_t *config = new cfg_t();
    config->isa = strdup(g_isa_override.value_or(g_isa_default).c_str());
    spdlog::debug("Using ISA: {}", config->isa);
    config->priv = strdup("M");
    config->hartids = std::vector<size_t>{0};

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
    std::vector<std::string> htif_args;
    htif_args.push_back(std::string("+payload=") + filename);
    htif_args.push_back(std::string(filename));

    reg_t dram_base = (reg_t) (g_dram_base_override.value_or(g_dram_base_default));
    size_t dram_size = g_dram_size_override.value_or(g_dram_size_default);

    std::vector<std::pair<reg_t, abstract_mem_t*>> mems;
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

/* Delete instance */
void spike_delete()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return;
    delete g_sim;
    g_sim = nullptr;
}

/* Step */
int spike_step()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return -1;
    try { return g_sim->dpi_step(1); } catch (...) { return -1; }
}

/* Reset */
void spike_reset()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return;
    try { g_sim->dpi_reset(); } catch (...) {}
}

/* PC */
uint64_t spike_get_pc(unsigned hartid)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try { return (uint64_t) g_sim->dpi_get_pc(hartid); } catch (...) { return 0; }
}

/* GPRs */
int spike_get_all_gprs(unsigned hartid, uint64_t out[32])
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim || !out) return 0;
    try { return g_sim->dpi_get_all_gprs(hartid, out); } catch (...) { return 0; }
}

/* CSR read (generic) */
uint64_t spike_get_csr(unsigned hartid, uint32_t csr_addr)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try { return g_sim->dpi_get_csr(hartid, csr_addr); } catch (...) { return 0; }
}

/* CSR write (best-effort) */
void spike_put_csr(unsigned hartid, uint32_t csr_addr, uint64_t value)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (p) p->put_csr((int)csr_addr, value);
    } catch (...) {}
}

/* --- Floating-point registers --- */
/* Read 32 FPRs as raw bit patterns. Returns 32 or 0. */
int spike_get_all_fprs(unsigned hartid, uint64_t out[32])
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim || !out) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        state_t* st = p->get_state();
        if (!st) return 0;
        // copy each FPR raw bytes into uint64_t (zero-extended if smaller)
        size_t fpr_size = sizeof(st->FPR[0]);
        for (int i = 0; i < 32; ++i) {
            uint64_t v = 0;
            size_t copy_bytes = std::min<size_t>(fpr_size, sizeof(uint64_t));
            std::memcpy(&v, &st->FPR[i], copy_bytes);
            out[i] = v;
        }
        return 32;
    } catch (...) {
        return 0;
    }
}

/* --- Vector registers dump --- */
/* Write vector register file as contiguous uint64_t words into 'out'.
   out_size_qwords is the capacity of out[] in 64-bit words.
   Returns number of qwords written or 0 on error.
*/
int spike_get_all_vregs(unsigned hartid, uint64_t *out, int out_size_qwords)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim || !out || out_size_qwords <= 0) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        vectorUnit_t &VU = p->VU;
        if (!VU.reg_file) return 0;
        reg_t VLEN = VU.get_vlen(); // bits
        if (VLEN == 0) return 0;
        size_t bytes_per_reg = (size_t)(VLEN >> 3);
        const int nregs = 32; // architectural vector registers
        size_t total_bytes = bytes_per_reg * (size_t)nregs;
        size_t total_qwords = (total_bytes + 7) / 8;
        int to_write = (int) std::min<size_t>((size_t)out_size_qwords, total_qwords);
        const uint8_t *base = reinterpret_cast<const uint8_t*>(VU.reg_file);
        for (int q = 0; q < to_write; ++q) {
            size_t byte_idx = (size_t)q * 8;
            uint64_t val = 0;
            if (byte_idx < total_bytes) {
                size_t copy_bytes = std::min<size_t>(8, total_bytes - byte_idx);
                std::memcpy(&val, base + byte_idx, copy_bytes);
            }
            out[q] = val;
        }
        return to_write;
    } catch (...) {
        return 0;
    }
}

/* VLEN in bits */
int spike_get_vlen(unsigned hartid)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        return (int)p->VU.get_vlen();
    } catch (...) {
        return 0;
    }
}

/* vlenb in bytes (VLEN/8) */
uint64_t spike_get_vlenb(unsigned hartid)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        return (uint64_t)p->VU.vlenb;
    } catch (...) {
        return 0;
    }
}

/* Vector CSRs: vxsat, vxrm, vstart, vl, vtype */
/* All return 0 on error. For csr-like fields (vstart/vl/vtype/vxrm/vxsat) try to read CSR object if present. */

uint64_t spike_get_vxsat(unsigned hartid)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        // vxsat may be a csr_t_p (pointer-like); if present call read()
        if (p->VU.vxsat) {
            return (uint64_t) p->VU.vxsat->read();
        }
        return 0;
    } catch (...) {
        return 0;
    }
}

uint64_t spike_get_vxrm(unsigned hartid)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        if (p->VU.vxrm) return (uint64_t) p->VU.vxrm->read();
        return 0;
    } catch (...) {
        return 0;
    }
}

uint64_t spike_get_vstart(unsigned hartid)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        if (p->VU.vstart) return (uint64_t) p->VU.vstart->read();
        return 0;
    } catch (...) {
        return 0;
    }
}

uint64_t spike_get_vl(unsigned hartid)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        if (p->VU.vl) return (uint64_t) p->VU.vl->read();
        // fallback: vlmax / setvl_count maybe available
        return (uint64_t) p->VU.vlmax;
    } catch (...) {
        return 0;
    }
}

uint64_t spike_get_vtype(unsigned hartid)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        if (p->VU.vtype) return (uint64_t) p->VU.vtype->read();
        // fallback: construct vtype from vsew and vflmul if possible
        uint64_t vtype = 0;
        vtype |= (uint64_t)(p->VU.vsew & 0xFF);
        // encoding of vflmul may not match vtype bits; leave rest zero if unknown
        return vtype;
    } catch (...) {
        return 0;
    }
}

/* Generic vector CSR reader by CSR address (useful if you want to read via CSR number) */
uint64_t spike_get_vcsr(unsigned hartid, uint32_t csr_addr)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_sim) return 0;
    try {
        processor_t* p = g_sim->get_core_by_id(hartid);
        if (!p) return 0;
        state_t* st = p->get_state();
        if (!st) return 0;
        auto it = st->csrmap.find((reg_t)csr_addr);
        if (it != st->csrmap.end() && it->second) {
            return (uint64_t) it->second->read();
        }
        return 0;
    } catch (...) {
        return 0;
    }
}

} // extern "C"
