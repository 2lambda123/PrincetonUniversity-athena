//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file noop.cpp
//  \brief Implements no-op versions of the general eos functions

// C headers

// C++ headers
#include <cmath>   // sqrt()
#include <fstream>
#include <iostream> // ifstream
#include <sstream>
#include <stdexcept> // std::invalid_argument
#include <string>

// Athena++ headers
#include "../eos.hpp"

Real EquationOfState::RiemannAsq(Real rho, Real hint) {
  std::stringstream msg;
  msg << "### FATAL ERROR in EquationOfState::RiemannAsq" << std::endl
      << "Function should not be called with current configuration." << std::endl;
  ATHENA_ERROR(msg);
  return -1.0;
}
Real EquationOfState::SimplePres(Real rho, Real egas) {
  std::stringstream msg;
  msg << "### FATAL ERROR in EquationOfState::SimplePres" << std::endl
      << "Function should not be called with current configuration." << std::endl;
  ATHENA_ERROR(msg);
  return -1.0;
}
Real EquationOfState::SimpleEgas(Real rho, Real pres) {
  std::stringstream msg;
  msg << "### FATAL ERROR in EquationOfState::SimpleEgas" << std::endl
      << "Function should not be called with current configuration." << std::endl;
  ATHENA_ERROR(msg);
  return -1.0;
}
Real EquationOfState::SimpleAsq(Real rho, Real pres) {
  std::stringstream msg;
  msg << "### FATAL ERROR in EquationOfState::SimpleAsq" << std::endl
      << "Function should not be called with current configuration." << std::endl;
  ATHENA_ERROR(msg);
  return -1.0;
}
void EquationOfState::PrepEOS(ParameterInput *pin) {
  std::stringstream msg;
  msg << "### FATAL ERROR in EquationOfState::PrepEOS" << std::endl
      << "Function should not be called with current configuration." << std::endl;
  ATHENA_ERROR(msg);
  return;
}
void EquationOfState::CleanEOS() {
  std::stringstream msg;
  msg << "### FATAL ERROR in EquationOfState::CleanEOS()" << std::endl
      << "Function should not be called with current configuration." << std::endl;
  ATHENA_ERROR(msg);
  return;
}
