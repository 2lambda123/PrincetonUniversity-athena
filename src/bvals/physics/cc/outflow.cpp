//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file outflow.cpp
//  \brief implementation of outflow BCs in each dimension for cell-centered AthenaArray

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../mesh/mesh.hpp"
#include "bvals.hpp"

//----------------------------------------------------------------------------------------
//! \fn void CellCenteredBoundaryVariable::OutflowInnerX1(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
//                          AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
//                          int kl, int ku, int ngh) {
//  \brief OUTFLOW boundary conditions, inner x1 boundary

void CellCenteredBoundaryVariable::OutflowInnerX1(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                    AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
                    int kl, int ku, int ngh) {
  // copy hydro variables into ghost zones
  for (int n=0; n<=nu_; ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma omp simd
        for (int i=0; i<=ngh; ++i) {
          var_cc(n,k,j,il-i) = var_cc(n,k,j,il);
        }
      }
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void CellCenteredBoundaryVariable::OutflowOuterX1(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
//                          AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
//                          int kl, int ku, int ngh) {
//  \brief OUTFLOW boundary conditions, outer x1 boundary

void CellCenteredBoundaryVariable::OutflowOuterX1(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                    AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
                    int kl, int ku, int ngh) {
  // copy hydro variables into ghost zones
  for (int n=0; n<=nu_; ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma omp simd
        for (int i=0; i<=ngh; ++i) {
          var_cc(n,k,j,iu+i) = var_cc(n,k,j,iu);
        }
      }
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void CellCenteredBoundaryVariable::OutflowInnerX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
//                          AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
//                          int kl, int ku, int ngh) {
//  \brief OUTFLOW boundary conditions, inner x2 boundary

void CellCenteredBoundaryVariable::OutflowInnerX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                    AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
                    int kl, int ku, int ngh) {
  // copy hydro variables into ghost zones
  for (int n=0; n<=nu_; ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=0; j<=ngh; ++j) {
#pragma omp simd
        for (int i=il; i<=iu; ++i) {
          var_cc(n,k,jl-j,i) = var_cc(n,k,jl,i);
        }
      }}
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void CellCenteredBoundaryVariable::OutflowOuterX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
//                          AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
//                          int kl, int ku, int ngh) {
//  \brief OUTFLOW boundary conditions, outer x2 boundary

void CellCenteredBoundaryVariable::OutflowOuterX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                    AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
                    int kl, int ku, int ngh) {
  // copy hydro variables into ghost zones
  for (int n=0; n<=nu_; ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=0; j<=ngh; ++j) {
#pragma omp simd
        for (int i=il; i<=iu; ++i) {
          var_cc(n,k,ju+j,i) = var_cc(n,k,ju,i);
        }
      }
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void CellCenteredBoundaryVariable::OutflowInnerX3(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
//                          AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
//                          int kl, int ku, int ngh) {
//  \brief OUTFLOW boundary conditions, inner x3 boundary

void CellCenteredBoundaryVariable::OutflowInnerX3(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                    AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
                    int kl, int ku, int ngh) {
  // copy hydro variables into ghost zones
  for (int n=0; n<=nu_; ++n) {
    for (int k=0; k<=ngh; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma omp simd
        for (int i=il; i<=iu; ++i) {
          var_cc(n,kl-k,j,i) = var_cc(n,kl,j,i);
        }
      }
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void CellCenteredBoundaryVariable::OutflowOuterX3(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
//                          AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
//                          int kl, int ku, int ngh) {
//  \brief OUTFLOW boundary conditions, outer x3 boundary

void CellCenteredBoundaryVariable::OutflowOuterX3(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                    AthenaArray<Real> &arr_cc, int il, int iu, int jl, int ju,
                    int kl, int ku, int ngh) {
  // copy hydro variables into ghost zones
  for (int n=0; n<=nu_; ++n) {
    for (int k=0; k<=ngh; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma omp simd
        for (int i=il; i<=iu; ++i) {
          var_cc(n,ku+k,j,i) = var_cc(n,ku,j,i);
        }
      }
    }
  }
  return;
}
