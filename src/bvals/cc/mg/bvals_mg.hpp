#ifndef BVALS_CC_MG_BVALS_MG_HPP_
#define BVALS_CC_MG_BVALS_MG_HPP_
//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file bvals_mg.hpp
//  \brief defines MGBoundaryValues class

// C headers

// C++ headers
#include <string>   // string

// Athena++ headers
#include "../../../athena.hpp"
#include "../../../athena_arrays.hpp"
#include "../../bvals.hpp"

// MPI headers
#ifdef MPI_PARALLEL
#include <mpi.h>
#endif

// forward declarations
class Multigrid;
class MeshBlock;
class MeshBlockTree;
class ParameterInput;
class Coordinates;

//----------------------------------------------------------------------------------------
//! \class MGBoundaryValues
//  \brief BVals data and functions

class MGBoundaryValues : public BoundaryBase {
 public:
  MGBoundaryValues(Multigrid *pmg, BoundaryFlag *input_bcs);
  ~MGBoundaryValues();

  void InitBoundaryData(BoundaryData<> &bd, BoundaryQuantity type);
  void DestroyBoundaryData(BoundaryData<> &bd);

  void ApplyPhysicalBoundaries();
  void StartReceivingMultigrid(int nc, BoundaryQuantity type);
  void ClearBoundaryMultigrid(BoundaryQuantity type);
  int LoadMultigridBoundaryBufferSameLevel(
      AthenaArray<Real> &src,
      int nvar, int nc, int ngh, Real *buf, const NeighborBlock& nb);
  bool SendMultigridBoundaryBuffers(AthenaArray<Real> &src,
                                    int nc, BoundaryQuantity type);
  void SetMultigridBoundarySameLevel(
      AthenaArray<Real> &dst,
      int nvar, int nc, int ngh, Real *buf, const NeighborBlock& nb);
  bool ReceiveMultigridBoundaryBuffers(AthenaArray<Real> &dst,
                                       int nc, BoundaryQuantity type);
  void CopyNeighborInfoFromMeshBlock();

 private:
  Multigrid *pmy_mg_;
  MGBoundaryFunc MGBoundaryFunction_[6];
  BoundaryData<> bd_mggrav_;

#ifdef MPI_PARALLEL
  MPI_Comm mgcomm_;
  int mg_grav_phys_id_; // for now, MPI assumes MGGravityDriver is associated driver
#endif

  friend class Multigrid;
};

#endif // BVALS_CC_MG_BVALS_MG_HPP_
