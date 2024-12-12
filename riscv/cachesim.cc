// See LICENSE for license details.

#include "cachesim.h"
#include "common.h"
#include <cstdlib>
#include <iostream>
#include <iomanip>

cache_sim_t::cache_sim_t(size_t _sets, size_t _ways, size_t _linesz, bool _lru, const char* _name)
: sets(_sets), ways(_ways), linesz(_linesz), lru(_lru), name(_name), log(false)
{
  init(); // init()
}

static void help()
{
  std::cerr << "Cache configurations must be of the form" << std::endl;
  std::cerr << "  sets:ways:blocksize (Random replacement policy)" << std::endl;
  std::cerr << "  sets:ways:blocksize:lru (LRU replacement policy)" << std::endl;
  std::cerr << "where sets, ways, and blocksize are positive integers, with" << std::endl;
  std::cerr << "sets and blocksize both powers of two and blocksize at least 8." << std::endl;
  exit(1);
}

cache_sim_t* cache_sim_t::construct(const char* config, const char* name)
{
  const char *wp = strchr(config, ':'); // strchr(): find target char in string
  if (!wp++) help(); // help message
  const char* bp = strchr(wp, ':');
  if (!bp++) help();
  const char *lp = strchr(bp, ':');

  size_t sets = atoi(std::string(config, wp).c_str()); // number of sets
  size_t ways = atoi(std::string(wp, bp).c_str()); // number of ways
  size_t linesz = !lp? atoi(bp): atoi(std::string(bp, lp + 1).c_str()); // block size
  bool lru;

  if (!lp++)
    lru = false;
  else if (std::string(lp) == "lru")
    lru = true;
  else
    help();

  if (ways > 4 /* empirical */ && sets == 1)
    return new fa_cache_sim_t(ways, linesz, lru, name); // Fully-Associative
  return new cache_sim_t(sets, ways, linesz, lru, name);
}

void cache_sim_t::init()
{
  if(sets == 0 || (sets & (sets - 1))) // check if powers of two
    help();
  if (linesz < 8 || (linesz & (linesz - 1))) // check if powers of two and at least 8
    help();

  idx_shift = 0; // Number of shift needed to get Index
  for (size_t x = linesz; x > 1; x >>= 1) // if block size is 8(2^3), then idx_shift is 3
    idx_shift++;

  tags = new uint64_t[sets*ways](); // initialize each element to 0
  tag_priority = new size_t[sets*ways]();

  read_accesses = 0;
  read_misses = 0;
  bytes_read = 0;
  write_accesses = 0;
  write_misses = 0;
  bytes_written = 0;
  writebacks = 0;

  miss_handler = NULL;
}

cache_sim_t::cache_sim_t(const cache_sim_t& rhs)
 : sets(rhs.sets), ways(rhs.ways), linesz(rhs.linesz),
   idx_shift(rhs.idx_shift), name(rhs.name), log(false)
{
  tags = new uint64_t[sets*ways];
  memcpy(tags, rhs.tags, sets*ways*sizeof(uint64_t));

  tag_priority = new size_t[sets * ways];
  memcpy(tag_priority, rhs.tag_priority, sets * ways * sizeof(size_t));
}

cache_sim_t::~cache_sim_t() // Destructor
{
  print_stats(); // Print a result of cache simulation
  delete[] tags;
  delete[] tag_priority;
}

void cache_sim_t::print_stats() // Result of cache simulation
{
  if(read_accesses + write_accesses == 0)
    return;

  // calculate miss rate
  float mr = 100.0f*(read_misses+write_misses)/(read_accesses+write_accesses);

  std::cout << std::setprecision(3) << std::fixed;
  std::cout << name << " ";
  std::cout << "Bytes Read:            " << bytes_read << std::endl;
  std::cout << name << " ";
  std::cout << "Bytes Written:         " << bytes_written << std::endl;
  std::cout << name << " ";
  std::cout << "Read Accesses:         " << read_accesses << std::endl;
  std::cout << name << " ";
  std::cout << "Write Accesses:        " << write_accesses << std::endl;
  std::cout << name << " ";
  std::cout << "Read Misses:           " << read_misses << std::endl;
  std::cout << name << " ";
  std::cout << "Write Misses:          " << write_misses << std::endl;
  std::cout << name << " ";
  std::cout << "Writebacks:            " << writebacks << std::endl;
  std::cout << name << " ";
  std::cout << "Miss Rate:             " << mr << '%' << std::endl;
}

