//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file slotted_cylinder.cpp
//  \brief Slotted cylinder passive scalar advection problem generator for 2D/3D problems.
//
//========================================================================================

// C++ headers
#include <algorithm>  // min, max
#include <iostream>   // endl
#include <sstream>    // stringstream
#include <stdexcept>  // runtime_error
#include <string>     // c_str()

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../coordinates/coordinates.hpp"
#include "../eos/eos.hpp"
#include "../field/field.hpp"
#include "../globals.hpp"
#include "../hydro/hydro.hpp"
#include "../mesh/mesh.hpp"
#include "../parameter_input.hpp"
#include "../scalars/scalars.hpp"
#include "../utils/gauss_legendre.hpp"

// Parameters which define initial solution -- made global so that they can be shared
namespace {
Real radius, omega_x1, omega_x2, omega, iso_cs;
Real s_width, s_height, center_x1, center_x2;

// slotted cylinder pointwise analytic initial condition
Real slotted_cylinder(Real x, Real y, void* data);
} // namespace

Real threshold;
int RefinementCondition(MeshBlock *pmb);

//========================================================================================
//! \fn void Mesh::InitUserMeshData(ParameterInput *pin)
//========================================================================================

void Mesh::InitUserMeshData(ParameterInput *pin) {
  // read and initialize global parameters
  iso_cs = pin->GetReal("hydro", "iso_sound_speed");
  // cylinder dimensions
  radius = pin->GetOrAddReal("problem", "radius", 0.15);
  center_x1 = pin->GetOrAddReal("problem", "center_x1", 0.50);
  center_x2 = pin->GetOrAddReal("problem", "center_x2", 0.75);
  // rotational speed and axis
  omega = pin->GetOrAddReal("problem", "omega", 1.0);
  omega_x1 = pin->GetOrAddReal("problem", "omega_x1", 0.50);
  omega_x2 = pin->GetOrAddReal("problem", "omega_x2", 0.50);
  // slot dimensions
  s_width = pin->GetOrAddReal("problem", "s_width", 0.05);
  s_height = pin->GetOrAddReal("problem", "s_height", 0.25);

  // Restrict to: 1) no-MHD 2) isothermal EOS only

  if (adaptive) {
    EnrollUserRefinementCondition(RefinementCondition);
    threshold = pin->GetReal("problem", "thr");
  }

  return;
}

//========================================================================================
//! \fn void Mesh::UserWorkAfterLoop(ParameterInput *pin)

//========================================================================================

