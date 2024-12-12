// See LICENSE for license details.

#ifndef _RISCV_CACHE_SIM_H
#define _RISCV_CACHE_SIM_H

#include "memtracer.h"
#include <cstring>
#include <string>
#include <map>
#include <cstdint>

class lfsr_t // related to generate random number
{
public:
  lfsr_t() : reg(1) {}
  lfsr_t(const lfsr_t& lfsr) : reg(lfsr.reg) {}
  uint32_t next() { return reg = (reg>>1)^(-(reg&1) & 0xd0000001); }
private:
  uint32_t reg;
};

class cache_sim_t // L2 Cache
{
public:
  cache_sim_t(size_t sets, size_t ways, size_t linesz, bool lru, const char *name);
  cache_sim_t(const cache_sim_t& rhs);
  virtual ~cache_sim_t();

  void access(uint64_t addr, size_t bytes, bool store);
  void print_stats();
  void set_miss_handler(cache_sim_t* mh) { miss_handler = mh; }
  void set_log(bool _log) { log = _log; }

  static cache_sim_t *construct(const char *config, const char *name); // cache_sim_t::construct(s, "L2$"));
  // config = <Number of set>:<Number of way>:<Block size>(:lru)

protected:
  static const uint64_t VALID = 1ULL << 63;
  static const uint64_t DIRTY = 1ULL << 62;

  virtual uint64_t* check_tag(uint64_t addr);
  virtual uint64_t victimize(uint64_t addr);

  lfsr_t lfsr;
  cache_sim_t* miss_handler;

  /* Cache Simulator */
  size_t sets;   // Number of sets
  size_t ways;   // Number of ways in set
  size_t linesz; // block size
  bool lru;      // LRU replacement policy
  /* --------------- */
  size_t idx_shift;

  uint64_t* tags;
  uint64_t read_accesses; // Number of access
  uint64_t read_misses;
  uint64_t bytes_read; // Number of bytes readed
  uint64_t write_accesses;

  /* Cache Simulator */
  uint64_t write_misses;
  uint64_t bytes_written;
  uint64_t writebacks;
  /* --------------- */

  std::string name;
  bool log;

  void init();
};

class fa_cache_sim_t : public cache_sim_t
{
public:
  fa_cache_sim_t(size_t ways, size_t linesz, bool lru, const char *name);
  uint64_t* check_tag(uint64_t addr);
  uint64_t victimize(uint64_t addr);
private:
  static bool cmp(uint64_t a, uint64_t b);
  std::map<uint64_t, uint64_t> tags;
};

class cache_memtracer_t : public memtracer_t
{
public:
  cache_memtracer_t(const char* config, const char* name)
  {
    cache = cache_sim_t::construct(config, name); // construct
  }
  ~cache_memtracer_t()
  {
    delete cache;
  }
  void set_miss_handler(cache_sim_t* mh)
  {
    cache->set_miss_handler(mh);
  }
  void set_log(bool log)
  {
    cache->set_log(log);
  }

protected:
  cache_sim_t* cache;
};

class icache_sim_t : public cache_memtracer_t // I-cache in L1 Cache
{
 public:
  icache_sim_t(const char *config) : cache_memtracer_t(config, "I$") {} // config = <Number of set>:<Number of way>:<Block size>(:lru)
  bool interested_in_range(uint64_t begin, uint64_t end, access_type type)
  {
    return type == FETCH;
  }
  void trace(uint64_t addr, size_t bytes, access_type type)
  {
    if (type == FETCH) cache->access(addr, bytes, false);
  }
};

class dcache_sim_t : public cache_memtracer_t // D-cache in L1 Cache
{
public:
  dcache_sim_t(const char* config) : cache_memtracer_t(config, "D$") {} // config
  bool interested_in_range(uint64_t begin, uint64_t end, access_type type)
  {
    return type == LOAD || type == STORE;
  }
  void trace(uint64_t addr, size_t bytes, access_type type)
  {
    if (type == LOAD || type == STORE) cache->access(addr, bytes, type == STORE);
  }
};

#endif
