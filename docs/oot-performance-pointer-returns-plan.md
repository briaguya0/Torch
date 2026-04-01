# Eliminate YAML::Node copies: pointer returns from gAddrMap

## Context
O2R generation takes ~35s. perf shows ~60% is memory allocation from YAML::Node
shared_ptr ref counting. The cause: `GetNodeByAddr` returns
`optional<tuple<string, YAML::Node>>` by value, copying the YAML::Node out of
`gAddrMap` on every call (~500K+ lookups). `unordered_map` guarantees
pointer/reference stability on insertion, so returning `const T*` into gAddrMap
is safe.

## Type alias
```cpp
using AddrEntry = std::tuple<std::string, YAML::Node>;
```

## Signature changes

| Function | Before | After |
|----------|--------|-------|
| GetNodeByAddr | `optional<tuple<string, YAML::Node>>` | `const AddrEntry*` |
| GetSafeNodeByAddr | `optional<tuple<string, YAML::Node>>` | `const AddrEntry*` |
| GetNodesByType | `optional<vector<tuple<...>>>` | `vector<const AddrEntry*>` |
| GetStringByAddr | `optional<string>` | unchanged (cheap string copy) |
| GetSafeStringByAddr | `optional<string>` | unchanged |
| GetVtxOverlap | `optional<tuple<string, YAML::Node>>` | `const AddrEntry*` |

## Caller migration pattern
```
.has_value()              →  != nullptr
!dec.has_value()          →  dec == nullptr
std::get<0>(dec.value())  →  std::get<0>(*dec)
std::get<1>(dec.value())  →  std::get<1>(*dec)
auto [p, n] = dec.value() →  const auto& [p, n] = *dec
```

## Implementation order

### Step 1: Companion.h
- Add `using AddrEntry = ...` before class
- Change GetNodeByAddr, GetSafeNodeByAddr, GetNodesByType signatures

### Step 2: Companion.cpp
- GetNodeByAddr: `return this->gAddrMap[file][addr]` → `return &this->gAddrMap[file][addr]`, nullopt → nullptr
- GetSafeNodeByAddr: dereference pointer, check type, return pointer through
- GetStringByAddr/GetSafeStringByAddr: internally use pointer, keep returning optional<string>
- GetSymbolFromAddr: pointer dereference
- AddAsset: pointer dereference
- GetNodesByType: return vector of pointers, no optional wrapper

### Step 3: DisplayListOverrides.h/.cpp
- GetVtxOverlap → `const AddrEntry*` (points into mVtxOverlaps)
- RegisterVTXOverlap takes `const AddrEntry&` (copies into mVtxOverlaps — necessary, separate map)
- All STANDALONE override functions: dereference pointers

### Step 4: DisplayListFactory.cpp
- SearchVtx: keeps returning `optional<tuple<...>>` (constructs new tuples, cold path)
- SearchVtx internals: iterate `vector<const AddrEntry*>` from GetNodesByType
- All GetNodeByAddr call sites: mechanical nullptr/dereference migration
- gSunDL GetNodesByType call: iterate pointer vector

### Step 5: SoundFontFactory.cpp (safety-critical)
- 4 sites do `.value()` without null check — add nullptr guard + throw

### Step 6: All remaining factories (mechanical)
- AssetArrayFactory, EADAnimationFactory, EADLimbFactory, PaintingFactory,
  GeoLayoutFactory, BehaviorScriptFactory, LevelScriptFactory, MovtexQuadFactory,
  WaterDropletFactory, SkeletonFactory, MessageLookupFactory, AudioContext,
  DeferredVtx, OoTSceneFactory

## Key decisions
- Raw `const T*` over `optional<reference_wrapper>` — idiomatic, simple, zero overhead
- GetStringByAddr/GetSafeStringByAddr keep `optional<string>` — string copy is cheap, 18 call sites not worth complicating
- SearchVtx keeps optional-of-tuple — constructs new data, can't point into gAddrMap, cold path only
- RegisterVTXOverlap still copies into mVtxOverlaps — separate map needs own storage, rare path

## Files to modify (20 files)
- `src/Companion.h` — signatures + type alias
- `src/Companion.cpp` — implementations
- `src/factories/DisplayListFactory.cpp` — SearchVtx + 7 call sites
- `src/factories/DisplayListOverrides.h` — GetVtxOverlap/RegisterVTXOverlap signatures
- `src/factories/DisplayListOverrides.cpp` — overlap map + STANDALONE overrides
- `src/factories/oot/DeferredVtx.cpp`
- `src/factories/oot/OoTSceneFactory.cpp`
- `src/factories/AssetArrayFactory.cpp`
- `src/factories/fzerox/EADAnimationFactory.cpp`
- `src/factories/fzerox/EADLimbFactory.cpp`
- `src/factories/sm64/PaintingFactory.cpp`
- `src/factories/sm64/GeoLayoutFactory.cpp`
- `src/factories/sm64/BehaviorScriptFactory.cpp`
- `src/factories/sm64/LevelScriptFactory.cpp`
- `src/factories/sm64/MovtexQuadFactory.cpp`
- `src/factories/sm64/WaterDropletFactory.cpp`
- `src/factories/sf64/SkeletonFactory.cpp`
- `src/factories/sf64/MessageLookupFactory.cpp`
- `src/factories/naudio/v1/AudioContext.cpp`
- `src/factories/naudio/v1/SoundFontFactory.cpp`

## Verification
- `cmake --build build -j32`
- `python3 soh/tools/test_assets.py soh/roms/pal_gc_0227d7.z64` → 35,386/35,386
- Compare timing before/after (baseline: ~35s)
