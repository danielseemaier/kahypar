/***************************************************************************
 *  Copyright (C) 2015 Tobias Heuer <tobias.heuer@gmx.net>
 **************************************************************************/

#pragma once

#include <algorithm>
#include <queue>
#include <vector>

#include "lib/datastructure/FastResetBitVector.h"
#include "lib/definitions.h"
#include "lib/utils/RandomFunctions.h"

using defs::HypernodeID;
using defs::HyperedgeID;
using defs::PartitionID;
using defs::Hypergraph;

namespace partition {
template <bool UseRandomStartHypernode = true>
struct BFSStartNodeSelectionPolicy {
  static inline void calculateStartNodes(std::vector<HypernodeID>& start_nodes, const Configuration& config,
                                         const Hypergraph& hg, const PartitionID k) noexcept {
    HypernodeID start_hn = 0;
    if (UseRandomStartHypernode) {
      start_hn = Randomize::getRandomInt(0, hg.initialNumNodes() - 1);
    }
    start_nodes.push_back(start_hn);
    FastResetBitVector<> in_queue(hg.initialNumNodes());
    FastResetBitVector<> hyperedge_in_queue(hg.initialNumEdges());

    while (start_nodes.size() != static_cast<size_t>(k)) {
      std::queue<HypernodeID> bfs;
      HypernodeID lastHypernode = -1;
      size_t visited_nodes = 0;
      for (const HypernodeID start_node : start_nodes) {
        bfs.push(start_node);
        in_queue.set(start_node, true);
      }


      while (!bfs.empty()) {
        const HypernodeID hn = bfs.front();
        bfs.pop();
        lastHypernode = hn;
        visited_nodes++;
        for (const HyperedgeID he : hg.incidentEdges(lastHypernode)) {
          if (!hyperedge_in_queue[he]) {
            if (hg.edgeSize(he) <= config.partition.hyperedge_size_threshold) {
              for (const HypernodeID pin : hg.pins(he)) {
                if (!in_queue[pin]) {
                  bfs.push(pin);
                  in_queue.set(pin, true);
                }
              }
            }
            hyperedge_in_queue.set(he, true);
          }
        }
        if (bfs.empty() && visited_nodes != hg.initialNumNodes()) {
          for (const HypernodeID hn : hg.nodes()) {
            if (!in_queue[hn]) {
              bfs.push(hn);
              in_queue.set(hn, true);
            }
          }
        }
      }
      start_nodes.push_back(lastHypernode);
      in_queue.reset();
      hyperedge_in_queue.reset();
    }
  }
};

struct RandomStartNodeSelectionPolicy {
  static inline void calculateStartNodes(std::vector<HypernodeID>& startNodes, const Configuration& UNUSED(config),
                                         const Hypergraph& hg, const PartitionID k) noexcept {
    if (k == 2) {
      startNodes.push_back(Randomize::getRandomInt(0, hg.initialNumNodes() - 1));
      return;
    }

    for (PartitionID i = 0; i < k; ++i) {
      while (true) {
        HypernodeID hn = Randomize::getRandomInt(0, hg.initialNumNodes() - 1);
        auto node = std::find(startNodes.begin(), startNodes.end(), hn);
        if (node == startNodes.end()) {
          startNodes.push_back(hn);
          break;
        }
      }
    }
  }
};
}  // namespace partition
