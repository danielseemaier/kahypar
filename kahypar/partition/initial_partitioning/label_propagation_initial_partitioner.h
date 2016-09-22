/***************************************************************************
 *  Copyright (C) 2015 Tobias Heuer <tobias.heuer@gmx.net>
 *  Copyright (C) 2016 Sebastian Schlag <sebastian.schlag@kit.edu>
 **************************************************************************/

#pragma once

#include <algorithm>
#include <limits>
#include <map>
#include <queue>
#include <utility>
#include <vector>

#include "kahypar/datastructure/fast_reset_flag_array.h"
#include "kahypar/datastructure/sparse_map.h"
#include "kahypar/definitions.h"
#include "kahypar/meta/mandatory.h"
#include "kahypar/partition/initial_partitioning/i_initial_partitioner.h"
#include "kahypar/partition/initial_partitioning/initial_partitioner_base.h"
#include "kahypar/partition/initial_partitioning/policies/ip_gain_computation_policy.h"
#include "kahypar/partition/metrics.h"
#include "kahypar/utils/randomize.h"

namespace partition {
template <class StartNodeSelection = Mandatory,
          class GainComputation = Mandatory>
class LabelPropagationInitialPartitioner : public IInitialPartitioner,
                                           private InitialPartitionerBase {
 private:
  using PartitionGainPair = std::pair<const PartitionID, const Gain>;

 public:
  LabelPropagationInitialPartitioner(Hypergraph& hypergraph,
                                     Configuration& config) :
    InitialPartitionerBase(hypergraph, config),
    _in_queue(hypergraph.initialNumNodes()),
    _tmp_scores(_config.initial_partitioning.k, 0),
    _unassigned_nodes(),
    _unconnected_nodes(),
    _unassigned_node_bound(0) {
    static_assert(std::is_same<GainComputation, FMGainComputationPolicy>::value,
                  "ScLaP-IP only supports FM gain");
    for (const HypernodeID hn : _hg.nodes()) {
      if (_hg.nodeDegree(hn > 0)) {
        _unassigned_nodes.push_back(hn);
      } else {
        _unconnected_nodes.push_back(hn);
      }
    }
    _unassigned_node_bound = _unassigned_nodes.size();
  }

  ~LabelPropagationInitialPartitioner() { }

  LabelPropagationInitialPartitioner(const LabelPropagationInitialPartitioner&) = delete;
  LabelPropagationInitialPartitioner& operator= (const LabelPropagationInitialPartitioner&) = delete;

  LabelPropagationInitialPartitioner(LabelPropagationInitialPartitioner&&) = delete;
  LabelPropagationInitialPartitioner& operator= (LabelPropagationInitialPartitioner&&) = delete;

  void partitionImpl() override final {
    PartitionID unassigned_part =
      _config.initial_partitioning.unassigned_part;
    _config.initial_partitioning.unassigned_part = -1;
    InitialPartitionerBase::resetPartitioning();
    _unassigned_node_bound = _unassigned_nodes.size();
    std::vector<HypernodeID> nodes;
    for (const HypernodeID hn : _hg.nodes()) {
      if (_hg.nodeDegree(hn) > 0) {
        nodes.push_back(hn);
      }
    }

    int connected_nodes = std::max(std::min(_config.initial_partitioning.lp_assign_vertex_to_part,
                                            static_cast<int>(_hg.initialNumNodes()
                                                             / _config.initial_partitioning.k)), 1);

    std::vector<HypernodeID> startNodes;
    StartNodeSelection::calculateStartNodes(startNodes, _config, _hg,
                                            _config.initial_partitioning.k);

    for (PartitionID i = 0; i < _config.initial_partitioning.k; ++i) {
      assignKConnectedHypernodesToPart(startNodes[i], i, connected_nodes);
    }

    ASSERT(
      [&]() {
        for (PartitionID i = 0; i < _config.initial_partitioning.k; ++i) {
          if (static_cast<int>(_hg.partSize(i)) != connected_nodes) {
            return false;
          }
        }
        return true;
      } (),
      "Size of a partition is not equal " << connected_nodes << "!");

    bool converged = false;
    size_t iterations = 0;

    while (!converged &&
           iterations < static_cast<size_t>(_config.initial_partitioning.lp_max_iteration)) {
      converged = true;

      int unvisited_pos = nodes.size();
      while (unvisited_pos) {
        int pos = std::rand() % unvisited_pos;
        std::swap(nodes[pos], nodes[unvisited_pos - 1]);
        HypernodeID v = nodes[--unvisited_pos];

        std::pair<PartitionID, Gain> max_move = computeMaxGainMove(v);
        PartitionID max_part = max_move.first;

        if (max_part != _hg.partID(v)) {
          ASSERT(
            [&]() {
              for (const HyperedgeID he : _hg.incidentEdges(v)) {
                for (const PartitionID part : _hg.connectivitySet(he)) {
                  if (part == max_part) {
                    return true;
                  }
                }
              }
              return false;
            } (),
            "Partition " << max_part << " is not an incident label of hypernode " << v << "!");

#ifndef NDEBUG
          PartitionID source_part = _hg.partID(v);
#endif
          if (InitialPartitionerBase::assignHypernodeToPartition(v, max_part)) {
            ASSERT(
              [&]() {
                ASSERT(GainComputation::getType() == GainType::fm_gain, "Error");
                if (source_part != -1) {
                  Gain gain = max_move.second;
                  _hg.changeNodePart(v, max_part, source_part);
                  HyperedgeWeight cut_before = metrics::hyperedgeCut(_hg);
                  _hg.changeNodePart(v, source_part, max_part);
                  if (metrics::hyperedgeCut(_hg) != (cut_before - gain)) {
                    LOGVAR(v);
                    LOGVAR(source_part);
                    LOGVAR(max_part);
                    LOGVAR(cut_before);
                    LOGVAR(gain);
                    LOGVAR(metrics::hyperedgeCut(_hg));
                    return false;
                  }
                }
                return true;
              } (),
              "Gain calculation failed of hypernode " << v << " failed from part "
              << source_part << " to " << max_part << "!");

            converged = false;
          }
        }
      }
      ++iterations;
      // If the algorithm is converged but there are still unassigned hypernodes left, we try to choose
      // five additional hypernodes and assign them to the part with minimum weight to continue with
      // Label Propagation.

      if (converged && getUnassignedNode2() != kInvalidNode) {
        for (auto i = 0; i < _config.initial_partitioning.lp_assign_vertex_to_part; ++i) {
          HypernodeID hn = getUnassignedNode2();
          if (hn == kInvalidNode) {
            break;
          }
          assignHypernodeToPartWithMinimumPartWeight(hn);
          converged = false;
        }
      }
    }

    // If there are any unassigned hypernodes left, we assign them to a part with minimum weight.
    while (getUnassignedNode2() != kInvalidNode) {
      HypernodeID hn = getUnassignedNode2();
      assignHypernodeToPartWithMinimumPartWeight(hn);
    }

    // If there are any unconnected hypernodes left, we assign them to a part with minimum weight.
    for (const HypernodeID hn : _unconnected_nodes) {
      if (_hg.partID(hn) == -1) {
        assignHypernodeToPartWithMinimumPartWeight(hn);
      }
    }

    _config.initial_partitioning.unassigned_part = unassigned_part;

    ASSERT([&]() {
        for (HypernodeID hn : _hg.nodes()) {
          if (_hg.partID(hn) == -1) {
            return false;
          }
        }
        return true;
      } (), "There are unassigned hypernodes!");

    _hg.initializeNumCutHyperedges();
    InitialPartitionerBase::performFMRefinement();
  }

 private:
  FRIEND_TEST(ALabelPropagationMaxGainMoveTest,
              AllMaxGainMovesAZeroGainMovesIfNoHypernodeIsAssigned);
  FRIEND_TEST(ALabelPropagationMaxGainMoveTest,
              MaxGainMoveIsAZeroGainMoveIfANetHasOnlyOneAssignedHypernode);
  FRIEND_TEST(ALabelPropagationMaxGainMoveTest,
              CalculateMaxGainMoveIfThereAStillUnassignedNodesOfAHyperedgeAreLeft);
  FRIEND_TEST(ALabelPropagationMaxGainMoveTest,
              CalculateMaxGainMoveOfAnAssignedHypernodeIfAllHypernodesAreAssigned);
  FRIEND_TEST(ALabelPropagationMaxGainMoveTest,
              CalculateMaxGainMoveOfAnUnassignedHypernodeIfAllHypernodesAreAssigned);

  PartitionGainPair computeMaxGainMoveForUnassignedSourcePart(const HypernodeID hn) {
    _tmp_scores.clear();

    HyperedgeWeight internal_weight = 0;
    for (const HyperedgeID he : _hg.incidentEdges(hn)) {
      const HyperedgeWeight he_weight = _hg.edgeWeight(he);
      if (_hg.connectivity(he) == 1) {
        const PartitionID connected_part = *_hg.connectivitySet(he).begin();
        internal_weight += he_weight;
        _tmp_scores[connected_part] += he_weight;
      } else {
        for (const PartitionID target_part : _hg.connectivitySet(he)) {
          _tmp_scores.add(target_part, 0);
        }
      }
    }

    const HypernodeWeight hn_weight = _hg.nodeWeight(hn);
    PartitionID max_part = -1;
    Gain max_score = std::numeric_limits<Gain>::min();
    for (auto& target_part : _tmp_scores) {
      target_part.value -= internal_weight;

      ASSERT([&]() {
          ds::FastResetFlagArray<> bv(_hg.initialNumNodes());
          Gain gain = GainComputation::calculateGain(_hg, hn, target_part.key, bv);
          if (target_part.value != gain) {
            LOGVAR(hn);
            LOGVAR(_hg.partID(hn));
            LOGVAR(target_part.key);
            LOGVAR(target_part.value);
            LOGVAR(gain);
            for (const HyperedgeID he : _hg.incidentEdges(hn)) {
              _hg.printEdgeState(he);
            }
            return false;
          }
          return true;
        } (), "Calculated gain is invalid");


      if (_hg.partWeight(target_part.key) + hn_weight
          <= _config.initial_partitioning.upper_allowed_partition_weight[target_part.key]) {
        if (target_part.value > max_score) {
          max_score = target_part.value;
          max_part = target_part.key;
        }
      }
    }
    return std::make_pair(max_part, max_score);
  }

  PartitionGainPair computeMaxGainMoveForAssignedSourcePart(const HypernodeID hn) {
    const PartitionID source_part = _hg.partID(hn);
    HyperedgeWeight internal_weight = 0;
    _tmp_scores.clear();
    for (const HyperedgeID he : _hg.incidentEdges(hn)) {
      const HypernodeID pins_in_source_part = _hg.pinCountInPart(he, source_part);
      const HyperedgeWeight he_weight = _hg.edgeWeight(he);
      switch (_hg.connectivity(he)) {
        case 1:
          if (pins_in_source_part > 1) {
            ASSERT(source_part == *_hg.connectivitySet(he).begin(), V(source_part));
            internal_weight += he_weight;
          }
          break;
        case 2:
          for (const PartitionID target_part : _hg.connectivitySet(he)) {
            _tmp_scores.add(target_part, 0);
            if (pins_in_source_part == 1 && _hg.pinCountInPart(he, target_part) != 0) {
              _tmp_scores[target_part] += he_weight;
            }
          }
          break;
        default:
          for (const PartitionID target_part : _hg.connectivitySet(he)) {
            _tmp_scores.add(target_part, 0);
          }
          break;
      }
    }

    const HypernodeWeight hn_weight = _hg.nodeWeight(hn);
    PartitionID max_part = source_part;
    Gain max_score = 0;
    for (auto& target_part : _tmp_scores) {
      if (target_part.key == source_part) {
        continue;
      }
      target_part.value -= internal_weight;

      ASSERT([&]() {
          ds::FastResetFlagArray<> bv(_hg.initialNumNodes());
          Gain gain = GainComputation::calculateGain(_hg, hn, target_part.key, bv);
          if (target_part.value != gain) {
            LOGVAR(hn);
            LOGVAR(_hg.partID(hn));
            LOGVAR(target_part.key);
            LOGVAR(target_part.value);
            LOGVAR(gain);
            for (const HyperedgeID he : _hg.incidentEdges(hn)) {
              _hg.printEdgeState(he);
            }
            return false;
          }
          return true;
        } (), "Calculated gain is invalid");

      if (_hg.partWeight(target_part.key) + hn_weight
          <= _config.initial_partitioning.upper_allowed_partition_weight[target_part.key]) {
        if (target_part.value > max_score) {
          max_score = target_part.value;
          max_part = target_part.key;
        }
      }
    }

    return std::make_pair(max_part, max_score);
  }

  PartitionGainPair computeMaxGainMove(const HypernodeID hn) {
    if (_hg.partID(hn) == -1) {
      return computeMaxGainMoveForUnassignedSourcePart(hn);
    }
    return computeMaxGainMoveForAssignedSourcePart(hn);
  }


  void assignKConnectedHypernodesToPart(const HypernodeID hn,
                                        const PartitionID p, const int k =
                                          std::numeric_limits<int>::max()) {
    std::queue<HypernodeID> _bfs_queue;
    int assigned_nodes = 0;
    _bfs_queue.push(hn);
    _in_queue.set(hn, true);
    while (!_bfs_queue.empty()) {
      HypernodeID node = _bfs_queue.front();
      _bfs_queue.pop();
      if (_hg.partID(node) == -1) {
        _hg.setNodePart(node, p);
        ++assigned_nodes;
        for (const HyperedgeID he : _hg.incidentEdges(node)) {
          if (_hg.edgeSize(he) <= _config.partition.hyperedge_size_threshold) {
            for (const HypernodeID pin : _hg.pins(he)) {
              if (_hg.partID(pin) == -1 && !_in_queue[pin]) {
                _bfs_queue.push(pin);
                _in_queue.set(pin, true);
              }
            }
          }
        }
      }
      if (assigned_nodes == k) {
        break;
      } else if (_bfs_queue.empty()) {
        const HypernodeID unassigned = getUnassignedNode2();
        if (unassigned == kInvalidNode) {
          break;
        } else {
          _bfs_queue.push(unassigned);
        }
      }
    }
    _in_queue.reset();
  }

  void assignHypernodeToPartWithMinimumPartWeight(const HypernodeID hn) {
    PartitionID p = kInvalidPart;
    HypernodeWeight min_part_weight = std::numeric_limits<HypernodeWeight>::max();
    for (PartitionID i = 0; i < _config.initial_partitioning.k; ++i) {
      p = (_hg.partWeight(i) < min_part_weight ? i : p);
      min_part_weight = std::min(_hg.partWeight(i), min_part_weight);
    }
    ASSERT(_hg.partID(hn) == -1, "Hypernode " << hn << " is already assigned to a part!");
    _hg.setNodePart(hn, p);
  }

  HypernodeID getUnassignedNode2() {
    HypernodeID unassigned_node = kInvalidNode;
    for (size_t i = 0; i < _unassigned_node_bound; ++i) {
      HypernodeID hn = _unassigned_nodes[i];
      if (_hg.partID(hn) == _config.initial_partitioning.unassigned_part) {
        unassigned_node = hn;
        break;
      } else {
        std::swap(_unassigned_nodes[i--], _unassigned_nodes[--_unassigned_node_bound]);
      }
    }
    return unassigned_node;
  }

  using InitialPartitionerBase::_hg;
  using InitialPartitionerBase::_config;
  using InitialPartitionerBase::kInvalidNode;
  using InitialPartitionerBase::kInvalidPart;
  ds::FastResetFlagArray<> _in_queue;
  ds::InsertOnlySparseMap<PartitionID, Gain> _tmp_scores;
  std::vector<HypernodeID> _unassigned_nodes;
  std::vector<HypernodeID> _unconnected_nodes;
  unsigned int _unassigned_node_bound;
};
}  // namespace partition
