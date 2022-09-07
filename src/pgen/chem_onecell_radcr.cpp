//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file chem_uniform_radcr.cpp
//! \brief problem generator, uniform chemistry and radiation
//======================================================================================

// c headers
#include <stdio.h>    // c style file
#include <string.h>   // strcmp()

// C++ headers
#include <algorithm>  // std::find()
#include <iostream>   // endl
#include <sstream>    // stringstream
#include <stdexcept>  // std::runtime_error()
#include <string>     // c_str()
#include <vector>     // vector container

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../bvals/bvals.hpp"
#include "../chemistry/utils/thermo.hpp"
#include "../coordinates/coordinates.hpp"
#include "../eos/eos.hpp"
#include "../field/field.hpp"
#include "../globals.hpp"
#include "../hydro/hydro.hpp"
#include "../mesh/mesh.hpp"
#include "../parameter_input.hpp"
#include "../radiation/integrators/rad_integrators.hpp"
#include "../radiation/radiation.hpp"
#include "../scalars/scalars.hpp"
#include "../utils/units.hpp"

//User defined boundary conditions
void SixRayBoundaryInnerX1(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh);

void SixRayBoundaryOuterX1(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh);

void SixRayBoundaryInnerX2(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh);

void SixRayBoundaryOuterX2(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh);

void SixRayBoundaryInnerX3(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh);

void SixRayBoundaryOuterX3(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh);

//Radiation boundary
namespace {
  AthenaArray<Real> G0_iang;
  Real G0, cr_rate;
} //namespace

//========================================================================================
//! \fn void Mesh::InitUserMeshData(ParameterInput *pin)
//! \brief Function to initialize problem-specific data in mesh class.  Can also be used
//  to initialize variables which are global to (and therefore can be passed to) other
//  functions in this file.  Called in Mesh constructor.
//========================================================================================

void Mesh::InitUserMeshData(ParameterInput *pin)
{
  EnrollUserBoundaryFunction(BoundaryFace::inner_x1, SixRayBoundaryInnerX1);
  EnrollUserBoundaryFunction(BoundaryFace::outer_x1, SixRayBoundaryOuterX1);
  EnrollUserBoundaryFunction(BoundaryFace::inner_x2, SixRayBoundaryInnerX2);
  EnrollUserBoundaryFunction(BoundaryFace::outer_x2, SixRayBoundaryOuterX2);
  EnrollUserBoundaryFunction(BoundaryFace::inner_x3, SixRayBoundaryInnerX3);
  EnrollUserBoundaryFunction(BoundaryFace::outer_x3, SixRayBoundaryOuterX3);
  G0 = pin->GetOrAddReal("radiation", "G0", 0.);
  G0_iang.NewAthenaArray(6);
  G0_iang(BoundaryFace::inner_x1) = pin->GetOrAddReal("radiation", "G0_inner_x1", G0);
  G0_iang(BoundaryFace::inner_x2) = pin->GetOrAddReal("radiation", "G0_inner_x2", G0);
  G0_iang(BoundaryFace::inner_x3) = pin->GetOrAddReal("radiation", "G0_inner_x3", G0);
  G0_iang(BoundaryFace::outer_x1) = pin->GetOrAddReal("radiation", "G0_outer_x1", G0);
  G0_iang(BoundaryFace::outer_x2) = pin->GetOrAddReal("radiation", "G0_outer_x2", G0);
  G0_iang(BoundaryFace::outer_x3) = pin->GetOrAddReal("radiation", "G0_outer_x3", G0);
  cr_rate = pin->GetOrAddReal("radiation", "CR", 2e-16);
  return;
}


