//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file bvals_shear_fc.cpp
//  \brief functions that apply shearing box BCs for face-centered quantities
//======================================================================================

// C headers

// C++ headers
#include <algorithm>  // min
#include <cmath>
#include <cstdlib>
#include <cstring>    // std::memcpy
#include <iomanip>
#include <iostream>   // endl
#include <sstream>    // stringstream
#include <stdexcept>  // runtime_error
#include <string>     // c_str()

// Athena++ headers
#include "../../athena.hpp"
#include "../../athena_arrays.hpp"
#include "../../coordinates/coordinates.hpp"
#include "../../eos/eos.hpp"
#include "../../field/field.hpp"
#include "../../globals.hpp"
#include "../../hydro/hydro.hpp"
#include "../../mesh/mesh.hpp"
#include "../../parameter_input.hpp"
#include "../../utils/buffer_utils.hpp"
#include "../bvals.hpp"
#include "../bvals_interfaces.hpp"

// MPI header
#ifdef MPI_PARALLEL
#include <mpi.h>
#endif


//--------------------------------------------------------------------------------------
//! \fn int FaceCenteredBoundaryVariable::LoadShearing(FaceField &src, Real *buf, int nb)
//  \brief Load shearing box field boundary buffers

void FaceCenteredBoundaryVariable::LoadShearing(FaceField &src, Real *buf, int nb) {
  // TODO(felker): deduplicate with CellCenteredBoundaryVariable::LoadShearing()
  // Only differences are the calculation of psj, pej, and 3x PackData calls
  MeshBlock *pmb = pmy_block_;
  Mesh *pmesh = pmb->pmy_mesh;
  int si, sj, sk, ei, ej, ek;
  int psj, pej; // indices for bx2
  int jo = pbval_->joverlap_;
  int nx2 = pmb->block_size.nx2 - NGHOST;

  si = pmb->is - NGHOST; ei = pmb->is - 1;
  sk = pmb->ks;        ek = pmb->ke;
  if (pmesh->mesh_size.nx3 > 1)  ek += NGHOST, sk -= NGHOST;
  // nb=0-3 for inner boundary; nb=4-7 for outer boundary
  switch(nb) {
    case 0:
      sj = pmb->je - jo - (NGHOST - 1); ej = pmb->je;
      if (jo > nx2) sj = pmb->js;
      psj = sj; pej = ej + 1;
      break;
    case 1:
      sj = pmb->js; ej = pmb->je - jo + NGHOST;
      if (jo < NGHOST) ej = pmb->je;
      psj = sj; pej = ej + 1;
      break;
    case 2:
      sj = pmb->je - (NGHOST - 1); ej = pmb->je;
      if (jo > nx2) sj = pmb->je - (jo - nx2) + 1;
      psj = sj; pej = ej;
      break;
    case 3:
      sj = pmb->js; ej = pmb->js + (NGHOST - 1);
      if (jo < NGHOST) ej = pmb->js + (NGHOST - jo) - 1;
      psj = sj + 1; pej = ej + 1;
      break;
    case 4:
      sj = pmb->js; ej = pmb->js + jo + NGHOST - 1;
      if (jo > nx2) ej = pmb->je;
      psj = sj; pej = ej + 1;
      break;
    case 5:
      sj = pmb->js + jo - NGHOST; ej = pmb->je;
      if (jo < NGHOST) sj = pmb->js;
      psj = sj; pej = ej + 1;
      break;
    case 6:
      sj = pmb->js; ej = pmb->js + (NGHOST - 1);
      if (jo > nx2) ej = pmb->js + (jo - nx2) - 1;
      psj = sj + 1; pej = ej + 1;
      break;
    case 7:
      sj = pmb->je - (NGHOST - 1); ej = pmb->je;
      if (jo < NGHOST) sj = pmb->je - (NGHOST - jo) + 1;
      psj = sj; pej = ej;
      break;
    default:
      std::stringstream msg;
      msg << "### FATAL ERROR in FaceCenteredBoundaryVariable:LoadShearing " << std::endl
          << "nb = " << nb << " not valid" << std::endl;
      ATHENA_ERROR(msg);
  }
  int p = 0;
  BufferUtility::PackData(src.x1f, buf, si, ei, sj, ej, sk, ek, p);
  BufferUtility::PackData(src.x2f, buf, si, ei, psj, pej, sk, ek, p);
  BufferUtility::PackData(src.x3f, buf, si, ei, sj, ej, sk, ek+1, p);
  return;
}

