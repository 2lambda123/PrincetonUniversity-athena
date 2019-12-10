//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file mgbval_zerofixed.cpp
//  \brief 6x zero fixed boundary functions for Multigrid
//         The boundary values are fixed to zero on the cell surfaces
//         Note that these are test implementation and should work only with ngh=1


// C headers

// C++ headers

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../defs.hpp"

//----------------------------------------------------------------------------------------
//! \fn MGZeroFixedInnerX1(AthenaArray<Real> &dst, Real time, int nvar,
//                         int is, int ie, int js, int je, int ks, int ke, int ngh,
//                         Real x0, Real y0, Real z0, Real dx, Real dy, Real dz)
//  \brief Zero fixed boundary condition in the inner-X1 direction

void MGZeroFixedInnerX1(AthenaArray<Real> &dst, Real time, int nvar,
                        int is, int ie, int js, int je, int ks, int ke, int ngh,
                        Real x0, Real y0, Real z0, Real dx, Real dy, Real dz) {
  for (int n=0; n<nvar; n++) {
    for (int k=ks; k<=ke; k++) {
      for (int j=js; j<=je; j++) {
        for (int i=0; i<ngh; i++)
          dst(n,k,j,is-i-1) = -dst(n,k,j,is+i);
      }
    }
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn MGZeroFixedOuterX1(AthenaArray<Real> &dst, Real time, int nvar,
//                         int is, int ie, int js, int je, int ks, int ke, int ngh,
//                         Real x0, Real y0, Real z0, Real dx, Real dy, Real dz)
//  \brief Zero fixed boundary condition in the outer-X1 direction

void MGZeroFixedOuterX1(AthenaArray<Real> &dst, Real time, int nvar,
                        int is, int ie, int js, int je, int ks, int ke, int ngh,
                        Real x0, Real y0, Real z0, Real dx, Real dy, Real dz) {
  for (int n=0; n<nvar; n++) {
    for (int k=ks; k<=ke; k++) {
      for (int j=js; j<=je; j++) {
        for (int i=0; i<ngh; i++)
          dst(n,k,j,ie+i+1) = -dst(n,k,j,ie-i);
      }
    }
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn MGZeroFixedInnerX2(AthenaArray<Real> &dst, Real time, int nvar,
//                         int is, int ie, int js, int je, int ks, int ke, int ngh,
//                         Real x0, Real y0, Real z0, Real dx, Real dy, Real dz)
//  \brief Zero fixed boundary condition in the inner-X2 direction

void MGZeroFixedInnerX2(AthenaArray<Real> &dst, Real time, int nvar,
                        int is, int ie, int js, int je, int ks, int ke, int ngh,
                        Real x0, Real y0, Real z0, Real dx, Real dy, Real dz) {
  for (int n=0; n<nvar; n++) {
    for (int k=ks; k<=ke; k++) {
      for (int j=0; j<ngh; j++) {
        for (int i=is; i<=ie; i++)
          dst(n,k,js-j-1,i) = -dst(n,k,js+j,i);
      }
    }
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn MGZeroFixedOuterX2(AthenaArray<Real> &dst, Real time, int nvar,
//                         int is, int ie, int js, int je, int ks, int ke, int ngh,
//                         Real x0, Real y0, Real z0, Real dx, Real dy, Real dz)
//  \brief Zero fixed boundary condition in the outer-X2 direction

void MGZeroFixedOuterX2(AthenaArray<Real> &dst, Real time, int nvar,
                        int is, int ie, int js, int je, int ks, int ke, int ngh,
                        Real x0, Real y0, Real z0, Real dx, Real dy, Real dz) {
  for (int n=0; n<nvar; n++) {
    for (int k=ks; k<=ke; k++) {
      for (int j=0; j<ngh; j++) {
        for (int i=is; i<=ie; i++)
          dst(n,k,je+j+1,i) = -dst(n,k,je-j,i);
      }
    }
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn MGZeroFixedInnerX3(AthenaArray<Real> &dst, Real time, int nvar,
//                         int is, int ie, int js, int je, int ks, int ke, int ngh,
//                         Real x0, Real y0, Real z0, Real dx, Real dy, Real dz)
//  \brief Zero fixed boundary condition in the inner-X3 direction

void MGZeroFixedInnerX3(AthenaArray<Real> &dst, Real time, int nvar,
                        int is, int ie, int js, int je, int ks, int ke, int ngh,
                        Real x0, Real y0, Real z0, Real dx, Real dy, Real dz) {
  for (int n=0; n<nvar; n++) {
    for (int k=0; k<ngh; k++) {
      for (int j=js; j<=je; j++) {
        for (int i=is; i<=ie; i++)
          dst(n,ks-k-1,j,i) = -dst(n,ks+k,j,i);
      }
    }
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn MGZeroFixedOuterX3(AthenaArray<Real> &dst, Real time, int nvar,
//                         int is, int ie, int js, int je, int ks, int ke, int ngh,
//                         Real x0, Real y0, Real z0, Real dx, Real dy, Real dz)
//  \brief Zero fixed boundary condition in the outer-X3 direction

void MGZeroFixedOuterX3(AthenaArray<Real> &dst, Real time, int nvar,
                        int is, int ie, int js, int je, int ks, int ke, int ngh,
                        Real x0, Real y0, Real z0, Real dx, Real dy, Real dz) {
  for (int n=0; n<nvar; n++) {
    for (int k=0; k<ngh; k++) {
      for (int j=js; j<=je; j++) {
        for (int i=is; i<=ie; i++)
          dst(n,ke+k+1,j,i) = -dst(n,ke-k,j,i);
      }
    }
  }
  return;
}