void Mesh::UserWorkAfterLoop(ParameterInput *pin) {
  if (!pin->GetOrAddBoolean("problem", "compute_error", false)) return;

  // Initialize errors to zero
  Real l1_err[NSCALARS]{}, max_err[NSCALARS]{};

  MeshBlock *pmb = pblock;
  // recalculate initial condition from ProblemGenerator on final Mesh configuration:
  // (may have changed due to AMR)
  while (pmb != nullptr) {
    int il = pmb->is, iu = pmb->ie, jl = pmb->js, ju = pmb->je,
        kl = pmb->ks, ku = pmb->ke;
    AthenaArray<Real> vol(pmb->ncells1);
    // only interested in error of the evolved passive scalar profiles
    constexpr int scalar_norm = NSCALARS > 0 ? NSCALARS : 1.0;
    if (NSCALARS > 0) {
      for (int n=0; n<NSCALARS; ++n) {
        for (int k=kl; k<=ku; k++) {
          for (int j=jl; j<=ju; j++) {
            pmb->pcoord->CellVolume(k, j, il, iu, vol);
            for (int i=il; i<=iu; i++) {
              Real cell_vol = vol(i);
              Real xl, xu, yl, yu;
              xl = pmb->pcoord->x1f(i);
              xu = pmb->pcoord->x1f(i+1);
              yl = pmb->pcoord->x2f(j);
              yu = pmb->pcoord->x2f(j+1);
              int N_gl = 8;

              Real cell_ave = gauss_legendre_2D_cube(N_gl, slotted_cylinder, nullptr,
                                                     xl, xu, yl, yu);
              cell_ave /= cell_vol;  // 2D: (pcoord->dx1v(i)*pcoord->dx2v(j));
              Real sol = 1.0/scalar_norm*cell_ave;
              l1_err[n] += std::fabs(sol - pmb->pscalars->s(n,k,j,i))*cell_vol;
              max_err[n] = std::max(
                  static_cast<Real>(std::fabs(sol - pmb->pscalars->s(n,k,j,i))),
                  max_err[n]);
            }
          }
        }
      }
    }
    pmb = pmb->next;
  }
#ifdef MPI_PARALLEL
  if (Globals::my_rank == 0) {
    MPI_Reduce(MPI_IN_PLACE, &l1_err, NSCALARS, MPI_ATHENA_REAL, MPI_SUM, 0,
               MPI_COMM_WORLD);
    MPI_Reduce(MPI_IN_PLACE, &max_err, NSCALARS, MPI_ATHENA_REAL, MPI_MAX, 0,
               MPI_COMM_WORLD);
  } else {
    MPI_Reduce(&l1_err, &l1_err, NSCALARS, MPI_ATHENA_REAL, MPI_SUM, 0,
               MPI_COMM_WORLD);
    MPI_Reduce(&max_err, &max_err, NSCALARS, MPI_ATHENA_REAL, MPI_MAX, 0,
               MPI_COMM_WORLD);
  }
#endif

  // only the root process outputs the data
  if (Globals::my_rank == 0) {
    // normalize errors by number of cells
    Real vol= (mesh_size.x1max - mesh_size.x1min)*(mesh_size.x2max - mesh_size.x2min)
              *(mesh_size.x3max - mesh_size.x3min);
    for (int i=0; i<NSCALARS; ++i) l1_err[i] = l1_err[i]/vol;
    // open output file and write out errors
    std::string fname;
    fname.assign("slotted_cylinder-errors.dat");
    std::stringstream msg;
    FILE *pfile;

    // The file exists -- reopen the file in append mode
    if ((pfile = std::fopen(fname.c_str(), "r")) != nullptr) {
      if ((pfile = std::freopen(fname.c_str(), "a", pfile)) == nullptr) {
        msg << "### FATAL ERROR in function Mesh::UserWorkAfterLoop"
            << std::endl << "Error output file could not be opened" <<std::endl;
        ATHENA_ERROR(msg);
      }

      // The file does not exist -- open the file in write mode and add headers
    } else {
      if ((pfile = std::fopen(fname.c_str(), "w")) == nullptr) {
        msg << "### FATAL ERROR in function Mesh::UserWorkAfterLoop"
            << std::endl << "Error output file could not be opened" <<std::endl;
        ATHENA_ERROR(msg);
      }
      std::fprintf(pfile, "# Nx1  Nx2  Nx3  Ncycle  ");
      for (int n=0; n<NSCALARS; ++n)
        std::fprintf(pfile, "s%d_L1  ", n);
      for (int n=0; n<NSCALARS; ++n)
        std::fprintf(pfile, "s%d_max  ", n);
      std::fprintf(pfile, "\n");
    }

    // write errors
    std::fprintf(pfile, "%d  %d", mesh_size.nx1, mesh_size.nx2);
    std::fprintf(pfile, "  %d  %d", mesh_size.nx3, ncycle);
    for (int n=0; n<NSCALARS; ++n)
      std::fprintf(pfile, "  %e", l1_err[n]);
    for (int n=0; n<NSCALARS; ++n)
      std::fprintf(pfile, "  %e", max_err[n]);
    std::fprintf(pfile, "\n");
    std::fclose(pfile);
  }

  return;
}

//========================================================================================
//! \fn void MeshBlock::ProblemGenerator(ParameterInput *pin)

//========================================================================================

