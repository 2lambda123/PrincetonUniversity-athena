#ifndef CHEMISTRY_UTILS_KIDA_SPECIES_HPP_
#define CHEMISTRY_UTILS_KIDA_SPECIES_HPP_

//======================================================================================
// Athena++ astrophysical MHD code
// Copyright (C) 2014 James M. Stone  <jmstone@princeton.edu>
// See LICENSE file for full public license information.
//======================================================================================
//! \file thermo.hpp
//  \brief definitions for heating and cooling processes
//======================================================================================

//c++ header
#include <sstream>    // stringstream
#include <string>     // string

// Athena++ classes headers
#include "../../athena.hpp"
#include "chemistry_utils.hpp"

class KidaSpecies{
  friend class ChemNetwork;
  friend class KidaNetwork;
 public:
  KidaSpecies(std::string line, int index);
  //starts form zero, matches the index in species name in ChemNetwork
  const int index;
  std::string name;
 private:
  static const int natom_ = 13;
  static const Real ma_atom_[natom_];//mass of atoms in u
  int charge_;
  int atom_count_[natom_]; // NOLINT (runtime/arrays)
  //mass of the species in g, used in grain - molecule reactions.
  //mass is automatically calculated using the number of atoms
  Real mass_;
  void SetMass(Real mass);//set the mass of species. used for grains.
};

#endif //CHEMISTRY_UTILS_KIDA_SPECIES_HPP_
