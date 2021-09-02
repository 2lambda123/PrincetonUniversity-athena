#ifndef CHEMISTRY_NETWORK_H2_HPP_
#define CHEMISTRY_NETWORK_H2_HPP_
//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file H2.hpp
//! \brief definitions for a very simple chemical network with H2 formation on grains,
//! and H2 distruction by CR.
//!
//! This has an analytic solution.

//c++ headers
#include <string> //std::string

// Athena++ classes headers
#include "../../athena.hpp"
#include "../../athena_arrays.hpp"
#include "network.hpp"

class Units;

//! \class ChemNetwork
//! \brief H2 Chemical Network.
//!
//!  Note: This is a template for chemistry network.
//!  When implementing a new chemistry network, all public functions should be
//!  in the same form.
//!  The internal calculations are in cgs units. The input and
//!  return of RHS and Edot must be in code units.
class ChemNetwork : public NetworkWrapper {
  //It would be convenient to know the species names in
  //initialization of chemical species in problem generator
  friend class MeshBlock;
 public:
  ChemNetwork(MeshBlock *pmb, ParameterInput *pin);
  ~ChemNetwork();

  //a list of species name, used in output
  static const std::string species_names[NSCALARS];

  void InitializeNextStep(const int k, const int j, const int i);

  void RHS(const Real t, const Real y[NSCALARS], const Real ED,
           Real ydot[NSCALARS]);

  Real Edot(const Real t, const Real y[NSCALARS], const Real ED);

  Units *punit;
 private:
  PassiveScalars *pmy_spec_;
  MeshBlock *pmy_mb_;

  std::string species_names_all_[NSCALARS];//all species
  //index of species
  static const int iH_;
  static const int iH2_;
  static const Real kgr_;//H2 formation rate on grains

  Real xi_cr_; //primary CRIR in s-1 H-1, read from input file, default 2e-16.
  Real kcr_; //CRIR for H2 = 3*xi_cr_
  Real nH_; //density, updated at InitializeNextStep from hydro variable
};

#endif // CHEMISTRY_NETWORK_H2_HPP_
