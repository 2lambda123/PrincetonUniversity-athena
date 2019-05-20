//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file mignone_advection.cpp
//  \brief 1D passive scalar advection tests in curvilinear coordinates from:
// Mignone, A. (2014). High-order conservative reconstruction schemes for finite volume
// methods in cylindrical and spherical coordinates. Journal of Computational Physics,
// 270, 784-814.
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
#include "../utils/gl_quadrature.hpp"

// Parameters which define initial solution -- made global so that they can be shared
namespace {
constexpr int N_gl = 12;
constexpr Real d0 = 1.0;
constexpr bool use_gl_quadrature = true;
Real (*IntegrandInitial)(Real x1);
Real (*IntegrandFinal)(Real x1);
Real a_width, b_center, alpha;
Real iso_cs;
int m_coord;
Real t_final;

// pointwise analytic initial condition for Gaussian bell curve
Real InitialGaussianProfile(Real x1);
// pointwise analytic exact solution at any t >=0 for a linear x1 velocity profile
Real FinalGaussianProfile(Real x1);
Real CylindricalIntegrandInitial(Real x1);
Real CylindricalIntegrandFinal(Real x1);
Real SphericalIntegrandInitial(Real x1);
Real SphericalIntegrandFinal(Real x1);
} // namespace

//========================================================================================
//! \fn void Mesh::InitUserMeshData(ParameterInput *pin)
//========================================================================================

void Mesh::InitUserMeshData(ParameterInput *pin) {
  // read and initialize global parameters
  iso_cs = pin->GetReal("hydro", "iso_sound_speed");
  // initial condition; see Mignone (2014) section 5.1.1
  alpha = pin->GetOrAddReal("problem", "alpha", 1.0);
  a_width = pin->GetOrAddReal("problem", "a_width", 10.0);
  b_center = pin->GetOrAddReal("problem", "b_center", 0.0);
  t_final = pin->GetOrAddReal("time", "tlim", 1.0);

  if (std::strcmp(COORDINATE_SYSTEM, "cartesian") == 0) {
    m_coord = 0;
    IntegrandInitial = &InitialGaussianProfile;
    IntegrandFinal = &FinalGaussianProfile;
  } else if (std::strcmp(COORDINATE_SYSTEM, "cylindrical") == 0) {
    m_coord = 1;
    IntegrandInitial = &CylindricalIntegrandInitial;
    IntegrandFinal = &CylindricalIntegrandFinal;
  } else { // if (std::strcmp(COORDINATE_SYSTEM, "spherical_polar") == 0)
    m_coord = 2;
    IntegrandInitial = &SphericalIntegrandInitial;
    IntegrandFinal = &SphericalIntegrandFinal;
  }
  // Restrict to: 1) no-MHD 2) isothermal EOS only 3) hydro/active=background
  // TODO(felker): add explicit user-safety checks for these conditions
  return;
}

//========================================================================================
//! \fn void Mesh::UserWorkAfterLoop(ParameterInput *pin)
//========================================================================================

