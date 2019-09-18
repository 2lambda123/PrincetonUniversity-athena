//=======================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file bvals.cpp
//  \brief constructor/destructor and utility functions for BoundaryValues class

// C headers

// C++ headers
#include <algorithm>  // min
#include <cmath>
#include <cstdlib>
#include <cstring>    // std::memcpy
#include <iomanip>
#include <iostream>   // endl
#include <iterator>
#include <limits>
#include <sstream>    // stringstream
#include <stdexcept>  // runtime_error
#include <string>     // c_str()
#include <utility>    // swap()
#include <vector>

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../coordinates/coordinates.hpp"
#include "../eos/eos.hpp"
#include "../field/field.hpp"
#include "../globals.hpp"
#include "../gravity/mg_gravity.hpp"
#include "../hydro/hydro.hpp"
#include "../mesh/mesh.hpp"
#include "../mesh/mesh_refinement.hpp"
#include "../multigrid/multigrid.hpp"
#include "../parameter_input.hpp"
#include "../scalars/scalars.hpp"
#include "../utils/buffer_utils.hpp"
#include "bvals.hpp"

// MPI header
#ifdef MPI_PARALLEL
#include <mpi.h>
#endif

// BoundaryValues constructor (the first object constructed inside the MeshBlock()
// constructor): sets functions for the appropriate boundary conditions at each of the 6
// dirs of a MeshBlock
BoundaryValues::BoundaryValues(MeshBlock *pmb, BoundaryFlag *input_bcs,
                               ParameterInput *pin)
    : BoundaryBase(pmb->pmy_mesh, pmb->loc, pmb->block_size, input_bcs), pmy_block_(pmb),
      shear_send_neighbor_{}, shear_recv_neighbor_{},
      shear_send_count_{}, shear_recv_count_{} {
  // Check BC functions for each of the 6 boundaries in turn ---------------------
  for (int i=0; i<6; i++) {
    switch (block_bcs[i]) {
      case BoundaryFlag::reflect:
      case BoundaryFlag::outflow:
      case BoundaryFlag::user:
      case BoundaryFlag::polar_wedge:
        apply_bndry_fn_[i] = true;
        break;
      default: // already initialized to false in class
        break;
    }
  }
  // Inner x1
  nface_ = 2; nedge_ = 0;
  CheckBoundaryFlag(block_bcs[BoundaryFace::inner_x1], CoordinateDirection::X1DIR);
  CheckBoundaryFlag(block_bcs[BoundaryFace::outer_x1], CoordinateDirection::X1DIR);

  if (pmb->block_size.nx2 > 1) {
    nface_ = 4; nedge_ = 4;
    CheckBoundaryFlag(block_bcs[BoundaryFace::inner_x2], CoordinateDirection::X2DIR);
    CheckBoundaryFlag(block_bcs[BoundaryFace::outer_x2], CoordinateDirection::X2DIR);
  }

  if (pmb->block_size.nx3 > 1) {
    nface_ = 6; nedge_ = 12;
    CheckBoundaryFlag(block_bcs[BoundaryFace::inner_x3], CoordinateDirection::X3DIR);
    CheckBoundaryFlag(block_bcs[BoundaryFace::outer_x3], CoordinateDirection::X3DIR);
  }
  // Perform compatibilty checks of user selections of polar vs. polar_wedge boundaries
  if (block_bcs[BoundaryFace::inner_x2] == BoundaryFlag::polar
      || block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::polar
      || block_bcs[BoundaryFace::inner_x2] == BoundaryFlag::polar_wedge
      || block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::polar_wedge) {
    CheckPolarBoundaries();
  }

  // polar boundary edge-case: single MeshBlock spans the entire azimuthal (x3) range
  if ((pmb->loc.level == pmy_mesh_->root_level && pmy_mesh_->nrbx3 == 1)
      && (block_bcs[BoundaryFace::inner_x2] == BoundaryFlag::polar
       || block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::polar
       || block_bcs[BoundaryFace::inner_x2] == BoundaryFlag::polar_wedge
       || block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::polar_wedge))
    azimuthal_shift_.NewAthenaArray(pmb->ke + NGHOST + 2);

  // prevent reallocation of contiguous memory space for each of 4x possible calls to
  // std::vector<BoundaryVariable *>.push_back() in Hydro, Field, Gravity, PassiveScalars
  bvars.reserve(3);
  // TOOD(KGF): rename to "bvars_time_int"? What about a std::vector for bvars_sts?
  bvars_main_int.reserve(2);

  // Matches initial value of Mesh::next_phys_id_
  // reserve phys=0 for former TAG_AMR=8; now hard-coded in Mesh::CreateAMRMPITag()
  bvars_next_phys_id_ = 1;

  // KGF: BVals constructor section only containing ALL shearing box-specific stuff
  // set parameters for shearing box bc and allocate buffers
  if (SHEARING_BOX) {
    // TODO(felker): add checks on the requisite number of dimensions
    // TODO(felker): move all of these to member initializer list of new shearing class
    Omega_0_ = pin->GetOrAddReal("problem", "Omega0", 0.001);
    qshear_  = pin->GetOrAddReal("problem", "qshear", 1.5);
    ShBoxCoord_ = pin->GetOrAddInteger("problem", "shboxcoord", 1);
    int level = pmb->loc.level - pmy_mesh_->root_level;
    // nblx2 is only used for allocating SimpleNeighborBlock arrays; nblx1 for loc_shear
    // TODO(felker): initialize loc_shear{0, pmy_mesh_->nrbx2*(1L << pmb->loc.level - ..)}
    // in ctor member initializer list and update as refinement occurs. And nblx2
    std::int64_t nblx1 = pmy_mesh_->nrbx1*(1L << level);
    std::int64_t nblx2 = pmy_mesh_->nrbx2*(1L << level);
    // is_shear{} in member init_list
    is_shear[0] = false;
    is_shear[1] = false;
    loc_shear[0] = 0;
    loc_shear[1] = nblx1 - 1;

    if (ShBoxCoord_ == 1) {
      int nc3 = pmb->ncells3;
      ssize_ = NGHOST*nc3;
      // TODO(KGF): much of this should be a part of InitBoundaryData()
      for (int upper=0; upper<2; upper++) {
        if (pmb->loc.lx1 == loc_shear[upper]) { // if true for shearing inner blocks
          is_shear[upper] = true;
          shbb_[upper] = new SimpleNeighborBlock[nblx2];
        } // end "if is a shearing boundary"
      }  // end loop over inner, outer shearing boundaries
    } // end "if (ShBoxCoord_ == 1)"
  } // end shearing box component of BoundaryValues ctor
}

