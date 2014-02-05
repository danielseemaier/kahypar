/***************************************************************************
 *  Copyright (C) 2014 Sebastian Schlag <sebastian.schlag@kit.edu>
 **************************************************************************/

#include "gmock/gmock.h"

#include "lib/datastructure/Hypergraph.h"
#include "partition/coarsening/Rater.h"

using::testing::Test;
using::testing::Eq;
using::testing::DoubleEq;
using::testing::AnyOf;

using datastructure::HypergraphType;
using datastructure::HypernodeWeight;
using datastructure::HyperedgeIndexVector;
using datastructure::HyperedgeVector;
using datastructure::HypernodeID;

namespace partition {
typedef Rater<HypergraphType, defs::RatingType, FirstRatingWins> FirstWinsRater;
typedef Rater<HypergraphType, defs::RatingType, LastRatingWins> LastWinsRater;
typedef Rater<HypergraphType, defs::RatingType, RandomRatingWins> RandomWinsRater;

class ARater : public Test {
  public:
  explicit ARater(HypergraphType* graph = nullptr) :
    hypergraph(graph),
    config() {
    config.coarsening.threshold_node_weight = 2;
  }

  std::unique_ptr<HypergraphType> hypergraph;
  Configuration<HypergraphType> config;

  private:
  DISALLOW_COPY_AND_ASSIGN(ARater);
};

class AFirstWinsRater : public ARater {
  public:
  AFirstWinsRater() :
    ARater(new HypergraphType(7, 4, HyperedgeIndexVector { 0, 2, 6, 9, /*sentinel*/ 12 },
                              HyperedgeVector { 0, 2, 0, 1, 3, 4, 3, 4, 6, 2, 5, 6 })),
    rater(*hypergraph, config) { }

  FirstWinsRater rater;
};

class ALastWinsRater : public ARater {
  public:
  ALastWinsRater() :
    ARater(new HypergraphType(7, 4, HyperedgeIndexVector { 0, 2, 6, 9, /*sentinel*/ 12 },
                              HyperedgeVector { 0, 2, 0, 1, 3, 4, 3, 4, 6, 2, 5, 6 })),
    rater(*hypergraph, config) { }

  LastWinsRater rater;
};

class ARandomWinsRater : public ARater {
  public:
  ARandomWinsRater() :
    ARater(new HypergraphType(7, 4, HyperedgeIndexVector { 0, 2, 6, 9, /*sentinel*/ 12 },
                              HyperedgeVector { 0, 2, 0, 1, 3, 4, 3, 4, 6, 2, 5, 6 })),
    rater(*hypergraph, config) { }

  RandomWinsRater rater;
};

TEST_F(AFirstWinsRater, UsesHeavyEdgeRatingToRateHypernodes) {
  ASSERT_THAT(rater.rate(0).value, Eq(1));
  ASSERT_THAT(rater.rate(0).target, Eq(2));
  ASSERT_THAT(rater.rate(3).value, DoubleEq(5.0 / 6));
  ASSERT_THAT(rater.rate(3).target, Eq(4));
}

TEST_F(AFirstWinsRater, UsesFirstRatingEntryOfEqualRatings) {
  ASSERT_THAT(rater.rate(6).value, DoubleEq(0.5));
  ASSERT_THAT(rater.rate(6).target, Eq(5));
  ASSERT_THAT(rater.rate(5).value, DoubleEq(0.5));
  ASSERT_THAT(rater.rate(5).target, Eq(6));
}

TEST_F(ALastWinsRater, UsesLastRatingEntryOfEqualRatings) {
  ASSERT_THAT(rater.rate(6).value, DoubleEq(0.5));
  ASSERT_THAT(rater.rate(6).target, Eq(3));
  ASSERT_THAT(rater.rate(5).value, DoubleEq(0.5));
  ASSERT_THAT(rater.rate(5).target, Eq(2));
}


TEST_F(ARandomWinsRater, UsesRandomRatingEntryOfEqualRatings) {
  ASSERT_THAT(rater.rate(6).value, DoubleEq(0.5));
  ASSERT_THAT(rater.rate(6).target, AnyOf(5, 2, 4, 3));
  ASSERT_THAT(rater.rate(5).value, DoubleEq(0.5));
  ASSERT_THAT(rater.rate(5).target, AnyOf(2, 6));
}

TEST_F(AFirstWinsRater, DoesNotRateNodePairsViolatingThresholdNodeWeight) {
  ASSERT_THAT(rater.rate(0).target, Eq(2));
  ASSERT_THAT(rater.rate(0).value, Eq(1));
  ASSERT_THAT(rater.rate(0).valid, Eq(true));

  hypergraph->contract(0, 2);

  ASSERT_THAT(rater.rate(0).target, Eq(std::numeric_limits<HypernodeID>::max()));
  ASSERT_THAT(rater.rate(0).value, Eq(std::numeric_limits<defs::RatingType>::min()));
  ASSERT_THAT(rater.rate(0).valid, Eq(false));
}

TEST_F(ARater, DoesNotEvaluateHyperedgesLargerThanPredefinedThreshold) {
  config.rating.hyperedge_size_threshold = 4;
  hypergraph.reset(new HypergraphType(6, 2, HyperedgeIndexVector { 0, 2, 7 },
                                      HyperedgeVector { 0, 1, 1, 2, 3, 4, 5 }));
  FirstWinsRater rater(*hypergraph, config);

  ASSERT_THAT(rater.rate(2).valid, Eq(false));
  ASSERT_THAT(rater.rate(3).valid, Eq(false));
  ASSERT_THAT(rater.rate(4).valid, Eq(false));
  ASSERT_THAT(rater.rate(5).valid, Eq(false));
}

TEST_F(ARater, EvaluatesHyperedgesSmallerOrEqualThanPredefinedThreshold) {
  config.rating.hyperedge_size_threshold = 5;
  hypergraph.reset(new HypergraphType(7, 4, HyperedgeIndexVector { 0, 2, 6, 9, /*sentinel*/ 12 },
                                      HyperedgeVector { 0, 2, 0, 1, 3, 4, 3, 4, 6, 2, 5, 6 }));
  ASSERT_THAT(true, Eq(false));
}
} // namespace partition