//--------------------------------------------------------------------------------------
//! \fn void FaceCenteredBoundaryVariable::SendShearingBoxBoundaryBuffers()
//  \brief Send shearing box boundary buffers for field variables

void FaceCenteredBoundaryVariable::SendShearingBoxBoundaryBuffers() {
  MeshBlock *pmb = pmy_block_;
  Mesh *pmesh = pmb->pmy_mesh;

  int is = pmb->is; int js = pmb->js; int ks = pmb->ks;
  int ie = pmb->ie; int je = pmb->je; int ke = pmb->ke;
  int ku, kl;
  if (pmesh->mesh_size.nx3 > 1) {
    ku = ke + NGHOST;
    kl = ks - NGHOST;
  } else {
    ku = ke;
    kl = ks;
  }

  int offset[2]{0, 4};
  int sign[2]{1, -1};
  int ib[2]{is - NGHOST, ie + 1};

  for (int upper=0; upper<2; upper++) {
    if (pbval_->is_shear[upper]) {
      Real eps = sign[upper]*pbval_->eps_;
      int jl_remap = js - upper;
      int ju_remap = je + 2 - upper;

      // step 1. -- load shboxvar_fc_
      for (int k=kl; k<=ku; k++) {
        for (int j=js-NGHOST; j<=je+NGHOST; j++) {
          for (int i=0; i<NGHOST; i++) {
            int ii = ib[upper] + i;
            // staggered mesh --> face indices are biased to lower x1
            if (upper) // start loading at first ghost-ghost x1 face (ie+2) at upper x1
              shear_fc_[upper].x1f(k,j,i) = (*var_fc).x1f(k,j,ii+1);
            else       // stop loading before first ghost-real x1 face (is) at lower x1
              shear_fc_[upper].x1f(k,j,i) = (*var_fc).x1f(k,j,ii);
            shear_fc_[upper].x2f(k,j,i) = (*var_fc).x2f(k,j,ii);
            shear_fc_[upper].x3f(k,j,i) = (*var_fc).x3f(k,j,ii);
          }
        }
      }
      // fill the extra cells for B2i and B3i
      int kp = ku + 1;
      for (int j=js-NGHOST; j<=je+NGHOST; j++) {
        for (int i=0; i<NGHOST; i++) {
          int ii = ib[upper] + i;
          shear_fc_[upper].x3f(kp,j,i) = (*var_fc).x3f(kp,j,ii);
        }
      }
      int jp = je + NGHOST + 1;
      for (int k=kl; k<=ku; k++) {
        for (int i=0; i<NGHOST; i++) {
          int ii = ib[upper] + i;
          shear_fc_[upper].x2f(k,jp,i) = (*var_fc).x2f(k,jp,ii);
        }
      }

      // step 2. -- conservative remapping
      for (int k=kl; k<=ku; k++) {  // bx1
        for (int i=0; i<NGHOST; i++) {
          RemapFlux(k, jl_remap, ju_remap, i, eps, shear_fc_[upper].x1f,
                    shear_flx_fc_[upper].x1f);
          for (int j=jl_remap; j<=ju_remap-1; j++) {
            shear_fc_[upper].x1f(k,j,i) -= (shear_flx_fc_[upper].x1f(j+1) -
                                            shear_flx_fc_[upper].x1f(j));
          }
        }
      }
      for (int k=kl; k<=ku; k++) {  // bx2
        for (int i=0; i<NGHOST; i++) {
          RemapFlux(k, jl_remap, ju_remap+1, i, eps, shear_fc_[upper].x2f,
                    shear_flx_fc_[upper].x2f);
          for (int j=jl_remap; j<=ju_remap; j++) {
            shear_fc_[upper].x2f(k,j,i) -= (shear_flx_fc_[upper].x2f(j+1) -
                                            shear_flx_fc_[upper].x2f(j));
          }
        }
      }
      for (int k=kl; k<=ku+1; k++) { // bx3
        for (int i=0; i<NGHOST; i++) {
          RemapFlux(k, jl_remap, ju_remap, i, eps, shear_fc_[upper].x3f,
                    shear_flx_fc_[upper].x3f);
          for (int j=jl_remap; j<=ju_remap-1; j++) {
            shear_fc_[upper].x3f(k,j,i) -= (shear_flx_fc_[upper].x3f(j+1) -
                                            shear_flx_fc_[upper].x3f(j));
          }
        }
      }

      // step 3. -- load sendbuf; memcpy to recvbuf if on same rank, else post MPI_Isend
      for (int n=0; n<4; n++) {
        SimpleNeighborBlock& snb = pbval_->shear_send_neighbor_[upper][n];
        if (snb.rank != -1) {
          LoadShearing(shear_fc_[upper], shear_bd_var_[upper].send[n], n+offset[upper]);
          if (snb.rank == Globals::my_rank) {
            CopyShearBufferSameProcess(snb, shear_send_count_fc_[upper][n], n, upper);
          } else { // MPI
#ifdef MPI_PARALLEL
            // bufid = n
            int tag = pbval_->CreateBvalsMPITag(snb.lid, n+offset[upper],
                                                shear_fc_phys_id_);
            MPI_Isend(shear_bd_var_[upper].send[n], shear_send_count_fc_[upper][n],
                      MPI_ATHENA_REAL, snb.rank, tag, MPI_COMM_WORLD,
                      &shear_bd_var_[upper].req_send[n]);
#endif
          }
        }
      }
    }  // if boundary is shearing
  }  // loop over inner/outer boundaries
  return;
}

