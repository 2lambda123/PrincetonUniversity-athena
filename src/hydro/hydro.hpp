#ifndef HYDRO_HPP
#define HYDRO_HPP
//======================================================================================
// Athena++ astrophysical MHD code
// Copyright (C) 2014 James M. Stone  <jmstone@princeton.edu>
// See LICENSE file for full public license information.
//======================================================================================
//! \file hydro.hpp
//  \brief definitions for Hydro class
//======================================================================================

// Athena++ classes headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"

class MeshBlock;
class ParameterInput;
class HydroFluxes;
class HydroSourceTerms;

//! \class Hydro
//  \brief hydro data and functions

class Hydro {
friend class Field;
public:
  Hydro(MeshBlock *pmb, ParameterInput *pin);
  ~Hydro();
  MeshBlock* pmy_block;    // ptr to MeshBlock containing this Hydro

  AthenaArray<Real> u,w;   // conserved and primitive variables
  AthenaArray<Real> u1,w1; // conserved and primitive variables at intermediate step

  AthenaArray<Real> flux[3];   // conserved and primitive variables

  AthenaArray<Real> g, g_inv;  // metric and its inverse

  AthenaArray<Real> ifov;  // internal hydro output variables for analysis

  HydroFluxes *pflux;
  HydroSourceTerms *psrc;

  Real NewBlockTimeStep(MeshBlock *pmb);    // computes new timestep on a MeshBlock

private:
  AthenaArray<Real> dt1_,dt2_,dt3_;  // scratch arrays used in NewTimeStep
};
#endif // HYDRO_HPP