//======================================================================================
//! \fn void MeshBlock::ProblemGenerator(ParameterInput *pin)
//! \brief initialize problem of uniform chemistry and radiation
//======================================================================================
void MeshBlock::ProblemGenerator(ParameterInput *pin) {
  //dimensions of meshblock
  const int Nx = ie - is + 1;
  const int Ny = je - js + 1;
  const int Nz = ke - ks + 1;
  //read density and radiation field strength
  const Real nH = pin->GetReal("problem", "nH");
  const Real vx = pin->GetOrAddReal("problem", "vx_kms", 0);
  const Real s_init = pin->GetReal("problem", "s_init");
  const Real iso_cs = pin->GetReal("hydro", "iso_sound_speed");
  const Real pres = nH*SQR(iso_cs);
  const Real gm1  = peos->GetGamma() - 1.0;
  //index to set density
  const int ix = pin->GetInteger("problem", "ix");
  const int iy = pin->GetInteger("problem", "iy");
  const int iz = pin->GetInteger("problem", "iz");
  const Real x1min =  pmy_mesh->mesh_size.x1min;
  const Real x2min =  pmy_mesh->mesh_size.x2min;
  const Real x3min =  pmy_mesh->mesh_size.x3min;
  Real ixx, iyy, izz;

  for (int k=ks; k<=ke; ++k) {
    for (int j=js; j<=je; ++j) {
      for (int i=is; i<=ie; ++i) {
        ixx = (pcoord->x1f(i) - x1min)/pcoord->dx1f(i);
        iyy = (pcoord->x2f(j) - x2min)/pcoord->dx2f(j);
        izz = (pcoord->x3f(k) - x3min)/pcoord->dx3f(k);
        if ( std::abs(ixx-ix)<0.1 && std::abs(iyy-iy)<0.1 && std::abs(izz-iz)<0.1 ) {
          //density
          phydro->u(IDN, k, j, i) = nH;
          //velocity, x direction
          phydro->u(IM1, k, j, i) = nH*vx;
          //energy
          if (NON_BAROTROPIC_EOS) {
            phydro->u(IEN, k, j, i) = pres/gm1;
          }
        }
      }
    }
  }


  //intialize radiation field
  if (RADIATION_ENABLED) {
    for (int k=ks; k<=ke; ++k) {
      for (int j=js; j<=je; ++j) {
        for (int i=is; i<=ie; ++i) {
          for (int ifreq=0; ifreq < prad->nfreq; ++ifreq) {
            for (int iang=0; iang < prad->nang; ++iang) {
              prad->ir(k, j, i, ifreq * prad->nang + iang) = G0_iang(iang);
            }
          }
#ifdef INCLUDE_CHEMISTRY
          for (int iang=0; iang < prad->nang; ++iang) {
            //cr rate
            prad->ir(k, j, i,
                pscalars->chemnet.index_cr_ * prad->nang + iang) = cr_rate;
          }
#endif
        }
      }
    }
    //calculate the average radiation field for output of the initial condition
    prad->pradintegrator->CopyToOutput();
  }

  //intialize chemical species
  if (NSCALARS > 0) {
    for (int k=ks; k<=ke; ++k) {
      for (int j=js; j<=je; ++j) {
        for (int i=is; i<=ie; ++i) {
          ixx = (pcoord->x1f(i) - x1min)/pcoord->dx1f(i);
          iyy = (pcoord->x2f(j) - x2min)/pcoord->dx2f(j);
          izz = (pcoord->x3f(k) - x3min)/pcoord->dx3f(k);
          if ( std::abs(ixx-ix)<0.1 && std::abs(iyy-iy)<0.1 && std::abs(izz-iz)<0.1 ) {
            for (int ispec=0; ispec < NSCALARS; ++ispec) {
              pscalars->s(ispec, iz, iy, ix) = s_init*nH;
#ifdef INCLUDE_CHEMISTRY
              Real s_ispec = pin->GetOrAddReal("problem",
                  "s_init_"+pscalars->chemnet.species_names[ispec], -1);
              if (s_ispec >= 0.) {
                pscalars->s(ispec, iz, iy, ix) = s_ispec*nH;
              }
#endif
            }
          }
        }
      }
    }
  }
  return;
}

