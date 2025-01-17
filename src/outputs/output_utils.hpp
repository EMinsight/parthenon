//========================================================================================
// Parthenon performance portable AMR framework
// Copyright(C) 2023 The Parthenon collaboration
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
// (C) (or copyright) 2020-2024. Triad National Security, LLC. All rights reserved.
//
// This program was produced under U.S. Government contract 89233218CNA000001 for Los
// Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
// for the U.S. Department of Energy/National Nuclear Security Administration. All rights
// in the program are reserved by Triad National Security, LLC, and the U.S. Department
// of Energy/National Nuclear Security Administration. The Government is granted for
// itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
// license in this material to reproduce, prepare derivative works, distribute copies to
// the public, perform publicly and display publicly, and to permit others to do so.
//========================================================================================

#ifndef OUTPUTS_OUTPUT_UTILS_HPP_
#define OUTPUTS_OUTPUT_UTILS_HPP_

// C++
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// Parthenon
#include "basic_types.hpp"
#include "interface/metadata.hpp"
#include "interface/variable.hpp"
#include "kokkos_abstraction.hpp"
#include "mesh/domain.hpp"
#include "mesh/mesh.hpp"
#include "mesh/meshblock.hpp"
#include "utils/error_checking.hpp"

namespace parthenon {
namespace OutputUtils {
// Helper struct containing some information about a variable
struct VarInfo {
 public:
  static constexpr int VNDIM = MAX_VARIABLE_DIMENSION;
  std::string label;
  int num_components;
  int tensor_rank; // 0- to 3-D for cell-centered variables, 0- to 6-D for arbitrary shape
                   // variables
  MetadataFlag where;
  bool is_sparse;
  bool is_vector;
  bool is_coordinate_field;
  IndexShape cellbounds;
  std::vector<std::string> component_labels;
  // list of topological elements in variable... e.g., Face1, Face2, etc
  std::vector<TopologicalElement> topological_elements;
  // how many topological elements are stored in variable, e.g., 3 for
  // face and edge vars.
  int ntop_elems;
  // whether or not topological element matters.
  bool element_matters;

  Triple_t<int> GetNumKJI(const IndexDomain domain) const;
  Triple_t<IndexRange> GetPaddedBoundsKJI(const IndexDomain domain) const;

  int Size() const;
  // Includes topological element shape
  int TensorSize() const;
  // Size of region that needs to be filled with 0s if not allocated
  int FillSize(const IndexDomain domain) const;
  // number of elements of data that describe variable shape
  int GetNDim() const;

  template <typename T>
  int FillShape(const IndexDomain domain, T *data) const {
    if (where == MetadataFlag({Metadata::None})) {
      for (int i = 0; i < tensor_rank; ++i) {
        data[i] = static_cast<T>(rnx_[rnx_.size() - tensor_rank + i]);
      }
    } else {
      // For nx1,nx2,nx3 find max storage required in each direction
      // accross topological elements. Unused indices will be written but
      // empty.
      auto [nx3, nx2, nx1] = GetNumKJI(domain);
      // fill topological element, if relevant
      if (element_matters) {
        data[0] = ntop_elems;
      }
      // fill the tensor rank
      for (int i = 0; i < tensor_rank; ++i) {
        data[i + element_matters] =
            static_cast<T>(rnx_[rnx_.size() - 3 - tensor_rank + i]);
      }
      // fill cell indices
      data[tensor_rank + element_matters] = static_cast<T>(nx3);
      data[tensor_rank + element_matters + 1] = static_cast<T>(nx2);
      data[tensor_rank + element_matters + 2] = static_cast<T>(nx1);
    }
    return GetNDim();
  }

  template <typename T, typename... Args>
  int FillShape(const IndexDomain domain, T *head, Args... args) const {
    int ndim_head = FillShape(domain, head);
    int ndim_tail = FillShape(domain, std::forward<Args>(args)...);
    // this check should be impossible to trigger but... just to be safe
    PARTHENON_DEBUG_REQUIRE(ndim_head == ndim_tail,
                            "Shape can't change for different arrays");
    return ndim_tail;
  }

  // Returns full shape as read to/written from I/O, with 1-padding.
  std::vector<int> GetPaddedShape(IndexDomain domain) const;
  std::vector<int> GetPaddedShapeReversed(IndexDomain domain) const;
  // nx accessors
  std::vector<int> GetRawShape() const;
  int GetDim(int i) const;

  VarInfo() = delete;