void MeshBlock::ProblemGenerator(ParameterInput *pin) {
  AthenaArray<Real> vol(ncells1);

  // initialize conserved variables
  for (int k=ks; k<=ke; k++) {
    for (int j=js; j<=je; j++) {
      pcoord->CellVolume(k, j, is, ie, vol);
      for (int i=is; i<=ie; i++) {
        // background fluid:
        phydro->u(IDN,k,j,i) = 1.0;
        phydro->u(IM1,k,j,i) = -TWO_PI*omega*(pcoord->x2v(j)
                                              - omega_x2)*phydro->u(IDN,k,j,i);
        phydro->u(IM2,k,j,i) = TWO_PI*omega*(pcoord->x1v(i)
                                             - omega_x1)*phydro->u(IDN,k,j,i);
        phydro->u(IM3,k,j,i) = 0.0;

        // Use Gauss-Legendre quadrature rules to compute cell-averaged initial condition
        // based on pointwise analytic formula
        Real xl, xu, yl, yu;
        xl = pcoord->x1f(i);
        xu = pcoord->x1f(i+1);
        yl = pcoord->x2f(j);
        yu = pcoord->x2f(j+1);
        int N_gl = 8;

        // GL implementation returns total integral, not average. Divide by cell volume
        Real cell_ave = gauss_legendre_2D_cube(N_gl, slotted_cylinder, nullptr, xl, xu,
                                               yl, yu);
        cell_ave /= vol(i);  // 2D: (pcoord->dx1v(i)*pcoord->dx2v(j));

        // TODO(felker): add switch to skip the quadrature eval. and use midpoint approx.
        // Use standard midpoint approximation with cell centered coords:
        // pscalars->s(n,k,j,i) = slotted_cylinder_ic(pcoord->x1v(i), pcoord->x2v(j),
        //                                            nullptr);

        // uniformly fill all scalars to have equal concentration
        constexpr int scalar_norm = NSCALARS > 0 ? NSCALARS : 1.0;
        if (NSCALARS > 0) {
          for (int n=0; n<NSCALARS; ++n) {
            pscalars->s(n,k,j,i) = 1.0/scalar_norm*cell_ave;
          }
        }
      }
    }
  }
  return;
}

namespace {
Real slotted_cylinder(Real x, Real y, void* data) {
  // positions relative to the center of the cylinder
  Real zx = x - center_x1;
  Real zy = y - center_x2;
  // distance from center of cylinder
  Real r = std::sqrt(SQR(zx) + SQR(zy));
  Real scalar = 0.0;

  // Initial condition is specified in pointwise fashion as follows:
  // cell-center is outside the cylinder
  if (r > radius)
    scalar = 0.0;
  // cell-center is inside the slot
  else if ((std::abs(2*zx) < s_width) && (zy + radius < s_height) && (0 < zy + radius))
    scalar = 0.0;
  // cell-center is inside the cylinder and outside the slot
  else
    scalar = 1.0;

  return scalar;
}
} // namespace


// refinement condition: maximum gradient of each passive scalar profile

int RefinementCondition(MeshBlock *pmb) {
  int f2 = pmb->pmy_mesh->f2, f3 = pmb->pmy_mesh->f3;
  AthenaArray<Real> &s = pmb->pscalars->s;
  Real maxeps = 0.0;
  if (f3) {
    for (int n=0; n<NSCALARS; ++n) {
      for (int k=pmb->ks-1; k<=pmb->ke+1; k++) {
        for (int j=pmb->js-1; j<=pmb->je+1; j++) {
          for (int i=pmb->is-1; i<=pmb->ie+1; i++) {
            Real eps = std::sqrt(SQR(0.5*(s(n,k,j,i+1) - s(n,k,j,i-1)))
                                 + SQR(0.5*(s(n,k,j+1,i) - s(n,k,j-1,i)))
                                 + SQR(0.5*(s(n,k+1,j,i) - s(n,k-1,j,i))));
            // /s(n,k,j,i); Do not normalize by scalar, since (unlike IDN and IPR) there
            // are are no physical floors / s=0 might be allowed. Compare w/ blast.cpp.
            maxeps = std::max(maxeps, eps);
          }
        }
      }
    }
  } else if (f2) {
    int k = pmb->ks;
    for (int n=0; n<NSCALARS; ++n) {
      for (int j=pmb->js-1; j<=pmb->je+1; j++) {
        for (int i=pmb->is-1; i<=pmb->ie+1; i++) {
          Real eps = std::sqrt(SQR(0.5*(s(n,k,j,i+1) - s(n,k,j,i-1)))
                               + SQR(0.5*(s(n,k,j+1,i) - s(n,k,j-1,i)))); // /s(n,k,j,i);
          maxeps = std::max(maxeps, eps);
        }
      }
    }
  } else {
    return 0;
  }

  if (maxeps > threshold) return 1;
  if (maxeps < 0.25*threshold) return -1;
  return 0;
}