// destructor

BoundaryValues::~BoundaryValues() {
  if (SHEARING_BOX) {
    for (int upper=0; upper<2; upper++)
      if (is_shear[upper]) delete[] shbb_[upper];
  }
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::SetupPersistentMPI()
//  \brief Setup persistent MPI requests to be reused throughout the entire simulation

void BoundaryValues::SetupPersistentMPI() {
  for (auto bvars_it = bvars_main_int.begin(); bvars_it != bvars_main_int.end();
       ++bvars_it) {
    (*bvars_it)->SetupPersistentMPI();
  }

  // KGF: begin exclusive shearing-box section in BoundaryValues::SetupPersistentMPI()
  // initialize the shearing block lists
  if (SHEARING_BOX) {
    MeshBlock *pmb = pmy_block_;
    int nbtotal = pmy_mesh_->nbtotal;
    int *ranklist = pmy_mesh_->ranklist;
    int *nslist = pmy_mesh_->nslist;
    LogicalLocation *loclist = pmy_mesh_->loclist;

    for (int upper=0; upper<2; upper++) {
      int count = 0;
      if (is_shear[upper]) {
        for (int i=0; i<nbtotal; i++) {
          if (loclist[i].lx1 == loc_shear[upper] && loclist[i].lx3 == pmb->loc.lx3 &&
              loclist[i].level == pmb->loc.level) {
            shbb_[upper][count].gid = i;
            shbb_[upper][count].lid = i - nslist[ranklist[i]];
            shbb_[upper][count].rank = ranklist[i];
            shbb_[upper][count].level = loclist[i].level;
            count++;
          }
        }
      }
    }
  } // end KGF: exclusive shearing box portion of SetupPersistentMPI()
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::CheckUserBoundaries()
//  \brief checks if the boundary functions are correctly enrolled (this compatibility
//  check is performed at the top of Mesh::Initialize(), after calling ProblemGenerator())

void BoundaryValues::CheckUserBoundaries() {
  for (int i=0; i<nface_; i++) {
    if (block_bcs[i] == BoundaryFlag::user) {
      if (pmy_mesh_->BoundaryFunction_[i] == nullptr) {
        std::stringstream msg;
        msg << "### FATAL ERROR in BoundaryValues::CheckBoundary" << std::endl
            << "A user-defined boundary is specified but the actual boundary function "
            << "is not enrolled in direction " << i  << " (in [0,6])." << std::endl;
        ATHENA_ERROR(msg);
      }
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::StartReceiving(BoundaryCommSubset phase)
//  \brief initiate MPI_Irecv()

void BoundaryValues::StartReceiving(BoundaryCommSubset phase) {
  for (auto bvars_it = bvars_main_int.begin(); bvars_it != bvars_main_int.end();
       ++bvars_it) {
    (*bvars_it)->StartReceiving(phase);
  }

  // KGF: begin shearing-box exclusive section of original StartReceivingForInit()
  // find send_block_id and recv_block_id;
  if (SHEARING_BOX) {
    StartReceivingShear(phase);
  }
  return;
}


void BoundaryValues::StartReceivingShear(BoundaryCommSubset phase) {
  switch (phase) {
    case BoundaryCommSubset::mesh_init:
      //FindShearBlock(pmy_mesh_->time);
      break;
    case BoundaryCommSubset::all:
      // KGF: must pass "time" parameter from time_integrator.cpp
      //FindShearBlock(time);

      // KGF: cannot simply combine StartReceivingShear() at end of StartReceiving()
      // (which is done for ClearBoundary), because the "shared"/non-virtual fn
      // BoundaryValues::FindShearBlock() must be called in between 2x fns

      // TODO(felker): consider calling FindShearBlock() at the beginning of this fn,
      // which will allow the 2x StartReceiving() to be combined
      for (auto bvar : bvars_main_int) {
        bvar->StartReceivingShear(phase);
      }
      break;
    case BoundaryCommSubset::gr_amr:
      // shearing box is currently incompatible with both GR and AMR
      std::stringstream msg;
      msg << "### FATAL ERROR in BoundaryValues::StartReceiving" << std::endl
          << "BoundaryCommSubset::gr_amr was passed as the 'phase' argument while\n"
          << "SHEARING_BOX=1 is enabled. Shearing box calculations are currently\n"
          << "incompatible with both AMR and GR" << std::endl;
      ATHENA_ERROR(msg);
      break;
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::ClearBoundary(BoundaryCommSubset phase)
//  \brief clean up the boundary flags after each loop

void BoundaryValues::ClearBoundary(BoundaryCommSubset phase) {
  // Note BoundaryCommSubset::mesh_init corresponds to initial exchange of conserved fluid
  // variables and magentic fields, while BoundaryCommSubset::gr_amr corresponds to fluid
  // primitive variables sent only in the case of GR with refinement
  for (auto bvars_it = bvars_main_int.begin(); bvars_it != bvars_main_int.end();
       ++bvars_it) {
    (*bvars_it)->ClearBoundary(phase);
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::ApplyPhysicalBoundaries(const Real time, const Real dt)
//  \brief Apply all the physical boundary conditions for both hydro and field

void BoundaryValues::ApplyPhysicalBoundaries(const Real time, const Real dt) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  int bis = pmb->is - NGHOST, bie = pmb->ie + NGHOST,
      bjs = pmb->js, bje = pmb->je,
      bks = pmb->ks, bke = pmb->ke;

  // Extend the transverse limits that correspond to periodic boundaries as they are
  // updated: x1, then x2, then x3
  if (!apply_bndry_fn_[BoundaryFace::inner_x2] && pmb->block_size.nx2 > 1)
    bjs = pmb->js - NGHOST;
  if (!apply_bndry_fn_[BoundaryFace::outer_x2] && pmb->block_size.nx2 > 1)
    bje = pmb->je + NGHOST;
  if (!apply_bndry_fn_[BoundaryFace::inner_x3] && pmb->block_size.nx3 > 1)
    bks = pmb->ks - NGHOST;
  if (!apply_bndry_fn_[BoundaryFace::outer_x3] && pmb->block_size.nx3 > 1)
    bke = pmb->ke + NGHOST;

  // KGF: temporarily hardcode Hydro and Field access for coupling in EOS U(W) + calc bcc
  // and when passed to user-defined boundary function stored in function pointer array

  // KGF: COUPLING OF QUANTITIES (must be manually specified)
  // downcast BoundaryVariable ptrs to known derived class types: RTTI via dynamic_cast
  HydroBoundaryVariable *phbvar =
      dynamic_cast<HydroBoundaryVariable *>(bvars_main_int[0]);
  Hydro *ph = pmb->phydro;

  // TODO(KGF): passing nullptrs (pf) if no MHD (coarse_* no longer in MeshRefinement)
  // (may be fine to unconditionally directly set to pmb->pfield. See bvals_refine.cpp)

  FaceCenteredBoundaryVariable *pfbvar = nullptr;
  Field *pf = nullptr;
  if (MAGNETIC_FIELDS_ENABLED) {
    pf = pmb->pfield;
    pfbvar = dynamic_cast<FaceCenteredBoundaryVariable *>(bvars_main_int[1]);
  }
  PassiveScalars *ps = nullptr;
  if (NSCALARS > 0) {
    ps = pmb->pscalars;
  }

  // Apply boundary function on inner-x1 and update W,bcc (if not periodic)
  if (apply_bndry_fn_[BoundaryFace::inner_x1]) {
    DispatchBoundaryFunctions(pmb, pco, time, dt,
                              pmb->is, pmb->ie, bjs, bje, bks, bke, NGHOST,
                              ph->w, pf->b, BoundaryFace::inner_x1);
    // KGF: COUPLING OF QUANTITIES (must be manually specified)
    if (MAGNETIC_FIELDS_ENABLED) {
      pmb->pfield->CalculateCellCenteredField(pf->b, pf->bcc, pco,
                                              pmb->is-NGHOST, pmb->is-1,
                                              bjs, bje, bks, bke);
    }
    pmb->peos->PrimitiveToConserved(ph->w, pf->bcc, ph->u, pco,
                                    pmb->is-NGHOST, pmb->is-1, bjs, bje, bks, bke);
    if (NSCALARS > 0) {
      pmb->peos->PassiveScalarPrimitiveToConserved(
          ps->r, ph->u, ps->s, pco, pmb->is-NGHOST, pmb->is-1, bjs, bje, bks, bke);
    }
  }

  // Apply boundary function on outer-x1 and update W,bcc (if not periodic)
  if (apply_bndry_fn_[BoundaryFace::outer_x1]) {
    DispatchBoundaryFunctions(pmb, pco, time, dt,
                              pmb->is, pmb->ie, bjs, bje, bks, bke, NGHOST,
                              ph->w, pf->b, BoundaryFace::outer_x1);
    // KGF: COUPLING OF QUANTITIES (must be manually specified)
    if (MAGNETIC_FIELDS_ENABLED) {
      pmb->pfield->CalculateCellCenteredField(pf->b, pf->bcc, pco,
                                              pmb->ie+1, pmb->ie+NGHOST,
                                              bjs, bje, bks, bke);
    }
    pmb->peos->PrimitiveToConserved(ph->w, pf->bcc, ph->u, pco,
                                    pmb->ie+1, pmb->ie+NGHOST, bjs, bje, bks, bke);
    if (NSCALARS > 0) {
      pmb->peos->PassiveScalarPrimitiveToConserved(
          ps->r, ph->u, ps->s, pco, pmb->ie+1, pmb->ie+NGHOST, bjs, bje, bks, bke);
    }
  }

  if (pmb->block_size.nx2 > 1) { // 2D or 3D
    // Apply boundary function on inner-x2 and update W,bcc (if not periodic)
    if (apply_bndry_fn_[BoundaryFace::inner_x2]) {
      DispatchBoundaryFunctions(pmb, pco, time, dt,
                                bis, bie, pmb->js, pmb->je, bks, bke, NGHOST,
                                ph->w, pf->b, BoundaryFace::inner_x2);
      // KGF: COUPLING OF QUANTITIES (must be manually specified)
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->pfield->CalculateCellCenteredField(pf->b, pf->bcc, pco,
                                                bis, bie, pmb->js-NGHOST, pmb->js-1,
                                                bks, bke);
      }
      pmb->peos->PrimitiveToConserved(ph->w, pf->bcc, ph->u, pco,
                                      bis, bie, pmb->js-NGHOST, pmb->js-1, bks, bke);
      if (NSCALARS > 0) {
        pmb->peos->PassiveScalarPrimitiveToConserved(
            ps->r, ph->u, ps->s, pco, bis, bie, pmb->js-NGHOST, pmb->js-1, bks, bke);
      }
    }

    // Apply boundary function on outer-x2 and update W,bcc (if not periodic)
    if (apply_bndry_fn_[BoundaryFace::outer_x2]) {
      DispatchBoundaryFunctions(pmb, pco, time, dt,
                                bis, bie, pmb->js, pmb->je, bks, bke, NGHOST,
                                ph->w, pf->b, BoundaryFace::outer_x2);
      // KGF: COUPLING OF QUANTITIES (must be manually specified)
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->pfield->CalculateCellCenteredField(pf->b, pf->bcc, pco,
                                                bis, bie, pmb->je+1, pmb->je+NGHOST,
                                                bks, bke);
      }
      pmb->peos->PrimitiveToConserved(ph->w, pf->bcc, ph->u, pco,
                                      bis, bie, pmb->je+1, pmb->je+NGHOST, bks, bke);
      if (NSCALARS > 0) {
        pmb->peos->PassiveScalarPrimitiveToConserved(
            ps->r, ph->u, ps->s, pco, bis, bie, pmb->je+1, pmb->je+NGHOST, bks, bke);
      }
    }
  }

  if (pmb->block_size.nx3 > 1) { // 3D
    bjs = pmb->js - NGHOST;
    bje = pmb->je + NGHOST;

    // Apply boundary function on inner-x3 and update W,bcc (if not periodic)
    if (apply_bndry_fn_[BoundaryFace::inner_x3]) {
      DispatchBoundaryFunctions(pmb, pco, time, dt,
                                bis, bie, bjs, bje, pmb->ks, pmb->ke, NGHOST,
                                ph->w, pf->b, BoundaryFace::inner_x3);
      // KGF: COUPLING OF QUANTITIES (must be manually specified)
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->pfield->CalculateCellCenteredField(pf->b, pf->bcc, pco,
                                                bis, bie, bjs, bje,
                                                pmb->ks-NGHOST, pmb->ks-1);
      }
      pmb->peos->PrimitiveToConserved(ph->w, pf->bcc, ph->u, pco,
                                      bis, bie, bjs, bje, pmb->ks-NGHOST, pmb->ks-1);
      if (NSCALARS > 0) {
        pmb->peos->PassiveScalarPrimitiveToConserved(
            ps->r, ph->u, ps->s, pco, bis, bie, bjs, bje, pmb->ks-NGHOST, pmb->ks-1);
      }
    }

    // Apply boundary function on outer-x3 and update W,bcc (if not periodic)
    if (apply_bndry_fn_[BoundaryFace::outer_x3]) {
      DispatchBoundaryFunctions(pmb, pco, time, dt,
                                bis, bie, bjs, bje, pmb->ks, pmb->ke, NGHOST,
                                ph->w, pf->b, BoundaryFace::outer_x3);
      // KGF: COUPLING OF QUANTITIES (must be manually specified)
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->pfield->CalculateCellCenteredField(pf->b, pf->bcc, pco,
                                                bis, bie, bjs, bje,
                                                pmb->ke+1, pmb->ke+NGHOST);
      }
      pmb->peos->PrimitiveToConserved(ph->w, pf->bcc, ph->u, pco,
                                      bis, bie, bjs, bje, pmb->ke+1, pmb->ke+NGHOST);
      if (NSCALARS > 0) {
        pmb->peos->PassiveScalarPrimitiveToConserved(
            ps->r, ph->u, ps->s, pco, bis, bie, bjs, bje, pmb->ke+1, pmb->ke+NGHOST);
      }
    }
  }
  return;
}


// KGF: should "bvars_it" be fixed in this class member function? Or passed as argument?
void BoundaryValues::DispatchBoundaryFunctions(
    MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
    int il, int iu, int jl, int ju, int kl, int ku, int ngh,
    AthenaArray<Real> &prim, FaceField &b, BoundaryFace face) {
  if (block_bcs[face] ==  BoundaryFlag::user) {  // user-enrolled BCs
    pmy_mesh_->BoundaryFunction_[face](pmb, pco, prim, b, time, dt,
                                       il, iu, jl, ju, kl, ku, NGHOST);
  }
  // KGF: this is only to silence the compiler -Wswitch warnings about not handling the
  // "undef" case when considering all possible BoundaryFace enumerator values. If "undef"
  // is actually passed to this function, it will likely die before that ATHENA_ERROR()
  // call, as it tries to access block_bcs[-1]
  std::stringstream msg;
  msg << "### FATAL ERROR in DispatchBoundaryFunctions" << std::endl
      << "face = BoundaryFace::undef passed to this function" << std::endl;

  // For any function in the BoundaryPhysics interface class, iterate over
  // BoundaryVariable pointers "enrolled"
  for (auto bvars_it = bvars_main_int.begin(); bvars_it != bvars_main_int.end();
       ++bvars_it) {
    switch (block_bcs[face]) {
      case BoundaryFlag::user: // handled above, outside loop over BoundaryVariable objs
        break;
      case BoundaryFlag::reflect:
        switch (face) {
          case BoundaryFace::undef:
            ATHENA_ERROR(msg);
          case BoundaryFace::inner_x1:
            (*bvars_it)->ReflectInnerX1(time, dt, il, jl, ju, kl, ku, NGHOST);
            break;
          case BoundaryFace::outer_x1:
            (*bvars_it)->ReflectOuterX1(time, dt, iu, jl, ju, kl, ku, NGHOST);
            break;
          case BoundaryFace::inner_x2:
            (*bvars_it)->ReflectInnerX2(time, dt, il, iu, jl, kl, ku, NGHOST);
            break;
          case BoundaryFace::outer_x2:
            (*bvars_it)->ReflectOuterX2(time, dt, il, iu, ju, kl, ku, NGHOST);
            break;
          case BoundaryFace::inner_x3:
            (*bvars_it)->ReflectInnerX3(time, dt, il, iu, jl, ju, kl, NGHOST);
            break;
          case BoundaryFace::outer_x3:
            (*bvars_it)->ReflectOuterX3(time, dt, il, iu, jl, ju, ku, NGHOST);
            break;
        }
        break;
      case BoundaryFlag::outflow:
        switch (face) {
          case BoundaryFace::undef:
            ATHENA_ERROR(msg);
          case BoundaryFace::inner_x1:
            (*bvars_it)->OutflowInnerX1(time, dt, il, jl, ju, kl, ku, NGHOST);
            break;
          case BoundaryFace::outer_x1:
            (*bvars_it)->OutflowOuterX1(time, dt, iu, jl, ju, kl, ku, NGHOST);
            break;
          case BoundaryFace::inner_x2:
            (*bvars_it)->OutflowInnerX2(time, dt, il, iu, jl, kl, ku, NGHOST);
            break;
          case BoundaryFace::outer_x2:
            (*bvars_it)->OutflowOuterX2(time, dt, il, iu, ju, kl, ku, NGHOST);
            break;
          case BoundaryFace::inner_x3:
            (*bvars_it)->OutflowInnerX3(time, dt, il, iu, jl, ju, kl, NGHOST);
            break;
          case BoundaryFace::outer_x3:
            (*bvars_it)->OutflowOuterX3(time, dt, il, iu, jl, ju, ku, NGHOST);
            break;
        }
        break;
      case BoundaryFlag::polar_wedge:
        switch (face) {
          case BoundaryFace::undef:
            ATHENA_ERROR(msg);
          case BoundaryFace::inner_x2:
            (*bvars_it)->PolarWedgeInnerX2(time, dt, il, iu, jl, kl, ku, NGHOST);
            break;
          case BoundaryFace::outer_x2:
            (*bvars_it)->PolarWedgeOuterX2(time, dt, il, iu, ju, kl, ku, NGHOST);
            break;
          default:
            std::stringstream msg_polar;
            msg_polar << "### FATAL ERROR in DispatchBoundaryFunctions" << std::endl
                      << "Attempting to call polar wedge boundary function on \n"
                      << "MeshBlock boundary other than inner x2 or outer x2"
                      << std::endl;
            ATHENA_ERROR(msg_polar);
        }
        break;
      default:
        std::stringstream msg_flag;
        msg_flag << "### FATAL ERROR in DispatchBoundaryFunctions" << std::endl
                 << "No BoundaryPhysics function associated with provided\n"
                 << "block_bcs[" << face << "] = BoundaryFlag::"
                 << GetBoundaryString(block_bcs[face]) << std::endl;
        ATHENA_ERROR(msg);
        break;
    } // end switch (block_bcs[face])
  } // end loop over BoundaryVariable *
}


//--------------------------------------------------------------------------------------
//! \fn void BoundaryValues::ComputeShear(const Real time)
//  \brief Calculate the following quantities:
//  send_gid recv_gid send_lid recv_lid send_rank recv_rank,
//  send_size_hydro  recv_size_hydro: for MPI_Irecv
//  eps_,joverlap_: for update the conservative

// TODO(felker): consider breaking up this ~200 (originally 400)line function:

void BoundaryValues::ComputeShear(const Real time) {
  MeshBlock *pmb = pmy_block_;
  Coordinates *pco = pmb->pcoord;
  Mesh *pmesh = pmb->pmy_mesh;
  int nx2 = pmb->block_size.nx2;
  int js = pmb->js; int je = pmb->je;

  int level = pmb->loc.level - pmesh->root_level;
  // TODO(felker): share nblx2 with ctor?
  std::int64_t nblx2 = pmesh->nrbx2*(1L << level);

  // Update the amount of shear:
  Real x1size = pmy_mesh_->mesh_size.x1max - pmy_mesh_->mesh_size.x1min;
  Real x2size = pmy_mesh_->mesh_size.x2max - pmy_mesh_->mesh_size.x2min;
  qomL_ = qshear_*Omega_0_*x1size;
  Real yshear = qomL_*time;
  Real deltay = std::fmod(yshear, x2size);
  int joffset = static_cast<int>(deltay/pco->dx2v(js)); // assumes uniform grid in azimuth
  int Ngrids  = static_cast<int>(joffset/nx2);
  joverlap_   = joffset - Ngrids*nx2;
  eps_ = (std::fmod(deltay, pco->dx2v(js)))/pco->dx2v(js);

  // TODO(felker): generalize from inner case. If upper==1, swap all send/recv arrays:
  // shear_send_neighbor_[][], shear_recv_neighbor_[][]
  // shear_send_count_*_ / shear_recv_count_*_
  for (int upper=0; upper<2; upper++) {
    if (is_shear[upper]) {
      int *counts1 = shear_send_count_[upper];
      int *counts2 = shear_recv_count_[upper];
      SimpleNeighborBlock *nb1 = shear_send_neighbor_[upper];
      SimpleNeighborBlock *nb2 = shear_recv_neighbor_[upper];
      // permute the 2x pairs of send/recv variables if we are at the outer shear boundary
      if (upper) {
        std::swap(counts1, counts2);
        std::swap(nb1, nb2);
      }

      for (int n=0; n<4; n++) {
        nb1[n].gid  = -1;
        nb1[n].lid  = -1;
        nb1[n].rank  = -1;

        nb2[n].gid  = -1;
        nb2[n].lid  = -1;
        nb2[n].rank  = -1;

        counts1[n] = 0;
        counts2[n] = 0;
      }

      int jblock = 0;
      for (int j=0; j<nblx2; j++) {
        // find global index of current MeshBlock on the shearing boundary block list
        if (shbb_[upper][j].gid == pmb->gid) jblock = j;
      }
      // send [js-NGHOST:je-joverlap] of the current MeshBlock to the shearing neighbor
      // attach [je-joverlap+1:MIN(je-joverlap + NGHOST, je-js+1)] to its right end.
      std::int64_t jtmp = jblock + Ngrids;
      if (jtmp > (nblx2 - 1)) jtmp -= nblx2;
      // TODO(felker): replace this with C++ copy semantics (also copy shbb_.level!):
      nb1[1].gid  = shbb_[upper][jtmp].gid;
      nb1[1].rank = shbb_[upper][jtmp].rank;
      nb1[1].lid  = shbb_[upper][jtmp].lid;

      int nx_attach = std::min(je - js - joverlap_ + 1 + NGHOST, je -js + 1);
      // KGF: ssize_=NGHOST*nc3 is unset if ShBoxCoord==2. Is this fine?
      // all counts are scaled by (nu_+1) e.g. NHYDRO in cc/
      counts1[1] = nx_attach;

      // recv [js+joverlap:je] of the current MeshBlock to the shearing neighbor
      // attach [je+1:MIN(je+NGHOST, je+joverlap)] to its right end.
      jtmp = jblock - Ngrids;
      if (jtmp < 0) jtmp += nblx2;
      nb2[1].gid  = shbb_[upper][jtmp].gid;
      nb2[1].rank = shbb_[upper][jtmp].rank;
      nb2[1].lid  = shbb_[upper][jtmp].lid;

      counts2[1] = nx_attach;

      // KGF: what is going on in the above code (since the end of the "for" loop)?

      // if there is overlap to next blocks
      if (joverlap_ != 0) {
        // COMMENT SYNTAX: inner then outer (x1) boundaries
        // send to the right
        // recv from the right
        jtmp = jblock + (Ngrids + 1);
        if (jtmp > (nblx2 - 1)) jtmp -= nblx2;
        nb1[0].gid  = shbb_[upper][jtmp].gid;
        nb1[0].rank = shbb_[upper][jtmp].rank;
        nb1[0].lid  = shbb_[upper][jtmp].lid;

        int nx_exchange = std::min(joverlap_+NGHOST, je -js + 1);
        counts1[0] = nx_exchange;

        // receive from its left
        // send to its left
        jtmp = jblock - (Ngrids + 1);
        if (jtmp < 0) jtmp += nblx2;
        nb2[0].gid  = shbb_[upper][jtmp].gid;
        nb2[0].rank = shbb_[upper][jtmp].rank;
        nb2[0].lid  = shbb_[upper][jtmp].lid;

        counts2[0] = nx_exchange;

        // deal the left boundary cells with send[2]
        if (joverlap_ > (nx2 - NGHOST)) {
          // send to Right
          // send to left
          jtmp = jblock + (Ngrids + 2);
          while (jtmp > (nblx2 - 1)) jtmp -= nblx2;
          nb1[2].gid  = shbb_[upper][jtmp].gid;
          nb1[2].rank = shbb_[upper][jtmp].rank;
          nb1[2].lid  = shbb_[upper][jtmp].lid;

          int nx_exchange_left = joverlap_ - (nx2 - NGHOST);
          counts1[2] = nx_exchange_left;

          // recv from Left
          // send to right
          jtmp = jblock - (Ngrids + 2);
          while (jtmp < 0) jtmp += nblx2;
          nb2[2].gid  = shbb_[upper][jtmp].gid;
          nb2[2].rank = shbb_[upper][jtmp].rank;
          nb2[2].lid  = shbb_[upper][jtmp].lid;

          counts2[2] = nx_exchange_left;
        }
        // deal with the right boundary cells with send[3]
        if (joverlap_ < NGHOST) {
          jtmp = jblock + (Ngrids - 1);
          while (jtmp > (nblx2 - 1)) jtmp -= nblx2;
          while (jtmp < 0) jtmp += nblx2;
          nb1[3].gid  = shbb_[upper][jtmp].gid;
          nb1[3].rank = shbb_[upper][jtmp].rank;
          nb1[3].lid  = shbb_[upper][jtmp].lid;

          int nx_exchange_right = NGHOST - joverlap_;
          counts1[3] = nx_exchange_right;

          jtmp = jblock - (Ngrids - 1);
          while (jtmp > (nblx2 - 1)) jtmp -= nblx2;
          while (jtmp < 0) jtmp += nblx2;
          nb2[3].gid  = shbb_[upper][jtmp].gid;
          nb2[3].rank = shbb_[upper][jtmp].rank;
          nb2[3].lid  = shbb_[upper][jtmp].lid;

          counts2[3] = nx_exchange_right;
        }
      } else {  // joverlap_ == 0
        // send [je-(NGHOST-1):je] to Right (outer x2)
        // recv [je + 1:je+NGHOST] from Left
        jtmp = jblock + (Ngrids + 1);
        while (jtmp > (nblx2 - 1)) jtmp -= nblx2;
        nb1[2].gid  = shbb_[upper][jtmp].gid;
        nb1[2].rank = shbb_[upper][jtmp].rank;
        nb1[2].lid  = shbb_[upper][jtmp].lid;

        int nx_exchange = NGHOST;
        counts1[2] = nx_exchange;

        // recv [js-NGHOST:js-1] from Left
        // send [js:js+NGHOST-1] to Right
        jtmp = jblock - (Ngrids + 1);
        while (jtmp < 0) jtmp += nblx2;
        nb2[2].gid  = shbb_[upper][jtmp].gid;
        nb2[2].rank = shbb_[upper][jtmp].rank;
        nb2[2].lid  = shbb_[upper][jtmp].lid;

        counts2[2] = nx_exchange;

        // send [js:js+(NGHOST-1)] to Left (inner x2)
        // recv [js-NGHOST:js-1] from Left
        jtmp = jblock + (Ngrids - 1);
        while (jtmp > (nblx2 - 1)) jtmp -= nblx2;
        while (jtmp < 0) jtmp += nblx2;
        nb1[3].gid  = shbb_[upper][jtmp].gid;
        nb1[3].rank = shbb_[upper][jtmp].rank;
        nb1[3].lid  = shbb_[upper][jtmp].lid;

        counts1[3] = nx_exchange;

        // recv [je + 1:je+(NGHOST-1)] from Right (outer x2)
        // send [je-(NGHOST-1):je] to Right
        jtmp = jblock - (Ngrids - 1);
        while (jtmp > (nblx2 - 1)) jtmp -= nblx2;
        while (jtmp < 0) jtmp += nblx2;
        nb2[3].gid  = shbb_[upper][jtmp].gid;
        nb2[3].rank = shbb_[upper][jtmp].rank;
        nb2[3].lid  = shbb_[upper][jtmp].lid;

        counts2[3] = nx_exchange;
      }
    }
  } // end loop over inner, outer shearing boundaries

  for (auto bvar : bvars_main_int) {
    bvar->ComputeShear(time);
  }
  return;
}


// Public function, to be called in MeshBlock ctor for keeping MPI tag bitfields
// consistent across MeshBlocks, even if certain MeshBlocks only construct a subset of
// physical variable classes

int BoundaryValues::AdvanceCounterPhysID(int num_phys) {
#ifdef MPI_PARALLEL
  // TODO(felker): add safety checks? input, output are positive, obey <= 31= MAX_NUM_PHYS
  int start_id = bvars_next_phys_id_;
  bvars_next_phys_id_ += num_phys;
  return start_id;
#else
  return 0;
#endif
}