//--------------------------------------------------------------------------------------
//! \fn void FaceCenteredBoundaryVariable::SetShearingBoxBoundarySameLevel(
//                                           Real *buf, const int nb)
//  \brief Set field shearing box boundary received from a block on the same level

// TODO(felker): deduplicate with CellCenteredBoundaryVariable::SetShearingBoxBound...()
// Only differences are the calculation of psi,pei,psj,pej, and 3x UnpackData calls

void FaceCenteredBoundaryVariable::SetShearingBoxBoundarySameLevel(Real *buf,
                                                                   const int nb) {
  MeshBlock *pmb = pmy_block_;
  Mesh *pmesh = pmb->pmy_mesh;
  int si, sj, sk, ei, ej, ek;
  int psi, pei, psj, pej;
  int jo = pbval_->joverlap_;
  int nx2 = pmb->block_size.nx2 - NGHOST;
  int nxo = pmb->block_size.nx2 - jo;

  sk = pmb->ks; ek = pmb->ke;
  if (pmesh->mesh_size.nx3 > 1) ek += NGHOST, sk -= NGHOST;
  switch(nb) {
    case 0:
      si = pmb->is - NGHOST; ei = pmb->is - 1;
      sj = pmb->js - NGHOST; ej = pmb->js + (jo - 1);
      if (jo > nx2) sj = pmb->js - nxo;
      psi = si; pei = ei; psj = sj; pej = ej + 1;
      break;
    case 1:
      si = pmb->is - NGHOST; ei = pmb->is - 1;
      sj = pmb->js + jo; ej = pmb->je + NGHOST;
      if (jo < NGHOST) ej = pmb->je + jo;
      psi = si; pei = ei; psj = sj; pej = ej + 1;
      break;
    case 2:
      si = pmb->is - NGHOST; ei = pmb->is - 1;
      sj = pmb->js - NGHOST; ej = pmb->js - 1;
      if (jo > nx2) ej = pmb->js - nxo - 1;
      psi = si; pei = ei; psj = sj; pej = ej;
      break;
    case 3:
      si = pmb->is - NGHOST; ei = pmb->is - 1;
      sj = pmb->je + jo + 1; ej = pmb->je + NGHOST;
      psi = si; pei = ei; psj = sj + 1; pej = ej + 1;
      break;
    case 4:
      si = pmb->ie + 1; ei = pmb->ie + NGHOST;
      sj = pmb->je - (jo - 1); ej = pmb->je + NGHOST;
      if (jo > nx2) ej = pmb->je + nxo;
      psi = si + 1; pei = ei + 1; psj = sj; pej = ej + 1;
      break;
    case 5:
      si = pmb->ie + 1; ei = pmb->ie + NGHOST;
      sj = pmb->js - NGHOST; ej = pmb->je - jo;
      if (jo < NGHOST) sj = pmb->js - jo;
      psi = si + 1; pei = ei + 1; psj = sj; pej = ej + 1;
      break;
    case 6:
      si = pmb->ie + 1; ei = pmb->ie + NGHOST;
      sj = pmb->je + 1; ej = pmb->je + NGHOST;
      if (jo > nx2) sj = pmb->je + nxo + 1;
      psi = si + 1; pei = ei + 1; psj = sj + 1; pej = ej + 1;
      break;
    case 7:
      si = pmb->ie + 1; ei = pmb->ie + NGHOST;
      sj = pmb->js - NGHOST; ej = pmb->js - jo - 1;
      psi = si + 1; pei = ei + 1; psj = sj; pej = ej;
      break;
    default:
      std::stringstream msg;
      msg << "### FATAL ERROR in FaceCenteredBoundaryVariable:SetShearing " << std::endl
          << "nb = " << nb << " not valid" << std::endl;
      ATHENA_ERROR(msg);
  }

  // set [sj:ej] of current meshblock
  int p = 0;
  BufferUtility::UnpackData(buf, (*var_fc).x1f, psi, pei, sj, ej, sk, ek, p);
  BufferUtility::UnpackData(buf, (*var_fc).x2f, si, ei, psj, pej, sk, ek, p);
  BufferUtility::UnpackData(buf, (*var_fc).x3f, si, ei, sj, ej, sk, ek+1, p);
  return;
}

