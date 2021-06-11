//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file mesh_refinement.cpp
//  \brief implements functions for static/adaptive mesh refinement

// C headers

// C++ headers
#include <algorithm>   // max()
#include <cmath>
#include <cstring>     // strcmp()
#include <sstream>
#include <stdexcept>  // runtime_error
#include <string>
#include <tuple>

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../coordinates/coordinates.hpp"
#include "../field/field.hpp"
#include "../globals.hpp"
#include "../hydro/hydro.hpp"
#include "../parameter_input.hpp"
#include "mesh.hpp"
#include "mesh_refinement.hpp"

#include "../utils/interp_univariate.hpp"

//----------------------------------------------------------------------------------------
//! \fn MeshRefinement::MeshRefinement(MeshBlock *pmb, ParameterInput *pin)
//  \brief constructor

MeshRefinement::MeshRefinement(MeshBlock *pmb, ParameterInput *pin) :
    pmy_block_(pmb), deref_count_(0),
    deref_threshold_(pin->GetOrAddInteger("mesh", "derefine_count", 10)),
    AMRFlag_(pmb->pmy_mesh->AMRFlag_) {

  // Create coarse mesh object for parent grid
  if (std::strcmp(COORDINATE_SYSTEM, "cartesian") == 0) {
    pcoarsec = new Cartesian(pmb, pin, true);
  } else if (std::strcmp(COORDINATE_SYSTEM, "cylindrical") == 0) {
    pcoarsec = new Cylindrical(pmb, pin, true);
  } else if (std::strcmp(COORDINATE_SYSTEM, "spherical_polar") == 0) {
    pcoarsec = new SphericalPolar(pmb, pin, true);
  } else if (std::strcmp(COORDINATE_SYSTEM, "minkowski") == 0) {
    pcoarsec = new Minkowski(pmb, pin, true);
  } else if (std::strcmp(COORDINATE_SYSTEM, "schwarzschild") == 0) {
    pcoarsec = new Schwarzschild(pmb, pin, true);
  } else if (std::strcmp(COORDINATE_SYSTEM, "kerr-schild") == 0) {
    pcoarsec = new KerrSchild(pmb, pin, true);
  } else if (std::strcmp(COORDINATE_SYSTEM, "gr_user") == 0) {
    pcoarsec = new GRUser(pmb, pin, true);
  }

  int nc1 = pmb->ncells1;
  fvol_[0][0].NewAthenaArray(nc1);
  fvol_[0][1].NewAthenaArray(nc1);
  fvol_[1][0].NewAthenaArray(nc1);
  fvol_[1][1].NewAthenaArray(nc1);
  sarea_x1_[0][0].NewAthenaArray(nc1+1);
  sarea_x1_[0][1].NewAthenaArray(nc1+1);
  sarea_x1_[1][0].NewAthenaArray(nc1+1);
  sarea_x1_[1][1].NewAthenaArray(nc1+1);
  sarea_x2_[0][0].NewAthenaArray(nc1);
  sarea_x2_[0][1].NewAthenaArray(nc1);
  sarea_x2_[0][2].NewAthenaArray(nc1);
  sarea_x2_[1][0].NewAthenaArray(nc1);
  sarea_x2_[1][1].NewAthenaArray(nc1);
  sarea_x2_[1][2].NewAthenaArray(nc1);
  sarea_x3_[0][0].NewAthenaArray(nc1);
  sarea_x3_[0][1].NewAthenaArray(nc1);
  sarea_x3_[1][0].NewAthenaArray(nc1);
  sarea_x3_[1][1].NewAthenaArray(nc1);
  sarea_x3_[2][0].NewAthenaArray(nc1);
  sarea_x3_[2][1].NewAthenaArray(nc1);

  // KGF: probably don't need to preallocate space for pointers in these vectors
  pvars_cc_.reserve(3);
  pvars_fc_.reserve(3);
  pvars_vc_.reserve(3);
}


//----------------------------------------------------------------------------------------
//! \fn MeshRefinement::~MeshRefinement()
//  \brief destructor

