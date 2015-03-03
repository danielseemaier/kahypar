/***************************************************************************
 *  Copyright (C) 2014 Sebastian Schlag <sebastian.schlag@kit.edu>
 **************************************************************************/

#ifndef SRC_PARTITION_COARSENING_ICOARSENER_H_
#define SRC_PARTITION_COARSENING_ICOARSENER_H_

#include <string>

#include "lib/definitions.h"
#include "lib/macros.h"
#include "lib/utils/Stats.h"

using defs::HypernodeID;
using utils::Stats;

namespace partition {
class IRefiner;

class ICoarsener {
  public:
  ICoarsener(const ICoarsener&) = delete;
  ICoarsener(ICoarsener&&) = delete;
  ICoarsener& operator = (const ICoarsener&) = delete;
  ICoarsener& operator = (ICoarsener&&) = delete;

  void coarsen(const HypernodeID limit) {
    coarsenImpl(limit);
  }

  bool uncoarsen(IRefiner& refiner) {
    return uncoarsenImpl(refiner);
  }

  std::string policyString() const {
    return policyStringImpl();
  }

  const Stats & stats() const {
    return statsImpl();
  }

  virtual ~ICoarsener() { }

  protected:
  ICoarsener() { }

  private:
  virtual void coarsenImpl(const HypernodeID limit) = 0;
  virtual bool uncoarsenImpl(IRefiner& refiner) = 0;
  virtual std::string policyStringImpl() const = 0;
  virtual const Stats & statsImpl() const = 0;
};
} // namespace partition

#endif  // SRC_PARTITION_COARSENING_ICOARSENER_H_