//--------------------------------------------------------------------------------------
//! \fn bool FaceCenteredBoundaryVariable::ReceiveShearingBoxBoundaryBuffers()
//  \brief receive shearing box boundary data for field(face-centered) variables

// TODO(felker): DRY. completely identical to CellCenteredBoundaryVariable implementation
bool FaceCenteredBoundaryVariable::ReceiveShearingBoxBoundaryBuffers() {
  bool flag[2]{true, true};
  int nb_offset[2]{0, 4};

  for (int upper=0; upper<2; upper++) {
    if (pbval_->is_shear[upper]) { // check inner boundaries
      for (int n=0; n<4; n++) {
        if (shear_bd_var_[upper].flag[n] == BoundaryStatus::completed) continue;
        if (shear_bd_var_[upper].flag[n] == BoundaryStatus::waiting) {
          // on the same process
          if (pbval_->shear_recv_neighbor_[upper][n].rank == Globals::my_rank) {
            flag[upper] = false;
            continue;
          } else { // MPI boundary
#ifdef MPI_PARALLEL
            int test;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &test,
                       MPI_STATUS_IGNORE);
            MPI_Test(&shear_bd_var_[upper].req_recv[n], &test, MPI_STATUS_IGNORE);
            if (!static_cast<bool>(test)) {
              flag[upper] = false;
              continue;
            }
            shear_bd_var_[upper].flag[n] = BoundaryStatus::arrived;
#endif
          }
        }
        // set dst if boundary arrived
        SetShearingBoxBoundarySameLevel(shear_bd_var_[upper].recv[n], n+nb_offset[upper]);
        shear_bd_var_[upper].flag[n] = BoundaryStatus::completed; // completed
      } // loop over recv[0] to recv[3]
    }  // if boundary is shearing
  }  // loop over inner/outer boundaries
  return (flag[0] && flag[1]);
}


//--------------------------------------------------------------------------------------
//! \fn void FaceCenteredBoundaryVariable::RemapFlux(const int k, const int jinner,
///                                         const int jouter, int i,
//                                          Real eps, static AthenaArray<Real> &U,
//                                          AthenaArray<Real> &Flux)
//  \brief compute the flux along j indices for remapping adopted from 2nd order RemapFlux
//         of Athena4.0

