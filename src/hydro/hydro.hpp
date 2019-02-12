#ifndef HYDRO_HYDRO_HPP_
#define HYDRO_HYDRO_HPP_
//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file hydro.hpp
//  \brief definitions for Hydro class

// C headers

// C++ headers

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"

class MeshBlock;
class ParameterInput;
class HydroSourceTerms;
class HydroDiffusion;

//! \class Hydro
//  \brief hydro data and functions

class Hydro {
  friend class Field;
  friend class EquationOfState;
 public:
  Hydro(MeshBlock *pmb, ParameterInput *pin);
  ~Hydro();

  // data
  MeshBlock* pmy_block;    // ptr to MeshBlock containing this Hydro
  // conserved and primitive variables
  AthenaArray<Real> u, w;      // time-integrator memory register #1
  AthenaArray<Real> u1, w1;    // time-integrator memory register #2
  AthenaArray<Real> u2;       // time-integrator memory register #3
  // (no more than MAX_NREGISTER allowed)

  AthenaArray<Real> flux[3];  // face-averaged flux vector

  // fourth-order intermediate quantities
  AthenaArray<Real> u_cc, w_cc;      // cell-centered approximations

  HydroSourceTerms *psrc;
  HydroDiffusion *phdif;

  // functions
  void NewBlockTimeStep();    // computes new timestep on a MeshBlock
  void WeightedAveU(AthenaArray<Real> &u_out, AthenaArray<Real> &u_in1,
                    AthenaArray<Real> &u_in2, const Real wght[3]);
  void AddFluxDivergenceToAverage(AthenaArray<Real> &w, AthenaArray<Real> &bcc,
                                  const Real wght, AthenaArray<Real> &u_out);
  void CalculateFluxes(AthenaArray<Real> &w, FaceField &b,
                       AthenaArray<Real> &bcc, const int order);
  void CalculateFluxes_STS();

  void RiemannSolver(
      const int k, const int j, const int il, const int iu,
      const int ivx, const AthenaArray<Real> &bx,
      AthenaArray<Real> &wl, AthenaArray<Real> &wr, AthenaArray<Real> &flx,
      AthenaArray<Real> &ey, AthenaArray<Real> &ez,
      AthenaArray<Real> &wct, const AthenaArray<Real> &dxw);

  void AddGravityFlux();
  void AddGravityFluxWithGflx();
  void CalculateGravityFlux(AthenaArray<Real> &phi_in);

 private:
  AthenaArray<Real> dt1_, dt2_, dt3_;  // scratch arrays used in NewTimeStep
  // scratch space used to compute fluxes
  AthenaArray<Real> wl_, wr_, wlb_;
  AthenaArray<Real> dxw_;
  AthenaArray<Real> x1face_area_, x2face_area_, x3face_area_;
  AthenaArray<Real> x2face_area_p1_, x3face_area_p1_;
  AthenaArray<Real> cell_volume_;
  AthenaArray<Real> dflx_;
  AthenaArray<Real> bb_normal_;    // normal magnetic field, for (SR/GR)MHD
  AthenaArray<Real> lambdas_p_l_;  // most positive wavespeeds in left state
  AthenaArray<Real> lambdas_m_l_;  // most negative wavespeeds in left state
  AthenaArray<Real> lambdas_p_r_;  // most positive wavespeeds in right state
  AthenaArray<Real> lambdas_m_r_;  // most negative wavespeeds in right state
  AthenaArray<Real> g_, gi_;       // metric and inverse, for some GR Riemann solvers
  AthenaArray<Real> cons_;         // conserved state, for some GR Riemann solvers

  // self-gravity
  AthenaArray<Real> gflx[3], gflx_old[3]; // gravity tensor (old Athena style)

  // fourth-order hydro
  // 4D scratch arrays
  AthenaArray<Real> scr1_nkji_, scr2_nkji_;
  AthenaArray<Real> wl3d_, wr3d_;
  AthenaArray<Real> laplacian_l_fc_, laplacian_r_fc_;

  TimeStepFunc UserTimeStep_;

  Real GetWeightForCT(Real dflx, Real rhol, Real rhor, Real dx, Real dt);
};
#endif // HYDRO_HYDRO_HPP_
