/***************************************************************************
 *  Copyright (C) 2016 Sebastian Schlag <sebastian.schlag@kit.edu>
 **************************************************************************/

#pragma once

#include <limits>
#include <memory>
#include <vector>

#include "meta/mandatory.h"

namespace partition {
template <typename T = Mandatory>
class TwoWayFMGainCache {
 private:
  static constexpr T kNotCached = std::numeric_limits<T>::max();

  struct CacheElement {
    T value;
    T delta;

    CacheElement() :
      value(kNotCached),
      delta(0) { }

    CacheElement(const CacheElement&) = delete;
    CacheElement& operator= (const CacheElement&) = delete;

    CacheElement(CacheElement&&) = default;
    CacheElement& operator= (CacheElement&&) = default;
  };

 public:
  explicit TwoWayFMGainCache(const size_t size) :
    _size(size),
    _cache(std::make_unique<CacheElement[]>(size)),
    _used_delta_entries() {
    _used_delta_entries.reserve(size);
  }

  TwoWayFMGainCache(const TwoWayFMGainCache&) = delete;
  TwoWayFMGainCache& operator= (const TwoWayFMGainCache&) = delete;

  TwoWayFMGainCache(TwoWayFMGainCache&&) = default;
  TwoWayFMGainCache& operator= (TwoWayFMGainCache&&) = default;

  T delta(const size_t index) const {
    ASSERT(index < _size);
    return _cache[index].delta;
  }

  T value(const size_t index) const {
    ASSERT(index < _size);
    return _cache[index].value;
  }

  size_t size() const {
    return _size;
  }

  void setDelta(const size_t index, const T value) {
    ASSERT(index < _size);
    if (_cache[index].delta == 0) {
      _used_delta_entries.push_back(index);
    }
    _cache[index].delta = value;
  }

  void setNotCached(const size_t index) {
    ASSERT(index < _size);
    _cache[index].value = kNotCached;
  }

  bool isCached(const size_t index) {
    ASSERT(index < _size);
    return _cache[index].value != kNotCached;
  }

  void setValue(const size_t index, const T value) {
    ASSERT(index < _size);
    _cache[index].value = value;
  }

  void updateValue(const size_t index, const T value) {
    ASSERT(index < _size);
    _cache[index].value += value;
  }

  void uncheckedSetDelta(const size_t index, const T value) {
    ASSERT(index < _size);
    ASSERT(_cache[index].delta != 0,
           "Index " << index << " is still unused and not tracked for reset!");
    _cache[index].delta = value;
  }

  void updateCacheAndDelta(const size_t index, const T delta) {
    ASSERT(index < _size);
    if (_cache[index].delta == 0) {
      _used_delta_entries.push_back(index);
    }
    _cache[index].value += delta;
    _cache[index].delta -= delta;
  }

  template <typename use_pqs, class PQ, class Hypergraph>
  void rollbackDelta(PQ& rb_pq, Hypergraph& hg) {
    for (auto rit = _used_delta_entries.crbegin(); rit != _used_delta_entries.crend(); ++rit) {
      if (_cache[*rit].value != kNotCached) {
        _cache[*rit].value += _cache[*rit].delta;
      }
      if (use_pqs()) {
        rb_pq[1 - hg.partID(*rit)].updateKeyBy(*rit, _cache[*rit].delta);
      }
      _cache[*rit].delta = 0;
    }
    _used_delta_entries.clear();
  }

  void resetDelta() {
    for (const size_t used_entry : _used_delta_entries) {
      _cache[used_entry].delta = 0;
    }
    _used_delta_entries.clear();
  }

  void clear() {
    for (size_t i = 0; i < _size; ++i) {
      _cache[i] = CacheElement();
    }
  }

 private:
  const size_t _size;
  std::unique_ptr<CacheElement[]> _cache;
  std::vector<size_t> _used_delta_entries;
};
}  // namespace partition