MeshRefinement::~MeshRefinement() {
  delete pcoarsec;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::RestrictCellCenteredValues(const AthenaArray<Real> &fine,
//                           AthenaArray<Real> &coarse, int sn, int en,
//                           int csi, int cei, int csj, int cej, int csk, int cek)
//  \brief restrict cell centered values

void MeshRefinement::RestrictCellCenteredValues(
    const AthenaArray<Real> &fine, AthenaArray<Real> &coarse, int sn, int en,
    int csi, int cei, int csj, int cej, int csk, int cek) {

  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  int si = (csi - pmb->cis)*2 + pmb->is, ei = (cei - pmb->cis)*2 + pmb->is + 1;

  // store the restricted data in the prolongation buffer for later use
  if (pmb->block_size.nx3>1) { // 3D
    for (int n=sn; n<=en; ++n) {
      for (int ck=csk; ck<=cek; ck++) {
        int k = (ck - pmb->cks)*2 + pmb->ks;
        for (int cj=csj; cj<=cej; cj++) {
          int j = (cj - pmb->cjs)*2 + pmb->js;
          pco->CellVolume(k,j,si,ei,fvol_[0][0]);
          pco->CellVolume(k,j+1,si,ei,fvol_[0][1]);
          pco->CellVolume(k+1,j,si,ei,fvol_[1][0]);
          pco->CellVolume(k+1,j+1,si,ei,fvol_[1][1]);
          for (int ci=csi; ci<=cei; ci++) {
            int i = (ci - pmb->cis)*2 + pmb->is;
            // KGF: add the off-centered quantities first to preserve FP symmetry
            Real tvol = ((fvol_[0][0](i) + fvol_[0][1](i))
                         + (fvol_[0][0](i+1) + fvol_[0][1](i+1)))
                        + ((fvol_[1][0](i) + fvol_[1][1](i))
                           + (fvol_[1][0](i+1) + fvol_[1][1](i+1)));
            // KGF: add the off-centered quantities first to preserve FP symmetry
            coarse(n,ck,cj,ci) =
                (((fine(n,k  ,j  ,i)*fvol_[0][0](i) + fine(n,k  ,j+1,i)*fvol_[0][1](i))
                  + (fine(n,k  ,j  ,i+1)*fvol_[0][0](i+1) +
                     fine(n,k  ,j+1,i+1)*fvol_[0][1](i+1)))
                 + ((fine(n,k+1,j  ,i)*fvol_[1][0](i) + fine(n,k+1,j+1,i)*fvol_[1][1](i))
                    + (fine(n,k+1,j  ,i+1)*fvol_[1][0](i+1) +
                       fine(n,k+1,j+1,i+1)*fvol_[1][1](i+1)))) / tvol;
          }
        }
      }
    }
  } else if (pmb->block_size.nx2>1) { // 2D
    for (int n=sn; n<=en; ++n) {
      for (int cj=csj; cj<=cej; cj++) {
        int j = (cj - pmb->cjs)*2 + pmb->js;
        pco->CellVolume(0,j  ,si,ei,fvol_[0][0]);
        pco->CellVolume(0,j+1,si,ei,fvol_[0][1]);
        for (int ci=csi; ci<=cei; ci++) {
          int i = (ci - pmb->cis)*2 + pmb->is;
          // KGF: add the off-centered quantities first to preserve FP symmetry
          Real tvol = (fvol_[0][0](i) + fvol_[0][1](i)) +
                      (fvol_[0][0](i+1) + fvol_[0][1](i+1));

          // KGF: add the off-centered quantities first to preserve FP symmetry
          coarse(n,0,cj,ci) =
              ((fine(n,0,j  ,i)*fvol_[0][0](i) + fine(n,0,j+1,i)*fvol_[0][1](i))
               + (fine(n,0,j ,i+1)*fvol_[0][0](i+1) + fine(n,0,j+1,i+1)*fvol_[0][1](i+1)))
              /tvol;
        }
      }
    }
  } else { // 1D
    // printf("1d_restr");
    int j = pmb->js, cj = pmb->cjs, k = pmb->ks, ck = pmb->cks;
    for (int n=sn; n<=en; ++n) {
      pco->CellVolume(k,j,si,ei,fvol_[0][0]);
      for (int ci=csi; ci<=cei; ci++) {
        int i = (ci - pmb->cis)*2 + pmb->is;
        Real tvol = fvol_[0][0](i) + fvol_[0][0](i+1);
        coarse(n,ck,cj,ci)
          = (fine(n,k,j,i)*fvol_[0][0](i) + fine(n,k,j,i+1)*fvol_[0][0](i+1))/tvol;

      }
    }
  }
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::RestrictFieldX1(const AthenaArray<Real> &fine
//      AthenaArray<Real> &coarse, int csi, int cei, int csj, int cej, int csk, int cek)
//  \brief restrict the x1 field data and set them into the coarse buffer

void MeshRefinement::RestrictFieldX1(
    const AthenaArray<Real> &fine, AthenaArray<Real> &coarse,
    int csi, int cei, int csj, int cej, int csk, int cek) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  int si = (csi - pmb->cis)*2 + pmb->is, ei = (cei - pmb->cis)*2 + pmb->is;

  // store the restricted data in the prolongation buffer for later use
  if (pmb->block_size.nx3>1) { // 3D
    for (int ck=csk; ck<=cek; ck++) {
      int k = (ck - pmb->cks)*2 + pmb->ks;
      for (int cj=csj; cj<=cej; cj++) {
        int j = (cj - pmb->cjs)*2 + pmb->js;
        pco->Face1Area(k,   j,   si, ei, sarea_x1_[0][0]);
        pco->Face1Area(k,   j+1, si, ei, sarea_x1_[0][1]);
        pco->Face1Area(k+1, j,   si, ei, sarea_x1_[1][0]);
        pco->Face1Area(k+1, j+1, si, ei, sarea_x1_[1][1]);
        for (int ci=csi; ci<=cei; ci++) {
          int i = (ci - pmb->cis)*2 + pmb->is;
          Real tarea = sarea_x1_[0][0](i) + sarea_x1_[0][1](i) +
                       sarea_x1_[1][0](i) + sarea_x1_[1][1](i);
          coarse(ck,cj,ci) =
              (fine(k  ,j,i)*sarea_x1_[0][0](i) + fine(k  ,j+1,i)*sarea_x1_[0][1](i)
               + fine(k+1,j,i)*sarea_x1_[1][0](i) + fine(k+1,j+1,i)*sarea_x1_[1][1](i)
               )/tarea;
        }
      }
    }
  } else if (pmb->block_size.nx2>1) { // 2D
    int k = pmb->ks;
    for (int cj=csj; cj<=cej; cj++) {
      int j = (cj - pmb->cjs)*2 + pmb->js;
      pco->Face1Area(k,  j,   si, ei, sarea_x1_[0][0]);
      pco->Face1Area(k,  j+1, si, ei, sarea_x1_[0][1]);
      for (int ci=csi; ci<=cei; ci++) {
        int i = (ci - pmb->cis)*2 + pmb->is;
        Real tarea = sarea_x1_[0][0](i) + sarea_x1_[0][1](i);
        coarse(csk,cj,ci) =
            (fine(k,j,i)*sarea_x1_[0][0](i) + fine(k,j+1,i)*sarea_x1_[0][1](i))/tarea;
      }
    }
  } else { // 1D - no restriction, just copy
    for (int ci=csi; ci<=cei; ci++) {
      int i = (ci - pmb->cis)*2 + pmb->is;
      coarse(csk,csj,ci) = fine(pmb->ks,pmb->js,i);
    }
  }

  return;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::RestrictFieldX2(const AthenaArray<Real> &fine
//      AthenaArray<Real> &coarse, int csi, int cei, int csj, int cej, int csk, int cek)
//  \brief restrict the x2 field data and set them into the coarse buffer

void MeshRefinement::RestrictFieldX2(
    const AthenaArray<Real> &fine, AthenaArray<Real> &coarse,
    int csi, int cei, int csj, int cej, int csk, int cek) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  int si = (csi - pmb->cis)*2 + pmb->is, ei = (cei - pmb->cis)*2 + pmb->is + 1;

  // store the restricted data in the prolongation buffer for later use
  if (pmb->block_size.nx3>1) { // 3D
    for (int ck=csk; ck<=cek; ck++) {
      int k = (ck - pmb->cks)*2 + pmb->ks;
      for (int cj=csj; cj<=cej; cj++) {
        int j = (cj - pmb->cjs)*2 + pmb->js;
        bool pole = pco->IsPole(j);
        if (!pole) {
          pco->Face2Area(k,   j,  si, ei, sarea_x2_[0][0]);
          pco->Face2Area(k+1, j,  si, ei, sarea_x2_[1][0]);
        } else {
          for (int ci = csi; ci <= cei; ++ci) {
            int i = (ci - pmb->cis) * 2 + pmb->is;
            sarea_x2_[0][0](i) = pco->dx1f(i);
            sarea_x2_[1][0](i) = pco->dx1f(i);
          }
        }
        for (int ci=csi; ci<=cei; ci++) {
          int i = (ci - pmb->cis)*2 + pmb->is;
          Real tarea = sarea_x2_[0][0](i) + sarea_x2_[0][0](i+1) +
                       sarea_x2_[1][0](i) + sarea_x2_[1][0](i+1);
          coarse(ck,cj,ci) =
              (fine(k  ,j,i)*sarea_x2_[0][0](i) + fine(k  ,j,i+1)*sarea_x2_[0][0](i+1)
               +fine(k+1,j,i)*sarea_x2_[1][0](i) + fine(k+1,j,i+1)*sarea_x2_[1][0](i+1))
              /tarea;
        }
      }
    }
  } else if (pmb->block_size.nx2>1) { // 2D
    int k = pmb->ks;
    for (int cj=csj; cj<=cej; cj++) {
      int j = (cj - pmb->cjs)*2 + pmb->js;
      bool pole = pco->IsPole(j);
      if (!pole) {
        pco->Face2Area(k, j, si, ei, sarea_x2_[0][0]);
      } else {
        for (int ci = csi; ci <= cei; ++ci) {
          int i = (ci - pmb->cis) * 2 + pmb->is;
          sarea_x2_[0][0](i) = pco->dx1f(i);
        }
      }
      for (int ci=csi; ci<=cei; ci++) {
        int i = (ci - pmb->cis)*2 + pmb->is;
        Real tarea = sarea_x2_[0][0](i) + sarea_x2_[0][0](i+1);
        coarse(pmb->cks,cj,ci) =
            (fine(k,j,i)*sarea_x2_[0][0](i) + fine(k,j,i+1)*sarea_x2_[0][0](i+1))/tarea;
      }
    }
  } else { // 1D
    int k = pmb->ks, j = pmb->js;
    pco->Face2Area(k, j, si, ei, sarea_x2_[0][0]);
    for (int ci=csi; ci<=cei; ci++) {
      int i = (ci - pmb->cis)*2 + pmb->is;
      Real tarea = sarea_x2_[0][0](i) + sarea_x2_[0][0](i+1);
      coarse(pmb->cks,pmb->cjs,ci) =
          (fine(k,j,i)*sarea_x2_[0][0](i) + fine(k,j,i+1)*sarea_x2_[0][0](i+1))/tarea;
    }
  }

  return;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::RestrictFieldX3(const AthenaArray<Real> &fine
//      AthenaArray<Real> &coarse, int csi, int cei, int csj, int cej, int csk, int cek)
//  \brief restrict the x3 field data and set them into the coarse buffer

void MeshRefinement::RestrictFieldX3(
    const AthenaArray<Real> &fine, AthenaArray<Real> &coarse,
    int csi, int cei, int csj, int cej, int csk, int cek) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  int si = (csi - pmb->cis)*2 + pmb->is, ei = (cei - pmb->cis)*2 + pmb->is + 1;

  // store the restricted data in the prolongation buffer for later use
  if (pmb->block_size.nx3>1) { // 3D
    for (int ck=csk; ck<=cek; ck++) {
      int k = (ck - pmb->cks)*2 + pmb->ks;
      for (int cj=csj; cj<=cej; cj++) {
        int j = (cj - pmb->cjs)*2 + pmb->js;
        pco->Face3Area(k,   j,  si, ei, sarea_x3_[0][0]);
        pco->Face3Area(k, j+1,  si, ei, sarea_x3_[0][1]);
        for (int ci=csi; ci<=cei; ci++) {
          int i = (ci - pmb->cis)*2 + pmb->is;
          Real tarea = sarea_x3_[0][0](i) + sarea_x3_[0][0](i+1) +
                       sarea_x3_[0][1](i) + sarea_x3_[0][1](i+1);
          coarse(ck,cj,ci)  =
              (fine(k,j  ,i)*sarea_x3_[0][0](i) + fine(k,j  ,i+1)*sarea_x3_[0][0](i+1)
               + fine(k,j+1,i)*sarea_x3_[0][1](i) + fine(k,j+1,i+1)*sarea_x3_[0][1](i+1)
               ) /tarea;
        }
      }
    }
  } else if (pmb->block_size.nx2>1) { // 2D
    int k = pmb->ks;
    for (int cj=csj; cj<=cej; cj++) {
      int j = (cj - pmb->cjs)*2 + pmb->js;
      pco->Face3Area(k,   j, si, ei, sarea_x3_[0][0]);
      pco->Face3Area(k, j+1, si, ei, sarea_x3_[0][1]);
      for (int ci=csi; ci<=cei; ci++) {
        int i = (ci - pmb->cis)*2 + pmb->is;
        Real tarea = sarea_x3_[0][0](i) + sarea_x3_[0][0](i+1) +
                     sarea_x3_[0][1](i) + sarea_x3_[0][1](i+1);
        coarse(pmb->cks,cj,ci) =
            (fine(k,j  ,i)*sarea_x3_[0][0](i) + fine(k,j  ,i+1)*sarea_x3_[0][0](i+1)
             + fine(k,j+1,i)*sarea_x3_[0][1](i) + fine(k,j+1,i+1)*sarea_x3_[0][1](i+1)
             ) /tarea;
      }
    }
  } else { // 1D
    int k = pmb->ks, j = pmb->js;
    pco->Face3Area(k, j, si, ei, sarea_x3_[0][0]);
    for (int ci=csi; ci<=cei; ci++) {
      int i = (ci - pmb->cis)*2 + pmb->is;
      Real tarea = sarea_x3_[0][0](i) + sarea_x3_[0][0](i+1);
      coarse(pmb->cks,pmb->cjs,ci) =
          (fine(k,j,i)*sarea_x3_[0][0](i) + fine(k,j,i+1)*sarea_x3_[0][0](i+1))/tarea;
    }
  }

  return;
}

//----------------------------------------------------------------------------------------
//! \fn inline void MeshRefinement::RestrictVertexCenteredIndicialHelper(...)
//  \brief De-duplicate some indicial logic
inline void MeshRefinement::RestrictVertexCenteredIndicialHelper(
  int ix,
  int ix_cvs, int ix_cve,
  int ix_vs, int ix_ve,
  int &f_ix) {

  // map for fine-index
  if (ix < ix_cvs) {
    f_ix = ix_vs - 2 * (ix_cvs - ix);
  } else if (ix > ix_cve) {
    f_ix = ix_ve + 2 * (ix - ix_cve);
  } else { // map to interior+boundary nodes
    f_ix = 2 * (ix - ix_cvs) + ix_vs;
  }

  return;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::RestrictVertexCenteredValues(const AthenaArray<Real> &fine,
//                           AthenaArray<Real> &coarse, int sn, int en,
//                           int csi, int cei, int csj, int cej, int csk, int cek)
//  \brief restrict vertex centered values

void MeshRefinement::RestrictVertexCenteredValues(
    const AthenaArray<Real> &fine, AthenaArray<Real> &coarse, int sn, int en,
    int csi, int cei, int csj, int cej, int csk, int cek) {
  MeshBlock *pmb = pmy_block_;

  // store the restricted data in the prolongation buffer for later use
  if (pmb->block_size.nx3 > 1) {
    int k, j, i;
    for (int n=sn; n<=en; ++n){
      for (int ck=csk; ck<=cek; ck++) {
        // int k = (ck - pmb->ckvs)*2 + pmb->kvs;
        RestrictVertexCenteredIndicialHelper(ck, pmb->ckvs, pmb->ckve,
                                             pmb->kvs, pmb->kve, k);
        for (int cj=csj; cj<=cej; cj++) {
          // int j = (cj - pmb->cjvs)*2 + pmb->jvs;
          RestrictVertexCenteredIndicialHelper(cj, pmb->cjvs, pmb->cjve,
                                              pmb->jvs, pmb->jve, j);
          for (int ci=csi; ci<=cei; ci++) {
            // int i = (ci - pmb->civs)*2 + pmb->ivs;
            RestrictVertexCenteredIndicialHelper(ci, pmb->civs, pmb->cive,
                                                pmb->ivs, pmb->ive, i);
            coarse(n, ck, cj, ci) = fine(n, k, j, i);
          }
        }
      }
    }
  } else if (pmb->block_size.nx2 > 1) {
    int k = pmb->kvs, ck = pmb->ckvs;
    int j, i;

    for (int n=sn; n<=en; ++n){
      for (int cj=csj; cj<=cej; cj++) {
        // int j = (cj - pmb->cjvs)*2 + pmb->jvs;
        RestrictVertexCenteredIndicialHelper(cj, pmb->cjvs, pmb->cjve,
                                             pmb->jvs, pmb->jve, j);
        for (int ci=csi; ci<=cei; ci++) {
          // int i = (ci - pmb->civs)*2 + pmb->ivs;
          RestrictVertexCenteredIndicialHelper(ci, pmb->civs, pmb->cive,
                                               pmb->ivs, pmb->ive, i);
          coarse(n, ck, cj, ci) = fine(n, k, j, i);
        }
      }
    }
  } else {
    int j = pmb->jvs, cj = pmb->cjvs, k = pmb->kvs, ck = pmb->ckvs;
    int i;
    for (int n=sn; n<=en; ++n) {
      for (int ci=csi; ci<=cei; ci++) {
        // int i = (ci - pmb->civs)*2 + pmb->ivs;
        RestrictVertexCenteredIndicialHelper(ci, pmb->civs, pmb->cive,
                                              pmb->ivs, pmb->ive, i);
        coarse(n, ck, cj, ci) = fine(n, k, j, i);
      }
    }

  }



}

void MeshRefinement::RestrictTwiceToBufferVertexCenteredValues(
    const AthenaArray<Real> &fine,
    Real *buf,
    int sn, int en,
    int csi, int cei, int csj, int cej, int csk, int cek,
    int &offset) {
  MeshBlock *pmb = pmy_block_;
  // Coordinates *pco = pmb->pcoord;

  // store the restricted data within input buffer
  if (pmb->block_size.nx3 > 1) { // 3D
    int k, j, i;
    for (int n=sn; n<=en; ++n){
      for (int ck=csk; ck<=cek; ck+=2) {
        // int k = (ck - pmb->ckvs)*2 + pmb->kvs;
        RestrictVertexCenteredIndicialHelper(ck, pmb->ckvs, pmb->ckve,
                                             pmb->kvs, pmb->kve, k);
        for (int cj=csj; cj<=cej; cj+=2) {
          // int j = (cj - pmb->cjvs)*2 + pmb->jvs;
          RestrictVertexCenteredIndicialHelper(cj, pmb->cjvs, pmb->cjve,
                                              pmb->jvs, pmb->jve, j);
          for (int ci=csi; ci<=cei; ci+=2) {
            // int i = (ci - pmb->civs)*2 + pmb->ivs;
            RestrictVertexCenteredIndicialHelper(ci, pmb->civs, pmb->cive,
                                                pmb->ivs, pmb->ive, i);
            buf[offset++] = fine(n, k, j, i);
          }
        }
      }
    }

  } else if (pmb->block_size.nx2 > 1) { // 2D
    int k = pmb->kvs;
    int j, i;
    for (int n=sn; n<=en; ++n){
      for (int cj=csj; cj<=cej; cj+=2) {
        // int j = (cj - pmb->cjvs)*2 + pmb->jvs;
        RestrictVertexCenteredIndicialHelper(cj, pmb->cjvs, pmb->cjve,
                                             pmb->jvs, pmb->jve, j);
        for (int ci=csi; ci<=cei; ci+=2) {
          // int i = (ci - pmb->civs)*2 + pmb->ivs;
          RestrictVertexCenteredIndicialHelper(ci, pmb->civs, pmb->cive,
                                               pmb->ivs, pmb->ive, i);
          buf[offset++] = fine(n, k, j, i);
        }
      }
    }

  } else { // 1D
    int j = pmb->jvs, k = pmb->kvs;
    int i;
    for (int n=sn; n<=en; ++n) {
      for (int ci=csi; ci<=cei; ci+=2) {
        // int i = (ci - pmb->civs)*2 + pmb->ivs;
        RestrictVertexCenteredIndicialHelper(ci, pmb->civs, pmb->cive,
                                             pmb->ivs, pmb->ive, i);
        buf[offset++] = fine(n, k, j, i);
      }
    }

  }


  return;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::ProlongateCellCenteredValues(
//        const AthenaArray<Real> &coarse,AthenaArray<Real> &fine, int sn, int en,,
//        int si, int ei, int sj, int ej, int sk, int ek)
//  \brief Prolongate cell centered values

void MeshRefinement::ProlongateCellCenteredValues(
    const AthenaArray<Real> &coarse, AthenaArray<Real> &fine,
    int sn, int en, int si, int ei, int sj, int ej, int sk, int ek) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;

  if (pmb->block_size.nx3 > 1) {
    for (int n=sn; n<=en; n++) {
      for (int k=sk; k<=ek; k++) {
        int fk = (k - pmb->cks)*2 + pmb->ks;
        const Real& x3m = pcoarsec->x3v(k-1);
        const Real& x3c = pcoarsec->x3v(k);
        const Real& x3p = pcoarsec->x3v(k+1);
        Real dx3m = x3c - x3m;
        Real dx3p = x3p - x3c;
        const Real& fx3m = pco->x3v(fk);
        const Real& fx3p = pco->x3v(fk+1);
        Real dx3fm =  x3c - fx3m;
        Real dx3fp =  fx3p - x3c;
        for (int j = sj; j<=ej; j++) {
          int fj = (j - pmb->cjs)*2 + pmb->js;
          const Real& x2m = pcoarsec->x2v(j-1);
          const Real& x2c = pcoarsec->x2v(j);
          const Real& x2p = pcoarsec->x2v(j+1);
          Real dx2m = x2c - x2m;
          Real dx2p = x2p - x2c;
          const Real& fx2m = pco->x2v(fj);
          const Real& fx2p = pco->x2v(fj+1);
          Real dx2fm = x2c - fx2m;
          Real dx2fp = fx2p - x2c;
          for (int i=si; i<=ei; i++) {
            int fi = (i - pmb->cis)*2 + pmb->is;
            const Real& x1m = pcoarsec->x1v(i-1);
            const Real& x1c = pcoarsec->x1v(i);
            const Real& x1p = pcoarsec->x1v(i+1);
            Real dx1m = x1c - x1m;
            Real dx1p = x1p - x1c;
            const Real& fx1m = pco->x1v(fi);
            const Real& fx1p = pco->x1v(fi+1);
            Real dx1fm = x1c - fx1m;
            Real dx1fp = fx1p - x1c;
            Real ccval = coarse(n,k,j,i);

            // calculate 3D gradients using the minmod limiter
            Real gx1c, gx2c, gx3c;

            Real gx1m = (ccval - coarse(n,k,j,i-1))/dx1m;
            Real gx1p = (coarse(n,k,j,i+1) - ccval)/dx1p;
            gx1c = 0.5*(SIGN(gx1m) + SIGN(gx1p))*
              std::min(std::abs(gx1m), std::abs(gx1p));

            Real gx2m = (ccval - coarse(n,k,j-1,i))/dx2m;
            Real gx2p = (coarse(n,k,j+1,i) - ccval)/dx2p;
            gx2c = 0.5*(SIGN(gx2m) + SIGN(gx2p))*
              std::min(std::abs(gx2m), std::abs(gx2p));

            Real gx3m = (ccval - coarse(n,k-1,j,i))/dx3m;
            Real gx3p = (coarse(n,k+1,j,i) - ccval)/dx3p;
            gx3c = 0.5*(SIGN(gx3m) + SIGN(gx3p))*
              std::min(std::abs(gx3m), std::abs(gx3p));

            // KGF: add the off-centered quantities first to preserve FP symmetry
            // interpolate onto the finer grid
            fine(n,fk  ,fj  ,fi  ) = ccval - (gx1c*dx1fm + gx2c*dx2fm + gx3c*dx3fm);
            fine(n,fk  ,fj  ,fi+1) = ccval + (gx1c*dx1fp - gx2c*dx2fm - gx3c*dx3fm);
            fine(n,fk  ,fj+1,fi  ) = ccval - (gx1c*dx1fm - gx2c*dx2fp + gx3c*dx3fm);
            fine(n,fk  ,fj+1,fi+1) = ccval + (gx1c*dx1fp + gx2c*dx2fp - gx3c*dx3fm);
            fine(n,fk+1,fj  ,fi  ) = ccval - (gx1c*dx1fm + gx2c*dx2fm - gx3c*dx3fp);
            fine(n,fk+1,fj  ,fi+1) = ccval + (gx1c*dx1fp - gx2c*dx2fm + gx3c*dx3fp);
            fine(n,fk+1,fj+1,fi  ) = ccval - (gx1c*dx1fm - gx2c*dx2fp - gx3c*dx3fp);
            fine(n,fk+1,fj+1,fi+1) = ccval + (gx1c*dx1fp + gx2c*dx2fp + gx3c*dx3fp);
          }
        }
      }
    }
  } else if (pmb->block_size.nx2 > 1) {
    int k = pmb->cks, fk = pmb->ks;
    for (int n=sn; n<=en; n++) {
      for (int j=sj; j<=ej; j++) {
        int fj = (j - pmb->cjs)*2 + pmb->js;
        const Real& x2m = pcoarsec->x2v(j-1);
        const Real& x2c = pcoarsec->x2v(j);
        const Real& x2p = pcoarsec->x2v(j+1);
        Real dx2m = x2c - x2m;
        Real dx2p = x2p - x2c;
        const Real& fx2m = pco->x2v(fj);
        const Real& fx2p = pco->x2v(fj+1);
        Real dx2fm = x2c - fx2m;
        Real dx2fp = fx2p - x2c;
        for (int i=si; i<=ei; i++) {
          int fi = (i - pmb->cis)*2 + pmb->is;
          const Real& x1m = pcoarsec->x1v(i-1);
          const Real& x1c = pcoarsec->x1v(i);
          const Real& x1p = pcoarsec->x1v(i+1);
          Real dx1m = x1c - x1m;
          Real dx1p = x1p - x1c;
          const Real& fx1m = pco->x1v(fi);
          const Real& fx1p = pco->x1v(fi+1);
          Real dx1fm = x1c - fx1m;
          Real dx1fp = fx1p - x1c;
          Real ccval = coarse(n,k,j,i);

          Real gx1c, gx2c;
          Real gx1m = (ccval - coarse(n,k,j,i-1))/dx1m;
          Real gx1p = (coarse(n,k,j,i+1) - ccval)/dx1p;
          gx1c = 0.5*(SIGN(gx1m) + SIGN(gx1p))*
            std::min(std::abs(gx1m), std::abs(gx1p));

          Real gx2m = (ccval - coarse(n,k,j-1,i))/dx2m;
          Real gx2p = (coarse(n,k,j+1,i) - ccval)/dx2p;
          gx2c = 0.5*(SIGN(gx2m) + SIGN(gx2p))*
            std::min(std::abs(gx2m), std::abs(gx2p));

          // KGF: add the off-centered quantities first to preserve FP symmetry
          // interpolate onto the finer grid
          fine(n,fk  ,fj  ,fi  ) = ccval - (gx1c*dx1fm + gx2c*dx2fm);
          fine(n,fk  ,fj  ,fi+1) = ccval + (gx1c*dx1fp - gx2c*dx2fm);
          fine(n,fk  ,fj+1,fi  ) = ccval - (gx1c*dx1fm - gx2c*dx2fp);
          fine(n,fk  ,fj+1,fi+1) = ccval + (gx1c*dx1fp + gx2c*dx2fp);
        }
      }
    }
  } else { // 1D
    int k = pmb->cks, fk = pmb->ks, j = pmb->cjs, fj = pmb->js;
    for (int n=sn; n<=en; n++) {
      for (int i=si; i<=ei; i++) {
        int fi = (i - pmb->cis)*2 + pmb->is;
        const Real& x1m = pcoarsec->x1v(i-1);
        const Real& x1c = pcoarsec->x1v(i);
        const Real& x1p = pcoarsec->x1v(i+1);
        Real dx1m = x1c - x1m;
        Real dx1p = x1p - x1c;
        const Real& fx1m = pco->x1v(fi);
        const Real& fx1p = pco->x1v(fi+1);
        Real dx1fm = x1c - fx1m;
        Real dx1fp = fx1p - x1c;
        Real ccval = coarse(n,k,j,i);

        Real gx1c;

        // calculate 1D gradient using the min-mod limiter
        Real gx1m = (ccval - coarse(n,k,j,i-1))/dx1m;
        Real gx1p = (coarse(n,k,j,i+1) - ccval)/dx1p;
        gx1c = 0.5*(SIGN(gx1m) + SIGN(gx1p))*std::min(std::abs(gx1m),
                                                      std::abs(gx1p));

        // interpolate on to the finer grid
        fine(n,fk  ,fj  ,fi  ) = ccval - gx1c*dx1fm;
        fine(n,fk  ,fj  ,fi+1) = ccval + gx1c*dx1fp;
      }
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::ProlongateSharedFieldX1(const AthenaArray<Real> &coarse,
//      AthenaArray<Real> &fine, int si, int ei, int sj, int ej, int sk, int ek)
//  \brief prolongate x1 face-centered fields shared between coarse and fine levels

void MeshRefinement::ProlongateSharedFieldX1(
    const AthenaArray<Real> &coarse, AthenaArray<Real> &fine,
    int si, int ei, int sj, int ej, int sk, int ek) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  if (pmb->block_size.nx3 > 1) {
    for (int k=sk; k<=ek; k++) {
      int fk = (k - pmb->cks)*2 + pmb->ks;
      const Real& x3m = pcoarsec->x3s1(k-1);
      const Real& x3c = pcoarsec->x3s1(k);
      const Real& x3p = pcoarsec->x3s1(k+1);
      Real dx3m = x3c - x3m;
      Real dx3p = x3p - x3c;
      const Real& fx3m = pco->x3s1(fk);
      const Real& fx3p = pco->x3s1(fk+1);
      for (int j=sj; j<=ej; j++) {
        int fj = (j - pmb->cjs)*2 + pmb->js;
        const Real& x2m = pcoarsec->x2s1(j-1);
        const Real& x2c = pcoarsec->x2s1(j);
        const Real& x2p = pcoarsec->x2s1(j+1);
        Real dx2m = x2c - x2m;
        Real dx2p = x2p - x2c;
        const Real& fx2m = pco->x2s1(fj);
        const Real& fx2p = pco->x2s1(fj+1);
        for (int i=si; i<=ei; i++) {
          int fi = (i - pmb->cis)*2 + pmb->is;
          Real ccval = coarse(k,j,i);

          Real gx2m = (ccval - coarse(k,j-1,i))/dx2m;
          Real gx2p = (coarse(k,j+1,i) - ccval)/dx2p;
          Real gx2c = 0.5*(SIGN(gx2m) + SIGN(gx2p))*std::min(std::abs(gx2m),
                                                             std::abs(gx2p));
          Real gx3m = (ccval - coarse(k-1,j,i))/dx3m;
          Real gx3p = (coarse(k+1,j,i) - ccval)/dx3p;
          Real gx3c = 0.5*(SIGN(gx3m) + SIGN(gx3p))*std::min(std::abs(gx3m),
                                                             std::abs(gx3p));

          fine(fk  ,fj  ,fi) = ccval - gx2c*(x2c - fx2m) - gx3c*(x3c - fx3m);
          fine(fk  ,fj+1,fi) = ccval + gx2c*(fx2p - x2c) - gx3c*(x3c - fx3m);
          fine(fk+1,fj  ,fi) = ccval - gx2c*(x2c - fx2m) + gx3c*(fx3p - x3c);
          fine(fk+1,fj+1,fi) = ccval + gx2c*(fx2p - x2c) + gx3c*(fx3p - x3c);
        }
      }
    }
  } else if (pmb->block_size.nx2 > 1) {
    int k = pmb->cks, fk = pmb->ks;
    for (int j=sj; j<=ej; j++) {
      int fj = (j - pmb->cjs)*2 + pmb->js;
      const Real& x2m = pcoarsec->x2s1(j-1);
      const Real& x2c = pcoarsec->x2s1(j);
      const Real& x2p = pcoarsec->x2s1(j+1);
      Real dx2m = x2c - x2m;
      Real dx2p = x2p - x2c;
      const Real& fx2m = pco->x2s1(fj);
      const Real& fx2p = pco->x2s1(fj+1);
      for (int i=si; i<=ei; i++) {
        int fi = (i - pmb->cis)*2 + pmb->is;
        Real ccval = coarse(k,j,i);

        Real gx2m = (ccval - coarse(k,j-1,i))/dx2m;
        Real gx2p = (coarse(k,j+1,i) - ccval)/dx2p;
        Real gx2c = 0.5*(SIGN(gx2m) + SIGN(gx2p))*std::min(std::abs(gx2m),
                                                           std::abs(gx2p));

        fine(fk,fj  ,fi) = ccval - gx2c*(x2c - fx2m);
        fine(fk,fj+1,fi) = ccval + gx2c*(fx2p - x2c);
      }
    }
  } else { // 1D
    for (int i=si; i<=ei; i++) {
      int fi = (i - pmb->cis)*2 + pmb->is;
      fine(0,0,fi) = coarse(0,0,i);
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::ProlongateSharedFieldX2(const AthenaArray<Real> &coarse,
//      AthenaArray<Real> &fine, int si, int ei, int sj, int ej, int sk, int ek)
//  \brief prolongate x2 face-centered fields shared between coarse and fine levels

void MeshRefinement::ProlongateSharedFieldX2(
    const AthenaArray<Real> &coarse, AthenaArray<Real> &fine,
    int si, int ei, int sj, int ej, int sk, int ek) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  if (pmb->block_size.nx3 > 1) {
    for (int k=sk; k<=ek; k++) {
      int fk = (k - pmb->cks)*2 + pmb->ks;
      const Real& x3m = pcoarsec->x3s2(k-1);
      const Real& x3c = pcoarsec->x3s2(k);
      const Real& x3p = pcoarsec->x3s2(k+1);
      Real dx3m = x3c - x3m;
      Real dx3p = x3p - x3c;
      const Real& fx3m = pco->x3s2(fk);
      const Real& fx3p = pco->x3s2(fk+1);
      for (int j=sj; j<=ej; j++) {
        int fj = (j - pmb->cjs)*2 + pmb->js;
        for (int i=si; i<=ei; i++) {
          int fi = (i - pmb->cis)*2 + pmb->is;
          const Real& x1m = pcoarsec->x1s2(i-1);
          const Real& x1c = pcoarsec->x1s2(i);
          const Real& x1p = pcoarsec->x1s2(i+1);
          Real dx1m = x1c - x1m;
          Real dx1p = x1p - x1c;
          const Real& fx1m = pco->x1s2(fi);
          const Real& fx1p = pco->x1s2(fi+1);
          Real ccval = coarse(k,j,i);

          Real gx1m = (ccval - coarse(k,j,i-1))/dx1m;
          Real gx1p = (coarse(k,j,i+1) - ccval)/dx1p;
          Real gx1c = 0.5*(SIGN(gx1m) + SIGN(gx1p))*std::min(std::abs(gx1m),
                                                             std::abs(gx1p));
          Real gx3m = (ccval - coarse(k-1,j,i))/dx3m;
          Real gx3p = (coarse(k+1,j,i) - ccval)/dx3p;
          Real gx3c = 0.5*(SIGN(gx3m) + SIGN(gx3p))*std::min(std::abs(gx3m),
                                                             std::abs(gx3p));

          fine(fk  ,fj,fi  ) = ccval - gx1c*(x1c - fx1m) - gx3c*(x3c - fx3m);
          fine(fk  ,fj,fi+1) = ccval + gx1c*(fx1p - x1c) - gx3c*(x3c - fx3m);
          fine(fk+1,fj,fi  ) = ccval - gx1c*(x1c - fx1m) + gx3c*(fx3p - x3c);
          fine(fk+1,fj,fi+1) = ccval + gx1c*(fx1p - x1c) + gx3c*(fx3p - x3c);
        }
      }
    }
  } else if (pmb->block_size.nx2 > 1) {
    int k = pmb->cks, fk = pmb->ks;
    for (int j=sj; j<=ej; j++) {
      int fj = (j - pmb->cjs)*2 + pmb->js;
      for (int i=si; i<=ei; i++) {
        int fi = (i - pmb->cis)*2 + pmb->is;
        const Real& x1m = pcoarsec->x1s2(i-1);
        const Real& x1c = pcoarsec->x1s2(i);
        const Real& x1p = pcoarsec->x1s2(i+1);
        const Real& fx1m = pco->x1s2(fi);
        const Real& fx1p = pco->x1s2(fi+1);
        Real ccval = coarse(k,j,i);

        Real gx1m = (ccval - coarse(k,j,i-1))/(x1c - x1m);
        Real gx1p = (coarse(k,j,i+1) - ccval)/(x1p - x1c);
        Real gx1c = 0.5*(SIGN(gx1m) + SIGN(gx1p))*std::min(std::abs(gx1m),
                                                           std::abs(gx1p));

        fine(fk,fj,fi  ) = ccval - gx1c*(x1c - fx1m);
        fine(fk,fj,fi+1) = ccval + gx1c*(fx1p - x1c);
      }
    }
  } else {
    for (int i=si; i<=ei; i++) {
      int fi = (i - pmb->cis)*2 + pmb->is;
      Real gxm = (coarse(0,0,i) - coarse(0,0,i-1))
                 /(pcoarsec->x1s2(i) - pcoarsec->x1s2(i-1));
      Real gxp = (coarse(0,0,i+1) - coarse(0,0,i))
                 /(pcoarsec->x1s2(i+1) - pcoarsec->x1s2(i));
      Real gxc = 0.5*(SIGN(gxm) + SIGN(gxp))*std::min(std::abs(gxm),
                                                      std::abs(gxp));
      fine(0,0,fi  ) = fine(0,1,fi  )
                     = coarse(0,0,i) - gxc*(pcoarsec->x1s2(i) - pco->x1s2(fi));
      fine(0,0,fi+1) = fine(0,1,fi+1)
                     = coarse(0,0,i) + gxc*(pco->x1s2(fi+1) - pcoarsec->x1s2(i));
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::ProlongateSharedFieldX3(const AthenaArray<Real> &coarse,
//      AthenaArray<Real> &fine, int si, int ei, int sj, int ej, int sk, int ek)
//  \brief prolongate x3 face-centered fields shared between coarse and fine levels

void MeshRefinement::ProlongateSharedFieldX3(
    const AthenaArray<Real> &coarse, AthenaArray<Real> &fine,
    int si, int ei, int sj, int ej, int sk, int ek) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  if (pmb->block_size.nx3 > 1) {
    for (int k=sk; k<=ek; k++) {
      int fk = (k - pmb->cks)*2 + pmb->ks;
      for (int j=sj; j<=ej; j++) {
        int fj = (j - pmb->cjs)*2 + pmb->js;
        const Real& x2m = pcoarsec->x2s3(j-1);
        const Real& x2c = pcoarsec->x2s3(j);
        const Real& x2p = pcoarsec->x2s3(j+1);
        Real dx2m = x2c - x2m;
        Real dx2p = x2p - x2c;
        const Real& fx2m = pco->x2s3(fj);
        const Real& fx2p = pco->x2s3(fj+1);
        for (int i=si; i<=ei; i++) {
          int fi = (i - pmb->cis)*2 + pmb->is;
          const Real& x1m = pcoarsec->x1s3(i-1);
          const Real& x1c = pcoarsec->x1s3(i);
          const Real& x1p = pcoarsec->x1s3(i+1);
          Real dx1m = x1c - x1m;
          Real dx1p = x1p - x1c;
          const Real& fx1m = pco->x1s3(fi);
          const Real& fx1p = pco->x1s3(fi+1);
          Real ccval = coarse(k,j,i);

          Real gx1m = (ccval - coarse(k,j,i-1))/dx1m;
          Real gx1p = (coarse(k,j,i+1) - ccval)/dx1p;
          Real gx1c = 0.5*(SIGN(gx1m) + SIGN(gx1p))*std::min(std::abs(gx1m),
                                                             std::abs(gx1p));
          Real gx2m = (ccval - coarse(k,j-1,i))/dx2m;
          Real gx2p = (coarse(k,j+1,i) - ccval)/dx2p;
          Real gx2c = 0.5*(SIGN(gx2m) + SIGN(gx2p))*std::min(std::abs(gx2m),
                                                             std::abs(gx2p));

          fine(fk,fj  ,fi  ) = ccval - gx1c*(x1c - fx1m) - gx2c*(x2c - fx2m);
          fine(fk,fj  ,fi+1) = ccval + gx1c*(fx1p - x1c) - gx2c*(x2c - fx2m);
          fine(fk,fj+1,fi  ) = ccval - gx1c*(x1c - fx1m) + gx2c*(fx2p - x2c);
          fine(fk,fj+1,fi+1) = ccval + gx1c*(fx1p - x1c) + gx2c*(fx2p - x2c);
        }
      }
    }
  } else if (pmb->block_size.nx2 > 1) {
    int k = pmb->cks, fk = pmb->ks;
    for (int j=sj; j<=ej; j++) {
      int fj = (j - pmb->cjs)*2 + pmb->js;
      const Real& x2m = pcoarsec->x2s3(j-1);
      const Real& x2c = pcoarsec->x2s3(j);
      const Real& x2p = pcoarsec->x2s3(j+1);
      Real dx2m = x2c - x2m;
      Real dx2p = x2p - x2c;
      const Real& fx2m = pco->x2s3(fj);
      const Real& fx2p = pco->x2s3(fj+1);
      Real dx2fm = x2c - fx2m;
      Real dx2fp = fx2p - x2c;
      for (int i=si; i<=ei; i++) {
        int fi = (i - pmb->cis)*2 + pmb->is;
        const Real& x1m = pcoarsec->x1s3(i-1);
        const Real& x1c = pcoarsec->x1s3(i);
        const Real& x1p = pcoarsec->x1s3(i+1);
        Real dx1m = x1c - x1m;
        Real dx1p = x1p - x1c;
        const Real& fx1m = pco->x1s3(fi);
        const Real& fx1p = pco->x1s3(fi+1);
        Real dx1fm = x1c - fx1m;
        Real dx1fp = fx1p - x1c;
        Real ccval = coarse(k,j,i);

        // calculate 2D gradients using the minmod limiter
        Real gx1m = (ccval - coarse(k,j,i-1))/dx1m;
        Real gx1p = (coarse(k,j,i+1) - ccval)/dx1p;
        Real gx1c = 0.5*(SIGN(gx1m) + SIGN(gx1p))*std::min(std::abs(gx1m),
                                                           std::abs(gx1p));
        Real gx2m = (ccval - coarse(k,j-1,i))/dx2m;
        Real gx2p = (coarse(k,j+1,i) - ccval)/dx2p;
        Real gx2c = 0.5*(SIGN(gx2m) + SIGN(gx2p))*std::min(std::abs(gx2m),
                                                           std::abs(gx2p));

        // interpolate on to the finer grid
        fine(fk,fj  ,fi  ) = fine(fk+1,fj  ,fi  ) = ccval - gx1c*dx1fm-gx2c*dx2fm;
        fine(fk,fj  ,fi+1) = fine(fk+1,fj  ,fi+1) = ccval + gx1c*dx1fp-gx2c*dx2fm;
        fine(fk,fj+1,fi  ) = fine(fk+1,fj+1,fi  ) = ccval - gx1c*dx1fm+gx2c*dx2fp;
        fine(fk,fj+1,fi+1) = fine(fk+1,fj+1,fi+1) = ccval + gx1c*dx1fp+gx2c*dx2fp;
      }
    }
  } else {
    for (int i=si; i<=ei; i++) {
      int fi = (i - pmb->cis)*2 + pmb->is;
      Real gxm = (coarse(0,0,i)   - coarse(0,0,i-1))
                 / (pcoarsec->x1s3(i) - pcoarsec->x1s3(i-1));
      Real gxp = (coarse(0,0,i+1) - coarse(0,0,i))
                 / (pcoarsec->x1s3(i+1) - pcoarsec->x1s3(i));
      Real gxc = 0.5*(SIGN(gxm) + SIGN(gxp))*std::min(std::abs(gxm),
                                                      std::abs(gxp));
      fine(0,0,fi  ) = fine(1,0,fi  )
                     = coarse(0,0,i) - gxc*(pcoarsec->x1s3(i) - pco->x1s3(fi));
      fine(0,0,fi+1) = fine(1,0,fi+1)
                     = coarse(0,0,i) + gxc*(pco->x1s3(fi+1) - pcoarsec->x1s3(i));
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::ProlongateInternalField(FaceField &fine,
//                           int si, int ei, int sj, int ej, int sk, int ek)
//  \brief prolongate the internal face-centered fields

void MeshRefinement::ProlongateInternalField(
    FaceField &fine, int si, int ei, int sj, int ej, int sk, int ek) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  int fsi = (si - pmb->cis)*2 + pmb->is, fei = (ei - pmb->cis)*2 + pmb->is + 1;
  if (pmb->block_size.nx3 > 1) {
    for (int k=sk; k<=ek; k++) {
      int fk = (k - pmb->cks)*2 + pmb->ks;
      for (int j=sj; j<=ej; j++) {
        int fj = (j - pmb->cjs)*2 + pmb->js;
        pco->Face1Area(fk,   fj,   fsi, fei+1, sarea_x1_[0][0]);
        pco->Face1Area(fk,   fj+1, fsi, fei+1, sarea_x1_[0][1]);
        pco->Face1Area(fk+1, fj,   fsi, fei+1, sarea_x1_[1][0]);
        pco->Face1Area(fk+1, fj+1, fsi, fei+1, sarea_x1_[1][1]);
        pco->Face2Area(fk,   fj,   fsi, fei,   sarea_x2_[0][0]);
        pco->Face2Area(fk,   fj+1, fsi, fei,   sarea_x2_[0][1]);
        pco->Face2Area(fk,   fj+2, fsi, fei,   sarea_x2_[0][2]);
        pco->Face2Area(fk+1, fj,   fsi, fei,   sarea_x2_[1][0]);
        pco->Face2Area(fk+1, fj+1, fsi, fei,   sarea_x2_[1][1]);
        pco->Face2Area(fk+1, fj+2, fsi, fei,   sarea_x2_[1][2]);
        pco->Face3Area(fk,   fj,   fsi, fei,   sarea_x3_[0][0]);
        pco->Face3Area(fk,   fj+1, fsi, fei,   sarea_x3_[0][1]);
        pco->Face3Area(fk+1, fj,   fsi, fei,   sarea_x3_[1][0]);
        pco->Face3Area(fk+1, fj+1, fsi, fei,   sarea_x3_[1][1]);
        pco->Face3Area(fk+2, fj,   fsi, fei,   sarea_x3_[2][0]);
        pco->Face3Area(fk+2, fj+1, fsi, fei,   sarea_x3_[2][1]);
        for (int i=si; i<=ei; i++) {
          int fi = (i - pmb->cis)*2 + pmb->is;
          Real Uxx = 0.0, Vyy = 0.0, Wzz = 0.0;
          Real Uxyz = 0.0, Vxyz = 0.0, Wxyz = 0.0;
#pragma unroll
          for (int jj=0; jj<2; jj++) {
            int js = 2*jj - 1, fjj = fj + jj, fjp = fj + 2*jj;
#pragma unroll
            for (int ii=0; ii<2; ii++) {
              int is = 2*ii - 1, fii = fi + ii, fip = fi + 2*ii;
              Uxx += is*(js*(fine.x2f(fk  ,fjp,fii)*sarea_x2_[0][2*jj](fii) +
                             fine.x2f(fk+1,fjp,fii)*sarea_x2_[1][2*jj](fii))
                         +(fine.x3f(fk+2,fjj,fii)*sarea_x3_[2][  jj](fii) -
                           fine.x3f(fk  ,fjj,fii)*sarea_x3_[0][  jj](fii)));
              Vyy += js*(   (fine.x3f(fk+2,fjj,fii)*sarea_x3_[2][  jj](fii) -
                             fine.x3f(fk  ,fjj,fii)*sarea_x3_[0][  jj](fii))
                            +is*(fine.x1f(fk  ,fjj,fip)*sarea_x1_[0][  jj](fip) +
                                 fine.x1f(fk+1,fjj,fip)*sarea_x1_[1][  jj](fip)));
              Wzz +=     is*(fine.x1f(fk+1,fjj,fip)*sarea_x1_[1][  jj](fip) -
                             fine.x1f(fk  ,fjj,fip)*sarea_x1_[0][  jj](fip))
                         +js*(fine.x2f(fk+1,fjp,fii)*sarea_x2_[1][2*jj](fii) -
                              fine.x2f(fk  ,fjp,fii)*sarea_x2_[0][2*jj](fii));
              Uxyz += is*js*(fine.x1f(fk+1,fjj,fip)*sarea_x1_[1][  jj](fip) -
                             fine.x1f(fk  ,fjj,fip)*sarea_x1_[0][  jj](fip));
              Vxyz += is*js*(fine.x2f(fk+1,fjp,fii)*sarea_x2_[1][2*jj](fii) -
                             fine.x2f(fk  ,fjp,fii)*sarea_x2_[0][2*jj](fii));
              Wxyz += is*js*(fine.x3f(fk+2,fjj,fii)*sarea_x3_[2][  jj](fii) -
                             fine.x3f(fk  ,fjj,fii)*sarea_x3_[0][  jj](fii));
            }
          }
          Real Sdx1 = SQR(pco->dx1f(fi) + pco->dx1f(fi+1));
          Real Sdx2 = SQR(pco->GetEdge2Length(fk+1,fj,fi+1) +
                          pco->GetEdge2Length(fk+1,fj+1,fi+1));
          Real Sdx3 = SQR(pco->GetEdge3Length(fk,fj+1,fi+1) +
                          pco->GetEdge3Length(fk+1,fj+1,fi+1));
          Uxx *= 0.125; Vyy *= 0.125; Wzz *= 0.125;
          Uxyz *= 0.125/(Sdx2 + Sdx3);
          Vxyz *= 0.125/(Sdx1 + Sdx3);
          Wxyz *= 0.125/(Sdx1 + Sdx2);
          fine.x1f(fk  ,fj  ,fi+1) =
              (0.5*(fine.x1f(fk  ,fj  ,fi  )*sarea_x1_[0][0](fi  ) +
                    fine.x1f(fk  ,fj  ,fi+2)*sarea_x1_[0][0](fi+2))
               + Uxx - Sdx3*Vxyz - Sdx2*Wxyz) /sarea_x1_[0][0](fi+1);
          fine.x1f(fk  ,fj+1,fi+1) =
              (0.5*(fine.x1f(fk  ,fj+1,fi  )*sarea_x1_[0][1](fi  ) +
                    fine.x1f(fk  ,fj+1,fi+2)*sarea_x1_[0][1](fi+2))
               + Uxx - Sdx3*Vxyz + Sdx2*Wxyz) /sarea_x1_[0][1](fi+1);
          fine.x1f(fk+1,fj  ,fi+1) =
              (0.5*(fine.x1f(fk+1,fj  ,fi  )*sarea_x1_[1][0](fi  ) +
                    fine.x1f(fk+1,fj  ,fi+2)*sarea_x1_[1][0](fi+2))
               + Uxx + Sdx3*Vxyz - Sdx2*Wxyz) /sarea_x1_[1][0](fi+1);
          fine.x1f(fk+1,fj+1,fi+1) =
              (0.5*(fine.x1f(fk+1,fj+1,fi  )*sarea_x1_[1][1](fi  ) +
                    fine.x1f(fk+1,fj+1,fi+2)*sarea_x1_[1][1](fi+2))
               + Uxx + Sdx3*Vxyz + Sdx2*Wxyz) /sarea_x1_[1][1](fi+1);

          fine.x2f(fk  ,fj+1,fi  ) =
              (0.5*(fine.x2f(fk  ,fj  ,fi  )*sarea_x2_[0][0](fi  ) +
                    fine.x2f(fk  ,fj+2,fi  )*sarea_x2_[0][2](fi  ))
               + Vyy - Sdx3*Uxyz - Sdx1*Wxyz) /sarea_x2_[0][1](fi  );
          fine.x2f(fk  ,fj+1,fi+1) =
              (0.5*(fine.x2f(fk  ,fj  ,fi+1)*sarea_x2_[0][0](fi+1) +
                    fine.x2f(fk  ,fj+2,fi+1)*sarea_x2_[0][2](fi+1))
               + Vyy - Sdx3*Uxyz + Sdx1*Wxyz) /sarea_x2_[0][1](fi+1);
          fine.x2f(fk+1,fj+1,fi  ) =
              (0.5*(fine.x2f(fk+1,fj  ,fi  )*sarea_x2_[1][0](fi  ) +
                    fine.x2f(fk+1,fj+2,fi  )*sarea_x2_[1][2](fi  ))
               + Vyy + Sdx3*Uxyz - Sdx1*Wxyz) /sarea_x2_[1][1](fi  );
          fine.x2f(fk+1,fj+1,fi+1) =
              (0.5*(fine.x2f(fk+1,fj  ,fi+1)*sarea_x2_[1][0](fi+1) +
                    fine.x2f(fk+1,fj+2,fi+1)*sarea_x2_[1][2](fi+1))
               + Vyy + Sdx3*Uxyz + Sdx1*Wxyz) /sarea_x2_[1][1](fi+1);

          fine.x3f(fk+1,fj  ,fi  ) =
              (0.5*(fine.x3f(fk+2,fj  ,fi  )*sarea_x3_[2][0](fi  ) +
                    fine.x3f(fk  ,fj  ,fi  )*sarea_x3_[0][0](fi  ))
               + Wzz - Sdx2*Uxyz - Sdx1*Vxyz) /sarea_x3_[1][0](fi  );
          fine.x3f(fk+1,fj  ,fi+1) =
              (0.5*(fine.x3f(fk+2,fj  ,fi+1)*sarea_x3_[2][0](fi+1) +
                    fine.x3f(fk  ,fj  ,fi+1)*sarea_x3_[0][0](fi+1))
               + Wzz - Sdx2*Uxyz + Sdx1*Vxyz) /sarea_x3_[1][0](fi+1);
          fine.x3f(fk+1,fj+1,fi  ) =
              (0.5*(fine.x3f(fk+2,fj+1,fi  )*sarea_x3_[2][1](fi  ) +
                    fine.x3f(fk  ,fj+1,fi  )*sarea_x3_[0][1](fi  ))
               + Wzz + Sdx2*Uxyz - Sdx1*Vxyz) /sarea_x3_[1][1](fi  );
          fine.x3f(fk+1,fj+1,fi+1) =
              (0.5*(fine.x3f(fk+2,fj+1,fi+1)*sarea_x3_[2][1](fi+1) +
                    fine.x3f(fk  ,fj+1,fi+1)*sarea_x3_[0][1](fi+1))
               + Wzz + Sdx2*Uxyz + Sdx1*Vxyz) /sarea_x3_[1][1](fi+1);
        }
      }
    }
  } else if (pmb->block_size.nx2 > 1) {
    int fk = pmb->ks;
    for (int j=sj; j<=ej; j++) {
      int fj = (j - pmb->cjs)*2 + pmb->js;
      pco->Face1Area(fk,   fj,   fsi, fei+1, sarea_x1_[0][0]);
      pco->Face1Area(fk,   fj+1, fsi, fei+1, sarea_x1_[0][1]);
      pco->Face2Area(fk,   fj,   fsi, fei,   sarea_x2_[0][0]);
      pco->Face2Area(fk,   fj+1, fsi, fei,   sarea_x2_[0][1]);
      pco->Face2Area(fk,   fj+2, fsi, fei,   sarea_x2_[0][2]);
      for (int i=si; i<=ei; i++) {
        int fi = (i - pmb->cis)*2 + pmb->is;
        Real tmp1 = 0.25*(fine.x2f(fk,fj+2,fi+1)*sarea_x2_[0][2](fi+1)
                          - fine.x2f(fk,fj,  fi+1)*sarea_x2_[0][0](fi+1)
                          - fine.x2f(fk,fj+2,fi  )*sarea_x2_[0][2](fi  )
                          + fine.x2f(fk,fj,  fi  )*sarea_x2_[0][0](fi  ));
        Real tmp2 = 0.25*(fine.x1f(fk,fj,  fi  )*sarea_x1_[0][0](fi  )
                          - fine.x1f(fk,fj,  fi+2)*sarea_x1_[0][0](fi+2)
                          - fine.x1f(fk,fj+1,fi  )*sarea_x1_[0][1](fi  )
                          + fine.x1f(fk,fj+1,fi+2)*sarea_x1_[0][1](fi+2));
        fine.x1f(fk,fj  ,fi+1) =
            (0.5*(fine.x1f(fk,fj,  fi  )*sarea_x1_[0][0](fi  )
                  +fine.x1f(fk,fj,  fi+2)*sarea_x1_[0][0](fi+2)) + tmp1)
            /sarea_x1_[0][0](fi+1);
        fine.x1f(fk,fj+1,fi+1) =
            (0.5*(fine.x1f(fk,fj+1,fi  )*sarea_x1_[0][1](fi  )
                  +fine.x1f(fk,fj+1,fi+2)*sarea_x1_[0][1](fi+2)) + tmp1)
            /sarea_x1_[0][1](fi+1);
        fine.x2f(fk,fj+1,fi  ) =
            (0.5*(fine.x2f(fk,fj,  fi  )*sarea_x2_[0][0](fi  )
                  +fine.x2f(fk,fj+2,fi  )*sarea_x2_[0][2](fi  )) + tmp2)
            /sarea_x2_[0][1](fi  );
        fine.x2f(fk,fj+1,fi+1) =
            (0.5*(fine.x2f(fk,fj,  fi+1)*sarea_x2_[0][0](fi+1)
                  +fine.x2f(fk,fj+2,fi+1)*sarea_x2_[0][2](fi+1)) + tmp2)
            /sarea_x2_[0][1](fi+1);
      }
    }
  } else {
    pco->Face1Area(0, 0, fsi, fei+1, sarea_x1_[0][0]);
    for (int i=si; i<=ei; i++) {
      int fi = (i - pmb->cis)*2 + pmb->is;
      Real ph = sarea_x1_[0][0](fi)*fine.x1f(0,0,fi);
      fine.x1f(0,0,fi+1) = ph/sarea_x1_[0][0](fi+1);
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn inline void MeshRefinement::ProlongateVertexCenteredIndicialHelper(...)
//  \brief De-duplicate some indicial logic
inline void MeshRefinement::ProlongateVertexCenteredIndicialHelper(
  int hs_sz, int ix,
  int ix_cvs, int ix_cve, int ix_cmp,
  int ix_vs, int ix_ve,
  int &f_ix, int &ix_b, int &ix_so, int &ix_eo, int &ix_l, int &ix_u) {

  // map for fine-index
  if (ix < ix_cvs) {
    f_ix = ix_vs - 2 * (ix_cvs - ix);
  } else if (ix > ix_cve) {
    f_ix = ix_ve + 2 * (ix - ix_cve);
  } else { // map to interior+boundary nodes
    f_ix = 2 * (ix - ix_cvs) + ix_vs;
  }

  // bias direction [nb. stencil still symmetric!]
  if (ix < ix_cmp) {
    ix_b = 1;
    ix_so = 0;
    ix_eo = 1;
  } else if (ix > ix_cmp) {
    ix_b = -1;
    ix_so = -1;
    ix_eo = 0;
  } else {
    // central node is unbiased, coincident, inject with no neighbors
    ix_so = ix_eo = 0;
    ix_b = -1;
  }

  ix_l = ix - hs_sz + 1 - (1 - ix_b) / 2;
  ix_u = ix + hs_sz - (1 - ix_b) / 2;

  return;

}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::ProlongateVertexCenteredValues(
//        const AthenaArray<Real> &coarse,AthenaArray<Real> &fine, int sn, int en,,
//        int si, int ei, int sj, int ej, int sk, int ek)
//  \brief Prolongate vertex centered values;
//  Faster implementation, bias towards center by default

void MeshRefinement::ProlongateVertexCenteredValues(
    const AthenaArray<Real> &coarse, AthenaArray<Real> &fine,
    int sn, int en, int si, int ei, int sj, int ej, int sk, int ek) {

  // half number of ghosts
  int const H_NCGHOST = NCGHOST / 2;

  // maximum stencil size for interpolator
  int const H_SZ = H_NCGHOST + 1;

  MeshBlock *pmb = pmy_block_;

  if (pmb->pmy_mesh->ndim == 3) {
#if ISEVEN(NGHOST)
    int const eo_offset = 0;

    int const si_inj{(si)};
    int const ei_inj{(ei)};

    int const sj_inj{(sj)};
    int const ej_inj{(ej)};

    int const sk_inj{(sk)};
    int const ek_inj{(ek)};
#else
    int const eo_offset = -1;

    int const fis_inj{(2 * (si - H_NCGHOST) + eo_offset)};
    int const fie_inj{(2 * (ei - H_NCGHOST) + eo_offset)};

    int const fjs_inj{(2 * (sj - H_NCGHOST) + eo_offset)};
    int const fje_inj{(2 * (ej - H_NCGHOST) + eo_offset)};

    int const fks_inj{(2 * (sk - H_NCGHOST) + eo_offset)};
    int const fke_inj{(2 * (ek - H_NCGHOST) + eo_offset)};

    int const si_inj{(fis_inj < 0) ? si + 1 : si};
    int const ei_inj{(fie_inj > 2 * NGHOST + pmb->block_size.nx1) ? ei - 1 : ei};

    int const sj_inj{(fjs_inj < 0) ? sj + 1 : sj};
    int const ej_inj{(fje_inj > 2 * NGHOST + pmb->block_size.nx2) ? ej - 1 : ej};

    int const sk_inj{(fks_inj < 0) ? sk + 1 : sk};
    int const ek_inj{(fke_inj > 2 * NGHOST + pmb->block_size.nx3) ? ek - 1 : ek};
#endif


    //-------------------------------------------------------------------------
    // bias offsets for prolongation (depends on location)
    int const si_prl{(si > pmb->cimp) ? si - 1 : si};
    int const ei_prl{(ei < pmb->cimp) ? ei + 1 : ei};
    int const sj_prl{(sj > pmb->cjmp) ? sj - 1 : sj};
    int const ej_prl{(ej < pmb->cjmp) ? ej + 1 : ej};
    int const sk_prl{(sk > pmb->ckmp) ? sk - 1 : sk};
    int const ek_prl{(ek < pmb->ckmp) ? ek + 1 : ek};

    // [running ix]: op

    for (int n = sn; n<= en; ++n) {
      //-----------------------------------------------------------------------
      // [k, j, i]: interp. 3d
      for (int k = sk_prl; k < ek_prl; ++k) {
        int const fk_inj = 2 * (k - H_NCGHOST) + eo_offset;
        int const fk_prl = fk_inj + 1;

        for (int j = sj_prl; j < ej_prl; ++j) {
          int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
          int const fj_prl = fj_inj + 1;

          for (int i = si_prl; i < ei_prl; ++i) {
            int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
            int const fi_prl = fi_inj + 1;

            fine(n, fk_prl, fj_prl, fi_prl) = 0.;

            for (int dk=0; dk<H_SZ; ++dk) {
              int const ck_u = k + dk + 1;
              int const ck_l = k - dk;

              Real const lck = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dk-1];

              for (int dj=0; dj<H_SZ; ++dj) {
                int const cj_u = j + dj + 1;
                int const cj_l = j - dj;

                Real const lckj = lck * InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dj-1];

                for (int di=0; di<H_SZ; ++di) {
                  int const ci_u = i + di + 1;
                  int const ci_l = i - di;

                  Real const lckji = lckj * InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-di-1];

                  Real const fc_uuu = coarse(n, ck_u, cj_u, ci_u);
                  Real const fc_lll = coarse(n, ck_l, cj_l, ci_l);

                  Real const fc_luu = coarse(n, ck_l, cj_u, ci_u);
                  Real const fc_ulu = coarse(n, ck_u, cj_l, ci_u);
                  Real const fc_uul = coarse(n, ck_u, cj_u, ci_l);

                  Real const fc_llu = coarse(n, ck_l, cj_l, ci_u);
                  Real const fc_ull = coarse(n, ck_u, cj_l, ci_l);
                  Real const fc_lul = coarse(n, ck_l, cj_u, ci_l);

                  fine(n, fk_prl, fj_prl, fi_prl) += lckji *
                    ((fc_uuu + fc_lll) +
                    (fc_uul + fc_llu) +
                    (fc_lul + fc_ulu) +
                    (fc_luu + fc_ull));
                }
              }
            }
          }
        }
      } // (prl, prl, prl)
      //-----------------------------------------------------------------------

      //-----------------------------------------------------------------------
      // [k, j, i]: interp. 2d & inject 1d
      for (int k = sk_inj; k <= ek_inj; ++k) {
        int const fk_inj = 2 * (k - H_NCGHOST) + eo_offset;
        int const fk_prl = fk_inj + 1;
        (void)fk_prl; // DR: why is this not used?

        for (int j = sj_prl; j < ej_prl; ++j) {
          int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
          int const fj_prl = fj_inj + 1;

          for (int i = si_prl; i < ei_prl; ++i) {
            int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
            int const fi_prl = fi_inj + 1;

            fine(n, fk_inj, fj_prl, fi_prl) = 0.;

            for (int dj=0; dj<H_SZ; ++dj) {
              int const cj_u = j + dj + 1;
              int const cj_l = j - dj;

              Real const lcj = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dj-1];

              for (int di=0; di<H_SZ; ++di) {
                int const ci_u = i + di + 1;
                int const ci_l = i - di;

                Real const lcji = lcj * InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-di-1];

                Real const fc_cuu = coarse(n, k, cj_u, ci_u);
                Real const fc_cul = coarse(n, k, cj_u, ci_l);
                Real const fc_clu = coarse(n, k, cj_l, ci_u);
                Real const fc_cll = coarse(n, k, cj_l, ci_l);

                fine(n, fk_inj, fj_prl, fi_prl) += lcji * ((fc_cuu + fc_cll) + (fc_clu + fc_cul));
              }
            }

          }
        }
      } // (inj, prl, prl)


      // [k, j, i]: interp. 2d & inject 1d
      for (int k = sk_prl; k < ek_prl; ++k) {
        int const fk_inj = 2 * (k - H_NCGHOST) + eo_offset;
        int const fk_prl = fk_inj + 1;

        for (int j = sj_inj; j <= ej_inj; ++j) {
          int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
          int const fj_prl = fj_inj + 1;
          (void)fj_prl; // DR: why is this not used?

          for (int i = si_prl; i < ei_prl; ++i) {
            int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
            int const fi_prl = fi_inj + 1;

            fine(n, fk_prl, fj_inj, fi_prl) = 0.;

            for (int dk=0; dk<H_SZ; ++dk) {
              int const ck_u = k + dk + 1;
              int const ck_l = k - dk;

              Real const lck = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dk-1];

              for (int di=0; di<H_SZ; ++di) {
                int const ci_u = i + di + 1;
                int const ci_l = i - di;

                Real const lcki = lck * InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-di-1];

                Real const fc_ucu = coarse(n, ck_u, j, ci_u);
                Real const fc_ucl = coarse(n, ck_u, j, ci_l);
                Real const fc_lcu = coarse(n, ck_l, j, ci_u);
                Real const fc_lcl = coarse(n, ck_l, j, ci_l);

                fine(n, fk_prl, fj_inj, fi_prl) += lcki * ((fc_lcu + fc_ucl) + (fc_ucu + fc_lcl));
              }
            }

          }
        }
      } // (prl, inj, prl)

      // [k, j, i]: interp. 2d & inject 1d
      for (int k = sk_prl; k < ek_prl; ++k) {
        int const fk_inj = 2 * (k - H_NCGHOST) + eo_offset;
        int const fk_prl = fk_inj + 1;

        for (int j = sj_prl; j < ej_prl; ++j) {
          int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
          int const fj_prl = fj_inj + 1;

          for (int i = si_inj; i <= ei_inj; ++i) {
            int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
            int const fi_prl = fi_inj + 1;
            (void)fi_prl;  // DR: why is this not used?

            fine(n, fk_prl, fj_prl, fi_inj) = 0.;

            for (int dk=0; dk<H_SZ; ++dk) {
              int const ck_u = k + dk + 1;
              int const ck_l = k - dk;

              Real const lck = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dk-1];

              for (int dj=0; dj<H_SZ; ++dj) {
                int const cj_u = j + dj + 1;
                int const cj_l = j - dj;

                Real const lckj = lck * InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dj-1];

                Real const fc_uuc = coarse(n, ck_u, cj_u, i);
                Real const fc_ulc = coarse(n, ck_u, cj_l, i);
                Real const fc_luc = coarse(n, ck_l, cj_u, i);
                Real const fc_llc = coarse(n, ck_l, cj_l, i);

                fine(n, fk_prl, fj_prl, fi_inj) += lckj * ((fc_uuc + fc_llc) + (fc_luc + fc_ulc));
              }
            }

          }
        }
      } // (prl, prl, inj)
      //-----------------------------------------------------------------------

      //-----------------------------------------------------------------------
      // [k, j, i]: interp. 1d & inject 2d
      for (int k = sk_inj; k <= ek_inj; ++k) {
        int const fk_inj = 2 * (k - H_NCGHOST) + eo_offset;
        int const fk_prl = fk_inj + 1;
        (void)fk_prl;   // DR: why is this not used?

        for (int j = sj_inj; j <= ej_inj; ++j) {
          int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
          int const fj_prl = fj_inj + 1;
          (void)fj_prl;  // DR: why is this not used?

          for (int i = si_prl; i < ei_prl; ++i) {
            int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
            int const fi_prl = fi_inj + 1;

            fine(n, fk_inj, fj_inj, fi_prl) = 0.;

            for (int di=0; di<H_SZ; ++di) {
              int const ci_u = i + di + 1;
              int const ci_l = i - di;

              Real const lci = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-di-1];

              Real const fc_ccu = coarse(n, k, j, ci_u);
              Real const fc_ccl = coarse(n, k, j, ci_l);

              fine(n, fk_inj, fj_inj, fi_prl) += lci * (fc_ccl + fc_ccu);
            }

          }
        }
      } // (inj, inj, prl)


      // [k, j, i]: interp. 1d & inject 2d
      for (int k = sk_inj; k <= ek_inj; ++k) {
        int const fk_inj = 2 * (k - H_NCGHOST) + eo_offset;
        int const fk_prl = fk_inj + 1;
        (void)fk_prl;  // DR: why is this not used?

        for (int j = sj_prl; j < ej_prl; ++j) {
          int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
          int const fj_prl = fj_inj + 1;

          for (int i = si_inj; i <= ei_inj; ++i) {
            int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
            int const fi_prl = fi_inj + 1;
            (void)fi_prl;  // DR: why is this not used?

            fine(n, fk_inj, fj_prl, fi_inj) = 0.;

            for (int dj=0; dj<H_SZ; ++dj) {
              int const cj_u = j + dj + 1;
              int const cj_l = j - dj;

              Real const lcj = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dj-1];

              Real const fc_cuc = coarse(n, k, cj_u, i);
              Real const fc_clc = coarse(n, k, cj_l, i);

              fine(n, fk_inj, fj_prl, fi_inj) += lcj * (fc_clc + fc_cuc);
            }

          }
        }
      } // (inj, prl, inj)

      // [k, j, i]: interp. 1d & inject 2d
      for (int k = sk_prl; k < ek_prl; ++k) {
        int const fk_inj = 2 * (k - H_NCGHOST) + eo_offset;
        int const fk_prl = fk_inj + 1;

        for (int j = sj_inj; j <= ej_inj; ++j) {
          int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
          int const fj_prl = fj_inj + 1;
          (void)fj_prl;  // DR: why is this not used?

          for (int i = si_inj; i <= ei_inj; ++i) {
            int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
            int const fi_prl = fi_inj + 1;
            (void)fi_prl;  // DR: why is this not used?

            fine(n, fk_prl, fj_inj, fi_inj) = 0.;

            for (int dk=0; dk<H_SZ; ++dk) {
              int const ck_u = k + dk + 1;
              int const ck_l = k - dk;

              Real const lck = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dk-1];

              Real const fc_ucc = coarse(n, ck_u, j, i);
              Real const fc_lcc = coarse(n, ck_l, j, i);

              fine(n, fk_prl, fj_inj, fi_inj) += lck * (fc_lcc + fc_ucc);
            }

          }
        }
      } // (prl, inj, inj)
      //-----------------------------------------------------------------------

      //-----------------------------------------------------------------------
      // [k, j, i]: inject 3d
      for (int k = sk_inj; k <= ek_inj; ++k) {
        int const fk_inj = 2 * (k - H_NCGHOST) + eo_offset;
        int const fk_prl = fk_inj + 1;
        (void)fk_prl;  // DR: why is this not used?

        for (int j = sj_inj; j <= ej_inj; ++j) {
          int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
          int const fj_prl = fj_inj + 1;
          (void)fj_prl;  // DR: why is this not used?

          for (int i = si_inj; i <= ei_inj; ++i) {
            int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
            int const fi_prl = fi_inj + 1;
            (void)fi_prl;  // DR: why is this not used?

            fine(n, fk_inj, fj_inj, fi_inj) = coarse(n, k, j, i);

          }
        }
      } // (inj, inj, inj)
      //-----------------------------------------------------------------------

    } // function component loop

  } else if (pmb->pmy_mesh->ndim == 2) {
#if ISEVEN(NGHOST)
    int const eo_offset = 0;

    int const si_inj{(si)};
    int const sj_inj{(sj)};

    int const ei_inj{(ei)};
    int const ej_inj{(ej)};
#else
    int const eo_offset = -1;

    int const fis_inj{(2 * (si - H_NCGHOST) + eo_offset)};
    int const fie_inj{(2 * (ei - H_NCGHOST) + eo_offset)};

    int const fjs_inj{(2 * (sj - H_NCGHOST) + eo_offset)};
    int const fje_inj{(2 * (ej - H_NCGHOST) + eo_offset)};


    int const si_inj{(fis_inj < 0) ? si + 1 : si};
    int const ei_inj{(fie_inj > 2 * NGHOST + pmb->block_size.nx1) ? ei - 1 : ei};

    int const sj_inj{(fjs_inj < 0) ? sj + 1 : sj};
    int const ej_inj{(fje_inj > 2 * NGHOST + pmb->block_size.nx2) ? ej - 1 : ej};
#endif

    //-------------------------------------------------------------------------
    // bias offsets for prolongation (depends on location)
    int const si_prl{(si > pmb->cimp) ? si - 1 : si};
    int const ei_prl{(ei < pmb->cimp) ? ei + 1 : ei};
    int const sj_prl{(sj > pmb->cjmp) ? sj - 1 : sj};
    int const ej_prl{(ej < pmb->cjmp) ? ej + 1 : ej};


    // [running ix]: op

    for (int n = sn; n<= en; ++n) {

      //-----------------------------------------------------------------------
      // [j, i]: interp. 2d
      for (int j = sj_prl; j < ej_prl; ++j) {
        int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
        int const fj_prl = fj_inj + 1;

        for (int i = si_prl; i < ei_prl; ++i) {
          int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
          int const fi_prl = fi_inj + 1;

          fine(n, 0, fj_prl, fi_prl) = 0.;

          // apply stencil via Cartesian product relation
          for (int dj=0; dj<H_SZ; ++dj) {
            int const cj_u = j + dj + 1;
            int const cj_l = j - dj;

            Real const lcj = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dj-1];

            for (int di=0; di<H_SZ; ++di) {
              int const ci_u = i + di + 1;
              int const ci_l = i - di;

              Real const lcji = lcj * InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-di-1];

              Real const fc_uu = coarse(n, 0, cj_u, ci_u);
              Real const fc_ul = coarse(n, 0, cj_u, ci_l);
              Real const fc_lu = coarse(n, 0, cj_l, ci_u);
              Real const fc_ll = coarse(n, 0, cj_l, ci_l);

              fine(n, 0, fj_prl, fi_prl) += lcji * ((fc_uu + fc_ll) + (fc_lu + fc_ul));
            }
          }
        }
      } // (prl, prl)
      //-----------------------------------------------------------------------

      //-----------------------------------------------------------------------
      // [j, i]: (interp. 1d, inject 1d)
      for (int j = sj_prl; j < ej_prl; ++j) {
        int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
        int const fj_prl = fj_inj + 1;

        for (int i = si_inj; i <= ei_inj; ++i) {
          int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
          int const fi_prl = fi_inj + 1;
          (void)fi_prl;  // DR: why is this not used?

          fine(n, 0, fj_prl, fi_inj) = 0.;

          for (int dj=0; dj<H_SZ; ++dj) {
            int const cj_u = j + dj + 1;
            int const cj_l = j - dj;

            Real const lcj = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-dj-1];

            Real const fc_uc = coarse(n, 0, cj_u, i);
            Real const fc_lc = coarse(n, 0, cj_l, i);

            fine(n, 0, fj_prl, fi_inj) += lcj * (fc_uc + fc_lc);
          }
        }
      } // (prl, inj)

      // [j, i]: (inject 1d, interp. 1d)
      for (int j = sj_inj; j <= ej_inj; ++j) {
        int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
        int const fj_prl = fj_inj + 1;
        (void)fj_prl;  // DR: why is this not used?

        for (int i = si_prl; i < ei_prl; ++i) {
          int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
          int const fi_prl = fi_inj + 1;

          fine(n, 0, fj_inj, fi_prl) = 0.;

          for (int di=0; di<H_SZ; ++di) {
            int const ci_u = i + di + 1;
            int const ci_l = i - di;

            Real const lci = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-di-1];

            Real const fc_cu = coarse(n, 0, j, ci_u);
            Real const fc_cl = coarse(n, 0, j, ci_l);

            fine(n, 0, fj_inj, fi_prl) += lci * (fc_cl + fc_cu);
          }
        }
      } // (inj, prl)
      //-----------------------------------------------------------------------

      // [j, i]: inject 2d
      for (int j = sj_inj; j <= ej_inj; ++j) {
        int const fj_inj = 2 * (j - H_NCGHOST) + eo_offset;
        int const fj_prl = fj_inj + 1;
        (void)fj_prl;  // DR: why is this not used?

        for (int i = si_inj; i <= ei_inj; ++i) {
          int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
          int const fi_prl = fi_inj + 1;
          (void)fi_prl;  // DR: why is this not used?

          // injected
          fine(n, 0, fj_inj, fi_inj) = coarse(n, 0, j, i);
        }
      } // (inj, inj)
      //-----------------------------------------------------------------------

    } // function component loop
    //-------------------------------------------------------------------------


  } else {
#if ISEVEN(NGHOST)
    int const eo_offset = 0;

    int const si_inj{(si)};
    int const ei_inj{(ei)};
#else
    int const eo_offset = -1;

    int const fis_inj{(2 * (si - H_NCGHOST) + eo_offset)};
    int const fie_inj{(2 * (ei - H_NCGHOST) + eo_offset)};

    int const si_inj{(fis_inj < 0) ? si + 1 : si};
    int const ei_inj{(fie_inj > 2 * NGHOST + pmb->block_size.nx1) ? ei - 1 : ei};
#endif

    //-------------------------------------------------------------------------
    // bias offsets for prolongation (depends on location)
    int const si_prl{(si > pmb->cimp) ? si - 1 : si};
    int const ei_prl{(ei < pmb->cimp) ? ei + 1 : ei};

    for (int n = sn; n<= en; ++n) {
      for (int i = si_prl; i < ei_prl; ++i) {
        int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
        int const fi_prl = fi_inj + 1;
        fine(n, 0, 0, fi_prl) = 0.;

        // apply stencil
        for (int di=0; di<H_SZ; ++di) {
          int const ci_u = i + di + 1;
          int const ci_l = i - di;

          Real const lc = InterpolateLagrangeUniform_opt<H_SZ>::coeff[H_SZ-di-1];

          Real const fc_u = coarse(n, 0, 0, ci_u);
          Real const fc_l = coarse(n, 0, 0, ci_l);

          fine(n, 0, 0, fi_prl) += lc * (fc_l + fc_u);
        }
      }
      //-----------------------------------------------------------------------
    } // function component loop

    // inject
    for (int n = sn; n<= en; ++n) {
      for (int i = si_inj; i <= ei_inj; ++i) {
        int const fi_inj = 2 * (i - H_NCGHOST) + eo_offset;
        fine(n, 0, 0, fi_inj) = coarse(n, 0, 0, i);
      }
      //-----------------------------------------------------------------------
    } // function component loop

  }

  return;
}

//----------------------------------------------------------------------------------------
//! \fn void MeshRefinement::CheckRefinementCondition()
//  \brief Check refinement criteria

void MeshRefinement::CheckRefinementCondition() {
  MeshBlock *pmb = pmy_block_;
  int ret = 0, aret = -1;
  refine_flag_ = 0;

  // *** should be implemented later ***
  // loop-over refinement criteria
  if (AMRFlag_ != nullptr)
    ret = AMRFlag_(pmb);
  aret = std::max(aret,ret);

  if (aret >= 0)
    deref_count_ = 0;
  if (aret > 0) {
    if (pmb->loc.level == pmb->pmy_mesh->max_level) {
      refine_flag_ = 0;
    } else {
      refine_flag_ = 1;
    }
  } else if (aret < 0) {
    if (pmb->loc.level == pmb->pmy_mesh->root_level) {
      refine_flag_ = 0;
      deref_count_ = 0;
    } else {
      deref_count_++;
      int ec = 0, js, je, ks, ke;
      if (pmb->block_size.nx2 > 1) {
        js = -1;
        je = 1;
      } else {
        js = 0;
        je = 0;
      }
      if (pmb->block_size.nx3 > 1) {
        ks = -1;
        ke = 1;
      } else {
        ks = 0;
        ke = 0;
      }
      for (int k=ks; k<=ke; k++) {
        for (int j=js; j<=je; j++) {
          for (int i=-1; i<=1; i++)
            if (pmb->pbval->nblevel[k+1][j+1][i+1]>pmb->loc.level) ec++;
        }
      }
      if (ec > 0) {
        refine_flag_ = 0;
      } else {
        if (deref_count_ >= deref_threshold_) {
          refine_flag_ = -1;
        } else {
          refine_flag_ = 0;
        }
      }
    }
  }
  return;
}

// TODO(felker): consider merging w/ MeshBlock::pvars_cc, etc. See meshblock.cpp
int MeshRefinement::AddToRefinementVC(AthenaArray<Real> *pvar_in,
                                    AthenaArray<Real> *pcoarse_in) {
  pvars_vc_.push_back(std::make_tuple(pvar_in, pcoarse_in));
  return static_cast<int>(pvars_vc_.size() - 1);
}

int MeshRefinement::AddToRefinementCC(AthenaArray<Real> *pvar_in,
                                    AthenaArray<Real> *pcoarse_in) {
  pvars_cc_.push_back(std::make_tuple(pvar_in, pcoarse_in));
  return static_cast<int>(pvars_cc_.size() - 1);
}

int MeshRefinement::AddToRefinementFC(FaceField *pvar_fc, FaceField *pcoarse_fc) {
  pvars_fc_.push_back(std::make_tuple(pvar_fc, pcoarse_fc));
  return static_cast<int>(pvars_fc_.size() - 1);
}

// Currently, only called in 2x functions in bvals_refine.cpp:
// ----------
// - BoundaryValues::RestrictGhostCellsOnSameLevel()--- to perform additional
// restriction on primitive Hydro standard/coarse arrays (only for GR) without changing
// the var_cc/coarse_buf pointer members of the HydroBoundaryVariable.

// - BoundaryValues::ProlongateGhostCells()--- to ensure prolongation occurs on conserved
// (not primitive) variable standard/coarse arrays for Hydro, PassiveScalars

// Should probably consolidate this function and std::vector of tuples with
// BoundaryVariable interface ptr members. Too much independent switching of ptrs!
// ----------
// Even though we currently do not have special GR functionality planned for
// PassiveScalars::coarse_r_ like Hydro::coarse_prim_
// (it is never transferred in Mesh::LoadBalancingAndAdaptiveMeshRefinement)
// the physical (non-periodic) boundary functions will still apply only to the PRIMITIVE
// scalar variable arrays, thus S/AMR demand 1) AthenaArray<Real> PassiveScalars::coarse_r
// 2) ability to switch (s, coarse_s) and (r, coarse_r) ptrs in MeshRefinement::bvals_cc_

void MeshRefinement::SetHydroRefinement(HydroBoundaryQuantity hydro_type) {
  // TODO(felker): make more general so it can be used as SetPassiveScalarsRefinement()
  // e.g. refer to "int Hydro::refinement_idx" instead of assuming that the correct tuple
  // is in the first vector entry
  Hydro *ph = pmy_block_->phydro;
  // hard-coded assumption that, if multilevel, then Hydro is always present
  // and enrolled in mesh refinement in the first pvars_cc_ vector entry
  switch (hydro_type) {
    case (HydroBoundaryQuantity::cons): {
      pvars_cc_.front() = std::make_tuple(&ph->u, &ph->coarse_cons_);
      break;
    }
    case (HydroBoundaryQuantity::prim): {
      pvars_cc_.front() = std::make_tuple(&ph->w, &ph->coarse_prim_);
      break;
    }
  }
  return;
}