void FaceCenteredBoundaryVariable::RemapFlux(const int k, const int jinner,
                                             const int jouter,
                                             const int i, const Real eps,
                                             const AthenaArray<Real> &var,
                                             AthenaArray<Real> &flux) {
  int j, jl, ju;
  Real dUc, dUl, dUr, dUm, lim_slope;

  // jinner, jouter are index range over which flux must be returned.  Set loop
  // limits depending on direction of upwind differences

  if (eps > 0.0) { //eps always > 0 for inner i boundary
    jl = jinner - 1;
    ju = jouter - 1;
  } else {         // eps always < 0 for outer i boundary
    jl = jinner;
    ju = jouter;
  }

  for (j=jl; j<=ju; j++) {
    dUc = var(k,j+1,i) - var(k,j-1,i);
    dUl = var(k,j,  i) - var(k,j-1,i);
    dUr = var(k,j+1,i) - var(k,j,  i);

    dUm = 0.0;
    if (dUl*dUr > 0.0) {
      lim_slope = std::min(std::abs(dUl),std::abs(dUr));
      dUm = SIGN(dUc)*std::min(0.5*std::abs(dUc),2.0*lim_slope);
    }

    if (eps > 0.0) { // eps always > 0 for inner i boundary
      flux(j+1) = eps*(var(k,j,i) + 0.5*(1.0 - eps)*dUm);
    } else {         // eps always < 0 for outer i boundary
      flux(j  ) = eps*(var(k,j,i) - 0.5*(1.0 + eps)*dUm);
    }
  }
  return;
}


void FaceCenteredBoundaryVariable::StartReceivingShear(BoundaryCommSubset phase) {
#ifdef MPI_PARALLEL
  int size, tag;
  int tag_offset[2]{0, 4};
  for (int upper=0; upper<2; upper++) {
    if (pbval_->is_shear[upper]) {
      for (int n=0; n<4; n++) {
        int target_rank = pbval_->shear_recv_neighbor_[upper][n].rank;
        if ((target_rank != Globals::my_rank) && (target_rank != -1)) {
          // var_vc
          size = shear_recv_count_fc_[upper][n];
          tag  = pbval_->CreateBvalsMPITag(pmy_block_->lid, n+tag_offset[upper],
                                           shear_fc_phys_id_);
          MPI_Irecv(shear_bd_var_[upper].recv[n], size, MPI_ATHENA_REAL,
                    pbval_->shear_recv_neighbor_[upper][n].rank, tag, MPI_COMM_WORLD,
                    &shear_bd_var_[upper].req_recv[n]);
          // emf
          size = shear_recv_count_emf_[upper][n];
          tag  = pbval_->CreateBvalsMPITag(pmy_block_->lid, n+tag_offset[upper],
                                           shear_emf_phys_id_);
          MPI_Irecv(shear_bd_emf_[upper].recv[n], size, MPI_ATHENA_REAL,
                    pbval_->shear_recv_neighbor_[upper][n].rank, tag, MPI_COMM_WORLD,
                    &shear_bd_emf_[upper].req_recv[n]);
        }
      }
    }
  }
#endif
  return;
}