uint64_t *cache_sim_t::check_tag(uint64_t addr) // Check Cache Hit or Cache Miss
{
  size_t idx = (addr >> idx_shift) & (sets - 1); // Index used to select the set
  size_t tag = (addr >> idx_shift) | VALID;

  for (size_t i = 0; i < ways; i++) // iterate all way in set
    if (tag == (tags[idx*ways + i] & ~DIRTY)) { // ignore dirty bit
      if (lru) {
        for (size_t j = 0; j < ways; j++)
          if (tag_priority[idx*ways + j] < tag_priority[idx*ways + i])
            tag_priority[idx*ways + j]++;
        
        tag_priority[idx*ways + i] = 0;
      }

      return &tags[idx*ways + i];
    }

  return NULL;
}

uint64_t cache_sim_t::victimize(uint64_t addr) // determine which data evicted from Cache
{
  uint64_t victim = 0;
  size_t idx = (addr >> idx_shift) & (sets - 1); // Index used to select the set

  if (lru) {
    size_t max_priority = 0, max_tag_idx = idx*ways;
    for (size_t i = 0; i < ways; i++) {
      if (++tag_priority[idx*ways + i] > max_priority) {
        max_priority = tag_priority[idx*ways + i];
        max_tag_idx = idx*ways + i;
      }
    }

    victim = tags[max_tag_idx];
    tags[max_tag_idx] = (addr >> idx_shift) | VALID; // replace Cache
    tag_priority[max_tag_idx] = 0;
  }
  else { // Random
    size_t way = lfsr.next() % ways; // lfsr.next(): generate random number

    victim = tags[idx*ways + way];
    tags[idx*ways + way] = (addr >> idx_shift) | VALID; // replace Cache
  }

  return victim;
}

void cache_sim_t::access(uint64_t addr, size_t bytes, bool store) // access to addr
{
  store ? write_accesses++ : read_accesses++; // store or load
  (store ? bytes_written : bytes_read) += bytes;

  uint64_t* hit_way = check_tag(addr); // Check Cache Hit or Cache Miss
  if (likely(hit_way != NULL)) // Cache Hit
  {
    if (store && wb) // write back
      *hit_way |= DIRTY; // on dirty bit
    else if (store && miss_handler) // write through
      miss_handler->access(addr & ~(linesz - 1), linesz, true);  
    return; // return
  }

  // Cache Miss!!
  store ? write_misses++ : read_misses++;
  if (log)
  {
    std::cerr << name << " "
              << (store ? "write" : "read") << " miss 0x"
              << std::hex << addr << std::endl;
  }

  uint64_t victim = victimize(addr); // determine to evict which data in Random

  if (wb && (victim & (VALID | DIRTY)) == (VALID | DIRTY)) // when victim exists and, victim is both valid and "dirty"
  {
    uint64_t dirty_addr = (victim & ~(VALID | DIRTY)) << idx_shift; // victim is tag, so shift as much as Index
    if (miss_handler) // if the cache is I-cache or D-cache, miss handler is L2 Cache. else NULL
      miss_handler->access(dirty_addr, linesz, true); // access dirty_addr in L2 Cache
    writebacks++;
  }

  if (miss_handler)
    miss_handler->access(addr & ~(linesz-1), linesz, false); // for including policy in L2 Cache

  if (store && wb) // write back
    *check_tag(addr) |= DIRTY; // on dirty bit
  else if (store && miss_handler) // write through
    miss_handler->access(addr & ~(linesz - 1), linesz, true);
}

// Fully Associative Cache
fa_cache_sim_t::fa_cache_sim_t(size_t ways, size_t linesz, bool lru, const char* name)
  : cache_sim_t(1, ways, linesz, lru, name)
{
}

uint64_t *fa_cache_sim_t::check_tag(uint64_t addr) // Check Cache Hit or Cache Miss
{
  auto it = tags.find(addr >> idx_shift);
  if (it == tags.end() || !(it->second & VALID))
    return NULL;
  else {
    if (lru) {
      for (auto& entry : tag_priority)
        if (entry.first != it->first && entry.second < tag_priority[it->first]) 
          entry.second++;
      tag_priority[it->first] = 0;
    }
    return &it->second;
  }
}

uint64_t fa_cache_sim_t::victimize(uint64_t addr) // determine to evict which data from Cache
{
  uint64_t victim = 0;
  if (tags.size() == ways) // cache is full
  {
    if (lru) {
      size_t max_priority = 0, max_tag_key;
      for (auto &entry : tag_priority)
        if (++entry.second > max_priority) {
          max_priority = entry.second;
          max_tag_key = entry.first;
        }

      victim = tags[max_tag_key];
      tags.erase(max_tag_key);
      tag_priority.erase(max_tag_key);
    }
    else { // Random
      auto it = tags.begin();
      std::advance(it, lfsr.next() % ways); // lfsr.next(): generate random number
      victim = it->second;
      tags.erase(it);
    }
  }
  tags[addr >> idx_shift] = (addr >> idx_shift) | VALID; // replace Cache
  if (lru) tag_priority[addr >> idx_shift] = 0;

  return victim;
}
