//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
// (C) (or copyright) 2020. Triad National Security, LLC. All rights reserved.
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
//! \file bvals_fc.cpp
//  \brief functions that apply BCs for FACE_CENTERED variables

// C headers

// C++ headers
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>    // memcpy()
#include <iomanip>
#include <iostream>   // endl
#include <sstream>    // stringstream
#include <stdexcept>  // runtime_error
#include <string>     // c_str()

// Athena++ headers
#include "coordinates/coordinates.hpp"
#include "globals.hpp"
#include "mesh/mesh.hpp"
#include "parameter_input.hpp"
#include "utils/buffer_utils.hpp"
#include "bvals_fc.hpp"

// MPI header
#ifdef MPI_PARALLEL
#include <mpi.h>
#endif

namespace parthenon {
// constructor

FaceCenteredBoundaryVariable::FaceCenteredBoundaryVariable(
    MeshBlock *pmb, FaceField *var, FaceField &coarse_buf, EdgeField &var_flux)
    : BoundaryVariable(pmb), var_fc(var), coarse_buf(coarse_buf) {
  // assuming Field, not generic FaceCenteredBoundaryVariable:

  InitBoundaryData(bd_var_, BoundaryQuantity::fc);
  InitBoundaryData(bd_var_flcor_, BoundaryQuantity::fc_flcor);

#ifdef MPI_PARALLEL
  // KGF: dead code, leaving for now:
  // fc_phys_id_ = pmb->pbval->ReserveTagVariableIDs(2);
  fc_phys_id_ = pmb->pbval->bvars_next_phys_id_;
  fc_flx_phys_id_ = fc_phys_id_ + 1;
#endif
}

// destructor

FaceCenteredBoundaryVariable::~FaceCenteredBoundaryVariable() {
  DestroyBoundaryData(bd_var_);
  DestroyBoundaryData(bd_var_flcor_);
}


int FaceCenteredBoundaryVariable::ComputeVariableBufferSize(const NeighborIndexes& ni, int cng) {
  MeshBlock *pmb = pmy_block_;
  int nx1 = pmb->block_size.nx1;
  int nx2 = pmb->block_size.nx2;
  int nx3 = pmb->block_size.nx3;
  const int f2 = (pmy_mesh_->ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (pmy_mesh_->ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d
  int cng1, cng2, cng3;
  cng1 = cng;
  cng2 = cng*f2;
  cng3 = cng*f3;

  int size1 = ((ni.ox1 == 0) ? (nx1 + 1) : NGHOST)
              *((ni.ox2 == 0) ? (nx2) : NGHOST)
              *((ni.ox3 == 0) ? (nx3) : NGHOST);
  int size2 = ((ni.ox1 == 0) ? (nx1) : NGHOST)
              *((ni.ox2 == 0) ? (nx2 + f2) : NGHOST)
              *((ni.ox3 == 0) ? (nx3) : NGHOST);
  int size3 = ((ni.ox1 == 0) ? (nx1) : NGHOST)
              *((ni.ox2 == 0) ? (nx2) : NGHOST)
              *((ni.ox3 == 0) ? (nx3 + f3) : NGHOST);
  int size = size1 + size2 + size3;
  if (pmy_mesh_->multilevel) {
    if (ni.type != NeighborConnect::face) {
      if (ni.ox1 != 0) size1 = size1/NGHOST*(NGHOST + 1);
      if (ni.ox2 != 0) size2 = size2/NGHOST*(NGHOST + 1);
      if (ni.ox3 != 0) size3 = size3/NGHOST*(NGHOST + 1);
    }
    size = size1 + size2 + size3;
    int f2c1 = ((ni.ox1 == 0) ? ((nx1 + 1)/2 + 1) : NGHOST)
               *((ni.ox2 == 0) ? ((nx2 + 1)/2) : NGHOST)
               *((ni.ox3 == 0) ? ((nx3 + 1)/2) : NGHOST);
    int f2c2 = ((ni.ox1 == 0) ? ((nx1 + 1)/2) : NGHOST)
               *((ni.ox2 == 0) ? ((nx2 + 1)/2 + f2)
                 : NGHOST)
               *((ni.ox3 == 0) ? ((nx3 + 1)/2) : NGHOST);
    int f2c3 = ((ni.ox1 == 0) ? ((nx1 + 1)/2) : NGHOST)
               *((ni.ox2 == 0) ? ((nx2 + 1)/2) : NGHOST)
               *((ni.ox3 == 0) ? ((nx3 + 1)/2 + f3)
                 : NGHOST);
    if (ni.type != NeighborConnect::face) {
      if (ni.ox1 != 0) f2c1 = f2c1/NGHOST*(NGHOST + 1);
      if (ni.ox2 != 0) f2c2 = f2c2/NGHOST*(NGHOST + 1);
      if (ni.ox3 != 0) f2c3 = f2c3/NGHOST*(NGHOST + 1);
    }
    int fsize = f2c1 + f2c2 + f2c3;
    int c2f1 =
        ((ni.ox1 == 0) ? ((nx1 + 1)/2 + cng1 + 1) : cng + 1)
        *((ni.ox2 == 0) ? ((nx2 + 1)/2 + cng2) : cng)
        *((ni.ox3 == 0) ? ((nx3 + 1)/2 + cng3) : cng);
    int c2f2 =
        ((ni.ox1 == 0) ?((nx1 + 1)/2 + cng1) : cng)
        *((ni.ox2 == 0) ? ((nx2 + 1)/2 + cng2 + f2) : cng + 1)
        *((ni.ox3 == 0) ? ((nx3 + 1)/2 + cng3) : cng);
    int c2f3 =
        ((ni.ox1 == 0) ? ((nx1 + 1)/2 + cng1) : cng)
        *((ni.ox2 == 0) ? ((nx2 + 1)/2 + cng2) : cng)
        *((ni.ox3 == 0) ?
          ((nx3 + 1)/2 + cng3 + f3) : cng + 1);
    int csize = c2f1 + c2f2 + c2f3;
    size = std::max(size, std::max(csize, fsize));
  }
  return size;
}

int FaceCenteredBoundaryVariable::ComputeFluxCorrectionBufferSize(
    const NeighborIndexes& ni, int cng) {
  MeshBlock *pmb = pmy_block_;
  int nx1 = pmb->block_size.nx1;
  int nx2 = pmb->block_size.nx2;
  int nx3 = pmb->block_size.nx3;
  int size = 0;

  if (ni.type == NeighborConnect::face) {
    if (nx3 > 1) { // 3D
      if (ni.ox1 != 0)
        size = (nx2 + 1)*(nx3) + (nx2)*(nx3 + 1);
      else if (ni.ox2 != 0)
        size = (nx1 + 1)*(nx3) + (nx1)*(nx3 + 1);
      else
        size = (nx1 + 1)*(nx2) + (nx1)*(nx2 + 1);
    } else if (nx2 > 1) { // 2D
      if (ni.ox1 != 0)
        size = (nx2 + 1) + nx2;
      else
        size = (nx1 + 1) + nx1;
    } else { // 1D
      size = 2;
    }
  } else if (ni.type == NeighborConnect::edge) {
    if (nx3 > 1) { // 3D
      if (ni.ox3 == 0) size = nx3;
      if (ni.ox2 == 0) size = nx2;
      if (ni.ox1 == 0) size = nx1;
    } else if (nx2 > 1) {
      size = 1;
    }
  }
  return size;
}

//----------------------------------------------------------------------------------------
//! \fn int FaceCenteredBoundaryVariable::LoadBoundaryBufferSameLevel(Real *buf,
//                                                               const NeighborBlock& nb)
//  \brief Set face-centered boundary buffers for sending to a block on the same level
// clang-format off
int FaceCenteredBoundaryVariable::LoadBoundaryBufferSameLevel(Real *buf,
                                                              const NeighborBlock& nb) {
  MeshBlock *pmb = pmy_block_;
  int si, sj, sk, ei, ej, ek;
  int p = 0;

  const IndexShape & cellbounds = pmb->cellbounds;
  // bx1
  if      (nb.ni.ox1 == 0) si = cellbounds.is(interior),            ei = cellbounds.ie(interior) + 1;
  else if (nb.ni.ox1 > 0)  si = cellbounds.ie(interior)-NGHOST + 1, ei = cellbounds.ie(interior);
  else                     si = cellbounds.is(interior) + 1,        ei = cellbounds.is(interior) + NGHOST;
  if      (nb.ni.ox2 == 0) sj = cellbounds.js(interior),            ej = cellbounds.je(interior);
  else if (nb.ni.ox2 > 0)  sj = cellbounds.je(interior)-NGHOST + 1, ej = cellbounds.je(interior);
  else                     sj = cellbounds.js(interior),            ej = cellbounds.js(interior) + NGHOST-1;
  if      (nb.ni.ox3 == 0) sk = cellbounds.ks(interior),            ek = cellbounds.ke(interior);
  else if (nb.ni.ox3 > 0)  sk = cellbounds.ke(interior)-NGHOST + 1, ek = cellbounds.ke(interior);
  else                     sk = cellbounds.ks(interior),            ek = cellbounds.ks(interior) + NGHOST-1;
  // for SMR/AMR, always include the overlapping faces in edge and corner boundaries
  if (pmy_mesh_->multilevel && nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox1 > 0) ei++;
    else if (nb.ni.ox1 < 0) si--;
  }
  BufferUtility::PackData((*var_fc).x1f, buf, si, ei, sj, ej, sk, ek, p);

  // bx2
  if      (nb.ni.ox1 == 0) si = cellbounds.is(interior),            ei = cellbounds.ie(interior);
  else if (nb.ni.ox1 > 0)  si = cellbounds.ie(interior)-NGHOST + 1, ei = cellbounds.ie(interior);
  else                     si = cellbounds.is(interior),            ei = cellbounds.is(interior) + NGHOST-1;
  if (pmb->block_size.nx2 == 1) sj = cellbounds.js(interior),       ej = cellbounds.je(interior);
  else if (nb.ni.ox2 == 0) sj = cellbounds.js(interior),            ej = cellbounds.je(interior) + 1;
  else if (nb.ni.ox2 > 0)  sj = cellbounds.je(interior)-NGHOST + 1, ej = cellbounds.je(interior);
  else                     sj = cellbounds.js(interior) + 1,        ej = cellbounds.js(interior) + NGHOST;
  if (pmy_mesh_->multilevel && nb.ni.type != NeighborConnect::face) {
    if      (nb.ni.ox2 > 0) ej++;
    else if (nb.ni.ox2 < 0) sj--;
  }
  BufferUtility::PackData((*var_fc).x2f, buf, si, ei, sj, ej, sk, ek, p);

  // bx3
  if      (nb.ni.ox2 == 0) sj = cellbounds.js(interior),            ej = cellbounds.je(interior);
  else if (nb.ni.ox2 > 0)  sj = cellbounds.je(interior)-NGHOST + 1, ej = cellbounds.je(interior);
  else                     sj = cellbounds.js(interior),            ej = cellbounds.js(interior) + NGHOST-1;
  if (pmb->block_size.nx3 == 1) sk = cellbounds.ks(interior),       ek = cellbounds.ke(interior);
  else if (nb.ni.ox3 == 0) sk = cellbounds.ks(interior),            ek = cellbounds.ke(interior) + 1;
  else if (nb.ni.ox3 > 0)  sk = cellbounds.ke(interior)-NGHOST + 1, ek = cellbounds.ke(interior);
  else                     sk = cellbounds.ks(interior) + 1,        ek = cellbounds.ks(interior) + NGHOST;
  if (pmy_mesh_->multilevel && nb.ni.type != NeighborConnect::face) {
    if      (nb.ni.ox3 > 0) ek++;
    else if (nb.ni.ox3 < 0) sk--;
  }
  BufferUtility::PackData((*var_fc).x3f, buf, si, ei, sj, ej, sk, ek, p);

  return p;
}
// clang-format on

//----------------------------------------------------------------------------------------
//! \fn int FaceCenteredBoundaryVariable::LoadBoundaryBufferToCoarser(Real *buf,
//                                                                const NeighborBlock& nb)
//  \brief Set face-centered boundary buffers for sending to a block on the coarser level

int FaceCenteredBoundaryVariable::LoadBoundaryBufferToCoarser(Real *buf,
                                                              const NeighborBlock& nb) {
  MeshBlock *pmb = pmy_block_;
  auto &pmr = pmb->pmr;
  int si, sj, sk, ei, ej, ek;
  int cng = NGHOST;
  int p = 0;

  int cis, cie, cjs, cje, cks, cke;
  pmb->c_cellbounds.GetIndices(interior,cis,cie,cjs,cje,cks,cke);
  // bx1
  // clang-format off
  if (nb.ni.ox1 == 0)     si = cis,         ei = cie + 1;
  else if (nb.ni.ox1 > 0) si = cie-cng + 1, ei = cie;
  else                    si = cis + 1,     ei = cis + cng;
  if (nb.ni.ox2 == 0)     sj = cjs,         ej = cje;
  else if (nb.ni.ox2 > 0) sj = cje-cng + 1, ej = cje;
  else                    sj = cjs,         ej = cjs + cng-1;
  if (nb.ni.ox3 == 0)     sk = cks,         ek = cke;
  else if (nb.ni.ox3 > 0) sk = cke-cng + 1, ek = cke;
  else                    sk = cks,         ek = cks + cng-1;
  // clang-format on
  // include the overlapping faces in edge and corner boundaries
  if (nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox1 > 0) ei++;
    else if (nb.ni.ox1 < 0) si--;
  }
  pmr->RestrictFieldX1((*var_fc).x1f, coarse_buf.x1f, si, ei, sj, ej, sk, ek);
  BufferUtility::PackData(coarse_buf.x1f, buf, si, ei, sj, ej, sk, ek, p);

  // bx2
  // clang-format off
  if (nb.ni.ox1 == 0)           si = cis,         ei = cie;
  else if (nb.ni.ox1 > 0)       si = cie-cng + 1, ei = cie;
  else                          si = cis,         ei = cis + cng-1;
  if (pmb->block_size.nx2 == 1) sj = cjs,         ej = cje;
  else if (nb.ni.ox2 == 0)      sj = cjs,         ej = cje + 1;
  else if (nb.ni.ox2 > 0)       sj = cje-cng + 1, ej = cje;
  else                          sj = cjs + 1,     ej = cjs + cng;
  if (nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox2 > 0)      ej++;
    else if (nb.ni.ox2 < 0) sj--;
  }
  // clang-format on
  pmr->RestrictFieldX2((*var_fc).x2f, coarse_buf.x2f, si, ei, sj, ej, sk, ek);
  if (pmb->block_size.nx2 == 1) { // 1D
    for (int i=si; i<=ei; i++)
      coarse_buf.x2f(sk,sj+1,i)=coarse_buf.x2f(sk,sj,i);
  }
  BufferUtility::PackData(coarse_buf.x2f, buf, si, ei, sj, ej, sk, ek, p);

