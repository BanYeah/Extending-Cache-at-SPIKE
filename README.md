# Spike RISC-V ISA Simulator assignment
## About
Assignments from the Computer Architecture course, Fall 2024.    

Spike, the RISC-V ISA Simulator, implements a functional model of one or more RISC-V harts.    
https://github.com/SKKU-ESLAB/Spike_Assignment
<br>
<br>

## Features Implemented  

### 🔁 LRU Cache Replacement Policy  
- Replaced the original **random (LFSR)** cache replacement policy with **Least Recently Used (LRU)**.  
- LRU is now selectable via command-line for each cache:
  ```
  ./build/spike --ic=8:8:8:lru --dc=8:8:8:lru --l2=16:16:16:lru ./pk/pk bench/test
  ```
<br>

### ✍️ Write Policy + Allocation Policy  
- Implemented support for both:
  - **Write-Back (wb)** / **Write-Through (wt)**
  - **Write-Allocate (wa)** / **No-Write-Allocate (nwa)**  
- Policies are now configurable using the following flags:
  ```
  --wp=[wb|wt]    // write policy
  --ap=[wa|nwa]   // allocation policy
  ```
<br>

### 🧪 Performance Evaluation  
#### Replacement Policy Comparison (Random vs. LRU)
- **I-Cache**: Both policies show very low miss rates due to high temporal/spatial locality. LRU performs slightly better at smaller cache sizes.
- **D-Cache**: LRU consistently outperforms Random, especially at smaller sizes, by retaining recently used data more effectively.
- **L2-Cache**: LRU performs better at small-to-medium cache sizes, but its aggressive reuse strategy results in increased miss rates at large cache sizes due to cascading effects from L1 hit reduction.
<br>

#### Write & Allocation Policy Comparison  
- **Write-Back + Write-Allocate (WB/WA)**: Balanced approach but suffers high miss rates at large cache sizes due to delayed memory writes.
- **Write-Back + No-Write-Allocate (WB/NWA)**: Worst miss rate, since missed blocks are never loaded into cache and updates are delayed.
- **Write-Through + Write-Allocate (WT/WA)**: Best overall performance. Every write hits memory immediately, improving L2 accuracy and reducing miss rate.
- **Write-Through + No-Write-Allocate (WT/NWA)**: Performs decently but suffers from increased write misses since missed blocks are not loaded.
<br>

#### 📊 L2 miss rate (test2 benchmark, 1024B–16384B L1):
```
Policy     | 1024B  | 2048B  | 4096B  | 8192B  | 16384B
-----------|--------|--------|--------|--------|---------
WB/WA      | 3.16%  | 4.13%  | 45.93% | 60.09% | 93.31%
WB/NWA     | 22.73% | 28.29% | 96.69% | 98.54% | 99.72%
WT/WA      | 0.11%  | 0.11%  | 0.10%  | 0.10%  | 0.10%
WT/NWA     | 0.92%  | 0.92%  | 0.93%  | 0.93%  | 0.93%
```

> ✅ Insight: **WT/WA** provides the most consistent and lowest miss rates across configurations, at the cost of higher memory traffic.
<br>
<br>

## Usage Example  
```bash
./build/spike \
  --ic=8:8:8:lru \
  --dc=8:8:8:lru \
  --l2=16:16:16:lru \
  --wp=wt --ap=nwa \
  ./pk/pk bench/test
```

> @SKKU (Sungkyunkwan University)
