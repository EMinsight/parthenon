//========================================================================================
// Parthenon performance portable AMR framework
// Copyright(C) 2020-2024 The Parthenon collaboration
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
#ifndef OUTPUTS_PARTHENON_HDF5_TYPES_HPP_
#define OUTPUTS_PARTHENON_HDF5_TYPES_HPP_

#include "config.hpp"

#ifdef ENABLE_HDF5

// Definitions common to parthenon restart and parthenon output for HDF5

#include <hdf5.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "kokkos_abstraction.hpp"
#include "utils/error_checking.hpp"

namespace parthenon {
namespace HDF5 {

// Number of dimension of HDF5 field data sets (block x nv x nu x nt x nz x ny x nx)
static constexpr size_t H5_NDIM = MAX_VARIABLE_DIMENSION + 1;

static constexpr int OUTPUT_VERSION_FORMAT = 4;

/**
 * @brief RAII handles for HDF5. Use the typedefs directly (e.g. `H5A`, `H5D`, etc.)
 *
 * @tparam CloseFn - function pointer to destructor for HDF5 object
 */
template <herr_t (*CloseFn)(hid_t)>
class H5Handle {
 public:
  H5Handle() = default;

  H5Handle(H5Handle const &) = delete;
  H5Handle &operator=(H5Handle const &) = delete;

  H5Handle(H5Handle &&other) : hid_(other.Release()) {}
  H5Handle &operator=(H5Handle &&other) {
    Reset();
    hid_ = other.Release();
    return *this;
  }

  static H5Handle FromHIDCheck(hid_t const hid) {
    PARTHENON_REQUIRE_THROWS(hid >= 0, "H5 FromHIDCheck failed");

    H5Handle handle;
    handle.hid_ = hid;
    return handle;
  }

  void Reset() {
    if (*this) {
      PARTHENON_HDF5_CHECK(CloseFn(hid_));
      hid_ = -1;
    }
  }

  hid_t Release() {
    auto hid = hid_;
    hid_ = -1;
    return hid;
  }

  ~H5Handle() { Reset(); }

  // Implicit conversion to hid_t for convenience
  operator hid_t() const { return hid_; }
  explicit operator bool() const { return hid_ >= 0; }

 private:
  hid_t hid_ = -1;
};

using H5A = H5Handle<&H5Aclose>;
using H5D = H5Handle<&H5Dclose>;
using H5F = H5Handle<&H5Fclose>;
using H5G = H5Handle<&H5Gclose>;
using H5O = H5Handle<&H5Oclose>;
using H5P = H5Handle<&H5Pclose>;
using H5T = H5Handle<&H5Tclose>;
using H5S = H5Handle<&H5Sclose>;

// Static functions to return HDF type
static hid_t getHDF5Type(const hbool_t *) { return H5T_NATIVE_HBOOL; }
static hid_t getHDF5Type(const int32_t *) { return H5T_NATIVE_INT32; }
static hid_t getHDF5Type(const int64_t *) { return H5T_NATIVE_INT64; }
static hid_t getHDF5Type(const uint32_t *) { return H5T_NATIVE_UINT32; }
static hid_t getHDF5Type(const uint64_t *) { return H5T_NATIVE_UINT64; }
static hid_t getHDF5Type(const float *) { return H5T_NATIVE_FLOAT; }
static hid_t getHDF5Type(const double *) { return H5T_NATIVE_DOUBLE; }
static hid_t getHDF5Type(const char *) { return H5T_NATIVE_CHAR; }

// On MacOS size_t is "unsigned long" and uint64_t is != "unsigned long".
// Thus, size_t is not captured by the overload above and needs to selectively enabled.
template <typename T,
          typename std::enable_if<std::is_same<T, unsigned long>::value && // NOLINT
                                      !std::is_same<T, uint64_t>::value,
                                  bool>::type = true>
static hid_t getHDF5Type(const T *) {
  return H5T_NATIVE_ULONG;
}

static H5T getHDF5Type(const char *const *) {
  H5T var_string_type = H5T::FromHIDCheck(H5Tcopy(H5T_C_S1));
  PARTHENON_HDF5_CHECK(H5Tset_size(var_string_type, H5T_VARIABLE));
  return var_string_type;
}

// JMM: This stuff is here, not in the rest of the
// attributes code for crazy reasons involving the restart reader
// and compile times.
std::tuple<int, std::vector<hsize_t>, std::size_t>
HDF5GetAttributeInfo(hid_t location, const std::string &name, H5A &attr);

void HDF5ReadAttribute(hid_t location, const std::string &name, std::string &val);

template <typename T>
std::vector<T> HDF5ReadAttributeVec(hid_t location, const std::string &name) {
  H5A attr;
  auto [rank, dim, size] = HDF5GetAttributeInfo(location, name, attr);
  std::vector<T> res(size);

  // Check type
  auto type = getHDF5Type(res.data());
  const H5T hdf5_type = H5T::FromHIDCheck(H5Aget_type(attr));
  auto status = PARTHENON_HDF5_CHECK(H5Tequal(type, hdf5_type));
  PARTHENON_REQUIRE_THROWS(status > 0, "Type mismatch for attribute " + name);

  // Read data from file
  PARTHENON_HDF5_CHECK(H5Aread(attr, type, res.data()));

  return res;
}

// template specialization for std::string (must go into cpp file)
template <>
std::vector<std::string> HDF5ReadAttributeVec(hid_t location, const std::string &name);

template <>
std::vector<bool> HDF5ReadAttributeVec(hid_t location, const std::string &name);

} // namespace HDF5
} // namespace parthenon

#endif // ENABLE_HDF5
#endif // OUTPUTS_PARTHENON_HDF5_TYPES_HPP_