  // bx3
  // clang-format off
  if (nb.ni.ox2 == 0)           sj = cjs,         ej = cje;
  else if (nb.ni.ox2 > 0)       sj = cje-cng + 1, ej = cje;
  else                          sj = cjs,         ej = cjs + cng-1;
  if (pmb->block_size.nx3 == 1) sk = cks,         ek = cke;
  else if (nb.ni.ox3 == 0)      sk = cks,         ek = cke + 1;
  else if (nb.ni.ox3 > 0)       sk = cke-cng + 1, ek = cke;
  else                          sk = cks + 1,     ek = cks + cng;
  if (nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox3 > 0)      ek++;
    else if (nb.ni.ox3 < 0) sk--;
  }
  // clang-format on
  pmr->RestrictFieldX3((*var_fc).x3f, coarse_buf.x3f, si, ei, sj, ej, sk, ek);
  if (pmb->block_size.nx3 == 1) { // 1D or 2D
    for (int j=sj; j<=ej; j++) {
      for (int i=si; i<=ei; i++)
        coarse_buf.x3f(sk+1,j,i)=coarse_buf.x3f(sk,j,i);
    }
  }
  BufferUtility::PackData(coarse_buf.x3f, buf, si, ei, sj, ej, sk, ek, p);

  return p;
}

