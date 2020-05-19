//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file wave_test.cpp
//  \brief Initial conditions for the wave equation

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
#include "../wave/wave.hpp"

using namespace std;

int RefinementCondition(MeshBlock *pmb);

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

  int il = pwave->mbi.il, iu = pwave->mbi.iu;
  int kl = pwave->mbi.kl, ku = pwave->mbi.ku;
  int jl = pwave->mbi.jl, ju = pwave->mbi.ju;

  Real c = pwave->c;

  for(int k = kl; k <= ku; ++k)
    for(int j = jl; j <= ju; ++j)
      for(int i = il; i <= iu; ++i) {

        Real x = pwave->mbi.x1(i);
        Real y = pwave->mbi.x2(j);
        Real z = pwave->mbi.x3(k);

        // Real cos_x = cos(PI*x);
        Real cos_2x = cos(2.*PI*x);
        Real sqr_cos_2x = SQR(cos_2x);
        // Real sqr_sin_x = SQR(sin(PI*x));

        pwave->u(0,k,j,i) = sqr_cos_2x;
        pwave->u(1,k,j,i) = -cos_2x / 2.;

        // dummy const. init
        // pwave->u(0,k,j,i) = 1.0 * i;
        // pwave->u(1,k,j,i) = -1.0 * i;
        // pwave->u(0,k,j,i) = x + 2;
        // pwave->u(1,k,j,i) = -x;
        // pwave->u(0,k,j,i) = 1;
        // pwave->u(1,k,j,i) = 0;

        // pwave->u(0,k,j,i) = i + 1 + 100 * (gid + 1);
        // pwave->u(1,k,j,i) = -(i + 1 + 100 * (gid + 1));

        if (DBG_VC_CONSISTENCY) {
          // BD: Debug- vertex consistency
          pwave->u(0,k,j,i) = 1;
          pwave->u(1,k,j,i) = 1;
          //-
        }

        pwave->exact(k,j,i) = pwave->u(0,k,j,i);
        pwave->error(k,j,i) = 0.0;
      }


  return;
}

void MeshBlock::WaveUserWorkInLoop() {
  Real max_err = 0;
  Real fun_max = 0;

  // int il = pwave->mbi.il, iu = pwave->mbi.iu;
  // int kl = pwave->mbi.kl, ku = pwave->mbi.ku;
  // int jl = pwave->mbi.jl, ju = pwave->mbi.ju;

  Real c = pwave->c;
  Real t = pmy_mesh->time + pmy_mesh->dt;
  bool const debug_inspect_error = pwave->debug_inspect_error;
  Real const debug_abort_threshold = pwave->debug_abort_threshold;

  // DebugWaveMeshBlock(pwave->u,
  //                    il, iu, jl, ju, kl, ku, false);

  int il = 0, iu = pwave->mbi.nn1 - 1;
  int jl = 0, ju = pwave->mbi.nn2 - 1;
  int kl = 0, ku = pwave->mbi.nn3 - 1;

  for(int k = kl; k <= ku; ++k)
    for(int j = jl; j <= ju; ++j)
      for(int i = il; i <= iu; ++i) {

        Real x = pwave->mbi.x1(i);
        Real y = pwave->mbi.x2(j);
        Real z = pwave->mbi.x3(k);

        Real cos_2x = cos(2.*PI*x);
        Real cos_4x = cos(4.*PI*x);
        Real cos_4ct = cos(4.*PI*c*t);
        Real sin_2ct = sin(2.*PI*c*t);

        // pwave->u(0,k,j,i) = i + 1 + 100 * gid;
        // pwave->u(1,k,j,i) = 0;
        pwave->exact(k,j,i) = (2. + 2. * cos_4ct * cos_4x -
                               cos_2x * sin_2ct / ( c * PI )) / 4.;

        pwave->error(k,j,i) = pwave->u(0,k,j,i) - pwave->exact(k,j,i);

        if (std::abs(pwave->error(k,j,i)) > max_err){
          max_err = std::abs(pwave->error(k,j,i));
          fun_max = pwave->u(0,k,j,i);
        }
      }

  if (debug_inspect_error) {
    printf(">>>\n");
    coutBoldRed("MB::UWIL gid = ");
    printf("%d\n", gid);
    printf("(max_err, fun_max, t)=(%1.18f, %1.18f, %1.18f)\n",
          max_err, fun_max, t);

    if (max_err > debug_abort_threshold) {
      printf("pwave->u:\n");
      pwave->u.print_all("%1.5f");

      printf("pwave->exact:\n");
      pwave->exact.print_all("%1.5f");

      printf("pwave->error:\n");
      pwave->error.print_all("%1.5f");

      Q();
    }

    printf("<<<\n");
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn
//  \brief refinement condition: simple time-dependent test

int RefinementCondition(MeshBlock *pmb){
  // don't do anything
  return 0;
}

//----------------------------------------------------------------------------------------
//! \fn
//  \brief debug for vertex dev
void MeshBlock::DebugWaveMeshBlock(AthenaArray<Real> &u_wave,
                                   int il, int iu,
                                   int jl, int ju,
                                   int kl, int ku,
                                   bool is_additive,
                                   bool is_coarse) {
  Mesh *pm = pmy_mesh;

  // compute next time-step (ensure not past limit)
  // not initially available from mesh
  Real dt = new_block_dt_;
  if (pm->time < pm->tlim && (pm->tlim - pm->time) < dt)
    dt = pm->tlim - pm->time;

  Real c = pwave->c;
  Real t = pm->time + dt;

  // u_wave.ZeroClear(); // kill all data
  // u_wave.print_all();

  // solution on fundamental grid of block
  for(int k = kl; k <= ku; ++k)
    for(int j = jl; j <= ju; ++j)
      for(int i = il; i <= iu; ++i) {

        Real x, y, z;

        if (is_coarse) {
          x = pwave->mbi.cx1(i);
          y = pwave->mbi.cx2(j);
          z = pwave->mbi.cx3(k);
        } else {
          x = pwave->mbi.x1(i);
          y = pwave->mbi.x2(j);
          z = pwave->mbi.x3(k);
        }

        Real cos_2x = cos(2.*PI*x);
        Real cos_4x = cos(4.*PI*x);
        Real cos_4ct = cos(4.*PI*c*t);
        Real sin_2ct = sin(2.*PI*c*t);

        Real dt_cos_4ct = -4.*PI*c*sin(4.*PI*c*t);
        Real dt_sin_2ct = 2.*PI*c*cos(2.*PI*c*t);

        pwave->exact(k,j,i) = (2. + 2. * cos_4ct * cos_4x -
                               cos_2x * sin_2ct / ( c * PI )) / 4.;

        Real u_0 = pwave->exact(k,j,i);
        Real u_1 = (2. * dt_cos_4ct * cos_4x -
                    cos_2x * dt_sin_2ct / ( c * PI )) / 4.;


        if (DBG_VC_CONSISTENCY) {
          // BD: Debug- vertex consistency
          u_0 = 1;
          u_1 = 1;
          //-
        }

        // unpack buffer additive logic emu
        if (is_additive) {
          u_wave(0, k, j, i) += u_0;
          u_wave(1, k, j, i) += u_1;
        } else {
          u_wave(0, k, j, i) = u_0;
          u_wave(1, k, j, i) = u_1;
        }

      }
}