  // TODO(JMM): Separate this into an implementation file again?
  VarInfo(const std::string &label, const std::vector<std::string> &component_labels_,
          int num_components, std::array<int, VNDIM> nx, Metadata metadata,
          const std::vector<TopologicalElement> topological_elements, bool is_sparse,
          bool is_vector, const IndexShape &cellbounds)
      : label(label), num_components(num_components), nx_(nx),
        tensor_rank(metadata.Shape().size()), where(metadata.Where()),
        topological_elements(topological_elements), is_sparse(is_sparse),
        is_vector(is_vector), cellbounds(cellbounds), rnx_(nx_.rbegin(), nx_.rend()),
        ntop_elems(topological_elements.size()), element_matters(ntop_elems > 1),
        is_coordinate_field(metadata.IsCoordinateField()) {
    if (num_components <= 0) {
      std::stringstream msg;
      msg << "### ERROR: Got variable " << label << " with " << num_components
          << " components."
          << " num_components must be greater than 0" << std::endl;
      PARTHENON_FAIL(msg);
    }

    // Full components labels will be composed according to the following rules:
    // If there just one component (e.g., a scalar var or a vector/tensor with a single
    // component) only the basename and no suffix is used unless a component label is
    // provided (which will then be added as suffix following an `_`). For variables with
    // >1 components, the final component label will be composed of the basename and a
    // suffix. This suffix is either a integer if no component labels are given, or the
    // component label itself.
    component_labels = {};
    if (num_components == 1) {
      const auto suffix = component_labels_.empty() ? "" : "_" + component_labels_[0];
      component_labels = std::vector<std::string>({label + suffix});
    } else if (component_labels_.size() == num_components) {
      for (int i = 0; i < num_components; i++) {
        component_labels.push_back(label + "_" + component_labels_[i]);
      }
    } else {
      for (int i = 0; i < num_components; i++) {
        component_labels.push_back(label + "_" + std::to_string(i));
      }
    }
  }

  explicit VarInfo(const std::shared_ptr<Variable<Real>> &var,
                   const IndexShape &cellbounds)
      : VarInfo(var->label(), var->metadata().getComponentLabels(), var->NumComponents(),
                var->GetDim(), var->metadata(), var->GetTopologicalElements(),
                var->IsSparse(), var->IsSet(Metadata::Vector), cellbounds) {}

  static std::vector<VarInfo> GetAll(const VariableVector<Real> &vars,
                                     const IndexShape &cellbounds,
                                     const IndexShape &f_cellbounds);

  bool operator==(const std::string &other) const { return other == label; }