//----------------------------------------------------------------------------------------
//! \fn int FaceCenteredBoundaryVariable::LoadBoundaryBufferToFiner(Real *buf,
//                                                                const NeighborBlock& nb)
//  \brief Set face-centered boundary buffers for sending to a block on the finer level

int FaceCenteredBoundaryVariable::LoadBoundaryBufferToFiner(Real *buf,
                                                            const NeighborBlock& nb) {
  MeshBlock *pmb = pmy_block_;
  int nx1 = pmb->block_size.nx1;
  int nx2 = pmb->block_size.nx2;
  int nx3 = pmb->block_size.nx3;

  int si, sj, sk, ei, ej, ek;
  int cn = pmb->cnghost - 1;
  int p = 0;

  const IndexShape & cellbounds = pmb->cellbounds;
  // send the data first and later prolongate on the target block
  // need to add edges for faces, add corners for edges
  // bx1
  if (nb.ni.ox1 == 0) {
    if (nb.ni.fi1 == 1)   si = cellbounds.is(interior) + nx1/2-pmb->cnghost, ei = cellbounds.ie(interior) + 1;
    else            si = cellbounds.is(interior), ei = cellbounds.ie(interior) + 1-nx1/2 + pmb->cnghost;
  } else if (nb.ni.ox1 > 0) { si = cellbounds.ie(interior) + 1-pmb->cnghost, ei = cellbounds.ie(interior) + 1;}
  else              si = cellbounds.is(interior),                ei = cellbounds.is(interior) + pmb->cnghost;
  if (nb.ni.ox2 == 0) {
    sj = cellbounds.js(interior),    ej = cellbounds.je(interior);
    if (nx2 > 1) {
      if (nb.ni.ox1 != 0) {
        if (nb.ni.fi1 == 1) sj += nx2/2-pmb->cnghost;
        else          ej -= nx2/2-pmb->cnghost;
      } else {
        if (nb.ni.fi2 == 1) sj += nx2/2-pmb->cnghost;
        else          ej -= nx2/2-pmb->cnghost;
      }
    }
  } else if (nb.ni.ox2 > 0) { sj = cellbounds.je(interior)-cn, ej = cellbounds.je(interior);}
  else              sj = cellbounds.js(interior),    ej = cellbounds.js(interior) + cn;
  if (nb.ni.ox3 == 0) {
    sk = cellbounds.ks(interior),    ek = cellbounds.ke(interior);
    if (nx3 > 1) {
      if (nb.ni.ox1 != 0 && nb.ni.ox2 != 0) {
        if (nb.ni.fi1 == 1) sk += nx3/2-pmb->cnghost;
        else          ek -= nx3/2-pmb->cnghost;
      } else {
        if (nb.ni.fi2 == 1) sk += nx3/2-pmb->cnghost;
        else          ek -= nx3/2-pmb->cnghost;
      }
    }
  } else if (nb.ni.ox3 > 0) { sk = cellbounds.ke(interior)-cn, ek = cellbounds.ke(interior);}
  else              sk = cellbounds.ks(interior),    ek = cellbounds.ks(interior) + cn;
  BufferUtility::PackData((*var_fc).x1f, buf, si, ei, sj, ej, sk, ek, p);

  // bx2
  if (nb.ni.ox1 == 0) {
    if (nb.ni.fi1 == 1)   si = cellbounds.is(interior) + nx1/2-pmb->cnghost, ei = cellbounds.ie(interior);
    else            si = cellbounds.is(interior), ei = cellbounds.ie(interior)-nx1/2 + pmb->cnghost;
  } else if (nb.ni.ox1 > 0) { si = cellbounds.ie(interior)-cn, ei = cellbounds.ie(interior);}
  else              si = cellbounds.is(interior),    ei = cellbounds.is(interior) + cn;
  if (nb.ni.ox2 == 0) {
    sj = cellbounds.js(interior),    ej = cellbounds.je(interior);
    if (nx2 > 1) {
      ej++;
      if (nb.ni.ox1 != 0) {
        if (nb.ni.fi1 == 1) sj += nx2/2-pmb->cnghost;
        else          ej -= nx2/2-pmb->cnghost;
      } else {
        if (nb.ni.fi2 == 1) sj += nx2/2-pmb->cnghost;
        else          ej -= nx2/2-pmb->cnghost;
      }
    }
  } else if (nb.ni.ox2 > 0) { sj = cellbounds.je(interior) + 1-pmb->cnghost, ej = cellbounds.je(interior) + 1;}
  else              sj = cellbounds.js(interior),                ej = cellbounds.js(interior) + pmb->cnghost;
  BufferUtility::PackData((*var_fc).x2f, buf, si, ei, sj, ej, sk, ek, p);

  // bx3
  if (nb.ni.ox2 == 0) {
    sj = cellbounds.js(interior),    ej = cellbounds.je(interior);
    if (nx2 > 1) {
      if (nb.ni.ox1 != 0) {
        if (nb.ni.fi1 == 1) sj += nx2/2-pmb->cnghost;
        else          ej -= nx2/2-pmb->cnghost;
      } else {
        if (nb.ni.fi2 == 1) sj += nx2/2-pmb->cnghost;
        else          ej -= nx2/2-pmb->cnghost;
      }
    }
  } else if (nb.ni.ox2 > 0) { sj = cellbounds.je(interior)-cn, ej = cellbounds.je(interior);}
  else              sj = cellbounds.js(interior),    ej = cellbounds.js(interior) + cn;
  if (nb.ni.ox3 == 0) {
    sk = cellbounds.ks(interior),    ek = cellbounds.ke(interior);
    if (nx3 > 1) {
      ek++;
      if (nb.ni.ox1 != 0 && nb.ni.ox2 != 0) {
        if (nb.ni.fi1 == 1) sk += nx3/2-pmb->cnghost;
        else          ek -= nx3/2-pmb->cnghost;
      } else {
        if (nb.ni.fi2 == 1) sk += nx3/2-pmb->cnghost;
        else          ek -= nx3/2-pmb->cnghost;
      }
    }
  } else if (nb.ni.ox3 > 0) { sk = cellbounds.ke(interior) + 1-pmb->cnghost, ek = cellbounds.ke(interior) + 1;}
  else              sk = cellbounds.ks(interior),                ek = cellbounds.ks(interior) + pmb->cnghost;
  BufferUtility::PackData((*var_fc).x3f, buf, si, ei, sj, ej, sk, ek, p);

  return p;
}


