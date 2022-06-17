//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file chem_Pn_grid.cpp
//! \brief problem generator, for producing a grid of P-n curves.
//!
//! x - nH, hydrogen nucleus number density in cm-3
//! y - chi, FUV radiation intensity relative to solar neighborhood
//! z - CRIR, primary CRIR per hydrogen nucleus
//! Gas and dust metallicities needs to be specified by the input file.
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


//======================================================================================
//! \fn void MeshBlock::ProblemGenerator(ParameterInput *pin)
//! \brief initialize problem of a grid of radiation and density conditions
//======================================================================================
void MeshBlock::ProblemGenerator(ParameterInput *pin) {
  //dimensions of meshblock
  const int Nx = ie - is + 1;
  const int Ny = je - js + 1;
  const int Nz = ke - ks + 1;
  //read density, FUV radiation, and CRIR grids
  const Real nH_min = pin->GetReal("problem", "nH_min"); //minimum density
  const Real nH_max = pin->GetReal("problem", "nH_max"); //maximum density
  const Real chi_min = pin->GetReal("problem", "chi_min"); //minimum FUV
  const Real chi_max = pin->GetReal("problem", "chi_max"); //maximum FUV
  const Real cr_min = pin->GetReal("problem", "cr_min"); //minimum CRIR
  const Real cr_max = pin->GetReal("problem", "cr_max"); //maximum CRIR
  //initial abundance and sound speed (which sets the initial temperature)
  const Real s_init = pin->GetReal("problem", "s_init");
  const Real iso_cs = pin->GetReal("hydro", "iso_sound_speed");
  const Real gm1  = peos->GetGamma() - 1.0;
  Real pres = 0.;
  //factors between the logspace grids cells
  const Real fac_nH = ( log10(nH_max) - log10(nH_min) ) / (Nx-1.);
  const Real fac_chi = ( log10(chi_max) - log10(chi_min) ) / (Ny-1.);
  const Real fac_cr = ( log10(cr_max) - log10(cr_min) ) / (Nz-1.);

  for (int k=ks; k<=ke; ++k) {
    for (int j=js; j<=je; ++j) {
      for (int i=is; i<=ie; ++i) {
        //density
        phydro->u(IDN, k, j, i) = nH_min * pow(10, (i-is)*fac_nH);
        //energy
        if (NON_BAROTROPIC_EOS) {
          pres = phydro->u(IDN, k, j, i) * SQR(iso_cs);
          phydro->u(IEN, k, j, i) = pres/gm1;
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
              prad->ir(k, j, i, ifreq * prad->nang + iang)
                = chi_min * pow(10, (j-js)*fac_chi);
            }
          }
#ifdef INCLUDE_CHEMISTRY
          for (int iang=0; iang < prad->nang; ++iang) {
            //cr rate
            prad->ir(k, j, i, pscalars->chemnet.index_cr_ * prad->nang + iang)
              = cr_min * pow(10, (k-ks)*fac_cr);
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
          for (int ispec=0; ispec < NSCALARS; ++ispec) {
            pscalars->s(ispec, k, j, i) = s_init * phydro->u(IDN, k, j, i);
#ifdef INCLUDE_CHEMISTRY
            Real s_ispec = pin->GetOrAddReal("problem",
                "s_init_"+pscalars->chemnet.species_names[ispec], -1);
            if (s_ispec >= 0.) {
              pscalars->s(ispec, k, j, i) = s_ispec * phydro->u(IDN, k, j, i);
            }
#endif
          }
        }
      }
    }
  }

  return;
}