// TODO(felker): also set sflag members of ShearingBoundaryData
void FaceCenteredBoundaryVariable::ComputeShear(const Real time) {
  MeshBlock *pmb = pmy_block_;
  int &jo = pbval_->joverlap_;
  int nx2 = pmb->block_size.nx2;
  int nx3 = pmb->block_size.nx3;
  int nc3 = pmb->ncells3;;

  // scaling terms when computing inner/outersize_fc_[], fc_flx_[]
  // where size = a1*nx + a0
  const int a1_fc = NGHOST*(NFIELD*nc3 + 1);
  const int a0_fc = NGHOST*nc3;   // only in a few cases

  const int a1_fc_flx = (2*nx3 + 1);
  const int a0_fc_flx = nx3;    // only in a few cases

  // unlike the CellCenteredBoundaryVariable implementation of this fn, the scaling factor
  // for the counts is not a constant ssize. the n=1 (always set) and n=0 (only set if
  // overlap) have different expressions than the other cases
  for (int upper=0; upper<2; upper++) {
    if (pbval_->is_shear[upper]) {
      for (int n=0; n<4; n++) {
        shear_send_count_fc_[upper][n] = 0;
        shear_recv_count_fc_[upper][n] = 0;
        shear_bd_var_[upper].flag[n] = BoundaryStatus::completed;

        shear_send_count_emf_[upper][n] = 0;
        shear_recv_count_emf_[upper][n] = 0;
        shear_bd_emf_[upper].flag[n] = BoundaryStatus::completed;
      }
      // send [js-NGHOST:je-joverlap] of the current MeshBlock to the shearing neighbor
      // attach [je-joverlap+1:MIN(je-joverlap + NGHOST, je-js+1)] to its right end.

      // recv [js+joverlap:je] of the current MeshBlock to the shearing neighbor
      // attach [je+1:MIN(je+NGHOST, je+joverlap)] to its right end.
      shear_send_count_fc_[upper][1] = pbval_->shear_send_count_[upper][1]*a1_fc + a0_fc;
      shear_recv_count_fc_[upper][1] = shear_send_count_fc_[upper][1];
      shear_bd_var_[upper].flag[1] = BoundaryStatus::waiting;

      shear_send_count_emf_[upper][1] = pbval_->shear_send_count_[upper][1]*a1_fc_flx
                                        + a0_fc_flx;
      shear_recv_count_emf_[upper][1] = shear_send_count_emf_[upper][1];
      shear_bd_emf_[upper].flag[1] = BoundaryStatus::waiting;

      // if there is overlap to next blocks
      if (jo != 0) {
        // send to the right
        // receive from its left
        shear_send_count_fc_[upper][0] = pbval_->shear_send_count_[upper][0]*a1_fc
                                         + a0_fc;
        shear_recv_count_fc_[upper][0] = shear_send_count_fc_[upper][0];
        shear_bd_var_[upper].flag[0] = BoundaryStatus::waiting;

        shear_send_count_emf_[upper][0] = pbval_->shear_send_count_[upper][0]*a1_fc_flx
                                          + a0_fc_flx;
        shear_recv_count_emf_[upper][0] = shear_send_count_emf_[upper][0];
        shear_bd_emf_[upper].flag[0] = BoundaryStatus::waiting;

        // deal the left boundary cells with send[2]
        if (jo > (nx2 - NGHOST)) {
          // send to Right, recv from Left
          shear_send_count_fc_[upper][2] = pbval_->shear_send_count_[upper][2]*a1_fc;
          shear_recv_count_fc_[upper][2] = shear_send_count_fc_[upper][2];
          shear_bd_var_[upper].flag[2] = BoundaryStatus::waiting;

          shear_send_count_emf_[upper][2] = pbval_->shear_send_count_[upper][2]*a1_fc_flx;
          shear_recv_count_emf_[upper][2] = shear_send_count_emf_[upper][2];
          shear_bd_emf_[upper].flag[2] = BoundaryStatus::waiting;
        }
        // deal with the right boundary cells with send[3]
        if (jo < NGHOST) {
          shear_send_count_fc_[upper][3] = pbval_->shear_send_count_[upper][3]*a1_fc;
          shear_recv_count_fc_[upper][3] = shear_send_count_fc_[upper][3];
          shear_bd_var_[upper].flag[3] = BoundaryStatus::waiting;

          shear_send_count_emf_[upper][3] = pbval_->shear_send_count_[upper][3]*a1_fc_flx;
          shear_recv_count_emf_[upper][3] = shear_send_count_emf_[upper][3];
          shear_bd_emf_[upper].flag[3] = BoundaryStatus::waiting;
        }
      } else {  // jo == 0
        // send [je-(NGHOST-1):je] to Right (outer x2)
        // recv [js-NGHOST:js-1] from Left
        shear_send_count_fc_[upper][2] = pbval_->shear_send_count_[upper][2]*a1_fc;
        shear_recv_count_fc_[upper][2] = shear_send_count_fc_[upper][2];
        shear_bd_var_[upper].flag[2] = BoundaryStatus::waiting;

        shear_send_count_emf_[upper][2] = pbval_->shear_send_count_[upper][2]*a1_fc_flx;
        shear_recv_count_emf_[upper][2] = shear_send_count_emf_[upper][2];
        shear_bd_emf_[upper].flag[2] = BoundaryStatus::waiting;

        // send [js:js+(NGHOST-1)] to Left (inner x2)
        // recv [je + 1:je+(NGHOST-1)] from Right
        shear_send_count_fc_[upper][3] = pbval_->shear_send_count_[upper][3]*a1_fc;
        shear_recv_count_fc_[upper][3] = shear_send_count_fc_[upper][3];
        shear_bd_var_[upper].flag[3] = BoundaryStatus::waiting;

        shear_send_count_emf_[upper][3] = pbval_->shear_send_count_[upper][3]*a1_fc_flx;
        shear_recv_count_emf_[upper][3] = shear_send_count_emf_[upper][3];
        shear_bd_emf_[upper].flag[3] = BoundaryStatus::waiting;
      }
    }
  }
  return;
}