//----------------------------------------------------------------------------------------
//! \fn void FaceCenteredBoundaryVariable::SetBoundarySameLevel(Real *buf,
//                                                              const NeighborBlock& nb)
//  \brief Set face-centered boundary received from a block on the same level

void FaceCenteredBoundaryVariable::SetBoundarySameLevel(Real *buf,
                                                        const NeighborBlock& nb) {
  MeshBlock *pmb = pmy_block_;
  int si, sj, sk, ei, ej, ek;

  int p = 0;
  const IndexShape & cellbounds = cellbounds;
  // bx1
  // for uniform grid: face-neighbors take care of the overlapping faces
  if (nb.ni.ox1 == 0)     si = cellbounds.is(interior),        ei = cellbounds.ie(interior) + 1;
  else if (nb.ni.ox1 > 0) si = cellbounds.ie(interior) + 2,      ei = cellbounds.ie(interior) + NGHOST + 1;
  else              si = cellbounds.is(interior) - NGHOST, ei = cellbounds.is(interior) - 1;
  if (nb.ni.ox2 == 0)     sj = cellbounds.js(interior),        ej = cellbounds.je(interior);
  else if (nb.ni.ox2 > 0) sj = cellbounds.je(interior) + 1,      ej = cellbounds.je(interior) + NGHOST;
  else              sj = cellbounds.js(interior) - NGHOST, ej = cellbounds.js(interior) - 1;
  if (nb.ni.ox3 == 0)     sk = cellbounds.ks(interior),        ek = cellbounds.ke(interior);
  else if (nb.ni.ox3 > 0) sk = cellbounds.ke(interior) + 1,      ek = cellbounds.ke(interior) + NGHOST;
  else              sk = cellbounds.ks(interior) - NGHOST, ek = cellbounds.ks(interior) - 1;
  // for SMR/AMR, always include the overlapping faces in edge and corner boundaries
  if (pmy_mesh_->multilevel && nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox1 > 0) si--;
    else if (nb.ni.ox1 < 0) ei++;
  }

  BufferUtility::UnpackData(buf, (*var_fc).x1f, si, ei, sj, ej, sk, ek, p);


  // bx2
  if (nb.ni.ox1 == 0)      si = cellbounds.is(interior),         ei = cellbounds.ie(interior);
  else if (nb.ni.ox1 > 0)  si = cellbounds.ie(interior) + 1,       ei = cellbounds.ie(interior) + NGHOST;
  else               si = cellbounds.is(interior) - NGHOST,  ei = cellbounds.is(interior) - 1;
  if (pmb->block_size.nx2 == 1) sj = cellbounds.js(interior), ej = cellbounds.je(interior);
  else if (nb.ni.ox2 == 0) sj = cellbounds.js(interior),         ej = cellbounds.je(interior) + 1;
  else if (nb.ni.ox2 > 0)  sj = cellbounds.je(interior) + 2,       ej = cellbounds.je(interior) + NGHOST + 1;
  else               sj = cellbounds.js(interior) - NGHOST,  ej = cellbounds.js(interior) - 1;
  // for SMR/AMR, always include the overlapping faces in edge and corner boundaries
  if (pmy_mesh_->multilevel && nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox2 > 0) sj--;
    else if (nb.ni.ox2 < 0) ej++;
  }

  BufferUtility::UnpackData(buf, (*var_fc).x2f, si, ei, sj, ej, sk, ek, p);

  if (pmb->block_size.nx2 == 1) { // 1D
#pragma omp simd
    for (int i=si; i<=ei; ++i)
      (*var_fc).x2f(sk,sj+1,i) = (*var_fc).x2f(sk,sj,i);
  }

  // bx3
  if (nb.ni.ox2 == 0)      sj = cellbounds.js(interior),         ej = cellbounds.je(interior);
  else if (nb.ni.ox2 > 0)  sj = cellbounds.je(interior) + 1,       ej = cellbounds.je(interior) + NGHOST;
  else               sj = cellbounds.js(interior) - NGHOST,  ej = cellbounds.js(interior) - 1;
  if (pmb->block_size.nx3 == 1) sk = cellbounds.ks(interior), ek = cellbounds.ke(interior);
  else if (nb.ni.ox3 == 0) sk = cellbounds.ks(interior),         ek = cellbounds.ke(interior) + 1;
  else if (nb.ni.ox3 > 0)  sk = cellbounds.ke(interior) + 2,       ek = cellbounds.ke(interior) + NGHOST + 1;
  else               sk = cellbounds.ks(interior) - NGHOST,  ek = cellbounds.ks(interior) - 1;
  // for SMR/AMR, always include the overlapping faces in edge and corner boundaries
  if (pmy_mesh_->multilevel && nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox3 > 0) sk--;
    else if (nb.ni.ox3 < 0) ek++;
  }

  BufferUtility::UnpackData(buf, (*var_fc).x3f, si, ei, sj, ej, sk, ek, p);

  if (pmb->block_size.nx3 == 1) { // 1D or 2D
    for (int j=sj; j<=ej; ++j) {
#pragma omp simd
      for (int i=si; i<=ei; ++i)
        (*var_fc).x3f(sk+1,j,i) = (*var_fc).x3f(sk,j,i);
    }
  }

  return;
}

//----------------------------------------------------------------------------------------
//! \fn void FaceCenteredBoundaryVariable::SetBoundaryFromCoarser(Real *buf,
//                                                                const NeighborBlock& nb)
//  \brief Set face-centered prolongation buffer received from a block on the same level

