//========================================================================================
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
#include "../utils/buffer_utils.hpp"
#include "bvals.hpp"

// MPI header
#ifdef MPI_PARALLEL
#include <mpi.h>
#endif

// BoundaryValues constructor - sets functions for the appropriate
// boundary conditions at each of the 6 dirs of a MeshBlock

// called in MeshBlock() constructor

BoundaryValues::BoundaryValues(MeshBlock *pmb, enum BoundaryFlag *input_bcs,
                               ParameterInput *pin)
    : BoundaryBase(pmb->pmy_mesh, pmb->loc, pmb->block_size, input_bcs) {
  pmy_block_=pmb;
  for (int i=0; i<6; i++)
    BoundaryFunction_[i]=nullptr;

  // Set BC functions for each of the 6 boundaries in turn ------------------------------
  // Inner x1
  nface_=2; nedge_=0;
  switch(block_bcs[INNER_X1]) {
    case REFLECTING_BNDRY:
      BoundaryFunction_[INNER_X1] = ReflectInnerX1;
      break;
    case OUTFLOW_BNDRY:
      BoundaryFunction_[INNER_X1] = OutflowInnerX1;
      break;
    case BLOCK_BNDRY: // block boundary
    case PERIODIC_BNDRY: // periodic boundary
      BoundaryFunction_[INNER_X1] = nullptr;
      break;
    // case SHEAR_PERIODIC_BNDRY: // shearing periodic boundary
    //   if (!SHEARING_BOX) block_bcs[INNER_X1]=PERIODIC_BNDRY;
    //   BoundaryFunction_[INNER_X1] = nullptr;
    //   break;
    case USER_BNDRY: // user-enrolled BCs
      BoundaryFunction_[INNER_X1] = pmy_mesh_->BoundaryFunction_[INNER_X1];
      break;
    default:
      std::stringstream msg;
      msg << "### FATAL ERROR in BoundaryValues constructor" << std::endl
          << "Flag ix1_bc=" << block_bcs[INNER_X1] << " not valid" << std::endl;
      ATHENA_ERROR(msg);
      break;
  }

  // Outer x1
  switch(block_bcs[OUTER_X1]) {
    case REFLECTING_BNDRY:
      BoundaryFunction_[OUTER_X1] = ReflectOuterX1;
      break;
    case OUTFLOW_BNDRY:
      BoundaryFunction_[OUTER_X1] = OutflowOuterX1;
      break;
    case BLOCK_BNDRY: // block boundary
    case PERIODIC_BNDRY: // periodic boundary
      BoundaryFunction_[OUTER_X1] = nullptr;
      break;
    // case SHEAR_PERIODIC_BNDRY: // shearing periodic boundary
    //   if (!SHEARING_BOX) block_bcs[OUTER_X1]=PERIODIC_BNDRY;
    //   BoundaryFunction_[OUTER_X1] = nullptr;
    //   break;
    case USER_BNDRY: // user-enrolled BCs
      BoundaryFunction_[OUTER_X1] = pmy_mesh_->BoundaryFunction_[OUTER_X1];
      break;
    default:
      std::stringstream msg;
      msg << "### FATAL ERROR in BoundaryValues constructor" << std::endl
          << "Flag ox1_bc=" << block_bcs[OUTER_X1] << " not valid" << std::endl;
      ATHENA_ERROR(msg);
  }

  if (pmb->block_size.nx2 > 1) {
    nface_=4; nedge_=4;
    // Inner x2
    switch(block_bcs[INNER_X2]) {
      case REFLECTING_BNDRY:
        BoundaryFunction_[INNER_X2] = ReflectInnerX2;
        break;
      case OUTFLOW_BNDRY:
        BoundaryFunction_[INNER_X2] = OutflowInnerX2;
        break;
      case BLOCK_BNDRY: // block boundary
      case PERIODIC_BNDRY: // periodic boundary
      case POLAR_BNDRY: // polar boundary
        BoundaryFunction_[INNER_X2] = nullptr;
        break;
      case POLAR_BNDRY_WEDGE: //polar boundary with a wedge
        BoundaryFunction_[INNER_X2] = PolarWedgeInnerX2;
        break;
      case USER_BNDRY: // user-enrolled BCs
        BoundaryFunction_[INNER_X2] = pmy_mesh_->BoundaryFunction_[INNER_X2];
        break;
      default:
        std::stringstream msg;
        msg << "### FATAL ERROR in BoundaryValues constructor" << std::endl
            << "Flag ix2_bc=" << block_bcs[INNER_X2] << " not valid" << std::endl;
        ATHENA_ERROR(msg);
    }

    // Outer x2
    switch(block_bcs[OUTER_X2]) {
      case REFLECTING_BNDRY:
        BoundaryFunction_[OUTER_X2] = ReflectOuterX2;
        break;
      case OUTFLOW_BNDRY:
        BoundaryFunction_[OUTER_X2] = OutflowOuterX2;
        break;
      case BLOCK_BNDRY: // block boundary
      case PERIODIC_BNDRY: // periodic boundary
      case POLAR_BNDRY: // polar boundary
        BoundaryFunction_[OUTER_X2] = nullptr;
        break;
      case POLAR_BNDRY_WEDGE: // polar boundary with a wedge
        BoundaryFunction_[OUTER_X2] = PolarWedgeOuterX2;
        break;
      case USER_BNDRY: // user-enrolled BCs
        BoundaryFunction_[OUTER_X2] = pmy_mesh_->BoundaryFunction_[OUTER_X2];
        break;
      default:
        std::stringstream msg;
        msg << "### FATAL ERROR in BoundaryValues constructor" << std::endl
            << "Flag ox2_bc=" << block_bcs[OUTER_X2] << " not valid" << std::endl;
        ATHENA_ERROR(msg);
    }
  }

  if (pmb->block_size.nx3 > 1) {
    nface_=6; nedge_=12;
    // Inner x3
    switch(block_bcs[INNER_X3]) {
      case REFLECTING_BNDRY:
        BoundaryFunction_[INNER_X3] = ReflectInnerX3;
        break;
      case OUTFLOW_BNDRY:
        BoundaryFunction_[INNER_X3] = OutflowInnerX3;
        break;
      case BLOCK_BNDRY: // block boundary
      case PERIODIC_BNDRY: // periodic boundary
        BoundaryFunction_[INNER_X3] = nullptr;
        break;
      case USER_BNDRY: // user-enrolled BCs
        BoundaryFunction_[INNER_X3] = pmy_mesh_->BoundaryFunction_[INNER_X3];
        break;
      default:
        std::stringstream msg;
        msg << "### FATAL ERROR in BoundaryValues constructor" << std::endl
            << "Flag ix3_bc=" << block_bcs[INNER_X3] << " not valid" << std::endl;
        ATHENA_ERROR(msg);
    }

    // Outer x3
    switch(block_bcs[OUTER_X3]) {
      case REFLECTING_BNDRY:
        BoundaryFunction_[OUTER_X3] = ReflectOuterX3;
        break;
      case OUTFLOW_BNDRY:
        BoundaryFunction_[OUTER_X3] = OutflowOuterX3;
        break;
      case BLOCK_BNDRY: // block boundary
      case PERIODIC_BNDRY: // periodic boundary
        BoundaryFunction_[OUTER_X3] = nullptr;
        break;
      case USER_BNDRY: // user-enrolled BCs
        BoundaryFunction_[OUTER_X3] = pmy_mesh_->BoundaryFunction_[OUTER_X3];
        break;
      default:
        std::stringstream msg;
        msg << "### FATAL ERROR in BoundaryValues constructor" << std::endl
            << "Flag ox3_bc=" << block_bcs[OUTER_X3] << " not valid" << std::endl;
        ATHENA_ERROR(msg);
    }
  }
  // Perform compatibilty checks of user selections of polar vs. polar_wedge boundaries
  if (block_bcs[INNER_X2] == POLAR_BNDRY || block_bcs[OUTER_X2] == POLAR_BNDRY
      || block_bcs[INNER_X2] == POLAR_BNDRY_WEDGE
      || block_bcs[OUTER_X2] == POLAR_BNDRY_WEDGE) {
    CheckPolarBoundaries();
  }

  // Count number of blocks wrapping around pole
  if (block_bcs[INNER_X2] == POLAR_BNDRY || block_bcs[INNER_X2] == POLAR_BNDRY_WEDGE) {
    int level = pmb->loc.level - pmy_mesh_->root_level;
    // KGF: possible 32-bit int overflow, if level > 31 (or possibly less, if nrbx3>1 !)
    num_north_polar_blocks_ = static_cast<int>(pmy_mesh_->nrbx3 * (1 << level));
  } else {
    num_north_polar_blocks_ = 0;
  }
  if (block_bcs[OUTER_X2] == POLAR_BNDRY || block_bcs[OUTER_X2] == POLAR_BNDRY_WEDGE) {
    int level = pmb->loc.level - pmy_mesh_->root_level;
    // KGF: possible 32-bit int overflow, if level > 31 (or possibly less, if nrbx3>1 !)
    num_south_polar_blocks_ = static_cast<int>(pmy_mesh_->nrbx3 * (1 << level));
  } else {
    num_south_polar_blocks_ = 0;
  }
  // end KGF: shared logic of setting boundary functions and counting spherical blocks

  //

  // polar boundary edge-case: single MeshBlock spans the entire azimuthal (x3) range
  // KGF: (fixed by Z. Zhu on 2016-01-15 in ff7b4b1)
  if (pmb->loc.level == pmy_mesh_->root_level &&
     pmy_mesh_->nrbx3 == 1 &&
     (block_bcs[INNER_X2]==POLAR_BNDRY || block_bcs[OUTER_X2]==POLAR_BNDRY ||
      block_bcs[INNER_X2]==POLAR_BNDRY_WEDGE || block_bcs[OUTER_X2]==POLAR_BNDRY_WEDGE))
       azimuthal_shift_.NewAthenaArray(pmb->ke+NGHOST+2);
  // end KGF: special handling for spherical coordinates polar boundary when nrbx3=1

  // KGF: prevent reallocation of contiguous memory space for each of 3x current calls to
  // std::vector<BoundaryVariable *>.push_back() for Hydro, Field, Gravity
  bvars.reserve(3);

  // KGF: BVals constructor section only containing ALL shearing box-specific stuff
  // set parameters for shearing box bc and allocate buffers
//   if (SHEARING_BOX) {
//     Omega_0_ = pin->GetOrAddReal("problem","Omega0",0.001);
//     qshear_  = pin->GetOrAddReal("problem","qshear",1.5);
//     ShBoxCoord_ = pin->GetOrAddInteger("problem","shboxcoord",1);
//     x1size_ = pmy_mesh_->mesh_size.x1max - pmy_mesh_->mesh_size.x1min;
//     x2size_ = pmy_mesh_->mesh_size.x2max - pmy_mesh_->mesh_size.x2min;
//     x3size_ = pmy_mesh_->mesh_size.x3max - pmy_mesh_->mesh_size.x3min;
//     int level = pmb->loc.level - pmy_mesh_->root_level;
//     std::int64_t nrbx1 = pmy_mesh_->nrbx1*(1L << level);
//     std::int64_t nrbx2 = pmy_mesh_->nrbx2*(1L << level);
//     shbb_.outer = false;
//     shbb_.inner = false;

//     if (ShBoxCoord_ == 1) {
//       int ncells2 = pmb->block_size.nx2 + 2*NGHOST;
//       int ncells3 = pmb->block_size.nx3;
//       if (pmy_mesh_->mesh_size.nx3>1) ncells3 += 2*NGHOST;
//       ssize_ = NGHOST*ncells3;

//       if (pmb->loc.lx1 == 0) { // if true for shearing inner blocks
//         if (block_bcs[INNER_X1] != SHEAR_PERIODIC_BNDRY) {
//           block_bcs[INNER_X1] = SHEAR_PERIODIC_BNDRY;
//           BoundaryFunction_[INNER_X1] = nullptr;
//         }
//         shboxvar_inner_hydro_.NewAthenaArray(NHYDRO,ncells3,ncells2,NGHOST);
//         flx_inner_hydro_.NewAthenaArray(ncells2);
//         if (MAGNETIC_FIELDS_ENABLED) {
//           shboxvar_inner_field_.x1f.NewAthenaArray(ncells3,ncells2,NGHOST);
//           shboxvar_inner_field_.x2f.NewAthenaArray(ncells3,ncells2+1,NGHOST);
//           shboxvar_inner_field_.x3f.NewAthenaArray(ncells3+1,ncells2,NGHOST);
//           flx_inner_field_.x1f.NewAthenaArray(ncells2);
//           flx_inner_field_.x2f.NewAthenaArray(ncells2+1);
//           flx_inner_field_.x3f.NewAthenaArray(ncells2);
//           shboxvar_inner_emf_.x2e.NewAthenaArray(ncells3+1,ncells2);
//           shboxvar_inner_emf_.x3e.NewAthenaArray(ncells3,ncells2+1);
//           shboxmap_inner_emf_.x2e.NewAthenaArray(ncells3+1,ncells2);
//           shboxmap_inner_emf_.x3e.NewAthenaArray(ncells3,ncells2+1);
//           flx_inner_emf_.x2e.NewAthenaArray(ncells2);
//           flx_inner_emf_.x3e.NewAthenaArray(ncells2+1);
//         }
//         shbb_.inner = true;
//         shbb_.igidlist=new int[nrbx2];
//         shbb_.ilidlist=new int[nrbx2];
//         shbb_.irnklist=new int[nrbx2];
//         shbb_.ilevlist=new int[nrbx2];
//         // attach corner cells from L/R side
//         int size = (pmb->block_size.nx2+NGHOST)*ssize_*NHYDRO;
//         int bsize=0, esize=0;
//         if (MAGNETIC_FIELDS_ENABLED) {
//           // extra cell in azimuth/vertical
//           bsize = (pmb->block_size.nx2+NGHOST+1)*(ssize_+NGHOST)*NFIELD;
//           // face plus edge for EMF
//           esize = 2*(pmb->block_size.nx2+NGHOST)*pmb->block_size.nx3
//                 +pmb->block_size.nx2+pmb->block_size.nx3+NGHOST;
//         }
//         for (int n=0; n<2; n++) {
//           send_innerbuf_hydro_[n] = new Real[size];
//           recv_innerbuf_hydro_[n] = new Real[size];
//           shbox_inner_hydro_flag_[n]=BNDRY_WAITING;
// #ifdef MPI_PARALLEL
//           rq_innersend_hydro_[n] = MPI_REQUEST_NULL;
//           rq_innerrecv_hydro_[n] = MPI_REQUEST_NULL;
// #endif
//           if (MAGNETIC_FIELDS_ENABLED) {
//             send_innerbuf_field_[n] = new Real[bsize];
//             recv_innerbuf_field_[n] = new Real[bsize];
//             shbox_inner_field_flag_[n]=BNDRY_WAITING;
//             send_innerbuf_emf_[n] = new Real[esize];
//             recv_innerbuf_emf_[n] = new Real[esize];
//             shbox_inner_emf_flag_[n]=BNDRY_WAITING;
// #ifdef MPI_PARALLEL
//             rq_innersend_field_[n] = MPI_REQUEST_NULL;
//             rq_innerrecv_field_[n] = MPI_REQUEST_NULL;
//             rq_innersend_emf_[n] = MPI_REQUEST_NULL;
//             rq_innerrecv_emf_[n] = MPI_REQUEST_NULL;
// #endif
//           }
//         }
//         size = NGHOST*ssize_*NHYDRO;// corner cells only
//         if (MAGNETIC_FIELDS_ENABLED) {
//             bsize = NGHOST*(ssize_+NGHOST)*NFIELD;
//             esize = 2*NGHOST*pmb->block_size.nx3+NGHOST;
//         }
//         for (int n=2; n<4; n++) {
//           send_innerbuf_hydro_[n] = new Real[size];
//           recv_innerbuf_hydro_[n] = new Real[size];
//           shbox_inner_hydro_flag_[n]=BNDRY_WAITING;
// #ifdef MPI_PARALLEL
//           rq_innersend_hydro_[n] = MPI_REQUEST_NULL;
//           rq_innerrecv_hydro_[n] = MPI_REQUEST_NULL;
// #endif
//           if (MAGNETIC_FIELDS_ENABLED) {
//             send_innerbuf_field_[n] = new Real[bsize];
//             recv_innerbuf_field_[n] = new Real[bsize];
//             shbox_inner_field_flag_[n]=BNDRY_WAITING;
//             send_innerbuf_emf_[n] = new Real[esize];
//             recv_innerbuf_emf_[n] = new Real[esize];
//             shbox_inner_emf_flag_[n]=BNDRY_WAITING;
// #ifdef MPI_PARALLEL
//             rq_innersend_field_[n] = MPI_REQUEST_NULL;
//             rq_innerrecv_field_[n] = MPI_REQUEST_NULL;
//             rq_innersend_emf_[n] = MPI_REQUEST_NULL;
//             rq_innerrecv_emf_[n] = MPI_REQUEST_NULL;
// #endif
//           }
//         }
//       }

//       if (pmb->loc.lx1 == (nrbx1-1)) { // if true for shearing outer blocks
//         if (block_bcs[OUTER_X1] != SHEAR_PERIODIC_BNDRY) {
//           block_bcs[OUTER_X1] = SHEAR_PERIODIC_BNDRY;
//           BoundaryFunction_[OUTER_X1] = nullptr;
//         }
//         shboxvar_outer_hydro_.NewAthenaArray(NHYDRO,ncells3,ncells2,NGHOST);
//         flx_outer_hydro_.NewAthenaArray(ncells2);
//         if (MAGNETIC_FIELDS_ENABLED) {
//           shboxvar_outer_field_.x1f.NewAthenaArray(ncells3,ncells2,NGHOST);
//           shboxvar_outer_field_.x2f.NewAthenaArray(ncells3,ncells2+1,NGHOST);
//           shboxvar_outer_field_.x3f.NewAthenaArray(ncells3+1,ncells2,NGHOST);
//           flx_outer_field_.x1f.NewAthenaArray(ncells2);
//           flx_outer_field_.x2f.NewAthenaArray(ncells2+1);
//           flx_outer_field_.x3f.NewAthenaArray(ncells2);
//           shboxvar_outer_emf_.x2e.NewAthenaArray(ncells3+1,ncells2);
//           shboxvar_outer_emf_.x3e.NewAthenaArray(ncells3,ncells2+1);
//           shboxmap_outer_emf_.x2e.NewAthenaArray(ncells3+1,ncells2);
//           shboxmap_outer_emf_.x3e.NewAthenaArray(ncells3,ncells2+1);
//           flx_outer_emf_.x2e.NewAthenaArray(ncells2);
//           flx_outer_emf_.x3e.NewAthenaArray(ncells2+1);
//         }
//         shbb_.outer = true;
//         shbb_.ogidlist=new int[nrbx2];
//         shbb_.olidlist=new int[nrbx2];
//         shbb_.ornklist=new int[nrbx2];
//         shbb_.olevlist=new int[nrbx2];
//         // attach corner cells from L/R side
//         int size = (pmb->block_size.nx2+NGHOST)*ssize_*NHYDRO;
//         int bsize=0, esize=0;
//         if (MAGNETIC_FIELDS_ENABLED) {
//           // extra cell in azimuth/vertical
//           bsize = (pmb->block_size.nx2+NGHOST+1)*(ssize_+NGHOST)*NFIELD;
//           // face plus edge for EMF
//           esize = 2*(pmb->block_size.nx2+NGHOST)*pmb->block_size.nx3
//                 +pmb->block_size.nx2+pmb->block_size.nx3+NGHOST;
//         }
//         for (int n=0; n<2; n++) {
//           send_outerbuf_hydro_[n] = new Real[size];
//           recv_outerbuf_hydro_[n] = new Real[size];
//           shbox_outer_hydro_flag_[n]=BNDRY_WAITING;
// #ifdef MPI_PARALLEL
//           rq_outersend_hydro_[n] = MPI_REQUEST_NULL;
//           rq_outerrecv_hydro_[n] = MPI_REQUEST_NULL;
// #endif
//           if (MAGNETIC_FIELDS_ENABLED) {
//             send_outerbuf_field_[n] = new Real[bsize];
//             recv_outerbuf_field_[n] = new Real[bsize];
//             shbox_outer_field_flag_[n]=BNDRY_WAITING;
//             send_outerbuf_emf_[n] = new Real[esize];
//             recv_outerbuf_emf_[n] = new Real[esize];
//             shbox_outer_emf_flag_[n]=BNDRY_WAITING;
// #ifdef MPI_PARALLEL
//             rq_outersend_field_[n] = MPI_REQUEST_NULL;
//             rq_outerrecv_field_[n] = MPI_REQUEST_NULL;
//             rq_outersend_emf_[n] = MPI_REQUEST_NULL;
//             rq_outerrecv_emf_[n] = MPI_REQUEST_NULL;
// #endif
//           }
//         }
//         size = NGHOST*ssize_*NHYDRO;// corner cells only
//         if (MAGNETIC_FIELDS_ENABLED) {
//           bsize = NGHOST*(ssize_+NGHOST)*NFIELD;
//           esize = 2*NGHOST*pmb->block_size.nx3+NGHOST;
//         }
//         for (int n=2; n<4; n++) {
//           send_outerbuf_hydro_[n] = new Real[size];
//           recv_outerbuf_hydro_[n] = new Real[size];
//           shbox_outer_hydro_flag_[n]=BNDRY_WAITING;
// #ifdef MPI_PARALLEL
//           rq_outersend_hydro_[n] = MPI_REQUEST_NULL;
//           rq_outerrecv_hydro_[n] = MPI_REQUEST_NULL;
// #endif
//           if (MAGNETIC_FIELDS_ENABLED) {
//             send_outerbuf_field_[n] = new Real[bsize];
//             recv_outerbuf_field_[n] = new Real[bsize];
//             shbox_outer_field_flag_[n]=BNDRY_WAITING;
//             send_outerbuf_emf_[n] = new Real[esize];
//             recv_outerbuf_emf_[n] = new Real[esize];
//             shbox_outer_emf_flag_[n]=BNDRY_WAITING;
// #ifdef MPI_PARALLEL
//             rq_outersend_field_[n] = MPI_REQUEST_NULL;
//             rq_outerrecv_field_[n] = MPI_REQUEST_NULL;
//             rq_outersend_emf_[n] = MPI_REQUEST_NULL;
//             rq_outerrecv_emf_[n] = MPI_REQUEST_NULL;
// #endif
//           }
//         }
//       }
//     }
//   } // end KGF: shearing box in BoundaryValues constructor
}

