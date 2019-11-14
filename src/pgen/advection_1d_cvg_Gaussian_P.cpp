//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file advection_1d_cvg_trig.cpp
//  \brief Initial conditions for the advection equation

#include <cassert> // assert
#include <cmath> // abs, exp, sin, fmod
#include <iostream>

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../parameter_input.hpp"
#include "../coordinates/coordinates.hpp"
#include "../mesh/mesh.hpp"
#include "../mesh/mesh_refinement.hpp"
#include "../advection/advection.hpp"

using namespace std;

int RefinementCondition(MeshBlock *pmb);

// standard deviation of packet
Real sigma = 1. / 16.;
Real phys_x1min = -1.0;
Real phys_x1max = 1.0;
Real cx1 = 1.0;

Real amr_sigma_mul = 1;
bool allow_restrict = true;

//========================================================================================
//! \fn void Mesh::InitUserMeshData(ParameterInput *pin)
//  \brief Function to initialize problem-specific data in mesh class.  Can also be used
//  to initialize variables which are global to (and therefore can be passed to) other
//  functions in this file.  Called in Mesh constructor.
//========================================================================================

void Mesh::InitUserMeshData(ParameterInput *pin) {
  if(adaptive==true)
    EnrollUserRefinementCondition(RefinementCondition);
  return;
}

//========================================================================================
//! \fn void MeshBlock::ProblemGenerator(ParameterInput *pin)
//  \brief Initialize the problem.
//========================================================================================

void MeshBlock::ProblemGenerator(ParameterInput *pin) {

  sigma = pin->GetOrAddReal("problem", "sigma", sigma);
  cx1 = pin->GetOrAddReal("problem", "cx1", cx1);
  // for amr
  amr_sigma_mul = pin->GetOrAddReal("problem", "amr_sigma_mul", amr_sigma_mul);
  phys_x1min = pin->GetOrAddReal("mesh", "x1min", phys_x1min);
  phys_x1max = pin->GetOrAddReal("mesh", "x1max", phys_x1max);
  allow_restrict = pin->GetOrAddBoolean("problem", "allow_restrict", allow_restrict);
  //-

  for(int k = ks; k <= ke; ++k)
    for(int j = js; j <= je; ++j)
      for(int i = is; i <= ie; ++i) {
        Real x = pcoord->x1v(i);
        Real y = pcoord->x2v(j);
        Real z = pcoord->x3v(k);

        Real x_2 = SQR(x);
        Real sigma_2 = SQR(sigma);
        Real Gaussian = exp(-x_2 / (2. * sigma_2));
        padv->u(k,j,i) = Gaussian;

        padv->exact(k,j,i) = padv->u(k,j,i);
        padv->error(k,j,i) = 0.0;
      }

  return;
}


// StackOverflow: 4633177
Real wrapMax (Real x, Real max) {
  // wrap x -> [0, max)
  // integer math: (max + x % max) % max
  return std::fmod(max + std::fmod(x, max), max);
}

Real wrapMinMax (Real x, Real min, Real max) {
  // wrap x -> [min, max)
  return min + wrapMax(x - min, max - min);
}

void MeshBlock::AdvectionUserWorkInLoop() {
  Real max_err = 0;
  Real fun_err = 0;

  Real t = pmy_mesh->time + pmy_mesh->dt;
  Real sigma_2 = SQR(sigma);

  for(int k = ks; k <= ke; ++k)
    for(int j = js; j <= je; ++j)
      for(int i = is; i <= ie; ++i) {
        Real x = pcoord->x1v(i);
        Real y = pcoord->x2v(j);
        Real z = pcoord->x3v(k);
        Real t = pmy_mesh->time + pmy_mesh->dt;

        // wrap argument to domain
        Real arg = wrapMinMax(x - t * cx1, phys_x1min, phys_x1max);
        Real Gaussian = exp(-SQR(arg) / (2. * sigma_2));

        padv->exact(k,j,i) = Gaussian;
        padv->error(k,j,i) = padv->u(k,j,i) - padv->exact(k,j,i);

        // if (std::abs(padv->error(k,j,i)) > max_err){
        //   max_err = std::abs(padv->error(k,j,i));
        //   fun_err = padv->u(k,j,i);
        // }
      }
  // printf("MB::UWIL: (max_err, fun_err)=(%1.7f, %1.7f)\n", max_err, fun_err);
}

//----------------------------------------------------------------------------------------
//! \fn
//  \brief refinement condition: simple time-dependent test

int RefinementCondition(MeshBlock *pmb){
  // physical parameters
  Real t = pmb->pmy_mesh->time;
  Real del = (phys_x1max - phys_x1min);

  Real r_width = sigma * amr_sigma_mul;

  // Gaussian refinement params
  Real gc_x0 = phys_x1min + del / 2;
  Real gl_x0 = phys_x1min + del / 2 - r_width;
  Real gr_x0 = phys_x1min + del / 2 + r_width;

  // propagated and wrapped physical coordinates
  Real gl_xc = wrapMinMax(gl_x0 + t * cx1, phys_x1min, phys_x1max);
  Real gc_xc = wrapMinMax(gc_x0 + t * cx1, phys_x1min, phys_x1max);
  Real gr_xc = wrapMinMax(gr_x0 + t * cx1, phys_x1min, phys_x1max);

  // current block (physical) geometry
  Real x1min = pmb->block_size.x1min;
  Real x1max = pmb->block_size.x1max;

  // if left or right edge within current box then refine
  if ((x1min <= gl_xc) && (gl_xc <= x1max)) {
    return 1;
  }

  if ((x1min <= gc_xc) && (gc_xc <= x1max)) {
    return 1;
  }

  if ((x1min <= gr_xc) && (gr_xc <= x1max)) {
    return 1;
  }

  // otherwise derefine
  if (allow_restrict)
    return -1;
  return 0;
}