void FaceCenteredBoundaryVariable::SetBoundaryFromCoarser(Real *buf,
                                                          const NeighborBlock& nb) {
  MeshBlock *pmb = pmy_block_;
  int si, sj, sk, ei, ej, ek;
  int cng = pmb->cnghost;
  int p = 0;

  int cis, cie, cjs, cje, cks, cke;
  pmb->c_cellbounds.GetIndices(interior,cis,cie,cjs,cje,cks,cke);

  // bx1
  if (nb.ni.ox1 == 0) {
    si = cis, ei = cie + 1;
    if ((pmb->loc.lx1 & 1LL) == 0LL) ei += cng;
    else             si -= cng;
  } else if (nb.ni.ox1 > 0) {  si = cie + 1,   ei = cie + 1 + cng;}
  else               si = cis - cng, ei = cis;
  if (nb.ni.ox2 == 0) {
    sj = cjs, ej = cje;
    if (pmb->block_size.nx2 > 1) {
      if ((pmb->loc.lx2 & 1LL) == 0LL) ej += cng;
      else             sj -= cng;
    }
  } else if (nb.ni.ox2 > 0) {  sj = cje + 1,   ej = cje + cng;}
  else               sj = cjs - cng, ej = cjs - 1;
  if (nb.ni.ox3 == 0) {
    sk = cks, ek = cke;
    if (pmb->block_size.nx3 > 1) {
      if ((pmb->loc.lx3 & 1LL) == 0LL) ek += cng;
      else             sk -= cng;
    }
  } else if (nb.ni.ox3 > 0) {  sk = cke + 1,   ek = cke + cng;}
  else               sk = cks - cng, ek = cks - 1;

  BufferUtility::UnpackData(buf, coarse_buf.x1f, si, ei, sj, ej, sk, ek, p);

  // bx2
  if (nb.ni.ox1 == 0) {
    si = cis, ei = cie;
    if ((pmb->loc.lx1 & 1LL) == 0LL) ei += cng;
    else             si -= cng;
  } else if (nb.ni.ox1 > 0) {  si = cie + 1,   ei = cie + cng;}
  else               si = cis - cng, ei = cis - 1;
  if (nb.ni.ox2 == 0) {
    sj = cjs, ej = cje;
    if (pmb->block_size.nx2 > 1) {
      ej++;
      if ((pmb->loc.lx2 & 1LL) == 0LL) ej += cng;
      else             sj -= cng;
    }
  } else if (nb.ni.ox2 > 0) {  sj = cje + 1,   ej = cje + 1 + cng;}
  else               sj = cjs - cng, ej = cjs;

  BufferUtility::UnpackData(buf, coarse_buf.x2f, si, ei, sj, ej, sk, ek, p);
  if (pmb->block_size.nx2  ==  1) { // 1D
#pragma omp simd
    for (int i=si; i<=ei; ++i)
      coarse_buf.x2f(sk,sj+1,i) = coarse_buf.x2f(sk,sj,i);
  }


  // bx3
  if (nb.ni.ox2 == 0) {
    sj = cjs, ej = cje;
    if (pmb->block_size.nx2 > 1) {
      if ((pmb->loc.lx2 & 1LL) == 0LL) ej += cng;
      else             sj -= cng;
    }
  } else if (nb.ni.ox2 > 0) {  sj = cje + 1,   ej = cje + cng;}
  else               sj = cjs - cng, ej = cjs - 1;
  if (nb.ni.ox3 == 0) {
    sk = cks, ek = cke;
    if (pmb->block_size.nx3 > 1) {
      ek++;
      if ((pmb->loc.lx3 & 1LL) == 0LL) ek += cng;
      else             sk -= cng;
    }
  } else if (nb.ni.ox3 > 0) {  sk = cke + 1,   ek = cke + 1 + cng;}
  else               sk = cks - cng, ek = cks;

  BufferUtility::UnpackData(buf, coarse_buf.x3f, si, ei, sj, ej, sk, ek, p);

  if (pmb->block_size.nx3 == 1) { // 2D
    for (int j=sj; j<=ej; ++j) {
      for (int i=si; i<=ei; ++i)
        coarse_buf.x3f(sk+1,j,i) = coarse_buf.x3f(sk,j,i);
    }
  }

  return;
}

//----------------------------------------------------------------------------------------
//! \fn void FaceCenteredBoundaryVariable::SetFielBoundaryFromFiner(Real *buf,
//                                                                const NeighborBlock& nb)
//  \brief Set face-centered boundary received from a block on the same level