// destructor

BoundaryValues::~BoundaryValues() {
  MeshBlock *pmb=pmy_block_;

  // KGF: edge-case of single block across pole in MHD spherical polar coordinates
  // Note, this conditional is outside "if MAGNETIC_FIELDS_ENABLED" in master (also
  // true for its counterpart in constructor). Probably should be inside.
  if (pmb->loc.level == pmy_mesh_->root_level &&
      pmy_mesh_->nrbx3 == 1 &&
      (block_bcs[INNER_X2]==POLAR_BNDRY || block_bcs[OUTER_X2]==POLAR_BNDRY ||
       block_bcs[INNER_X2]==POLAR_BNDRY_WEDGE || block_bcs[OUTER_X2]==POLAR_BNDRY_WEDGE))
    azimuthal_shift_.DeleteAthenaArray();
  // end KGF: edge-case...

  // end KGF: destructor counterpart of special handling of emf in spherical polar

  // KGF: shearing box destructor
  // if (SHEARING_BOX) {
  //   int level = pmb->loc.level - pmb->pmy_mesh->root_level;
  //   std::int64_t nrbx1 = pmb->pmy_mesh->nrbx1*(1L << level);
  //   if (pmb->loc.lx1 == 0) { // if true for shearing inner blocks
  //     shboxvar_inner_hydro_.DeleteAthenaArray();
  //     flx_inner_hydro_.DeleteAthenaArray();
  //     for (int n=0; n<4; n++) {
  //       delete[] send_innerbuf_hydro_[n];
  //       delete[] recv_innerbuf_hydro_[n];
  //     }
  //     if (MAGNETIC_FIELDS_ENABLED) {
  //       shboxvar_inner_field_.x1f.DeleteAthenaArray();
  //       shboxvar_inner_field_.x2f.DeleteAthenaArray();
  //       shboxvar_inner_field_.x3f.DeleteAthenaArray();
  //       flx_inner_field_.x1f.DeleteAthenaArray();
  //       flx_inner_field_.x2f.DeleteAthenaArray();
  //       flx_inner_field_.x3f.DeleteAthenaArray();
  //       shboxvar_inner_emf_.x2e.DeleteAthenaArray();
  //       shboxvar_inner_emf_.x3e.DeleteAthenaArray();
  //       flx_inner_emf_.x2e.DeleteAthenaArray();
  //       flx_inner_emf_.x3e.DeleteAthenaArray();
  //       for (int n=0; n<4; n++) {
  //         delete[] send_innerbuf_field_[n];
  //         delete[] recv_innerbuf_field_[n];
  //         delete[] send_innerbuf_emf_[n];
  //         delete[] recv_innerbuf_emf_[n];
  //       }
  //     }
  //   }
  //   if (pmb->loc.lx1 == (nrbx1-1)) { // if true for shearing outer blocks
  //     shboxvar_outer_hydro_.DeleteAthenaArray();
  //     flx_outer_hydro_.DeleteAthenaArray();
  //     for (int n=0; n<4; n++) {
  //       delete[] send_outerbuf_hydro_[n];
  //       delete[] recv_outerbuf_hydro_[n];
  //     }
  //     if (MAGNETIC_FIELDS_ENABLED) {
  //       shboxvar_outer_field_.x1f.DeleteAthenaArray();
  //       shboxvar_outer_field_.x2f.DeleteAthenaArray();
  //       shboxvar_outer_field_.x3f.DeleteAthenaArray();
  //       flx_outer_field_.x1f.DeleteAthenaArray();
  //       flx_outer_field_.x2f.DeleteAthenaArray();
  //       flx_outer_field_.x3f.DeleteAthenaArray();
  //       shboxvar_outer_emf_.x2e.DeleteAthenaArray();
  //       shboxvar_outer_emf_.x3e.DeleteAthenaArray();
  //       flx_outer_emf_.x2e.DeleteAthenaArray();
  //       flx_outer_emf_.x3e.DeleteAthenaArray();
  //       for (int n=0; n<4; n++) {
  //         delete[] send_outerbuf_field_[n];
  //         delete[] recv_outerbuf_field_[n];
  //         delete[] send_outerbuf_emf_[n];
  //         delete[] recv_outerbuf_emf_[n];
  //       }
  //     }
  //   }
  // } // KGF: end shearing box handling in destructor
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::Initialize(void)
//  \brief Initialize MPI requests

// KGF: called in Mesh::Initialize(), after CheckBoundary() and before
// StartReceivingForInit(true)

// TODO(felker): rename to a less generic name to avoid confusion with InitBoundaryData

// KGF: unlike InitBoundaryData(), splitting this up into constituent derived class
// methods will be painful
void BoundaryValues::Initialize(void) {
  MeshBlock* pmb=pmy_block_;
  int &mylevel=pmb->loc.level;
  // 6x unused vars:
  // std::int64_t &lx1=pmb->loc.lx1;
  // std::int64_t &lx2=pmb->loc.lx2;
  // std::int64_t &lx3=pmb->loc.lx3;
  // int myox1 = ((lx1 & 1LL) == 1LL);
  // int myox2 = ((lx2 & 1LL) == 1LL);
  // int myox3 = ((lx3 & 1LL) == 1LL);

  // KGF: shared logic of counting of neighbor MeshBlock coarse/same/refined
  // count the number of the fine meshblocks contacting on each edge
  int eid=0;
  if (pmb->block_size.nx2 > 1) {
    for (int ox2=-1; ox2<=1; ox2+=2) {
      for (int ox1=-1; ox1<=1; ox1+=2) {
        int nis, nie, njs, nje;
        nis=std::max(ox1-1,-1), nie=std::min(ox1+1,1);
        njs=std::max(ox2-1,-1), nje=std::min(ox2+1,1);
        int nf=0, fl=mylevel;
        for (int nj=njs; nj<=nje; nj++) {
          for (int ni=nis; ni<=nie; ni++) {
            if (nblevel[1][nj+1][ni+1] > fl)
              fl++, nf=0;
            if (nblevel[1][nj+1][ni+1]==fl)
              nf++;
          }
        }
        edge_flag_[eid]=(fl==mylevel);
        nedge_fine_[eid++]=nf;
      }
    }
  }
  if (pmb->block_size.nx3 > 1) {
    for (int ox3=-1; ox3<=1; ox3+=2) {
      for (int ox1=-1; ox1<=1; ox1+=2) {
        int nis, nie, nks, nke;
        nis=std::max(ox1-1,-1), nie=std::min(ox1+1,1);
        nks=std::max(ox3-1,-1), nke=std::min(ox3+1,1);
        int nf=0, fl=mylevel;
        for (int nk=nks; nk<=nke; nk++) {
          for (int ni=nis; ni<=nie; ni++) {
            if (nblevel[nk+1][1][ni+1] > fl)
              fl++, nf=0;
            if (nblevel[nk+1][1][ni+1]==fl)
              nf++;
          }
        }
        edge_flag_[eid]=(fl==mylevel);
        nedge_fine_[eid++]=nf;
      }
    }
    for (int ox3=-1; ox3<=1; ox3+=2) {
      for (int ox2=-1; ox2<=1; ox2+=2) {
        int njs, nje, nks, nke;
        njs=std::max(ox2-1,-1), nje=std::min(ox2+1,1);
        nks=std::max(ox3-1,-1), nke=std::min(ox3+1,1);
        int nf=0, fl=mylevel;
        for (int nk=nks; nk<=nke; nk++) {
          for (int nj=njs; nj<=nje; nj++) {
            if (nblevel[nk+1][nj+1][1] > fl)
              fl++, nf=0;
            if (nblevel[nk+1][nj+1][1]==fl)
              nf++;
          }
        }
        edge_flag_[eid]=(fl==mylevel);
        nedge_fine_[eid++]=nf;
      }
    }
  }
  // end KGF: shared logic of counting of neighbor MeshBlock coarse/same/refined

  // KGF: (although the counting is for 2x BoundaryVariables private members that are only
  // used for emf purposes in flux_correction_fc.cpp)
  // num_north_polar_blocks_, num_south_polar_blocks_, nedge_, nface_ are calculated in
  // BoundaryValues() constructor.

  for (auto bvars_it = bvars.begin(); bvars_it != bvars.end(); ++bvars_it) {
    bvars_it->Initialize();
  }


  // KGF: begin exclusive shearing-box section in Initialize

  // initialize the shearing block lists
  // if (SHEARING_BOX) {
  //   Mesh *pmesh = pmb->pmy_mesh;
  //   int level = pmb->loc.level - pmesh->root_level;
  //   std::int64_t nrbx1 = pmesh->nrbx1*(1L << level);
  //   // std::int64_t nrbx2 = pmesh->nrbx2*(1L << level); // unused variable
  //   int nbtotal = pmesh->nbtotal;
  //   int *ranklist = pmesh->ranklist;
  //   int *nslist = pmesh->nslist;
  //   LogicalLocation *loclist = pmesh->loclist;

  //   int count = 0;
  //   if (shbb_.inner) {
  //     for (int i=0;i<nbtotal;i++) {
  //       if (loclist[i].lx1 == 0 && loclist[i].lx3 == pmb->loc.lx3 &&
  //           loclist[i].level == pmb->loc.level) {
  //         shbb_.igidlist[count] = i;
  //         shbb_.ilidlist[count] = i - nslist[ranklist[i]];
  //         shbb_.irnklist[count] = ranklist[i];
  //         shbb_.ilevlist[count] = loclist[i].level;
  //         count++;
  //       }
  //     }
  //   }
  //   count = 0;
  //   if (shbb_.outer) {
  //     for (int i=0;i<nbtotal;i++) {
  //       if (loclist[i].lx1 == (nrbx1-1) && loclist[i].lx3 == pmb->loc.lx3 &&
  //         loclist[i].level == pmb->loc.level) {
  //         shbb_.ogidlist[count] = i;
  //         shbb_.olidlist[count] = i - nslist[ranklist[i]];
  //         shbb_.ornklist[count] = ranklist[i];
  //         shbb_.olevlist[count] = loclist[i].level;
  //         count++;
  //       }
  //     }
  //   }
  // } // end KGF: exclusive shearing box portion of Initialize()
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::CheckBoundary(void)
//  \brief checks if the boundary conditions are correctly enrolled (and other boundary
//  values compatibility checks performed at the top of Mesh::Initialize())

void BoundaryValues::CheckBoundary(void) {
  for (int i=0; i<nface_; i++) {
    if (block_bcs[i]==USER_BNDRY) {
      if (BoundaryFunction_[i]==nullptr) {
        std::stringstream msg;
        msg << "### FATAL ERROR in BoundaryValues::CheckBoundary" << std::endl
            << "A user-defined boundary is specified but the hydro boundary function "
            << "is not enrolled in direction " << i  << "." << std::endl;
        ATHENA_ERROR(msg);
      }
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::StartReceivingForInit(bool cons_and_field)
//  \brief initiate MPI_Irecv for initialization

void BoundaryValues::StartReceivingForInit(bool cons_and_field) {
  // KGF: approach #1: call each fn of element of bvar vector inside #ifdef and loop
// #ifdef MPI_PARALLEL
//   for (int n=0; n<nneighbor; n++) {
//     NeighborBlock& nb = neighbor[n];
//     if (nb.rank != Globals::my_rank) {
//       if (cons_and_field) {  // normal case
//         for (std::list<Multigrid *>::iterator bvars_it = bvars.begin();
//              bvars_it != bvars.end();
//              ++bvars_it) {
//         MPI_Start(&(bvars_it->bd_var_.req_recv[nb.bufid]));
//         //MPI_Start(&(bd_hydro_.req_recv[nb.bufid]));
//         // if (MAGNETIC_FIELDS_ENABLED)
//         //   MPI_Start(&(bd_field_.req_recv[nb.bufid]));
//         }
//       } else { // must be primitive initialization
//         // KGF: assuming 1st vector element corresponds to HydroBoundaryVariable obj
//         MPI_Start(&(bvars[0].bd_var_.req_recv[nb.bufid]));
//         //MPI_Start(&(bd_hydro_.req_recv[nb.bufid]));
//       }
//     }
//   }
// #endif
  // KGF: approach #2
  for (auto bvars_it = bvars.begin(); bvars_it != bvars.end(); ++bvars_it) {
    bvars_it->StartReceivingForInit(cons_and_field);
  }

  // KGF: begin shearing-box exclusive section of StartReceivingForInit
  // find send_block_id and recv_block_id;
  // if (SHEARING_BOX) {
  //   MeshBlock *pmb=pmy_block_;
  //   Mesh *pmesh = pmb->pmy_mesh;
  //   FindShearBlock(pmesh->time);
  // }
  // end KGF: shearing box
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::StartReceivingAll(const Real time)
//  \brief initiate MPI_Irecv for all the sweeps

void BoundaryValues::StartReceivingAll(const Real time) {
  firsttime_=true;

  // KGF: approach #1: call each fn of element of bvar vector inside #ifdef and loop
// #ifdef MPI_PARALLEL
//   MeshBlock *pmb=pmy_block_;
//   int mylevel=pmb->loc.level;
//   for (int n=0; n<nneighbor; n++) {
//     NeighborBlock& nb = neighbor[n];
//     if (nb.rank!=Globals::my_rank) {
//       for (auto bvars_it = bvars.begin(); bvars_it != bvars.end(); ++bvars_it) {
//         bvars_it->StartReceivingAll(time);
//       }
//     }
//   }
// #endif

  // KGF: approach #2: make loop over bvar vector the outermost loop; separate,
  // independent loops over nneighbor
  for (auto bvars_it = bvars.begin(); bvars_it != bvars.end(); ++bvars_it) {
    bvars_it->StartReceivingAll(time);
  }

  // KGF: begin shearing-box exclusive section of StartReceivingAll
  // find send_block_id and recv_block_id; post non-blocking recv
//   if (SHEARING_BOX) {
//     FindShearBlock(time);
// #ifdef MPI_PARALLEL
//      //     Mesh *pmesh = pmb->pmy_mesh;
//     int size,tag;
//     if (shbb_.inner) { // inner boundary
//       for (int n=0; n<4; n++) {
//         if ((recv_inner_rank_[n]!=Globals::my_rank) &&
//                           (recv_inner_rank_[n]!=-1)) {
//           size = ssize_*NHYDRO*recv_innersize_hydro_[n];
//           tag  = CreateBvalsMPITag(pmb->lid, TAG_SHBOX_HYDRO, n);
//           MPI_Irecv(recv_innerbuf_hydro_[n],size,MPI_ATHENA_REAL,
//                     recv_inner_rank_[n],tag,MPI_COMM_WORLD,
//                     &rq_innerrecv_hydro_[n]);
//           if (MAGNETIC_FIELDS_ENABLED) {
//             size = recv_innersize_field_[n];
//             tag  = CreateBvalsMPITag(pmb->lid, TAG_SHBOX_FIELD, n);
//             MPI_Irecv(recv_innerbuf_field_[n],size,MPI_ATHENA_REAL,
//                       recv_inner_rank_[n],tag,MPI_COMM_WORLD,
//                       &rq_innerrecv_field_[n]);
//             size = recv_innersize_emf_[n];
//             tag  = CreateBvalsMPITag(pmb->lid, TAG_SHBOX_EMF, n);
//             MPI_Irecv(recv_innerbuf_emf_[n],size,MPI_ATHENA_REAL,
//                       recv_inner_rank_[n],tag,MPI_COMM_WORLD,
//                       &rq_innerrecv_emf_[n]);
//           }
//         }
//       }
//     }

//     if (shbb_.outer) { // outer boundary
//       int offset=4;
//       for (int n=0; n<4; n++) {
//         if ((recv_outer_rank_[n]!=Globals::my_rank) &&
//                           (recv_outer_rank_[n]!=-1)) {
//           size = ssize_*NHYDRO*recv_outersize_hydro_[n];
//           tag  = CreateBvalsMPITag(pmb->lid, TAG_SHBOX_HYDRO, n+offset);
//           MPI_Irecv(recv_outerbuf_hydro_[n],size,MPI_ATHENA_REAL,
//                     recv_outer_rank_[n],tag,MPI_COMM_WORLD,
//                     &rq_outerrecv_hydro_[n]);
//           if (MAGNETIC_FIELDS_ENABLED) {
//             size = recv_outersize_field_[n];
//             tag  = CreateBvalsMPITag(pmb->lid, TAG_SHBOX_FIELD, n+offset);
//             MPI_Irecv(recv_outerbuf_field_[n],size,MPI_ATHENA_REAL,
//                       recv_outer_rank_[n],tag,MPI_COMM_WORLD,
//                       &rq_outerrecv_field_[n]);
//             size = recv_outersize_emf_[n];
//             tag  = CreateBvalsMPITag(pmb->lid, TAG_SHBOX_EMF, n+offset);
//             MPI_Irecv(recv_outerbuf_emf_[n],size,MPI_ATHENA_REAL,
//                       recv_outer_rank_[n],tag,MPI_COMM_WORLD,
//                       &rq_outerrecv_emf_[n]);
//           }
//         }
//       }
//     }
// #endif
//   } // end KGF: shearing-box exclusive section of StartReceivingAll
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::ClearBoundaryForInit(void)
//  \brief clean up the boundary flags for initialization

void BoundaryValues::ClearBoundaryForInit(bool cons_and_field) {
  // Note step==0 corresponds to initial exchange of conserved variables, while step==1
  // corresponds to primitives sent only in the case of GR with refinement
  for (auto bvars_it = bvars.begin(); bvars_it != bvars.end(); ++bvars_it) {
    bvars_it->ClearBoundaryForInit(cons_and_field);
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::ClearBoundaryAll(void)
//  \brief clean up the boundary flags after each loop

void BoundaryValues::ClearBoundaryAll(void) {
  for (auto bvars_it = bvars.begin(); bvars_it != bvars.end(); ++bvars_it) {
    bvars_it->ClearBoundaryAll();
  }

  // KGF: begin shearing-box exclusive section of ClearBoundaryAll
  // clear shearingbox boundary communications
//   if (SHEARING_BOX) {
//     if (shbb_.inner == true) {
//       for (int n=0; n<4; n++) {
//         if (send_inner_rank_[n] == -1) continue;
//         shbox_inner_hydro_flag_[n] = BNDRY_WAITING;
//         if (MAGNETIC_FIELDS_ENABLED) {
//           shbox_inner_field_flag_[n] = BNDRY_WAITING;
//           shbox_inner_emf_flag_[n] = BNDRY_WAITING;
//         }
// #ifdef MPI_PARALLEL
//         if (send_inner_rank_[n]!=Globals::my_rank) {
//           MPI_Wait(&rq_innersend_hydro_[n],MPI_STATUS_IGNORE);
//           if (MAGNETIC_FIELDS_ENABLED) {
//             MPI_Wait(&rq_innersend_field_[n],MPI_STATUS_IGNORE);
//             MPI_Wait(&rq_innersend_emf_[n],MPI_STATUS_IGNORE);
//           }
//         }
// #endif
//       }
//     } // inner boundary

//     if (shbb_.outer == true) {
//       for (int n=0; n<4; n++) {
//         if (send_outer_rank_[n] == -1) continue;
//         shbox_outer_hydro_flag_[n] = BNDRY_WAITING;
//         if (MAGNETIC_FIELDS_ENABLED) {
//           shbox_outer_field_flag_[n] = BNDRY_WAITING;
//         }
// #ifdef MPI_PARALLEL
//         if (send_outer_rank_[n]!=Globals::my_rank) {
//           MPI_Wait(&rq_outersend_hydro_[n],MPI_STATUS_IGNORE);
//           if (MAGNETIC_FIELDS_ENABLED) {
//             MPI_Wait(&rq_outersend_field_[n],MPI_STATUS_IGNORE);
//             MPI_Wait(&rq_outersend_emf_[n],MPI_STATUS_IGNORE);
//           }
//         }
// #endif
//       }
//     }
//   } // end KGF: shearing box
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::ApplyPhysicalBoundaries(AthenaArray<Real> &pdst,
//           AthenaArray<Real> &cdst, FaceField &bfdst, AthenaArray<Real> &bcdst,
//           const Real time, const Real dt)
//  \brief Apply all the physical boundary conditions for both hydro and field

void BoundaryValues::ApplyPhysicalBoundaries(
    AthenaArray<Real> &pdst, AthenaArray<Real> &cdst,
    FaceField &bfdst, AthenaArray<Real> &bcdst,
    const Real time, const Real dt) {
  MeshBlock *pmb=pmy_block_;
  Coordinates *pco=pmb->pcoord;
  int bis=pmb->is-NGHOST, bie=pmb->ie+NGHOST, bjs=pmb->js, bje=pmb->je,
      bks=pmb->ks, bke=pmb->ke;
  if (BoundaryFunction_[INNER_X2]==nullptr && pmb->block_size.nx2>1) bjs=pmb->js-NGHOST;
  if (BoundaryFunction_[OUTER_X2]==nullptr && pmb->block_size.nx2>1) bje=pmb->je+NGHOST;
  if (BoundaryFunction_[INNER_X3]==nullptr && pmb->block_size.nx3>1) bks=pmb->ks-NGHOST;
  if (BoundaryFunction_[OUTER_X3]==nullptr && pmb->block_size.nx3>1) bke=pmb->ke+NGHOST;
  // Apply boundary function on inner-x1
  if (BoundaryFunction_[INNER_X1] != nullptr) {
    BoundaryFunction_[INNER_X1](pmb, pco, pdst, bfdst, time, dt,
                                pmb->is, pmb->ie, bjs, bje, bks, bke, 1, NGHOST);
    if (MAGNETIC_FIELDS_ENABLED) {
      pmb->pfield->CalculateCellCenteredField(bfdst, bcdst, pco,
                                              pmb->is-NGHOST, pmb->is-1,
                                              bjs, bje, bks, bke);
    }
    pmb->peos->PrimitiveToConserved(pdst, bcdst, cdst, pco,
                                    pmb->is-NGHOST, pmb->is-1, bjs, bje, bks, bke);
  }

  // Apply boundary function on outer-x1
  if (BoundaryFunction_[OUTER_X1] != nullptr) {
    BoundaryFunction_[OUTER_X1](pmb, pco, pdst, bfdst, time, dt,
                                pmb->is, pmb->ie, bjs, bje, bks, bke, 1, NGHOST);
    if (MAGNETIC_FIELDS_ENABLED) {
      pmb->pfield->CalculateCellCenteredField(bfdst, bcdst, pco,
                                              pmb->ie+1, pmb->ie+NGHOST,
                                              bjs, bje, bks, bke);
    }
    pmb->peos->PrimitiveToConserved(pdst, bcdst, cdst, pco,
                                    pmb->ie+1, pmb->ie+NGHOST, bjs, bje, bks, bke);
  }

  if (pmb->block_size.nx2>1) { // 2D or 3D
    // Apply boundary function on inner-x2
    if (BoundaryFunction_[INNER_X2] != nullptr) {
      BoundaryFunction_[INNER_X2](pmb, pco, pdst, bfdst, time, dt,
                                  bis, bie, pmb->js, pmb->je, bks, bke, 1, NGHOST);
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->pfield->CalculateCellCenteredField(bfdst, bcdst, pco,
                                                bis, bie, pmb->js-NGHOST, pmb->js-1,
                                                bks, bke);
      }
      pmb->peos->PrimitiveToConserved(pdst, bcdst, cdst, pco,
                                      bis, bie, pmb->js-NGHOST, pmb->js-1, bks, bke);
    }

    // Apply boundary function on outer-x2
    if (BoundaryFunction_[OUTER_X2] != nullptr) {
      BoundaryFunction_[OUTER_X2](pmb, pco, pdst, bfdst, time, dt,
                                  bis, bie, pmb->js, pmb->je, bks, bke, 1, NGHOST);
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->pfield->CalculateCellCenteredField(bfdst, bcdst, pco,
                                                bis, bie, pmb->je+1, pmb->je+NGHOST,
                                                bks, bke);
      }
      pmb->peos->PrimitiveToConserved(pdst, bcdst, cdst, pco,
                                      bis, bie, pmb->je+1, pmb->je+NGHOST, bks, bke);
    }
  }

  if (pmb->block_size.nx3>1) { // 3D
    bjs=pmb->js-NGHOST;
    bje=pmb->je+NGHOST;

    // Apply boundary function on inner-x3
    if (BoundaryFunction_[INNER_X3] != nullptr) {
      BoundaryFunction_[INNER_X3](pmb, pco, pdst, bfdst, time, dt,
                                  bis, bie, bjs, bje, pmb->ks, pmb->ke, 1, NGHOST);
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->pfield->CalculateCellCenteredField(bfdst, bcdst, pco,
                                                bis, bie, bjs, bje,
                                                pmb->ks-NGHOST, pmb->ks-1);
      }
      pmb->peos->PrimitiveToConserved(pdst, bcdst, cdst, pco,
                                      bis, bie, bjs, bje, pmb->ks-NGHOST, pmb->ks-1);
    }

    // Apply boundary function on outer-x3
    if (BoundaryFunction_[OUTER_X3] != nullptr) {
      BoundaryFunction_[OUTER_X3](pmb, pco, pdst, bfdst, time, dt,
                                  bis, bie, bjs, bje, pmb->ks, pmb->ke, 1, NGHOST);
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->pfield->CalculateCellCenteredField(bfdst, bcdst, pco,
                                                bis, bie, bjs, bje,
                                                pmb->ke+1, pmb->ke+NGHOST);
      }
      pmb->peos->PrimitiveToConserved(pdst, bcdst, cdst, pco,
                                      bis, bie, bjs, bje, pmb->ke+1, pmb->ke+NGHOST);
    }
  }

  return;
}

//----------------------------------------------------------------------------------------
//! \fn void BoundaryValues::ProlongateBoundaries(AthenaArray<Real> &pdst,
//           AthenaArray<Real> &cdst, FaceField &bdst, AthenaArray<Real> &bcdst,
//           const Real time, const Real dt)
//  \brief Prolongate the level boundary using the coarse data

void BoundaryValues::ProlongateBoundaries(
    AthenaArray<Real> &pdst, AthenaArray<Real> &cdst,
    FaceField &bfdst, AthenaArray<Real> &bcdst,
    const Real time, const Real dt) {
  MeshBlock *pmb=pmy_block_;
  MeshRefinement *pmr=pmb->pmr;
  std::int64_t &lx1=pmb->loc.lx1;
  std::int64_t &lx2=pmb->loc.lx2;
  std::int64_t &lx3=pmb->loc.lx3;
  int &mylevel=pmb->loc.level;

  for (int n=0; n<nneighbor; n++) {
    NeighborBlock& nb = neighbor[n];
    if (nb.level >= mylevel) continue;
    // fill the required ghost-ghost zone
    int nis, nie, njs, nje, nks, nke;
    nis=std::max(nb.ox1-1,-1);
    nie=std::min(nb.ox1+1,1);
    if (pmb->block_size.nx2==1) {
      njs=0;
      nje=0;
    } else {
      njs=std::max(nb.ox2-1,-1);
      nje=std::min(nb.ox2+1,1);
    }

    if (pmb->block_size.nx3==1) {
      nks=0;
      nke=0;
    } else {
      nks=std::max(nb.ox3-1,-1);
      nke=std::min(nb.ox3+1,1);
    }

    for (int nk=nks; nk<=nke; nk++) {
      for (int nj=njs; nj<=nje; nj++) {
        for (int ni=nis; ni<=nie; ni++) {
          int ntype=std::abs(ni)+std::abs(nj)+std::abs(nk);
          // skip myself or coarse levels; only the same level must be restricted
          if (ntype==0 || nblevel[nk+1][nj+1][ni+1]!=mylevel) continue;

          // this neighbor block is on the same level
          // and needs to be restricted for prolongation
          int ris, rie, rjs, rje, rks, rke;
          if (ni==0) {
            ris=pmb->cis;
            rie=pmb->cie;
            if (nb.ox1==1) {
              ris=pmb->cie;
            } else if (nb.ox1==-1) {
              rie=pmb->cis;
            }
          } else if (ni== 1) {
            ris=pmb->cie+1, rie=pmb->cie+1;
          } else { //(ni==-1)
            ris=pmb->cis-1, rie=pmb->cis-1;
          }
          if (nj==0) {
            rjs=pmb->cjs, rje=pmb->cje;
            if (nb.ox2==1) rjs=pmb->cje;
            else if (nb.ox2==-1) rje=pmb->cjs;
          } else if (nj== 1) {
            rjs=pmb->cje+1, rje=pmb->cje+1;
          } else { //(nj==-1)
            rjs=pmb->cjs-1, rje=pmb->cjs-1;
          }
          if (nk==0) {
            rks=pmb->cks, rke=pmb->cke;
            if (nb.ox3==1) rks=pmb->cke;
            else if (nb.ox3==-1) rke=pmb->cks;
          } else if (nk== 1) {
            rks=pmb->cke+1, rke=pmb->cke+1;
          } else { //(nk==-1)
            rks=pmb->cks-1, rke=pmb->cks-1;
          }

          pmb->pmr->RestrictCellCenteredValues(cdst, pmr->coarse_cons_, 0, NHYDRO-1,
                                               ris, rie, rjs, rje, rks, rke);
          if (GENERAL_RELATIVITY)
            pmb->pmr->RestrictCellCenteredValues(pdst, pmr->coarse_prim_, 0, NHYDRO-1,
                                                 ris, rie, rjs, rje, rks, rke);
          if (MAGNETIC_FIELDS_ENABLED) {
            int rs=ris, re=rie+1;
            if (rs==pmb->cis   && nblevel[nk+1][nj+1][ni  ]<mylevel) rs++;
            if (re==pmb->cie+1 && nblevel[nk+1][nj+1][ni+2]<mylevel) re--;
            pmr->RestrictFieldX1(bfdst.x1f, pmr->coarse_b_.x1f, rs, re, rjs, rje, rks,
                                 rke);
            if (pmb->block_size.nx2 > 1) {
              rs=rjs, re=rje+1;
              if (rs==pmb->cjs   && nblevel[nk+1][nj  ][ni+1]<mylevel) rs++;
              if (re==pmb->cje+1 && nblevel[nk+1][nj+2][ni+1]<mylevel) re--;
              pmr->RestrictFieldX2(bfdst.x2f, pmr->coarse_b_.x2f, ris, rie, rs, re, rks,
                                   rke);
            } else { // 1D
              pmr->RestrictFieldX2(bfdst.x2f, pmr->coarse_b_.x2f, ris, rie, rjs, rje, rks,
                                   rke);
              for (int i=ris; i<=rie; i++)
                pmr->coarse_b_.x2f(rks,rjs+1,i)=pmr->coarse_b_.x2f(rks,rjs,i);
            }
            if (pmb->block_size.nx3 > 1) {
              rs=rks, re=rke+1;
              if (rs==pmb->cks   && nblevel[nk  ][nj+1][ni+1]<mylevel) rs++;
              if (re==pmb->cke+1 && nblevel[nk+2][nj+1][ni+1]<mylevel) re--;
              pmr->RestrictFieldX3(bfdst.x3f, pmr->coarse_b_.x3f, ris, rie, rjs, rje, rs,
                                   re);
            } else { // 1D or 2D
              pmr->RestrictFieldX3(bfdst.x3f, pmr->coarse_b_.x3f, ris, rie, rjs, rje, rks,
                                   rke);
              for (int j=rjs; j<=rje; j++) {
                for (int i=ris; i<=rie; i++)
                  pmr->coarse_b_.x3f(rks+1,j,i)=pmr->coarse_b_.x3f(rks,j,i);
              }
            }
          }
        }
      }
    }

    // calculate the loop limits for the ghost zones
    int cn = pmb->cnghost-1;
    int si, ei, sj, ej, sk, ek, fsi, fei, fsj, fej, fsk, fek;
    if (nb.ox1==0) {
      si=pmb->cis, ei=pmb->cie;
      if ((lx1 & 1LL)==0LL) ei+=cn;
      else             si-=cn;
    } else if (nb.ox1>0) { si=pmb->cie+1,  ei=pmb->cie+cn;}
    else              si=pmb->cis-cn, ei=pmb->cis-1;
    if (nb.ox2==0) {
      sj=pmb->cjs, ej=pmb->cje;
      if (pmb->block_size.nx2 > 1) {
        if ((lx2 & 1LL)==0LL) ej+=cn;
        else             sj-=cn;
      }
    } else if (nb.ox2>0) { sj=pmb->cje+1,  ej=pmb->cje+cn;}
    else              sj=pmb->cjs-cn, ej=pmb->cjs-1;
    if (nb.ox3==0) {
      sk=pmb->cks, ek=pmb->cke;
      if (pmb->block_size.nx3 > 1) {
        if ((lx3 & 1LL)==0LL) ek+=cn;
        else             sk-=cn;
      }
    } else if (nb.ox3>0) { sk=pmb->cke+1,  ek=pmb->cke+cn;}
    else              sk=pmb->cks-cn, ek=pmb->cks-1;

    // convert the ghost zone and ghost-ghost zones into primitive variables
    // this includes cell-centered field calculation
    int f1m=0, f1p=0, f2m=0, f2p=0, f3m=0, f3p=0;
    if (nb.ox1==0) {
      if (nblevel[1][1][0]!=-1) f1m=1;
      if (nblevel[1][1][2]!=-1) f1p=1;
    } else {
      f1m=1;
      f1p=1;
    }
    if (pmb->block_size.nx2>1) {
      if (nb.ox2==0) {
        if (nblevel[1][0][1]!=-1) f2m=1;
        if (nblevel[1][2][1]!=-1) f2p=1;
      } else {
        f2m=1;
        f2p=1;
      }
    }
    if (pmb->block_size.nx3>1) {
      if (nb.ox3==0) {
        if (nblevel[0][1][1]!=-1) f3m=1;
        if (nblevel[2][1][1]!=-1) f3p=1;
      } else {
        f3m=1;
        f3p=1;
      }
    }

    pmb->peos->ConservedToPrimitive(pmr->coarse_cons_, pmr->coarse_prim_,
                                    pmr->coarse_b_, pmr->coarse_prim_,
                                    pmr->coarse_bcc_, pmr->pcoarsec,
                                    si-f1m, ei+f1p, sj-f2m, ej+f2p, sk-f3m, ek+f3p);

    // Apply physical boundaries
    if (nb.ox1==0) {
      if (BoundaryFunction_[INNER_X1]!=nullptr) {
        BoundaryFunction_[INNER_X1](pmb, pmr->pcoarsec, pmr->coarse_prim_,
                                    pmr->coarse_b_, time, dt, pmb->cis, pmb->cie,
                                    sj, ej, sk, ek, 1);
      }
      if (BoundaryFunction_[OUTER_X1]!=nullptr) {
        BoundaryFunction_[OUTER_X1](pmb, pmr->pcoarsec, pmr->coarse_prim_,
                                    pmr->coarse_b_, time, dt, pmb->cis, pmb->cie,
                                    sj, ej, sk, ek, 1);
      }
    }
    if (nb.ox2==0 && pmb->block_size.nx2 > 1) {
      if (BoundaryFunction_[INNER_X2]!=nullptr) {
        BoundaryFunction_[INNER_X2](pmb, pmr->pcoarsec, pmr->coarse_prim_,
                                    pmr->coarse_b_, time, dt, si, ei, pmb->cjs, pmb->cje,
                                    sk, ek, 1);
      }
      if (BoundaryFunction_[OUTER_X2]!=nullptr) {
        BoundaryFunction_[OUTER_X2](pmb, pmr->pcoarsec, pmr->coarse_prim_,
                                    pmr->coarse_b_, time, dt, si, ei, pmb->cjs, pmb->cje,
                                    sk, ek, 1);
      }
    }
    if (nb.ox3==0 && pmb->block_size.nx3 > 1) {
      if (BoundaryFunction_[INNER_X3]!=nullptr) {
        BoundaryFunction_[INNER_X3](pmb, pmr->pcoarsec, pmr->coarse_prim_,
                                    pmr->coarse_b_, time, dt, si, ei, sj, ej,
                                    pmb->cks, pmb->cke, 1);
      }
      if (BoundaryFunction_[OUTER_X3]!=nullptr) {
        BoundaryFunction_[OUTER_X3](pmb, pmr->pcoarsec, pmr->coarse_prim_,
                                    pmr->coarse_b_, time, dt, si, ei, sj, ej,
                                    pmb->cks, pmb->cke, 1);
      }
    }

    // now that the ghost-ghost zones are filled
    // calculate the loop limits for the finer grid
    fsi=(si-pmb->cis)*2+pmb->is;
    fei=(ei-pmb->cis)*2+pmb->is+1;
    if (pmb->block_size.nx2 > 1) {
      fsj=(sj-pmb->cjs)*2+pmb->js;
      fej=(ej-pmb->cjs)*2+pmb->js+1;
    } else {
      fsj=pmb->js;
      fej=pmb->je;
    }
    if (pmb->block_size.nx3 > 1) {
      fsk=(sk-pmb->cks)*2+pmb->ks;
      fek=(ek-pmb->cks)*2+pmb->ks+1;
    } else {
      fsk=pmb->ks;
      fek=pmb->ke;
    }
    // prolongate hydro variables using primitive
    pmr->ProlongateCellCenteredValues(pmr->coarse_prim_, pdst, 0, NHYDRO-1,
                                      si, ei, sj, ej, sk, ek);
    // prolongate magnetic fields
    if (MAGNETIC_FIELDS_ENABLED) {
      int il, iu, jl, ju, kl, ku;
      il=si, iu=ei+1;
      if ((nb.ox1>=0) && (nblevel[nb.ox3+1][nb.ox2+1][nb.ox1  ]>=mylevel)) il++;
      if ((nb.ox1<=0) && (nblevel[nb.ox3+1][nb.ox2+1][nb.ox1+2]>=mylevel)) iu--;
      if (pmb->block_size.nx2 > 1) {
        jl=sj, ju=ej+1;
        if ((nb.ox2>=0) && (nblevel[nb.ox3+1][nb.ox2  ][nb.ox1+1]>=mylevel)) jl++;
        if ((nb.ox2<=0) && (nblevel[nb.ox3+1][nb.ox2+2][nb.ox1+1]>=mylevel)) ju--;
      } else {
        jl=sj;
        ju=ej;
      }
      if (pmb->block_size.nx3 > 1) {
        kl=sk, ku=ek+1;
        if ((nb.ox3>=0) && (nblevel[nb.ox3  ][nb.ox2+1][nb.ox1+1]>=mylevel)) kl++;
        if ((nb.ox3<=0) && (nblevel[nb.ox3+2][nb.ox2+1][nb.ox1+1]>=mylevel)) ku--;
      } else {
        kl=sk;
        ku=ek;
      }

      // step 1. calculate x1 outer surface fields and slopes
      pmr->ProlongateSharedFieldX1(pmr->coarse_b_.x1f, bfdst.x1f, il, iu, sj, ej, sk, ek);
      // step 2. calculate x2 outer surface fields and slopes
      pmr->ProlongateSharedFieldX2(pmr->coarse_b_.x2f, bfdst.x2f, si, ei, jl, ju, sk, ek);
      // step 3. calculate x3 outer surface fields and slopes
      pmr->ProlongateSharedFieldX3(pmr->coarse_b_.x3f, bfdst.x3f, si, ei, sj, ej, kl, ku);
      // step 4. calculate the internal finer fields using the Toth & Roe method
      pmr->ProlongateInternalField(bfdst, si, ei, sj, ej, sk, ek);
      // Field prolongation completed, calculate cell centered fields
      pmb->pfield->CalculateCellCenteredField(bfdst, bcdst, pmb->pcoord,
                                              fsi, fei, fsj, fej, fsk, fek);
    }
    // calculate conservative variables
    pmb->peos->PrimitiveToConserved(pdst, bcdst, cdst, pmb->pcoord,
                                    fsi, fei, fsj, fej, fsk, fek);
  }
  return;
}