 private:
  // TODO(JMM): Probably nx_ and rnx_ both not necessary... but it was
  // easiest for me to reason about it this way.
  std::array<int, VNDIM> nx_;
  std::vector<int> rnx_;
};

struct SwarmVarInfo {
  std::array<int, 5> n;
  int nvar, tensor_rank;
  bool vector;
  std::string swtype; // string for xdmf. "Int" or "Float"
  SwarmVarInfo() = default;
  SwarmVarInfo(int n6, int n5, int n4, int n3, int n2, int rank,
               const std::string &swtype, bool vector)
      : n({n2, n3, n4, n5, n6}), nvar(n6 * n5 * n4 * n3 * n2), tensor_rank(rank),
        swtype(swtype), vector((tensor_rank == 1) && (nvar == 3) && vector) {}
  int GetN(int d) const {
    PARTHENON_DEBUG_REQUIRE_THROWS(1 < d && d <= 6, "allowed dim");
    return n[d - 2];
  }
};
// Contains information about a particle swarm spanning
// meshblocks. Everything needed for output
struct SwarmInfo {
  SwarmInfo() = default;
  template <typename T>
  using MapToVarVec = std::map<std::string, ParticleVariableVector<T>>;
  std::tuple<MapToVarVec<int>, MapToVarVec<Real>> vars; // SwarmVars on each meshblock
  std::map<std::string, SwarmVarInfo> var_info;         // size of each swarmvar
  std::size_t count_on_rank = 0;                        // per-meshblock
  std::size_t global_offset;                            // global
  std::size_t global_count;                             // global
  std::vector<std::size_t> counts;                      // per-meshblock
  std::vector<std::size_t> offsets;                     // global
  // std::vector<ParArray1D<bool>> masks; // used for reading swarms without defrag
  std::vector<std::size_t> max_indices;   // JMM: If we defrag, unneeded?
  void AddOffsets(const SP_Swarm &swarm); // sets above metadata
  template <typename T>
  MapToVarVec<T> &Vars() {
    return std::get<MapToVarVec<T>>(vars);
  }
  template <typename T>
  void Add(const std::string &varname, const ParticleVarPtr<T> &var) {
    Vars<T>()[varname].push_back(var);
    auto m = var->metadata();
    bool vector = m.IsSet(Metadata::Vector);
    auto shape = m.Shape();
    int rank = shape.size();
    std::string t = std::is_same<T, int>::value ? "Int" : "Float";
    var_info[varname] = SwarmVarInfo(var->GetDim(6), var->GetDim(5), var->GetDim(4),
                                     var->GetDim(3), var->GetDim(2), rank, t, vector);
  }
  // Copies swarmvar to host in prep for output
  template <typename T>
  std::vector<T> FillHostBuffer(const std::string vname,
                                ParticleVariableVector<T> &swmvarvec) {
    const auto &vinfo = var_info.at(vname);
    std::vector<T> host_data(count_on_rank * vinfo.nvar);
    std::size_t ivec = 0;
    for (int n6 = 0; n6 < vinfo.GetN(6); ++n6) {
      for (int n5 = 0; n5 < vinfo.GetN(5); ++n5) {
        for (int n4 = 0; n4 < vinfo.GetN(4); ++n4) {
          for (int n3 = 0; n3 < vinfo.GetN(3); ++n3) {
            for (int n2 = 0; n2 < vinfo.GetN(2); ++n2) {
              std::size_t block_idx = 0;
              for (auto &swmvar : swmvarvec) {
                // Copied extra times. JMM: If we defrag, unneeded?
                // auto mask_h = masks[block_idx].GetHostMirrorAndCopy();
                // Prevents us from having to copy extra data for swarm vars
                // with multiple components
                auto v_h = swmvar->GetHostMirrorAndCopy(n6, n5, n4, n3, n2);
                // DO NOT use GetDim, as it does not reflect particle count
                std::size_t particles_to_add = counts[block_idx];
                std::size_t particles_added = 0;
                for (std::size_t i = 0; i < particles_to_add; ++i) {
                  if (true) { //(mask_h(i)) {
                    host_data[ivec++] = v_h(i);
                    particles_added++;
                  }
                }
                if (particles_added != particles_to_add) {
                  std::string msg = StringPrintf(
                      "Not all active particles output! "
                      "var, n6, n5, n4, n3, n2, block, particles_added, counts = "
                      "%s %d %d %d %d %d %d %d %d",
                      vname.c_str(), n6, n5, n4, n3, n2, block_idx, particles_added,
                      particles_to_add);
                  PARTHENON_THROW(msg);
                }
                ++block_idx;
              }
            }
          }
        }
      }
    }
    return host_data; // move semantics
  }
};
struct AllSwarmInfo {
  std::map<std::string, SwarmInfo> all_info;
  AllSwarmInfo(BlockList_t &block_list,
               const std::map<std::string, std::set<std::string>> &swarmnames,
               bool is_restart);
};

template <typename T, typename Function_t>
std::vector<T> FlattenBlockInfo(Mesh *pm, int shape, Function_t f) {
  const int num_blocks_local = static_cast<int>(pm->block_list.size());
  std::vector<T> data(shape * num_blocks_local);
  int i = 0;
  for (auto &pmb : pm->block_list) {
    f(pmb.get(), data, i);
  }
  return data;
}

// mirror must be provided because copying done externally
template <typename idx_t, typename Function_t>
void PackOrUnpackVar(const VarInfo &info, bool do_ghosts, idx_t &idx, Function_t f) {
  const IndexDomain domain = (do_ghosts ? IndexDomain::entire : IndexDomain::interior);
  // shape as written to or read from. contains additional padding
  // in orthogonal directions.
  // e.g., Face1-centered var is shape (N1+1)x(N2+1)x(N3+1)
  // format is
  // topological_elems x tensor_elems x block_elems
  const auto shape = info.GetPaddedShapeReversed(domain);
  // TODO(JMM): Should I hide this inside VarInfo?
  auto [kb, jb, ib] = info.GetPaddedBoundsKJI(domain);
  if (info.where == MetadataFlag({Metadata::None})) {
    kb.s = 0;
    kb.e = shape[4];
    jb.s = 0;
    jb.e = shape[5];
    ib.s = 0;
    ib.e = shape[6];
  }
  for (int topo = 0; topo < shape[0]; ++topo) {
    for (int t = 0; t < shape[1]; ++t) {
      for (int u = 0; u < shape[2]; ++u) {
        for (int v = 0; v < shape[3]; ++v) {
          for (int k = kb.s; k <= kb.e; ++k) {
            for (int j = jb.s; j <= jb.e; ++j) {
              for (int i = ib.s; i <= ib.e; ++i) {
                f(idx, topo, t, u, v, k, j, i);
                idx++;
              }
            }
          }
        }
      }
    }
  }
}

void ComputeCoords(Mesh *pm, bool face, const IndexRange &ib, const IndexRange &jb,
                   const IndexRange &kb, std::vector<Real> &x, std::vector<Real> &y,
                   std::vector<Real> &z);
std::vector<Real> ComputeXminBlocks(Mesh *pm);
std::vector<int64_t> ComputeLocs(Mesh *pm);
std::vector<int> ComputeIDsAndFlags(Mesh *pm);
std::vector<int> ComputeDerefinementCount(Mesh *pm);

// TODO(JMM): Potentially unsafe if MPI_UNSIGNED_LONG_LONG isn't a size_t
// however I think it's probably safe to assume we'll be on systems
// where this is the case?
// TODO(JMM): If we ever need non-int need to generalize
std::size_t MPIPrefixSum(std::size_t local, std::size_t &tot_count);
std::size_t MPISum(std::size_t local);

} // namespace OutputUtils
} // namespace parthenon

#endif // OUTPUTS_OUTPUT_UTILS_HPP_