void FaceCenteredBoundaryVariable::SetBoundaryFromFiner(Real *buf,
                                                        const NeighborBlock& nb) {
  MeshBlock *pmb = pmy_block_;
  // receive already restricted data
  int si, sj, sk, ei, ej, ek;
  int p = 0;

  const IndexShape & cellbounds = pmb->cellbounds;
  // bx1
  if (nb.ni.ox1 == 0) {
    si = cellbounds.is(interior), ei = cellbounds.ie(interior) + 1;
    if (nb.ni.fi1 == 1)   si += pmb->block_size.nx1/2;
    else            ei -= pmb->block_size.nx1/2;
  } else if (nb.ni.ox1 > 0) { si = cellbounds.ie(interior) + 2,      ei = cellbounds.ie(interior) + NGHOST + 1;}
  else              si = cellbounds.is(interior) - NGHOST, ei = cellbounds.is(interior) - 1;
  // include the overlapping faces in edge and corner boundaries
  if (nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox1 > 0) si--;
    else if (nb.ni.ox1 < 0) ei++;
  }
  if (nb.ni.ox2 == 0) {
    sj = cellbounds.js(interior), ej = cellbounds.je(interior);
    if (pmb->block_size.nx2 > 1) {
      if (nb.ni.ox1 != 0) {
        if (nb.ni.fi1 == 1) sj += pmb->block_size.nx2/2;
        else          ej -= pmb->block_size.nx2/2;
      } else {
        if (nb.ni.fi2 == 1) sj += pmb->block_size.nx2/2;
        else          ej -= pmb->block_size.nx2/2;
      }
    }
  } else if (nb.ni.ox2 > 0) { sj = cellbounds.je(interior) + 1,      ej = cellbounds.je(interior) + NGHOST;}
  else              sj = cellbounds.js(interior) - NGHOST, ej = cellbounds.js(interior) - 1;
  if (nb.ni.ox3 == 0) {
    sk = cellbounds.ks(interior), ek = cellbounds.ke(interior);
    if (pmb->block_size.nx3 > 1) {
      if (nb.ni.ox1 != 0 && nb.ni.ox2 != 0) {
        if (nb.ni.fi1 == 1) sk += pmb->block_size.nx3/2;
        else          ek -= pmb->block_size.nx3/2;
      } else {
        if (nb.ni.fi2 == 1) sk += pmb->block_size.nx3/2;
        else          ek -= pmb->block_size.nx3/2;
      }
    }
  } else if (nb.ni.ox3 > 0) { sk = cellbounds.ke(interior) + 1,      ek = cellbounds.ke(interior) + NGHOST;}
  else              sk = cellbounds.ks(interior) - NGHOST, ek = cellbounds.ks(interior) - 1;

  BufferUtility::UnpackData(buf, (*var_fc).x1f, si, ei, sj, ej, sk, ek, p);

  // bx2
  if (nb.ni.ox1 == 0) {
    si = cellbounds.is(interior), ei = cellbounds.ie(interior);
    if (nb.ni.fi1 == 1)   si += pmb->block_size.nx1/2;
    else            ei -= pmb->block_size.nx1/2;
  } else if (nb.ni.ox1 > 0) { si = cellbounds.ie(interior) + 1,      ei = cellbounds.ie(interior) + NGHOST;}
  else              si = cellbounds.is(interior) - NGHOST, ei = cellbounds.is(interior) - 1;
  if (nb.ni.ox2 == 0) {
    sj = cellbounds.js(interior), ej = cellbounds.je(interior);
    if (pmb->block_size.nx2 > 1) {
      ej++;
      if (nb.ni.ox1 != 0) {
        if (nb.ni.fi1 == 1) sj += pmb->block_size.nx2/2;
        else          ej -= pmb->block_size.nx2/2;
      } else {
        if (nb.ni.fi2 == 1) sj += pmb->block_size.nx2/2;
        else          ej -= pmb->block_size.nx2/2;
      }
    }
  } else if (nb.ni.ox2 > 0) { sj = cellbounds.je(interior) + 2,      ej = cellbounds.je(interior) + NGHOST + 1;}
  else              sj = cellbounds.js(interior) - NGHOST, ej = cellbounds.js(interior) - 1;
  // include the overlapping faces in edge and corner boundaries
  if (nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox2 > 0) sj--;
    else if (nb.ni.ox2 < 0) ej++;
  }

  BufferUtility::UnpackData(buf, (*var_fc).x2f, si, ei, sj, ej, sk, ek, p);

  if (pmb->block_size.nx2 == 1) { // 1D
#pragma omp simd
    for (int i=si; i<=ei; ++i)
      (*var_fc).x2f(sk,sj+1,i) = (*var_fc).x2f(sk,sj,i);
  }

  // bx3
  if (nb.ni.ox2 == 0) {
    sj = cellbounds.js(interior), ej = cellbounds.je(interior);
    if (pmb->block_size.nx2 > 1) {
      if (nb.ni.ox1 != 0) {
        if (nb.ni.fi1 == 1) sj += pmb->block_size.nx2/2;
        else          ej -= pmb->block_size.nx2/2;
      } else {
        if (nb.ni.fi2 == 1) sj += pmb->block_size.nx2/2;
        else          ej -= pmb->block_size.nx2/2;
      }
    }
  } else if (nb.ni.ox2 > 0) { sj = cellbounds.je(interior) + 1,      ej = cellbounds.je(interior) + NGHOST;}
  else              sj = cellbounds.js(interior) - NGHOST, ej = cellbounds.js(interior) - 1;
  if (nb.ni.ox3 == 0) {
    sk = cellbounds.ks(interior), ek = cellbounds.ke(interior);
    if (pmb->block_size.nx3 > 1) {
      ek++;
      if (nb.ni.ox1 != 0 && nb.ni.ox2 != 0) {
        if (nb.ni.fi1 == 1) sk += pmb->block_size.nx3/2;
        else          ek -= pmb->block_size.nx3/2;
      } else {
        if (nb.ni.fi2 == 1) sk += pmb->block_size.nx3/2;
        else          ek -= pmb->block_size.nx3/2;
      }
    }
  } else if (nb.ni.ox3 > 0) { sk = cellbounds.ke(interior) + 2,      ek = cellbounds.ke(interior) + NGHOST + 1;}
  else              sk = cellbounds.ks(interior) - NGHOST, ek = cellbounds.ks(interior) - 1;
  // include the overlapping faces in edge and corner boundaries
  if (nb.ni.type != NeighborConnect::face) {
    if (nb.ni.ox3 > 0) sk--;
    else if (nb.ni.ox3 < 0) ek++;
  }

  BufferUtility::UnpackData(buf, (*var_fc).x3f, si, ei, sj, ej, sk, ek, p);

  if (pmb->block_size.nx3 == 1) { // 1D or 2D
    for (int j=sj; j<=ej; ++j) {
#pragma omp simd
      for (int i=si; i<=ei; ++i)
        (*var_fc).x3f(sk+1,j,i) = (*var_fc).x3f(sk,j,i);
    }
  }
  return;
}

void FaceCenteredBoundaryVariable::CountFineEdges() {
  MeshBlock* pmb = pmy_block_;
  int &mylevel = pmb->loc.level;

  // count the number of the fine meshblocks contacting on each edge
  int eid = 0;
  if (pmb->block_size.nx2 > 1) {
    for (int ox2=-1; ox2<=1; ox2+=2) {
      for (int ox1=-1; ox1<=1; ox1+=2) {
        int nis, nie, njs, nje;
        nis = std::max(ox1-1, -1), nie = std::min(ox1+1, 1);
        njs = std::max(ox2-1, -1), nje = std::min(ox2+1, 1);
        int nf = 0, fl = mylevel;
        for (int nj=njs; nj<=nje; nj++) {
          for (int ni=nis; ni<=nie; ni++) {
            if (pmb->pbval->nblevel[1][nj+1][ni+1] > fl)
              fl++, nf = 0;
            if (pmb->pbval->nblevel[1][nj+1][ni+1] == fl)
              nf++;
          }
        }
        edge_flag_[eid] = (fl == mylevel);
        nedge_fine_[eid++] = nf;
      }
    }
  }
  if (pmb->block_size.nx3 > 1) {
    for (int ox3=-1; ox3<=1; ox3+=2) {
      for (int ox1=-1; ox1<=1; ox1+=2) {
        int nis, nie, nks, nke;
        nis = std::max(ox1-1, -1), nie = std::min(ox1+1, 1);
        nks = std::max(ox3-1, -1), nke = std::min(ox3+1, 1);
        int nf = 0, fl = mylevel;
        for (int nk=nks; nk<=nke; nk++) {
          for (int ni=nis; ni<=nie; ni++) {
            if (pmb->pbval->nblevel[nk+1][1][ni+1] > fl)
              fl++, nf = 0;
            if (pmb->pbval->nblevel[nk+1][1][ni+1] == fl)
              nf++;
          }
        }
        edge_flag_[eid] = (fl == mylevel);
        nedge_fine_[eid++] = nf;
      }
    }
    for (int ox3=-1; ox3<=1; ox3+=2) {
      for (int ox2=-1; ox2<=1; ox2+=2) {
        int njs, nje, nks, nke;
        njs = std::max(ox2-1, -1), nje = std::min(ox2+1, 1);
        nks = std::max(ox3-1, -1), nke = std::min(ox3+1, 1);
        int nf = 0, fl = mylevel;
        for (int nk=nks; nk<=nke; nk++) {
          for (int nj=njs; nj<=nje; nj++) {
            if (pmb->pbval->nblevel[nk+1][nj+1][1] > fl)
              fl++, nf = 0;
            if (pmb->pbval->nblevel[nk+1][nj+1][1] == fl)
              nf++;
          }
        }
        edge_flag_[eid] = (fl == mylevel);
        nedge_fine_[eid++] = nf;
      }
    }
  }
}