void SixRayBoundaryInnerX1(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh) {
  //set species and column boundary to zero
  for (int n=0; n<(NSCALARS); ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma simd
        for (int i=1; i<=ngh; ++i) {
          pmb->pscalars->s(n,k,j,il-i) = 0;
        }
      }
    }
  }
  //set hydro variables to zero
  for (int n=0; n<(NHYDRO); ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma simd
        for (int i=1; i<=ngh; ++i) {
          prim(n,k,j,il-i) = 0;
        }
      }
    }
  }
  return;
}

void SixRayBoundaryInnerX2(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh) {
  //set species and column boundary to zero
  for (int n=0; n<(NSCALARS); ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=1; j<=ngh; ++j) {
#pragma simd
        for (int i=il; i<=iu; ++i) {
          pmb->pscalars->s(n,k,jl-j,i) = 0;
        }
      }
    }
  }
  //set hydro variables to zero
  for (int n=0; n<(NHYDRO); ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=1; j<=ngh; ++j) {
#pragma simd
        for (int i=il; i<=iu; ++i) {
          prim(n,k,jl-j,i) = 0;
        }
      }
    }
  }
  return;
}

void SixRayBoundaryInnerX3(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh) {
  //set species and column boundary to zero
  for (int n=0; n<(NSCALARS); ++n) {
    for (int k=1; k<=ngh; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma simd
        for (int i=il; i<=iu; ++i) {
          pmb->pscalars->s(n,kl-k,j,i) = 0;
        }
      }
    }
  }
  //set hydro variables to zero
  for (int n=0; n<(NHYDRO); ++n) {
    for (int k=1; k<=ngh; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma simd
        for (int i=il; i<=iu; ++i) {
          prim(n,kl-k,j,i) = 0;
        }
      }
    }
  }
  return;
}

void SixRayBoundaryOuterX1(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh) {
  //set species and column boundary to zero
  for (int n=0; n<(NSCALARS); ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma simd
        for (int i=1; i<=ngh; ++i) {
          pmb->pscalars->s(n,k,j,iu+i) = 0;
        }
      }
    }
  }
  //set hydro variables to zero
  for (int n=0; n<(NHYDRO); ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma simd
        for (int i=1; i<=ngh; ++i) {
          prim(n,k,j,iu+i) = 0;
        }
      }
    }
  }
  return;
}

void SixRayBoundaryOuterX2(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh) {
  //set species and column boundary to zero
  for (int n=0; n<(NSCALARS); ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=1; j<=ngh; ++j) {
#pragma simd
        for (int i=il; i<=iu; ++i) {
          pmb->pscalars->s(n,k,ju+j,i) = 0;
        }
      }
    }
  }
  //set hydro variables to zero
  for (int n=0; n<(NHYDRO); ++n) {
    for (int k=kl; k<=ku; ++k) {
      for (int j=1; j<=ngh; ++j) {
#pragma simd
        for (int i=il; i<=iu; ++i) {
          prim(n,k,ju+j,i) = 0;
        }
      }
    }
  }
  return;
}

void SixRayBoundaryOuterX3(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
                           FaceField &b, Real time, Real dt,
                           int il, int iu, int jl, int ju, int kl, int ku, int ngh) {
  //set species and column boundary to zero
  for (int n=0; n<(NSCALARS); ++n) {
    for (int k=1; k<=ngh; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma simd
        for (int i=il; i<=iu; ++i) {
          pmb->pscalars->s(n,ku+k,j,i) = 0;
        }
      }
    }
  }
  //set hydro variables to zero
  for (int n=0; n<(NHYDRO); ++n) {
    for (int k=1; k<=ngh; ++k) {
      for (int j=jl; j<=ju; ++j) {
#pragma simd
        for (int i=il; i<=iu; ++i) {
          prim(n,ku+k,j,i) = 0;
        }
      }
    }
  }
  return;
}
