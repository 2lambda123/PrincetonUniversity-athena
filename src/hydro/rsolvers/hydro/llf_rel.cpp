//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file llf_rel.cpp
//  \brief Implements local Lax-Friedrichs Riemann solver for relativistic hydrodynamics.

// C++ headers
#include <algorithm>  // max(), min()
#include <cmath>      // sqrt()

// Athena++ headers
#include "../../hydro.hpp"
#include "../../../athena.hpp"                   // enums, macros
#include "../../../athena_arrays.hpp"            // AthenaArray
#include "../../../coordinates/coordinates.hpp"  // Coordinates
#include "../../../eos/eos.hpp"                  // EquationOfState
#include "../../../mesh/mesh.hpp"                // MeshBlock

// Declarations
static void LLFTransforming(MeshBlock *pmb, const int k, const int j, const int il,
    const int iu, const int ivx, const AthenaArray<Real> &bb,
    AthenaArray<Real> &bb_normal, AthenaArray<Real> &g, AthenaArray<Real> &gi,
    AthenaArray<Real> &prim_l, AthenaArray<Real> &prim_r, AthenaArray<Real> &cons,
    AthenaArray<Real> &flux, AthenaArray<Real> &ey, AthenaArray<Real> &ez);
static void LLFNonTransforming(MeshBlock *pmb, const int k, const int j, const int il,
    const int iu, AthenaArray<Real> &g, AthenaArray<Real> &gi, AthenaArray<Real> &prim_l,
    AthenaArray<Real> &prim_r, AthenaArray<Real> &flux);

//----------------------------------------------------------------------------------------
// Riemann solver
// Inputs:
//   kl,ku,jl,ju,il,iu: lower and upper x1-, x2-, and x3-indices
//   ivx: type of interface (IVX for x1, IVY for x2, IVZ for x3)
//   bb: 3D array of normal magnetic fields (not used)
//   prim_l,prim_r: 3D arrays of left and right primitive states
// Outputs:
//   flux: 3D array of hydrodynamical fluxes across interfaces
//   ey,ez: 3D arrays of magnetic fluxes (electric fields) across interfaces (not used)
// Notes:
//   prim_l, prim_r overwritten
//   implements LLF algorithm similar to that of fluxcalc() in step_ch.c in Harm

