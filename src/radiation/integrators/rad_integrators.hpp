#ifndef RADIATION_INTEGRATORS_RAD_INTEGRATORS_HPP_
#define RADIATION_INTEGRATORS_RAD_INTEGRATORS_HPP_
//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file rad_integrators.hpp
//! \brief definitions for RadIntegrator class

// Athena++ classes headers
#include "../../athena.hpp"
#include "../../athena_arrays.hpp"
#include "../../scalars/scalars.hpp"
#include "../radiation.hpp" // radiation

class MeshBlock;
class Radiation;
#ifdef INCLUDE_CHEMISTRY
class ChemNetwork;
#endif //INCLUDE_CHEMISTRY

//! \class RadIntegrator
//! \brief integrate algorithm for radiative transfer

class RadIntegrator {
  friend class Radiation;
  friend class BoundaryValues;
 public:
  RadIntegrator(Radiation *prad, ParameterInput *pin);
  ~RadIntegrator();

  Radiation *pmy_rad;
  MeshBlock *pmy_mb;
#ifdef INCLUDE_CHEMISTRY
  ChemNetwork* pmy_chemnet;
  int ncol; //number of column densities needed to track
  AthenaArray<Real> col; //column densitites
#ifdef DEBUG
  AthenaArray<Real> col_avg, col_Htot, col_CO, col_H2,  col_C;//for debug output
#endif //DEBUG
#endif //INCLUDE_CHEMISTRY

  void CopyToOutput();

  void UpdateRadiation(int direction);
 private:
#ifdef INCLUDE_CHEMISTRY
  //calculate column densities within the meshblock, for six_ray
  void GetColMB(int direction);
#endif //INCLUDE_CHEMISTRY
};

#endif // RADIATION_INTEGRATORS_RAD_INTEGRATORS_HPP_