void Mesh::UserWorkAfterLoop(ParameterInput *pin) {
  if (!pin->GetOrAddBoolean("problem", "compute_error", false)) return;

  if (NSCALARS > 0) { // TODO(felker): error-out at compile time if NSCALARS < 1
    // Initialize errors to zero
    // (temp. workaround for zero-length array prohibition from ISO C++; replace with
    // dynamically-sized arrays)
    Real l1_err[(NSCALARS > 0 ? NSCALARS : 1)]{},
        max_err[(NSCALARS > 0 ? NSCALARS : 1)]{};
    Real total_vol = 0.0;
    MeshBlock *pmb = pblock;
    AthenaArray<Real> vol(pmb->ncells1);
    // recalculate initial condition from ProblemGenerator on final Mesh configuration:
    // (may have changed due to AMR)
    while (pmb != nullptr) {
      int il = pmb->is, iu = pmb->ie, jl = pmb->js, ju = pmb->je,
          kl = pmb->ks, ku = pmb->ke;
      // only interested in error of the evolved passive scalar profiles
      constexpr int scalar_norm = NSCALARS > 0 ? NSCALARS : 1.0;

      for (int n=0; n<NSCALARS; ++n) {
        for (int k=kl; k<=ku; k++) {
          for (int j=jl; j<=ju; j++) {
            pmb->pcoord->CellVolume(k, j, il, iu, vol);
            for (int i=il; i<=iu; i++) {
              Real cell_ave;
              if (use_gl_quadrature) {
                Real xl, xu;
                xl = pmb->pcoord->x1f(i);
                xu = pmb->pcoord->x1f(i+1);

                // GL implementation returns total integral, not ave. Divide by delta_r
                Real cell_quad = GaussLegendre::integrate(N_gl,
                                                          IntegrandFinal,
                                                          xl, xu);
                cell_ave = cell_quad/vol(i);
                // assuming that the Gaussian profile is 1D in radial coordinate to pull
                // out 2x integrals from the triple volume integral
                if (std::strcmp(COORDINATE_SYSTEM, "cartesian") == 0 ||    // dy*dz
                    std::strcmp(COORDINATE_SYSTEM, "cylindrical") == 0) {  // dz*dphi
                  cell_ave *= pmb->pcoord->dx2f(j);
                  cell_ave *= pmb->pcoord->dx3f(k);
                } else { // if (std::strcmp(COORDINATE_SYSTEM, "spherical_polar") == 0)
                  // sin(theta)*dtheta*dphi
                  cell_ave *= std::cos(pmb->pcoord->x2f(j)) -
                              std::cos(pmb->pcoord->x2f(j+1));
                  cell_ave *= pmb->pcoord->dx3f(k);
                }
              } else {
                // Use standard midpoint approximation with cell-centered coords:
                cell_ave = FinalGaussianProfile(pmb->pcoord->x1v(i));
              }

              Real sol = 1.0/scalar_norm*cell_ave;
              l1_err[n] += std::abs(sol - pmb->pscalars->s(n,k,j,i))*vol(i);
              max_err[n] = std::max(
                  static_cast<Real>(std::abs(sol - pmb->pscalars->s(n,k,j,i))),
                  max_err[n]);
              total_vol += vol(i);
            }
          }
        }
        pmb = pmb->next;
      }
    }
#ifdef MPI_PARALLEL
    if (Globals::my_rank == 0) {
      MPI_Reduce(MPI_IN_PLACE, &l1_err, NSCALARS, MPI_ATHENA_REAL, MPI_SUM, 0,
                 MPI_COMM_WORLD);
      MPI_Reduce(MPI_IN_PLACE, &total_vol, 1, MPI_ATHENA_REAL, MPI_SUM, 0,
                 MPI_COMM_WORLD);
      MPI_Reduce(MPI_IN_PLACE, &max_err, NSCALARS, MPI_ATHENA_REAL, MPI_MAX, 0,
                 MPI_COMM_WORLD);
    } else {
      MPI_Reduce(&l1_err, &l1_err, NSCALARS, MPI_ATHENA_REAL, MPI_SUM, 0,
                 MPI_COMM_WORLD);
      MPI_Reduce(&total_vol, &total_vol, 1, MPI_ATHENA_REAL, MPI_SUM, 0,
                 MPI_COMM_WORLD);
      MPI_Reduce(&max_err, &max_err, NSCALARS, MPI_ATHENA_REAL, MPI_MAX, 0,
                 MPI_COMM_WORLD);
    }
#endif

    // only the root process outputs the data
    if (Globals::my_rank == 0) {
      // normalize errors by total domain volume
      for (int i=0; i<NSCALARS; ++i) l1_err[i] = l1_err[i]/total_vol;
      // open output file and write out errors
      std::string fname;
      fname.assign("mignone_radial-errors.dat");
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
  } // if NSCALARS > 0
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
        //--- iprob=1
        //if (iprob == 1) {

        // background fluid:
        phydro->u(IDN,k,j,i) = d0;
        // midpoint approximation for intiialization of linear velocity profile:
        // phydro->u(IM1,k,j,i) = d0*alpha*pcoord->x1v(i);
        phydro->u(IM2,k,j,i) = 0.0;
        phydro->u(IM3,k,j,i) = 0.0;
        // assuming isothermal EOS:
        //  phydro->u(IEN,k,j,i) =
        Real cell_ave;
        if (use_gl_quadrature) {
          // Use Gauss-Legendre quadrature rules to compute cell-averaged initial
          // condition based on pointwise analytic formula
          Real xl, xu;
          xl = pcoord->x1f(i);
          xu = pcoord->x1f(i+1);

          // GL implementation returns total integral, not average. Divide by delta_r
          Real cell_quad = GaussLegendre::integrate(N_gl,
                                                    IntegrandInitial,
                                                    xl, xu);
          cell_ave = cell_quad/vol(i);
          // assuming that the Gaussian profile is 1D in radial coordinate to pull
          // out 2x integrals from the triple volume integral
          if (std::strcmp(COORDINATE_SYSTEM, "cartesian") == 0 ||    // dy*dz
              std::strcmp(COORDINATE_SYSTEM, "cylindrical") == 0) {  // dz*dphi
            cell_ave *= pcoord->dx2f(j);
            cell_ave *= pcoord->dx3f(k);
            // exact integral of cell-averaged linear velocity profile:
            phydro->u(IM1,k,j,i) =
                d0*alpha*ONE_3RD*(pcoord->x1f(i+1)*SQR(pcoord->x1f(i+1)) -
                                  pcoord->x1f(i)*SQR(pcoord->x1f(i)));
            phydro->u(IM1,k,j,i) *= pcoord->dx2f(j)*pcoord->dx3f(k)/vol(i);
          } else { // if (std::strcmp(COORDINATE_SYSTEM, "spherical_polar") == 0)
            // sin(theta)*dtheta*dphi
            cell_ave *= std::cos(pcoord->x2f(j)) - std::cos(pcoord->x2f(j+1));
            cell_ave *= pcoord->dx3f(k);
            // exact integral of cell-averaged linear velocity profile:
            phydro->u(IM1,k,j,i) =
                d0*alpha*0.25*(SQR(SQR(pcoord->x1f(i+1))) -
                               SQR(SQR(pcoord->x1f(i))));
            phydro->u(IM1,k,j,i) *= pcoord->dx3f(k)/vol(i);
            phydro->u(IM1,k,j,i) *= std::cos(pcoord->x2f(j)) - std::cos(pcoord->x2f(j+1));
          }
        } else {
          // Use standard midpoint approximation with cell centered coords:
          cell_ave = InitialGaussianProfile(pcoord->x1v(i));
        }

        // uniformly fill all scalars to have equal concentration
        constexpr int scalar_norm = NSCALARS > 0 ? NSCALARS : 1.0;
        if (NSCALARS > 0) {
          for (int n=0; n<NSCALARS; ++n) {
            pscalars->s(n,k,j,i) = 1.0/scalar_norm*cell_ave*d0;
          }
        }
      }
    }
  }
  return;
}

namespace {
Real InitialGaussianProfile(Real x1) {
  return std::exp(-SQR(a_width)*SQR(x1 - b_center));  // Mignone eq 73
}


Real FinalGaussianProfile(Real x1) {
  // hardcoding t=t_final to maintain compatiblity with GL quadrature routines
  Real x_initial = x1*std::exp(-alpha*t_final);
  Real q_initial = InitialGaussianProfile(x_initial);
  Real amp = std::exp(-(m_coord + 1)*alpha*t_final);
  return amp*q_initial;  // Mignone eq 72
}


Real CylindricalIntegrandInitial(Real x1) {
  return x1*InitialGaussianProfile(x1);
}


Real CylindricalIntegrandFinal(Real x1) {
  return x1*FinalGaussianProfile(x1);
}


Real SphericalIntegrandInitial(Real x1) {
  return SQR(x1)*InitialGaussianProfile(x1);
}


Real SphericalIntegrandFinal(Real x1) {
  return SQR(x1)*FinalGaussianProfile(x1);
}
} // namespace