void FaceCenteredBoundaryVariable::SetupPersistentMPI() {
  CountFineEdges();

#ifdef MPI_PARALLEL
  MeshBlock* pmb = pmy_block_;
  int nx1 = pmb->block_size.nx1;
  int nx2 = pmb->block_size.nx2;
  int nx3 = pmb->block_size.nx3;
  int &mylevel = pmb->loc.level;

  const int f2 = (pmy_mesh_->ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (pmy_mesh_->ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d
  int cng, cng1, cng2, cng3;
  cng  = cng1 = pmb->cnghost;
  cng2 = cng*f2;
  cng3 = cng*f3;
  int ssize, rsize;
  int tag;
  // Initialize non-polar neighbor communications to other ranks
  for (int n=0; n<pmb->pbval->nneighbor; n++) {
    NeighborBlock& nb = pmb->pbval->neighbor[n];
    if (nb.snb.rank != Globals::my_rank) {
      int size, csize, fsize;
      int size1 = ((nb.ni.ox1 == 0) ? (nx1 + 1) : NGHOST)
                  *((nb.ni.ox2 == 0) ? (nx2) : NGHOST)
                  *((nb.ni.ox3 == 0) ? (nx3) : NGHOST);
      int size2 = ((nb.ni.ox1 == 0) ? (nx1) : NGHOST)
                  *((nb.ni.ox2 == 0) ? (nx2 + f2) : NGHOST)
                  *((nb.ni.ox3 == 0) ? (nx3) : NGHOST);
      int size3 = ((nb.ni.ox1 == 0) ? (nx1) : NGHOST)
                  *((nb.ni.ox2 == 0) ? (nx2) : NGHOST)
                  *((nb.ni.ox3 == 0) ? (nx3 + f3) : NGHOST);
      size = size1 + size2 + size3;
      if (pmy_mesh_->multilevel) {
        if (nb.ni.type != NeighborConnect::face) {
          if (nb.ni.ox1 != 0) size1 = size1/NGHOST*(NGHOST + 1);
          if (nb.ni.ox2 != 0) size2 = size2/NGHOST*(NGHOST + 1);
          if (nb.ni.ox3 != 0) size3 = size3/NGHOST*(NGHOST + 1);
        }
        size = size1 + size2 + size3;
        int f2c1 = ((nb.ni.ox1 == 0) ? ((nx1 + 1)/2 + 1) : NGHOST)
                   *((nb.ni.ox2 == 0) ? ((nx2 + 1)/2) : NGHOST)
                   *((nb.ni.ox3 == 0) ? ((nx3 + 1)/2) : NGHOST);
        int f2c2 = ((nb.ni.ox1 == 0) ? ((nx1 + 1)/2) : NGHOST)
                   *((nb.ni.ox2 == 0) ? ((nx2 + 1)/2 + f2) : NGHOST)
                   *((nb.ni.ox3 == 0) ? ((nx3 + 1)/2) : NGHOST);
        int f2c3 = ((nb.ni.ox1 == 0) ? ((nx1 + 1)/2) : NGHOST)
                   *((nb.ni.ox2 == 0) ? ((nx2 + 1)/2) : NGHOST)
                   *((nb.ni.ox3 == 0) ? ((nx3 + 1)/2 + f3) : NGHOST);
        if (nb.ni.type != NeighborConnect::face) {
          if (nb.ni.ox1 != 0) f2c1 = f2c1/NGHOST*(NGHOST + 1);
          if (nb.ni.ox2 != 0) f2c2 = f2c2/NGHOST*(NGHOST + 1);
          if (nb.ni.ox3 != 0) f2c3 = f2c3/NGHOST*(NGHOST + 1);
        }
        fsize = f2c1 + f2c2 + f2c3;
        int c2f1 = ((nb.ni.ox1 == 0) ? ((nx1 + 1)/2 + cng1 + 1) : cng + 1)
                   *((nb.ni.ox2 == 0) ? ((nx2 + 1)/2 + cng2) : cng)
                   *((nb.ni.ox3 == 0) ? ((nx3 + 1)/2 + cng3) : cng);
        int c2f2 = ((nb.ni.ox1 == 0) ? ((nx1 + 1)/2 + cng1) : cng)
                   *((nb.ni.ox2 == 0) ? ((nx2 + 1)/2 + cng2 + f2) : cng + 1)
                   *((nb.ni.ox3 == 0) ? ((nx3 + 1)/2 + cng3) : cng);
        int c2f3 = ((nb.ni.ox1 == 0) ? ((nx1 + 1)/2 + cng1) : cng)
                   *((nb.ni.ox2 == 0) ? ((nx2 + 1)/2 + cng2) : cng)
                   *((nb.ni.ox3 == 0) ? ((nx3 + 1)/2 + cng3 + f3) : cng + 1);
        csize = c2f1 + c2f2 + c2f3;
      } // end of multilevel
      if (nb.snb.level == mylevel) // same refinement level
        ssize = size, rsize = size;
      else if (nb.snb.level < mylevel) // coarser
        ssize = fsize, rsize = csize;
      else // finer
        ssize = csize, rsize = fsize;

      // face-centered field: bd_var_
      tag = pmb->pbval->CreateBvalsMPITag(nb.snb.lid, nb.targetid, fc_phys_id_);
      if (bd_var_.req_send[nb.bufid] != MPI_REQUEST_NULL)
        MPI_Request_free(&bd_var_.req_send[nb.bufid]);
      MPI_Send_init(bd_var_.send[nb.bufid], ssize, MPI_ATHENA_REAL,
                    nb.snb.rank, tag, MPI_COMM_WORLD, &(bd_var_.req_send[nb.bufid]));
      tag = pmb->pbval->CreateBvalsMPITag(pmb->lid, nb.bufid, fc_phys_id_);
      if (bd_var_.req_recv[nb.bufid] != MPI_REQUEST_NULL)
        MPI_Request_free(&bd_var_.req_recv[nb.bufid]);
      MPI_Recv_init(bd_var_.recv[nb.bufid], rsize, MPI_ATHENA_REAL,
                    nb.snb.rank, tag, MPI_COMM_WORLD, &(bd_var_.req_recv[nb.bufid]));

      // set up flux correction MPI communication buffers
      int f2csize;
      if (nb.ni.type == NeighborConnect::face) { // face
        if (nx3 > 1) { // 3D
          if (nb.fid == BoundaryFace::inner_x1 || nb.fid == BoundaryFace::outer_x1) {
            size = (nx2 + 1)*(nx3) + (nx2)*(nx3 + 1);
            f2csize = (nx2/2 + 1)*(nx3/2) + (nx2/2)*(nx3/2 + 1);
          } else if (nb.fid == BoundaryFace::inner_x2
                     || nb.fid == BoundaryFace::outer_x2) {
            size = (nx1 + 1)*(nx3) + (nx1)*(nx3 + 1);
            f2csize = (nx1/2 + 1)*(nx3/2) + (nx1/2)*(nx3/2 + 1);
          } else if (nb.fid == BoundaryFace::inner_x3
                     || nb.fid == BoundaryFace::outer_x3) {
            size = (nx1 + 1)*(nx2) + (nx1)*(nx2 + 1);
            f2csize = (nx1/2 + 1)*(nx2/2) + (nx1/2)*(nx2/2 + 1);
          }
        } else if (nx2 > 1) { // 2D
          if (nb.fid == BoundaryFace::inner_x1 || nb.fid == BoundaryFace::outer_x1) {
            size = (nx2 + 1) + nx2;
            f2csize = (nx2/2 + 1) + nx2/2;
          } else if (nb.fid == BoundaryFace::inner_x2
                     || nb.fid == BoundaryFace::outer_x2) {
            size = (nx1 + 1) + nx1;
            f2csize = (nx1/2 + 1) + nx1/2;
          }
        } else { // 1D
          size = f2csize = 2;
        }
      } else if (nb.ni.type == NeighborConnect::edge) { // edge
        if (nx3 > 1) { // 3D
          if (nb.eid >= 0 && nb.eid < 4) {
            size = nx3;
            f2csize = nx3/2;
          } else if (nb.eid >= 4 && nb.eid < 8) {
            size = nx2;
            f2csize = nx2/2;
          } else if (nb.eid >= 8 && nb.eid < 12) {
            size = nx1;
            f2csize = nx1/2;
          }
        } else if (nx2 > 1) { // 2D
          size = f2csize = 1;
        }
      } else { // corner
        continue;
      }

      if (nb.snb.level == mylevel) { // the same level
        if ((nb.ni.type == NeighborConnect::face)
            || ((nb.ni.type == NeighborConnect::edge)
                && (edge_flag_[nb.eid]))) {
          tag = pmb->pbval->CreateBvalsMPITag(nb.snb.lid, nb.targetid, fc_flx_phys_id_);
          if (bd_var_flcor_.req_send[nb.bufid] != MPI_REQUEST_NULL)
            MPI_Request_free(&bd_var_flcor_.req_send[nb.bufid]);
          MPI_Send_init(bd_var_flcor_.send[nb.bufid], size, MPI_ATHENA_REAL,
                        nb.snb.rank, tag, MPI_COMM_WORLD,
                        &(bd_var_flcor_.req_send[nb.bufid]));
          tag = pmb->pbval->CreateBvalsMPITag(pmb->lid, nb.bufid, fc_flx_phys_id_);
          if (bd_var_flcor_.req_recv[nb.bufid] != MPI_REQUEST_NULL)
            MPI_Request_free(&bd_var_flcor_.req_recv[nb.bufid]);
          MPI_Recv_init(bd_var_flcor_.recv[nb.bufid], size, MPI_ATHENA_REAL,
                        nb.snb.rank, tag, MPI_COMM_WORLD,
                        &(bd_var_flcor_.req_recv[nb.bufid]));
        }
      }
      if (nb.snb.level>mylevel) { // finer neighbor
        tag = pmb->pbval->CreateBvalsMPITag(pmb->lid, nb.bufid, fc_flx_phys_id_);
        if (bd_var_flcor_.req_recv[nb.bufid] != MPI_REQUEST_NULL)
          MPI_Request_free(&bd_var_flcor_.req_recv[nb.bufid]);
        MPI_Recv_init(bd_var_flcor_.recv[nb.bufid], f2csize, MPI_ATHENA_REAL,
                      nb.snb.rank, tag, MPI_COMM_WORLD,
                      &(bd_var_flcor_.req_recv[nb.bufid]));
      }
      if (nb.snb.level<mylevel) { // coarser neighbor
        tag = pmb->pbval->CreateBvalsMPITag(nb.snb.lid, nb.targetid, fc_flx_phys_id_);
        if (bd_var_flcor_.req_send[nb.bufid] != MPI_REQUEST_NULL)
          MPI_Request_free(&bd_var_flcor_.req_send[nb.bufid]);
        MPI_Send_init(bd_var_flcor_.send[nb.bufid], f2csize, MPI_ATHENA_REAL,
                      nb.snb.rank, tag, MPI_COMM_WORLD,
                      &(bd_var_flcor_.req_send[nb.bufid]));
      }
    } // neighbor block is on separate MPI process
  } // end loop over neighbors
#endif
  return;
}


void FaceCenteredBoundaryVariable::StartReceiving(BoundaryCommSubset phase) {
  if (phase == BoundaryCommSubset::all)
    recv_flx_same_lvl_ = true;
#ifdef MPI_PARALLEL
  MeshBlock *pmb = pmy_block_;
  int mylevel = pmb->loc.level;
  for (int n=0; n<pmb->pbval->nneighbor; n++) {
    NeighborBlock& nb = pmb->pbval->neighbor[n];
    if (nb.snb.rank != Globals::my_rank && phase != BoundaryCommSubset::gr_amr) {
      MPI_Start(&(bd_var_.req_recv[nb.bufid]));
      if (phase == BoundaryCommSubset::all &&
          (nb.ni.type == NeighborConnect::face || nb.ni.type == NeighborConnect::edge)) {
        if ((nb.snb.level > mylevel) ||
            ((nb.snb.level == mylevel) && ((nb.ni.type == NeighborConnect::face)
                                           || ((nb.ni.type == NeighborConnect::edge)
                                               && (edge_flag_[nb.eid])))))
          MPI_Start(&(bd_var_flcor_.req_recv[nb.bufid]));
      }
    }
  }
#endif
  return;
}


void FaceCenteredBoundaryVariable::ClearBoundary(BoundaryCommSubset phase) {
  // Clear non-polar boundary communications
  for (int n=0; n < pmy_block_->pbval->nneighbor; n++) {
    NeighborBlock& nb = pmy_block_->pbval->neighbor[n];
    bd_var_.flag[nb.bufid] = BoundaryStatus::waiting;
    bd_var_.sflag[nb.bufid] = BoundaryStatus::waiting;
    if (((nb.ni.type == NeighborConnect::face) || (nb.ni.type == NeighborConnect::edge))
        && phase == BoundaryCommSubset::all) {
      bd_var_flcor_.flag[nb.bufid] = BoundaryStatus::waiting;
      bd_var_flcor_.sflag[nb.bufid] = BoundaryStatus::waiting;
    }
#ifdef MPI_PARALLEL
    MeshBlock *pmb = pmy_block_;
    int mylevel = pmb->loc.level;
    if (nb.snb.rank != Globals::my_rank && phase != BoundaryCommSubset::gr_amr) {
      // Wait for Isend
      MPI_Wait(&(bd_var_.req_send[nb.bufid]), MPI_STATUS_IGNORE);

      if (phase == BoundaryCommSubset::all) {
        if (nb.ni.type == NeighborConnect::face || nb.ni.type == NeighborConnect::edge) {
          if (nb.snb.level < mylevel)
            MPI_Wait(&(bd_var_flcor_.req_send[nb.bufid]), MPI_STATUS_IGNORE);
          else if ((nb.snb.level == mylevel)
                   && ((nb.ni.type == NeighborConnect::face)
                       || ((nb.ni.type == NeighborConnect::edge)
                           && (edge_flag_[nb.eid]))))
            MPI_Wait(&(bd_var_flcor_.req_send[nb.bufid]), MPI_STATUS_IGNORE);
        }
      }
    }
#endif
  }
}
}