void Hydro::RiemannSolver(const int kl, const int ku, const int jl, const int ju,
    const int il, const int iu, const int ivx, const AthenaArray<Real> &bb,
    AthenaArray<Real> &prim_l, AthenaArray<Real> &prim_r, AthenaArray<Real> &flux,
    AthenaArray<Real> &ey, AthenaArray<Real> &ez) {
  for (int k = kl; k <= ku; ++k) {
    for (int j = jl; j <= ju; ++j) {
      if (GENERAL_RELATIVITY and ivx == IVY and pmy_block->pcoord->IsPole(j)) {
        LLFNonTransforming(pmy_block, k, j, il, iu, g_, gi_, prim_l, prim_r, flux);
      } else {
        LLFTransforming(pmy_block, k, j, il, iu, ivx, bb, bb_normal_, g_, gi_, prim_l,
            prim_r, cons_, flux, ey, ez);
      }
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
// Frame-transforming LLF implementation
// Inputs:
//   pmb: pointer to MeshBlock object
//   k,j: x3- and x2-indices
//   il,iu: lower and upper x1-indices
//   ivx: type of interface (IVX for x1, IVY for x2, IVZ for x3)
//   bb: 3D array of normal magnetic fields (not used)
//   bb_normal: 1D scratch array for normal magnetic fields
//   g,gi: 1D scratch arrays for metric coefficients
//   prim_l,prim_r: 3D arrays of left and right primitive states
//   cons: 1D scratch array for conserved quantities
// Outputs:
//   flux: 3D array of hydrodynamical fluxes across interfaces
//   ey,ez: 3D arrays of magnetic fluxes (electric fields) across interfaces (not used)
// Notes:
//   prim_l, prim_r overwritten
//   implements LLF algorithm similar to that of fluxcalc() in step_ch.c in Harm
//   references Mignone & Bodo 2005, MNRAS 364 126 (MB)

static void LLFTransforming(MeshBlock *pmb, const int k, const int j, const int il,
    const int iu, const int ivx, const AthenaArray<Real> &bb,
    AthenaArray<Real> &bb_normal, AthenaArray<Real> &g, AthenaArray<Real> &gi,
    AthenaArray<Real> &prim_l, AthenaArray<Real> &prim_r, AthenaArray<Real> &cons,
    AthenaArray<Real> &flux, AthenaArray<Real> &ey, AthenaArray<Real> &ez) {

  // Transform primitives to locally flat coordinates if in GR
  #if GENERAL_RELATIVITY
  {
    switch (ivx) {
      case IVX:
        pmb->pcoord->PrimToLocal1(k, j, il, iu, bb, prim_l, prim_r, bb_normal);
        break;
      case IVY:
        pmb->pcoord->PrimToLocal2(k, j, il, iu, bb, prim_l, prim_r, bb_normal);
        break;
      case IVZ:
        pmb->pcoord->PrimToLocal3(k, j, il, iu, bb, prim_l, prim_r, bb_normal);
        break;
    }
  }
  #endif  // GENERAL_RELATIVITY

  // Calculate cyclic permutations of indices
  int ivy = IVX + ((ivx-IVX)+1)%3;
  int ivz = IVX + ((ivx-IVX)+2)%3;

  // Extract ratio of specific heats
  const Real gamma_adi = pmb->peos->GetGamma();
  const Real gamma_prime = gamma_adi/(gamma_adi - 1.0);

  Real cons_l[NWAVE][SIMD_WIDTH] __attribute__((aligned(CACHELINE_BYTES)));
  Real flux_l[NWAVE][SIMD_WIDTH] __attribute__((aligned(CACHELINE_BYTES)));
  Real cons_r[NWAVE][SIMD_WIDTH] __attribute__((aligned(CACHELINE_BYTES)));
  Real flux_r[NWAVE][SIMD_WIDTH] __attribute__((aligned(CACHELINE_BYTES)));

  // Go through each interface
  for (int i = il; i <= iu; i+=SIMD_WIDTH) {
#pragma omp simd simdlen(SIMD_WIDTH)
    for (int m=0; m<std::min(SIMD_WIDTH, iu-i+1); m++) {
      int ipm = i+m;

      // Extract left primitives
      Real rho_l = prim_l(IDN,k,j,ipm);
      Real pgas_l = prim_l(IPR,k,j,ipm);
      Real u_l[4];
      if (GENERAL_RELATIVITY) {
        Real vx_l = prim_l(ivx,k,j,ipm);
        Real vy_l = prim_l(ivy,k,j,ipm);
        Real vz_l = prim_l(ivz,k,j,ipm);
        u_l[0] = std::sqrt(1.0 + SQR(vx_l) + SQR(vy_l) + SQR(vz_l));
        u_l[1] = vx_l;
        u_l[2] = vy_l;
        u_l[3] = vz_l;
      } else {  // SR
        Real vx_l = prim_l(ivx,k,j,ipm);
        Real vy_l = prim_l(ivy,k,j,ipm);
        Real vz_l = prim_l(ivz,k,j,ipm);
        u_l[0] = std::sqrt(1.0 / (1.0 - SQR(vx_l) - SQR(vy_l) - SQR(vz_l)));
        u_l[1] = u_l[0] * vx_l;
        u_l[2] = u_l[0] * vy_l;
        u_l[3] = u_l[0] * vz_l;
      }

      // Extract right primitives
      Real rho_r = prim_r(IDN,k,j,ipm);
      Real pgas_r = prim_r(IPR,k,j,ipm);
      Real u_r[4];
      if (GENERAL_RELATIVITY) {
        Real vx_r = prim_r(ivx,k,j,ipm);
        Real vy_r = prim_r(ivy,k,j,ipm);
        Real vz_r = prim_r(ivz,k,j,ipm);
        u_r[0] = std::sqrt(1.0 + SQR(vx_r) + SQR(vy_r) + SQR(vz_r));
        u_r[1] = vx_r;
        u_r[2] = vy_r;
        u_r[3] = vz_r;
      } else {  // SR
        Real vx_r = prim_r(ivx,k,j,ipm);
        Real vy_r = prim_r(ivy,k,j,ipm);
        Real vz_r = prim_r(ivz,k,j,ipm);
        u_r[0] = std::sqrt(1.0 / (1.0 - SQR(vx_r) - SQR(vy_r) - SQR(vz_r)));
        u_r[1] = u_r[0] * vx_r;
        u_r[2] = u_r[0] * vy_r;
        u_r[3] = u_r[0] * vz_r;
      }

      // Calculate wavespeeds in left state (MB 23)
      Real lambda_p_l, lambda_m_l;
      Real wgas_l = rho_l + gamma_prime * pgas_l;
      pmb->peos->SoundSpeedsSR(wgas_l, pgas_l, u_l[1]/u_l[0], SQR(u_l[0]), &lambda_p_l,
                               &lambda_m_l);

      // Calculate wavespeeds in right state (MB 23)
      Real lambda_p_r, lambda_m_r;
      Real wgas_r = rho_r + gamma_prime * pgas_r;
      pmb->peos->SoundSpeedsSR(wgas_r, pgas_r, u_r[1]/u_r[0], SQR(u_r[0]), &lambda_p_r,
                               &lambda_m_r);

      // Calculate extremal wavespeed
      Real lambda_l = std::min(lambda_m_l, lambda_m_r);
      Real lambda_r = std::max(lambda_p_l, lambda_p_r);
      Real lambda = std::max(lambda_r, -lambda_l);

      // Calculate conserved quantities in L region (MB 3)
      cons_l[IDN][m] = rho_l * u_l[0];
      cons_l[IEN][m] = wgas_l * u_l[0] * u_l[0] - pgas_l;
      cons_l[ivx][m] = wgas_l * u_l[1] * u_l[0];
      cons_l[ivy][m] = wgas_l * u_l[2] * u_l[0];
      cons_l[ivz][m] = wgas_l * u_l[3] * u_l[0];

      // Calculate fluxes in L region (MB 2,3)
      flux_l[IDN][m] = rho_l * u_l[1];
      flux_l[IEN][m] = wgas_l * u_l[0] * u_l[1];
      flux_l[ivx][m] = wgas_l * u_l[1] * u_l[1] + pgas_l;
      flux_l[ivy][m] = wgas_l * u_l[2] * u_l[1];
      flux_l[ivz][m] = wgas_l * u_l[3] * u_l[1];

      // Calculate conqserved quantities in R region (MB 3)
      cons_r[IDN][m] = rho_r * u_r[0];
      cons_r[IEN][m] = wgas_r * u_r[0] * u_r[0] - pgas_r;
      cons_r[ivx][m] = wgas_r * u_r[1] * u_r[0];
      cons_r[ivy][m] = wgas_r * u_r[2] * u_r[0];
      cons_r[ivz][m] = wgas_r * u_r[3] * u_r[0];

      // Calculate fluxes in R region (MB 2,3)
      flux_r[IDN][m] = rho_r * u_r[1];
      flux_r[IEN][m] = wgas_r * u_r[0] * u_r[1];
      flux_r[ivx][m] = wgas_r * u_r[1] * u_r[1] + pgas_r;
      flux_r[ivy][m] = wgas_r * u_r[2] * u_r[1];
      flux_r[ivz][m] = wgas_r * u_r[3] * u_r[1];

      // Set conserved quantities in GR
      if (GENERAL_RELATIVITY) {
        for (int n = 0; n < NWAVE; ++n) {
          cons(n,ipm) = 0.5 * (cons_r[n][m] + cons_l[n][m] + (flux_l[n][m] - flux_r[n][m])
                               / lambda);
        }
      }

      // Set fluxes
      for (int n = 0; n < NHYDRO; ++n) {
        flux(n,k,j,ipm) = 0.5 * (flux_l[n][m] + flux_r[n][m] -
                                 lambda * (cons_r[n][m] - cons_l[n][m]));
      }
    }
  }

  // Transform fluxes to global coordinates if in GR
  #if GENERAL_RELATIVITY
  {
    switch (ivx) {
      case IVX:
        pmb->pcoord->FluxToGlobal1(k, j, il, iu, cons, bb_normal, flux, ey, ez);
        break;
      case IVY:
        pmb->pcoord->FluxToGlobal2(k, j, il, iu, cons, bb_normal, flux, ey, ez);
        break;
      case IVZ:
        pmb->pcoord->FluxToGlobal3(k, j, il, iu, cons, bb_normal, flux, ey, ez);
        break;
    }
  }
  #endif  // GENERAL_RELATIVITY
  return;
}

//----------------------------------------------------------------------------------------
// Non-frame-transforming LLF implementation
// Inputs:
//   pmb: pointer to MeshBlock object
//   k,j: x3- and x2-indices
//   il,iu: lower and upper x1-indices
//   g,gi: 1D scratch arrays for metric coefficients
//   prim_l,prim_r: 3D arrays of left and right primitive states
// Outputs:
//   flux: 3D array of hydrodynamical fluxes across interfaces
// Notes:
//   implements LLF algorithm similar to that of fluxcalc() in step_ch.c in Harm
//   derived from RiemannSolver() in llf_rel_no_transform.cpp assuming ivx = IVY

static void LLFNonTransforming(MeshBlock *pmb, const int k, const int j, const int il,
    const int iu, AthenaArray<Real> &g, AthenaArray<Real> &gi, AthenaArray<Real> &prim_l,
    AthenaArray<Real> &prim_r, AthenaArray<Real> &flux)
#if GENERAL_RELATIVITY
{
  // Extract ratio of specific heats
  const Real gamma_adi = pmb->peos->GetGamma();
  const Real gamma_prime = gamma_adi/(gamma_adi-1.0);

  // Get metric components
  pmb->pcoord->Face2Metric(k, j, il, iu, g, gi);

  Real cons_l[NWAVE][SIMD_WIDTH] __attribute__((aligned(CACHELINE_BYTES)));
  Real flux_l[NWAVE][SIMD_WIDTH] __attribute__((aligned(CACHELINE_BYTES)));
  Real cons_r[NWAVE][SIMD_WIDTH] __attribute__((aligned(CACHELINE_BYTES)));
  Real flux_r[NWAVE][SIMD_WIDTH] __attribute__((aligned(CACHELINE_BYTES)));

  // Go through each interface
  for (int i = il; i <= iu; i+=SIMD_WIDTH) {
#pragma omp simd simdlen(SIMD_WIDTH)
    for (int m=0; m<std::min(SIMD_WIDTH, iu-i+1); m++) {
      int ipm = i+m;

      // Extract metric
      Real g_00 = g(I00,ipm), g_01 = g(I01,ipm), g_02 = g(I02,ipm), g_03 = g(I03,ipm),
        g_10 = g(I01,ipm), g_11 = g(I11,ipm), g_12 = g(I12,ipm), g_13 = g(I13,ipm),
        g_20 = g(I02,ipm), g_21 = g(I12,ipm), g_22 = g(I22,ipm), g_23 = g(I23,ipm),
        g_30 = g(I03,ipm), g_31 = g(I13,ipm), g_32 = g(I23,ipm), g_33 = g(I33,ipm);
      Real g00 = gi(I00,ipm), g01 = gi(I01,ipm), g02 = gi(I02,ipm), g03 = gi(I03,ipm),
        g10 = gi(I01,ipm), g11 = gi(I11,ipm), g12 = gi(I12,ipm), g13 = gi(I13,ipm),
        g20 = gi(I02,ipm), g21 = gi(I12,ipm), g22 = gi(I22,ipm), g23 = gi(I23,ipm),
        g30 = gi(I03,ipm), g31 = gi(I13,ipm), g32 = gi(I23,ipm), g33 = gi(I33,ipm);
      Real alpha = std::sqrt(-1.0/g00);

      // Extract left primitives
      Real rho_l = prim_l(IDN,k,j,ipm);
      Real pgas_l = prim_l(IPR,k,j,ipm);
      Real uu1_l = prim_l(IVX,k,j,ipm);
      Real uu2_l = prim_l(IVY,k,j,ipm);
      Real uu3_l = prim_l(IVZ,k,j,ipm);

      // Extract right primitives
      Real rho_r = prim_r(IDN,k,j,ipm);
      Real pgas_r = prim_r(IPR,k,j,ipm);
      Real uu1_r = prim_r(IVX,k,j,ipm);
      Real uu2_r = prim_r(IVY,k,j,ipm);
      Real uu3_r = prim_r(IVZ,k,j,ipm);

      // Calculate 4-velocity in left state
      Real ucon_l[4], ucov_l[4];
      Real tmp = g_11*SQR(uu1_l) + 2.0*g_12*uu1_l*uu2_l + 2.0*g_13*uu1_l*uu3_l
        + g_22*SQR(uu2_l) + 2.0*g_23*uu2_l*uu3_l
        + g_33*SQR(uu3_l);
      Real gamma_l = std::sqrt(1.0 + tmp);
      ucon_l[0] = gamma_l / alpha;
      ucon_l[1] = uu1_l - alpha * gamma_l * g01;
      ucon_l[2] = uu2_l - alpha * gamma_l * g02;
      ucon_l[3] = uu3_l - alpha * gamma_l * g03;
      ucov_l[0] = g_00*ucon_l[0] + g_01*ucon_l[1] + g_02*ucon_l[2] + g_03*ucon_l[3];
      ucov_l[1] = g_10*ucon_l[0] + g_11*ucon_l[1] + g_12*ucon_l[2] + g_13*ucon_l[3];
      ucov_l[2] = g_20*ucon_l[0] + g_21*ucon_l[1] + g_22*ucon_l[2] + g_23*ucon_l[3];
      ucov_l[3] = g_30*ucon_l[0] + g_31*ucon_l[1] + g_32*ucon_l[2] + g_33*ucon_l[3];

      // Calculate 4-velocity in right state
      Real ucon_r[4], ucov_r[4];
      tmp = g_11*SQR(uu1_r) + 2.0*g_12*uu1_r*uu2_r + 2.0*g_13*uu1_r*uu3_r
        + g_22*SQR(uu2_r) + 2.0*g_23*uu2_r*uu3_r
        + g_33*SQR(uu3_r);
      Real gamma_r = std::sqrt(1.0 + tmp);
      ucon_r[0] = gamma_r / alpha;
      ucon_r[1] = uu1_r - alpha * gamma_r * g01;
      ucon_r[2] = uu2_r - alpha * gamma_r * g02;
      ucon_r[3] = uu3_r - alpha * gamma_r * g03;
      ucov_r[0] = g_00*ucon_r[0] + g_01*ucon_r[1] + g_02*ucon_r[2] + g_03*ucon_r[3];
      ucov_r[1] = g_10*ucon_r[0] + g_11*ucon_r[1] + g_12*ucon_r[2] + g_13*ucon_r[3];
      ucov_r[2] = g_20*ucon_r[0] + g_21*ucon_r[1] + g_22*ucon_r[2] + g_23*ucon_r[3];
      ucov_r[3] = g_30*ucon_r[0] + g_31*ucon_r[1] + g_32*ucon_r[2] + g_33*ucon_r[3];

      // Calculate wavespeeds in left state
      Real lambda_p_l, lambda_m_l;
      Real wgas_l = rho_l + gamma_prime * pgas_l;
      pmb->peos->SoundSpeedsGR(wgas_l, pgas_l, ucon_l[0], ucon_l[IVY], g00, g02, g22,
                               &lambda_p_l, &lambda_m_l);

      // Calculate wavespeeds in right state
      Real lambda_p_r, lambda_m_r;
      Real wgas_r = rho_r + gamma_prime * pgas_r;
      pmb->peos->SoundSpeedsGR(wgas_r, pgas_r, ucon_r[0], ucon_r[IVY], g00, g02, g22,
                               &lambda_p_r, &lambda_m_r);

      // Calculate extremal wavespeed
      Real lambda_l = std::min(lambda_m_l, lambda_m_r);
      Real lambda_r = std::max(lambda_p_l, lambda_p_r);
      Real lambda = std::max(lambda_r, -lambda_l);

      // Calculate conserved quantities in L region (rho u^0 and T^0_\mu)
      cons_l[IDN][m] = rho_l * ucon_l[0];
      cons_l[IEN][m] = wgas_l * ucon_l[0] * ucov_l[0] + pgas_l;
      cons_l[IVX][m] = wgas_l * ucon_l[0] * ucov_l[1];
      cons_l[IVY][m] = wgas_l * ucon_l[0] * ucov_l[2];
      cons_l[IVZ][m] = wgas_l * ucon_l[0] * ucov_l[3];

      // Calculate fluxes in L region (rho u^i and T^i_\mu, where i = IVY)
      flux_l[IDN][m] = rho_l * ucon_l[IVY];
      flux_l[IEN][m] = wgas_l * ucon_l[IVY] * ucov_l[0];
      flux_l[IVX][m] = wgas_l * ucon_l[IVY] * ucov_l[1];
      flux_l[IVY][m] = wgas_l * ucon_l[IVY] * ucov_l[2];
      flux_l[IVZ][m] = wgas_l * ucon_l[IVY] * ucov_l[3];
      flux_l[IVY][m] += pgas_l;

      // Calculate conserved quantities in R region (rho u^0 and T^0_\mu)
      cons_r[IDN][m] = rho_r * ucon_r[0];
      cons_r[IEN][m] = wgas_r * ucon_r[0] * ucov_r[0] + pgas_r;
      cons_r[IVX][m] = wgas_r * ucon_r[0] * ucov_r[1];
      cons_r[IVY][m] = wgas_r * ucon_r[0] * ucov_r[2];
      cons_r[IVZ][m] = wgas_r * ucon_r[0] * ucov_r[3];

      // Calculate fluxes in R region (rho u^i and T^i_\mu, where i = IVY)
      flux_r[IDN][m] = rho_r * ucon_r[IVY];
      flux_r[IEN][m] = wgas_r * ucon_r[IVY] * ucov_r[0];
      flux_r[IVX][m] = wgas_r * ucon_r[IVY] * ucov_r[1];
      flux_r[IVY][m] = wgas_r * ucon_r[IVY] * ucov_r[2];
      flux_r[IVZ][m] = wgas_r * ucon_r[IVY] * ucov_r[3];
      flux_r[IVY][m] += pgas_r;

      // Set fluxes
      for (int n = 0; n < NHYDRO; ++n) {
        flux(n,k,j,ipm) = 0.5 * (flux_l[n][m] + flux_r[n][m]
                                 - lambda * (cons_r[n][m] - cons_l[n][m]));
      }
    }
  }
  return;
}

#else
{
  return;
}
#endif  // GENERAL_RELATIVITY
