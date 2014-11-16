//======================================================================================
// Athena++ astrophysical MHD code
// Copyright (C) 2014 James M. Stone  <jmstone@princeton.edu>
//
// This program is free software: you can redistribute and/or modify it under the terms
// of the GNU General Public License (GPL) as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of GNU GPL in the file LICENSE included in the code
// distribution.  If not see <http://www.gnu.org/licenses/>.
//======================================================================================

// Primary header
#include "../field.hpp"    // Field
#include "field_integrator.hpp"

// C++ headers
#include <algorithm>  // max(), min()

// Athena headers
#include "../../athena.hpp"         // enums, macros, Real
#include "../../athena_arrays.hpp"  // AthenaArray
#include "../../mesh.hpp"           // MeshBlock
#include "../../fluid/fluid.hpp"    // Fluid

//======================================================================================
//! \file corner_emf.cpp
//  \brief
//======================================================================================

//--------------------------------------------------------------------------------------
//! \fn  void FieldIntegrator::ComputeCornerEMFs
//  \brief

void FieldIntegrator::ComputeCornerEMFs(MeshBlock *pmb)
{
  int is = pmb->is; int js = pmb->js; int ks = pmb->ks;
  int ie = pmb->ie; int je = pmb->je; int ke = pmb->ke;

  AthenaArray<Real> w = pmb->pfluid->w.ShallowCopy();
  AthenaArray<Real> bcc = pmb->pfield->bcc.ShallowCopy();

  AthenaArray<Real> emf1 = pmb->pfield->emf1.ShallowCopy();
  AthenaArray<Real> emf2 = pmb->pfield->emf2.ShallowCopy();
  AthenaArray<Real> emf3 = pmb->pfield->emf3.ShallowCopy();
  AthenaArray<Real> e_x1f = pmb->pfield->e.x1f.ShallowCopy();
  AthenaArray<Real> e_x2f = pmb->pfield->e.x2f.ShallowCopy();
  AthenaArray<Real> e_x3f = pmb->pfield->e.x3f.ShallowCopy();
  AthenaArray<Real> w_x1f = pmb->pfield->wght.x1f.ShallowCopy();
  AthenaArray<Real> w_x2f = pmb->pfield->wght.x2f.ShallowCopy();
  AthenaArray<Real> w_x3f = pmb->pfield->wght.x3f.ShallowCopy();
 
//---- 1-D update:
//  copy face-centered E-fields to edges and return.

  if (pmb->block_size.nx2 == 1) {
    for (int i=is; i<=ie+1; ++i) {
      emf2(ks  ,js,i) = e_x1f(X1E2,ks,js,i);
      emf2(ks+1,js,i) = e_x1f(X1E2,ks,js,i);
      emf3(ks,js  ,i) = e_x1f(X1E3,ks,js,i);
      emf3(ks,js+1,i) = e_x1f(X1E3,ks,js,i);
    }
    return;
  }

//---- 2-D/3-D update:
// compute cell-centered E3

  for (int k=ks; k<=ke; ++k) {
  for (int j=js; j<=je+1; ++j) {
    for (int i=is; i<=ie+1; ++i) {
      cc_emf3_(k,j,i) = w(IVX,k,j,i)*bcc(IB2,k,j,i) - w(IVY,k,j,i)*bcc(IB1,k,j,i);
    }
  }}

// integrate E3 to corner using SG07

  for (int k=ks; k<=ke; ++k) {
  for (int j=js; j<=je+1; ++j) {
    for (int i=is; i<=ie+1; ++i) {
      Real de3_l2 = (1.0-w_x2f(k,j,i))*(e_x2f(X2E3,k,j,i  ) - cc_emf3_(k,j-1,i  )) +
                    (    w_x2f(k,j,i))*(e_x2f(X2E3,k,j,i-1) - cc_emf3_(k,j-1,i-1));

      Real de3_r2 = (1.0-w_x2f(k,j,i))*(e_x2f(X2E3,k,j,i  ) - cc_emf3_(k,j,i  )) +
                    (    w_x2f(k,j,i))*(e_x2f(X2E3,k,j,i-1) - cc_emf3_(k,j,i-1));

      Real de3_l1 = (1.0-w_x1f(k,j,i))*(e_x1f(X1E3,k,j  ,i) - cc_emf3_(k,j  ,i-1)) +
                    (    w_x1f(k,j,i))*(e_x1f(X1E3,k,j-1,i) - cc_emf3_(k,j-1,i-1));

      Real de3_r1 = (1.0-w_x1f(k,j,i))*(e_x1f(X1E3,k,j  ,i) - cc_emf3_(k,j  ,i)) +
                    (    w_x1f(k,j,i))*(e_x1f(X1E3,k,j-1,i) - cc_emf3_(k,j-1,i));

      emf3(k,j,i) = 0.25*(de3_l1 + de3_r1 + de3_l2 + de3_r2 +
        e_x2f(X2E3,k,j,i-1) +e_x2f(X2E3,k,j,i) -e_x1f(X1E3,k,j-1,i) -e_x1f(X1E3,k,j,i));
    }
  }}

// for 2D: copy E1 and E2 to edges and return

  if (pmb->block_size.nx3 == 1) {
    for (int j=js; j<=je; ++j) {
    for (int i=is; i<=ie+1; ++i) {
      emf2(ks  ,j,i) = e_x1f(X1E2,ks,j,i);
      emf2(ks+1,j,i) = e_x1f(X1E2,ks,j,i);
    }}
    for (int j=js; j<=je+1; ++j) {
    for (int i=is; i<=ie; ++i) {
      emf1(ks  ,j,i) = e_x2f(X2E1,ks,j,i);
      emf1(ks+1,j,i) = e_x2f(X2E1,ks,j,i);
    }}
    return;
  }

//---- 3-D update:
// integrate E1 and E2 to corners (E3 already done above)


  return;
}
