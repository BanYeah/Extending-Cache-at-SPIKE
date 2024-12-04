// See LICENSE for license details.

#include "cachesim.h"
#include "common.h"
#include <cstdlib>
#include <iostream>
#include <iomanip>

cache_sim_t::cache_sim_t(size_t _sets, size_t _ways, size_t _linesz, const char* _name)
: sets(_sets), ways(_ways), linesz(_linesz), name(_name), log(false)
{
  init(); // init()
}

static void help()
{
  std::cerr << "Cache configurations must be of the form" << std::endl;
  std::cerr << "  sets:ways:blocksize" << std::endl;
  std::cerr << "where sets, ways, and blocksize are positive integers, with" << std::endl;
  std::cerr << "sets and blocksize both powers of two and blocksize at least 8." << std::endl;
  exit(1);
}

cache_sim_t* cache_sim_t::construct(const char* config, const char* name)
{
  const char *wp = strchr(config, ':'); // strchr: find target char in string
  if (!wp++) help(); // help message
  const char* bp = strchr(wp, ':');
  if (!bp++) help();

  size_t sets = atoi(std::string(config, wp).c_str()); // number of sets
  size_t ways = atoi(std::string(wp, bp).c_str()); // number of ways
  size_t linesz = atoi(bp); // block size

  if (ways > 4 /* empirical */ && sets == 1)
    return new fa_cache_sim_t(ways, linesz, name); // Fully-Associative
  return new cache_sim_t(sets, ways, linesz, name);
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
}

cache_sim_t::~cache_sim_t() // Destructor
{
  print_stats(); // Print a result of cache simulation
  delete [] tags;
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
    if (tag == (tags[idx*ways + i] & ~DIRTY))
      return &tags[idx*ways + i];

  return NULL;
}

uint64_t cache_sim_t::victimize(uint64_t addr) // determine to evict which data from Cache
{
  size_t idx = (addr >> idx_shift) & (sets - 1); // Index used to select the set
  size_t way = lfsr.next() % ways; // lfsr.next(): generate random number

  uint64_t victim = tags[idx*ways + way];
  tags[idx*ways + way] = (addr >> idx_shift) | VALID; // replace Cache

  return victim;
}

void cache_sim_t::access(uint64_t addr, size_t bytes, bool store) // access to addr
{
  store ? write_accesses++ : read_accesses++; // store or load
  (store ? bytes_written : bytes_read) += bytes;

  uint64_t* hit_way = check_tag(addr); // Check Cache Hit or Cache Miss
  if (likely(hit_way != NULL)) // Cache Hit
  {
    if (store) // write
      *hit_way |= DIRTY; // on Dirty bit
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

  if ((victim & (VALID | DIRTY)) == (VALID | DIRTY)) // when exist victim and, victim is both valid and "dirty"
  {
    uint64_t dirty_addr = (victim & ~(VALID | DIRTY)) << idx_shift; // victim is tag, so shift as much as Index
    if (miss_handler) // if the cache is I-cache or D-cache, miss handler is L2 Cache. else NULL
      miss_handler->access(dirty_addr, linesz, true); // access dirty_addr in L2 Cache
    writebacks++;
  }

  if (miss_handler)
    miss_handler->access(addr & ~(linesz-1), linesz, false); // for including policy in L2 Cache

  if (store) // write
    *check_tag(addr) |= DIRTY; // on Dirty bit
}

// Fully Associative Cache
fa_cache_sim_t::fa_cache_sim_t(size_t ways, size_t linesz, const char* name)
  : cache_sim_t(1, ways, linesz, name)
{
}

uint64_t *fa_cache_sim_t::check_tag(uint64_t addr) // Check Cache Hit or Cache Miss
{
  auto it = tags.find(addr >> idx_shift);
  return it == tags.end() ? NULL : &it->second;
}

uint64_t fa_cache_sim_t::victimize(uint64_t addr) // determine to evict which data from Cache
{
  uint64_t old_tag = 0;
  if (tags.size() == ways) // cache is full
  {
    auto it = tags.begin();
    std::advance(it, lfsr.next() % ways); // lfsr.next(): generate random number
    old_tag = it->second;
    tags.erase(it);
  }
  tags[addr >> idx_shift] = (addr >> idx_shift) | VALID; // replace Cache
  return old_tag;
}
