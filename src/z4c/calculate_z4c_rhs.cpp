//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file adm_z4c.cpp
//  \brief implementation of functions in the Z4c class related to ADM decomposition

// C++ standard headers
#include <algorithm> // max
#include <cmath> // exp, pow, sqrt
#include <iomanip>
#include <iostream>
#include <fstream>

// Athena++ headers
#include "z4c.hpp"
#include "z4c_macro.hpp"
#include "../coordinates/coordinates.hpp"
#include "../mesh/mesh.hpp"

// External libraries
#ifdef GSL
#include <gsl/gsl_sf_bessel.h>   // Bessel functions
#endif

#ifdef Z4C_TRACKER
#include "trackers.hpp"
#endif // Z4C_TRACKER


//----------------------------------------------------------------------------------------
// \!fn void Z4c::Z4cRHS(AthenaArray<Real> & u, AthenaArray<Real> & u_mat, AthenaArray<Real> & u_rhs)
// \brief compute the RHS given the state vector and matter state
//
// This function operates only on the interior points of the MeshBlock

void Z4c::Z4cRHS(AthenaArray<Real> & u, AthenaArray<Real> & u_mat,
                 AthenaArray<Real> & u_rhs) {

  Z4c_vars z4c, rhs;
  SetZ4cAliases(u, z4c);
  SetZ4cAliases(u_rhs, rhs);

  Matter_vars mat;
  SetMatterAliases(u_mat, mat);

  //---------------------------------------------------------------------------
  // Scratch arrays for spatially dependent eta shift damping
#if defined(Z4C_ETA_CONF)
  int nn1 = mbi.nn1;
  // 1/psi^2 (guarded); derivative and shift eta scratch
  AthenaTensor<Real, TensorSymm::NONE, NDIM, 0> oopsi2;
  AthenaTensor<Real, TensorSymm::NONE, NDIM, 1> doopsi2_d;
  AthenaTensor<Real, TensorSymm::NONE, NDIM, 0> shift_eta_spa;

  oopsi2.NewAthenaTensor(nn1);
  doopsi2_d.NewAthenaTensor(nn1);
  shift_eta_spa.NewAthenaTensor(nn1);

#elif defined(Z4C_ETA_TRACK_TP)

  // int nn1 = mbi.nn1;
  // AthenaTensor<Real, TensorSymm::NONE, NDIM, 0> eta_damp;

  // eta_damp.NewAthenaTensor(nn1);

#endif // Z4C_ETA_CONF, Z4C_ETA_TRACK_TP
  //---------------------------------------------------------------------------

  //---------------------------------------------------------------------------
#if (0) // DEBUG: (for output in y-direction, to be read by Mathematica)
  Real i_test = mbi.il; //where to evaluate things
  std::cout << "Writing test output to file..." << std::endl;
  std::ofstream outdata;
  outdata.open ("output.dat");
#endif  // END DEBUG

#if (0) // DEBUG
  int body = 0;

  coutBoldBlue("body_0 tracker:\n");
  for (int i_dim = 0; i_dim < NDIM; ++i_dim) {
    Real tt = pmy_block->pmy_mesh->pz4c_tracker->pos_body[body].pos[i_dim];
    printf("%1.6f, ", tt);
  }
  coutBoldBlue("\n=\n");
#endif  // END DEBUG
  //---------------------------------------------------------------------------

  ILOOP2(k,j) {
    // -----------------------------------------------------------------------------------
    // 1st derivatives
    //
    // Scalars
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        dalpha_d(a,i) = FD.Dx(a, z4c.alpha(k,j,i));
        dchi_d(a,i)   = FD.Dx(a, z4c.chi(k,j,i));
        dKhat_d(a,i)  = FD.Dx(a, z4c.Khat(k,j,i));
        dTheta_d(a,i) = FD.Dx(a, z4c.Theta(k,j,i));
      }
    }
    // Vectors
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b) {
      ILOOP1(i) {
        dbeta_du(b,a,i) = FD.Dx(b, z4c.beta_u(a,k,j,i));
        dGam_du(b,a,i)  = FD.Dx(b, z4c.Gam_u(a,k,j,i));
      }
    }
    // Tensors
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b)
    for(int c = 0; c < NDIM; ++c) {
      ILOOP1(i) {
        dg_ddd(c,a,b,i) = FD.Dx(c, z4c.g_dd(a,b,k,j,i));
        dA_ddd(c,a,b,i) = FD.Dx(c, z4c.A_dd(a,b,k,j,i));
      }
    }

    // -----------------------------------------------------------------------------------
    // 2nd derivatives
    //
    // Scalars
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        ddalpha_dd(a,a,i) = FD.Dxx(a, z4c.alpha(k,j,i));
        ddchi_dd(a,a,i) = FD.Dxx(a, z4c.chi(k,j,i));
      }
      for(int b = a + 1; b < NDIM; ++b) {
        ILOOP1(i) {
          ddalpha_dd(a,b,i) = FD.Dxy(a, b, z4c.alpha(k,j,i));
          ddchi_dd(a,b,i) = FD.Dxy(a, b, z4c.chi(k,j,i));
        }
      }
    }
    // Vectors
    for(int c = 0; c < NDIM; ++c)
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      if(a == b) {
        ILOOP1(i) {
          ddbeta_ddu(a,b,c,i) = FD.Dxx(a, z4c.beta_u(c,k,j,i));
        }
      }
      else {
        ILOOP1(i) {
          ddbeta_ddu(a,b,c,i) = FD.Dxy(a, b, z4c.beta_u(c,k,j,i));
        }
      }
    }
    // Tensors
    for(int c = 0; c < NDIM; ++c)
    for(int d = c; d < NDIM; ++d)
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      if(a == b) {
        ILOOP1(i) {
          ddg_dddd(a,b,c,d,i) = FD.Dxx(a, z4c.g_dd(c,d,k,j,i));
        }
      }
      else {
        ILOOP1(i) {
          ddg_dddd(a,b,c,d,i) = FD.Dxy(a, b, z4c.g_dd(c,d,k,j,i));
        }
      }
    }

    // -----------------------------------------------------------------------------------
    // Advective derivatives
    //
    // Scalars
    Lalpha.ZeroClear();
    Lchi.ZeroClear();
    LKhat.ZeroClear();
    LTheta.ZeroClear();
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        Lalpha(i) += FD.Lx(a, z4c.beta_u(a,k,j,i), z4c.alpha(k,j,i));
        Lchi(i) += FD.Lx(a, z4c.beta_u(a,k,j,i), z4c.chi(k,j,i));
        LKhat(i) += FD.Lx(a, z4c.beta_u(a,k,j,i), z4c.Khat(k,j,i));
        LTheta(i) += FD.Lx(a, z4c.beta_u(a,k,j,i), z4c.Theta(k,j,i));
      }
    }
    // Vectors
    Lbeta_u.ZeroClear();
    LGam_u.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b) {
      ILOOP1(i) {
        Lbeta_u(b,i) += FD.Lx(a, z4c.beta_u(a,k,j,i), z4c.beta_u(b,k,j,i));
        LGam_u(b,i)  += FD.Lx(a, z4c.beta_u(a,k,j,i), z4c.Gam_u(b,k,j,i));
      }
    }
    // Tensors
    Lg_dd.ZeroClear();
    LA_dd.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b)
    for(int c = 0; c < NDIM; ++c) {
      ILOOP1(i) {
        Lg_dd(a,b,i) += FD.Lx(c, z4c.beta_u(c,k,j,i), z4c.g_dd(a,b,k,j,i));
        LA_dd(a,b,i) += FD.Lx(c, z4c.beta_u(c,k,j,i), z4c.A_dd(a,b,k,j,i));
      }
    }

    // -----------------------------------------------------------------------------------
    // Get K from Khat
    //
    ILOOP1(i) {
      K(i) = z4c.Khat(k,j,i) + 2.*z4c.Theta(k,j,i);
    }
// TODO: remove dg_duu as it is not needed
//    for(int a = 0; a < NDIM; ++a) {
//      ILOOP1(i) {
//        dK_d(a,i) = dKhat_d(a,i) + 2*dTheta_d(a,i);
//      }
//    }

    // -----------------------------------------------------------------------------------
    // Inverse metric
    //
    ILOOP1(i) {
      detg(i) = SpatialDet(z4c.g_dd, k, j, i);
      SpatialInv(1.0/detg(i),
          z4c.g_dd(0,0,k,j,i), z4c.g_dd(0,1,k,j,i), z4c.g_dd(0,2,k,j,i),
          z4c.g_dd(1,1,k,j,i), z4c.g_dd(1,2,k,j,i), z4c.g_dd(2,2,k,j,i),
          &g_uu(0,0,i), &g_uu(0,1,i), &g_uu(0,2,i),
          &g_uu(1,1,i), &g_uu(1,2,i), &g_uu(2,2,i));
    }
    // TODO: remove dg_duu as it is not needed
    //dg_duu.ZeroClear();
    //for(int a = 0; a < NDIM; ++a)
    //for(int b = 0; b < NDIM; ++b)
    //for(int c = b; c < NDIM; ++c)
    //for(int d = 0; d < NDIM; ++d)
    //for(int e = 0; e < NDIM; ++e) {
    //  ILOOP1(i) {
    //    dg_duu(a,b,c,i) -= g_uu(b,d,i) * g_uu(c,e,i) * dg_ddd(a,d,e,i);
    //  }
    //}

    // -----------------------------------------------------------------------------------
    // Christoffel symbols
    //
    for(int c = 0; c < NDIM; ++c)
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      ILOOP1(i) {
        Gamma_ddd(c,a,b,i) = 0.5*(dg_ddd(a,b,c,i) + dg_ddd(b,a,c,i) - dg_ddd(c,a,b,i));
      }
    }
    Gamma_udd.ZeroClear();
    for(int c = 0; c < NDIM; ++c)
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b)
    for(int d = 0; d < NDIM; ++d) {
      ILOOP1(i) {
        Gamma_udd(c,a,b,i) += g_uu(c,d,i)*Gamma_ddd(d,a,b,i);
      }
    }
    // Gamma's computed from the conformal metric (not evolved)
    Gamma_u.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b)
    for(int c = 0; c < NDIM; ++c) {
      ILOOP1(i) {
        Gamma_u(a,i) += g_uu(b,c,i)*Gamma_udd(a,b,c,i);
      }
    }

    // -----------------------------------------------------------------------------------
    // Curvature of conformal metric
    //
    R_dd.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      for(int c = 0; c < NDIM; ++c) {
        ILOOP1(i) {
          R_dd(a,b,i) += 0.5*(z4c.g_dd(c,a,k,j,i)*dGam_du(b,c,i) +
                              z4c.g_dd(c,b,k,j,i)*dGam_du(a,c,i) +
                              Gamma_u(c,i)*(Gamma_ddd(a,b,c,i) + Gamma_ddd(b,a,c,i)));
        }
      }
      for(int c = 0; c < NDIM; ++c)
      for(int d = 0; d < NDIM; ++d) {
        ILOOP1(i) {
          R_dd(a,b,i) -= 0.5*g_uu(c,d,i)*ddg_dddd(c,d,a,b,i);
        }
      }
      for(int c = 0; c < NDIM; ++c)
      for(int d = 0; d < NDIM; ++d)
      for(int e = 0; e < NDIM; ++e) {
        ILOOP1(i) {
          R_dd(a,b,i) += g_uu(c,d,i)*(
              Gamma_udd(e,c,a,i)*Gamma_ddd(b,e,d,i) +
              Gamma_udd(e,c,b,i)*Gamma_ddd(a,e,d,i) +
              Gamma_udd(e,a,d,i)*Gamma_ddd(e,c,b,i));
        }
      }
    }

    // -----------------------------------------------------------------------------------
    // Derivatives of conformal factor phi
    //
    ILOOP1(i) {
      chi_guarded(i) = std::max(z4c.chi(k,j,i), opt.chi_div_floor);
      oopsi4(i) = pow(chi_guarded(i), -4./opt.chi_psi_power);
    }

    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        dphi_d(a,i) = dchi_d(a,i)/(chi_guarded(i) * opt.chi_psi_power);
      }
    }
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      ILOOP1(i) {
        Real const ddphi_ab = ddchi_dd(a,b,i)/(chi_guarded(i) * opt.chi_psi_power) -
          opt.chi_psi_power * dphi_d(a,i) * dphi_d(b,i);
        Ddphi_dd(a,b,i) = ddphi_ab;
      }
      for(int c = 0; c < NDIM; ++c) {
        ILOOP1(i) {
          Ddphi_dd(a,b,i) -= Gamma_udd(c,a,b,i)*dphi_d(c,i);
        }
      }
    }

    // -----------------------------------------------------------------------------------
    // Curvature contribution from conformal factor
    //
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      ILOOP1(i) {
        Rphi_dd(a,b,i) = 4.*dphi_d(a,i)*dphi_d(b,i) - 2.*Ddphi_dd(a,b,i);
      }
      for(int c = 0; c < NDIM; ++c)
      for(int d = 0; d < NDIM; ++d) {
        ILOOP1(i) {
          Rphi_dd(a,b,i) -= 2.*z4c.g_dd(a,b,k,j,i) * g_uu(c,d,i)*(Ddphi_dd(c,d,i) +
              2.*dphi_d(c,i)*dphi_d(d,i));
        }
      }
    }

    // -----------------------------------------------------------------------------------
    // Trace of the matter stress tensor
    //
    S.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b) {
      ILOOP1(i) {
        S(i) += oopsi4(i) * g_uu(a,b,i) * mat.S_dd(a,b,k,j,i);
      }
    }

    // -----------------------------------------------------------------------------------
    // 2nd covariant derivative of the lapse
    //
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b) {
      ILOOP1(i) {
        Ddalpha_dd(a,b,i) = ddalpha_dd(a,b,i)
                          - 2.*(dphi_d(a,i)*dalpha_d(b,i) + dphi_d(b,i)*dalpha_d(a,i));
      }
      for(int c = 0; c < NDIM; ++c) {
        ILOOP1(i) {
          Ddalpha_dd(a,b,i) -= Gamma_udd(c,a,b,i)*dalpha_d(c,i);
        }
        for(int d = 0; d < NDIM; ++d) {
          ILOOP1(i) {
            Ddalpha_dd(a,b,i) += 2.*z4c.g_dd(a,b,k,j,i) * g_uu(c,d,i) * dphi_d(c,i) * dalpha_d(d,i);
          }
        }
      }
    }

    Ddalpha.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b) {
      ILOOP1(i) {
        Ddalpha(i) += oopsi4(i) * g_uu(a,b,i) * Ddalpha_dd(a,b,i);
      }
    }

    // -----------------------------------------------------------------------------------
    // Contractions of A_ab, inverse, and derivatives
    //
    AA_dd.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b)
    for(int c = 0; c < NDIM; ++c)
    for(int d = 0; d < NDIM; ++d) {
      ILOOP1(i) {
        AA_dd(a,b,i) += g_uu(c,d,i) * z4c.A_dd(a,c,k,j,i) * z4c.A_dd(d,b,k,j,i);
      }
    }
    AA.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b) {
      ILOOP1(i) {
        AA(i) += g_uu(a,b,i) * AA_dd(a,b,i);
      }
    }
    A_uu.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b)
    for(int c = 0; c < NDIM; ++c)
    for(int d = 0; d < NDIM; ++d) {
      ILOOP1(i) {
        A_uu(a,b,i) += g_uu(a,c,i) * g_uu(b,d,i) * z4c.A_dd(c,d,k,j,i);
      }
    }
    DA_u.ZeroClear();
    for(int a = 0; a < NDIM; ++a) {
      for(int b = 0; b < NDIM; ++b) {
        ILOOP1(i) {
          DA_u(a,i) -= (3./2.) * A_uu(a,b,i) * dchi_d(b,i) / chi_guarded(i);
          DA_u(a,i) -= (1./3.) * g_uu(a,b,i) * (2.*dKhat_d(b,i) + dTheta_d(b,i));
        }
      }
      for(int b = 0; b < NDIM; ++b)
      for(int c = 0; c < NDIM; ++c) {
        ILOOP1(i) {
          DA_u(a,i) += Gamma_udd(a,b,c,i) * A_uu(b,c,i);
        }
      }
    }

    // -----------------------------------------------------------------------------------
    // Ricci scalar
    //
    R.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b) {
      ILOOP1(i) {
        R(i) += oopsi4(i) * g_uu(a,b,i) * (R_dd(a,b,i) + Rphi_dd(a,b,i));
      }
    }

    // -----------------------------------------------------------------------------------
    // Hamiltonian constraint
    //
    ILOOP1(i) {
      Ht(i) = R(i) + (2./3.)*SQR(K(i)) - AA(i);
    }

    // -----------------------------------------------------------------------------------
    // Finalize advective (Lie) derivatives
    //
    // Shift vector contractions
    dbeta.ZeroClear();
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        dbeta(i) += dbeta_du(a,a,i);
      }
    }
    ddbeta_d.ZeroClear();
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b) {
      ILOOP1(i) {
        ddbeta_d(a,i) += (1./3.) * ddbeta_ddu(a,b,b,i);
      }
    }
    // Finalize Lchi
    ILOOP1(i) {
      Lchi(i) += (1./6.) * opt.chi_psi_power * chi_guarded(i) * dbeta(i);
    }
    // Finalize LGam_u (note that this is not a real Lie derivative)
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        LGam_u(a,i) += (2./3.) * Gamma_u(a,i) * dbeta(i);
      }
      for(int b = 0; b < NDIM; ++b) {
        ILOOP1(i) {
          LGam_u(a,i) += g_uu(a,b,i) * ddbeta_d(b,i) - Gamma_u(b,i) * dbeta_du(b,a,i);
        }
        for(int c = 0; c < NDIM; ++c) {
          ILOOP1(i) {
            LGam_u(a,i) += g_uu(b,c,i) * ddbeta_ddu(b,c,a,i);
          }
        }
      }
    }
    // Finalize Lg_dd and LA_dd
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      ILOOP1(i) {
        Lg_dd(a,b,i) -= (2./3.) * z4c.g_dd(a,b,k,j,i) * dbeta(i);
        LA_dd(a,b,i) -= (2./3.) * z4c.A_dd(a,b,k,j,i) * dbeta(i);
      }
      for(int c = 0; c < NDIM; ++c) {
        ILOOP1(i) {
          Lg_dd(a,b,i) += dbeta_du(a,c,i) * z4c.g_dd(b,c,k,j,i);
          LA_dd(a,b,i) += dbeta_du(b,c,i) * z4c.A_dd(a,c,k,j,i);
          Lg_dd(a,b,i) += dbeta_du(b,c,i) * z4c.g_dd(a,c,k,j,i);
          LA_dd(a,b,i) += dbeta_du(a,c,i) * z4c.A_dd(b,c,k,j,i);
        }
      }
    }

    // -----------------------------------------------------------------------------------
    // Assemble RHS
    //
    // Khat, chi, and Theta
    ILOOP1(i) {
      rhs.Khat(k,j,i) = - Ddalpha(i) + z4c.alpha(k,j,i) * (AA(i) + (1./3.)*SQR(K(i))) +
        LKhat(i) + opt.damp_kappa1*(1 - opt.damp_kappa2) * z4c.alpha(k,j,i) * z4c.Theta(k,j,i);
      rhs.Khat(k,j,i) += 4*M_PI * z4c.alpha(k,j,i) * (S(i) + mat.rho(k,j,i));
      rhs.chi(k,j,i) = Lchi(i) - (1./6.) * opt.chi_psi_power *
        chi_guarded(i) * z4c.alpha(k,j,i) * K(i);
      rhs.Theta(k,j,i) = LTheta(i) + z4c.alpha(k,j,i) * (
          0.5*Ht(i) - (2. + opt.damp_kappa2) * opt.damp_kappa1 * z4c.Theta(k,j,i));
      rhs.Theta(k,j,i) -= 8.*M_PI * z4c.alpha(k,j,i) * mat.rho(k,j,i);
    }
    // Gamma's
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        rhs.Gam_u(a,k,j,i) = 2.*z4c.alpha(k,j,i)*DA_u(a,i) + LGam_u(a,i);
        rhs.Gam_u(a,k,j,i) -= 2.*z4c.alpha(k,j,i) * opt.damp_kappa1 *
            (z4c.Gam_u(a,k,j,i) - Gamma_u(a,i));
      }
      for(int b = 0; b < NDIM; ++b) {
        ILOOP1(i) {
          rhs.Gam_u(a,k,j,i) -= 2. * A_uu(a,b,i) * dalpha_d(b,i);
          rhs.Gam_u(a,k,j,i) -= 16.*M_PI * z4c.alpha(k,j,i) * g_uu(a,b,i) * mat.S_d(b,k,j,i);
        }
      }
    }
    // g and A
    //LOOK
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      ILOOP1(i) {
        rhs.g_dd(a,b,k,j,i) = - 2. * z4c.alpha(k,j,i) * z4c.A_dd(a,b,k,j,i) + Lg_dd(a,b,i);
        rhs.A_dd(a,b,k,j,i) = oopsi4(i) *
            (-Ddalpha_dd(a,b,i) + z4c.alpha(k,j,i) * (R_dd(a,b,i) + Rphi_dd(a,b,i)));
        rhs.A_dd(a,b,k,j,i) -= (1./3.) * z4c.g_dd(a,b,k,j,i) * (-Ddalpha(i) + z4c.alpha(k,j,i)*R(i));
        rhs.A_dd(a,b,k,j,i) += z4c.alpha(k,j,i) * (K(i)*z4c.A_dd(a,b,k,j,i) - 2.*AA_dd(a,b,i));
        rhs.A_dd(a,b,k,j,i) += LA_dd(a,b,i);
        rhs.A_dd(a,b,k,j,i) -= 8.*M_PI * z4c.alpha(k,j,i) *
            (oopsi4(i)*mat.S_dd(a,b,k,j,i) - (1./3.)*S(i)*z4c.g_dd(a,b,k,j,i));
      }
    }
    // lapse function
    ILOOP1(i) {
      Real const f = opt.lapse_oplog * opt.lapse_harmonicf + opt.lapse_harmonic * z4c.alpha(k,j,i);
      rhs.alpha(k,j,i) = opt.lapse_advect * Lalpha(i) - f * z4c.alpha(k,j,i) * z4c.Khat(k,j,i);
    }

    // shift vector
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        rhs.beta_u(a,k,j,i) = z4c.Gam_u(a,k,j,i) + opt.shift_advect * Lbeta_u(a,i);
        // rhs.beta_u(a,k,j,i) -= opt.shift_eta * z4c.beta_u(a,k,j,i);
      }
    }

#if defined(Z4C_ETA_CONF)
    // compute based on conformal factor

    // relevant fields:
    // g_uu(c, d, i) [con. conf. g]
    // z4c.chi(k,j,i)
    // dchi_d(a,i)   [cov. pd of chi]

    eta_damp.ZeroClear();

    for(int a = 0; a < NDIM; ++a) {
      for(int b = 0; b < NDIM; ++b) {
        ILOOP1(i) {
          eta_damp(i) += g_uu(a,b,i) * dchi_d(a,i) * dchi_d(b,i);
        }
      }
    }

    ILOOP1(i) {
      eta_damp(i) = SQRT(eta_damp(i) / z4c.chi(k,j,i))
        * pow(1. - pow(z4c.chi(k,j,i), opt.shift_eta_a / 2.),
              opt.shift_eta_b);
      eta_damp(i) *= opt.shift_eta_R_0 / 2.;
    }

    // mask and damp
    for(int a = 0; a< NDIM; ++a) {
      ILOOP1(i) {
        rhs.beta_u(a,k,j,i) -= eta_damp(i) * z4c.beta_u(a,k,j,i);
      }
    }

#elif defined(Z4C_ETA_TRACK_TP)
    int const b_ix = opt.shift_eta_TP_ix;

    eta_damp.ZeroClear();

    // compute prefactors
    ILOOP1(i) {

      eta_damp(i) += \
        POW2(pmy_block->pmy_mesh->pz4c_tracker->pos_body[b_ix].pos[0]
          - mbi.x1(i));
      eta_damp(i) += \
        POW2(pmy_block->pmy_mesh->pz4c_tracker->pos_body[b_ix].pos[1]
          - mbi.x2(j));
      eta_damp(i) += \
        POW2(pmy_block->pmy_mesh->pz4c_tracker->pos_body[b_ix].pos[2]
          - mbi.x3(k));

      eta_damp(i) = 1. + pow(POW2(eta_damp(i) / opt.shift_eta_w),
                             opt.shift_eta_delta);
      eta_damp(i) = opt.shift_eta \
        + (opt.shift_eta_P - opt.shift_eta) / eta_damp(i);
    }

    // mask and damp
    for(int a = 0; a< NDIM; ++a) {
      ILOOP1(i) {
        rhs.beta_u(a,k,j,i) -= eta_damp(i) * z4c.beta_u(a,k,j,i);
      }
    }

#else
    // global constant [original implementation]
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        rhs.beta_u(a,k,j,i) -= opt.shift_eta * z4c.beta_u(a,k,j,i);
      }
    }
#endif // Z4C_ETA_CONF, Z4C_ETA_TRACK_TP

#if (0)// DEBUG (force analytical polarised Gowdy lapse, zero shift)
#ifdef GSL

  // compute info for \Lambda--------------------------------------------------
  Real t = opt.AwA_polarised_Gowdy_t0;

  Real J0 = gsl_sf_bessel_J0(2. * PI);
  Real J1 = gsl_sf_bessel_J1(2. * PI);
  Real sqr_J0 = SQR(J0);
  Real sqr_J1 = SQR(J1);

  Real J0_t = gsl_sf_bessel_J0(2. * PI * t);
  Real J1_t = gsl_sf_bessel_J1(2. * PI * t);
  Real sqr_J0_t = SQR(J0_t);
  Real sqr_J1_t = SQR(J1_t);

  Real dt_J0_t = -2. * PI * J1_t;
  Real dt_J1_t = 2. * PI * J0_t - J1_t / t;

  Real sqr_pi = SQR(PI);
  Real sqr_t = SQR(t);

  // Real L = -2. * PI * t * J0_t * J1_t * sqr_cos_x;
  // L += 2. * sqr_pi * sqr_t * (sqr_J0_t + sqr_J1_t);
  // L -= 1. / 2. * (4. * sqr_pi * (sqr_J0 + sqr_J1) - 2. * PI * J0 * J1);

  Real pow_t_m_1_4 = std::pow(t, -1./4.);
  //-----------


  GLOOP3(k,j,i) {
    Real cos_x = std::cos(2. * PI * mbi.x1(i));
    Real sqr_cos_x = SQR(cos_x);

    Real L = -2. * PI * t * J0_t * J1_t * sqr_cos_x;
    L += 2. * sqr_pi * sqr_t * (sqr_J0_t + sqr_J1_t);
    L -= 1. / 2. * (4. * sqr_pi * (sqr_J0 + sqr_J1) - 2. * PI * J0 * J1);

    rhs.alpha(k,j,i) = pow_t_m_1_4 * std::exp(L / 4.);
  }


  ILOOP2(k,j) {
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        rhs.beta_u(a,k,j,i) = 0.;
      }
    }
  }
#endif // ENDGSL
#endif // ENDDEBUG

#if (0)// DEBUG (forcing zero shift)
    for(int a = 0; a < NDIM; ++a) {
      ILOOP1(i) {
        rhs.beta_u(a,k,j,i) = 0.;
      }
    }
#endif // ENDDEBUG

#if (0)    // DEBUG
      outdata //(output in y-direction, to be read by Mathematica)
      << std::setprecision(17)
      << z4c.alpha(k,j,i_test) << " "
      << dalpha_d(0,i_test) << " "
      << dalpha_d(1,i_test) << " "
      << mbi.x2(j) //y-grid
      << std::endl;
#endif// END DEBUG

#if (0) // DEBUG (output in x-direction, to be read by Mathematica)
    if (j == mbi.ju) {
      std::cout << "---> Writing test output to file..." << std::endl;
      std::cout << "(j,y(j)) = (" << j << "," << mbi.x2(j) << ")" << std::endl;

      std::ofstream outdata;
      outdata.open ("output.dat");
      ILOOP1(i) {
        outdata
        << std::setprecision(17)

        << rhs.g_dd(0,0,k,j,i) << " "
        << rhs.g_dd(1,1,k,j,i) << " "
        << rhs.g_dd(2,2,k,j,i) << " "
        << rhs.A_dd(0,0,k,j,i) << " "
        << rhs.A_dd(1,1,k,j,i) << " "
        << rhs.A_dd(2,2,k,j,i) << " "
        << rhs.Gam_u(0,k,j,i) << " "
        << rhs.Khat(k,j,i) << " "
        << rhs.chi(k,j,i) << " "
        << rhs.Theta(k,j,i) << " "
        << rhs.alpha(k,j,i) << " "
        << rhs.beta_u(0,k,j,i) << " "

        << mbi.x1(i) //x-grid
        << std::endl;
      }
      outdata // To evaluate Mathematica functions
      << 0. << " "   // t
      << mbi.x2(j) // y
      << std::endl;
      outdata.close();
    }
    outdata // To evaluate Mathematica functions
    << 0. << " "   // t
    << mbi.x1(i_test) // x
    << std::endl;
    outdata.close();
#endif  // ENDDEBUG

  }

  // ===================================================================================
  // Add dissipation for stability
  //
  for(int n = 0; n < N_Z4c; ++n)
  for(int a = 0; a < NDIM; ++a) {
    ILOOP3(k,j,i) {
      u_rhs(n,k,j,i) += FD.Diss(a, u(n,k,j,i));
    }
  }
}

//----------------------------------------------------------------------------------------
// \!fn void Z4c::Z4cBoundaryRHS(AthenaArray<Real> & u, AthenaArray<Real> & u_mat, AthenaArray<Real> & u_rhs)
// \brief compute the boundary RHS given the state vector and matter state
//
// This function operates only on a thin layer of points at the physical
// boundary of the domain.

void Z4c::Z4cBoundaryRHS(AthenaArray<Real> & u, AthenaArray<Real> & u_mat, AthenaArray<Real> & u_rhs) {
  MeshBlock * pmb = pmy_block;
  if(pmb->pbval->block_bcs[BoundaryFace::inner_x1] == BoundaryFlag::extrapolate_outflow ||
     pmb->pbval->block_bcs[BoundaryFace::inner_x1] == BoundaryFlag::outflow) {
    Z4cSommerfeld_(u, u_rhs, mbi.il, mbi.il, mbi.jl, mbi.ju, mbi.kl, mbi.ku);
  }
  if(pmb->pbval->block_bcs[BoundaryFace::outer_x1] == BoundaryFlag::extrapolate_outflow ||
     pmb->pbval->block_bcs[BoundaryFace::outer_x1] == BoundaryFlag::outflow) {
    Z4cSommerfeld_(u, u_rhs, mbi.iu, mbi.iu, mbi.jl, mbi.ju, mbi.kl, mbi.ku);
  }
  if(pmb->pbval->block_bcs[BoundaryFace::inner_x2] == BoundaryFlag::extrapolate_outflow ||
     pmb->pbval->block_bcs[BoundaryFace::inner_x2] == BoundaryFlag::outflow) {
    Z4cSommerfeld_(u, u_rhs, mbi.il, mbi.iu, mbi.jl, mbi.jl, mbi.kl, mbi.ku);
  }
  if(pmb->pbval->block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::extrapolate_outflow ||
     pmb->pbval->block_bcs[BoundaryFace::outer_x2] == BoundaryFlag::outflow) {
    Z4cSommerfeld_(u, u_rhs, mbi.il, mbi.iu, mbi.ju, mbi.ju, mbi.kl, mbi.ku);
  }
  if(pmb->pbval->block_bcs[BoundaryFace::inner_x3] == BoundaryFlag::extrapolate_outflow ||
     pmb->pbval->block_bcs[BoundaryFace::inner_x3] == BoundaryFlag::outflow) {
    Z4cSommerfeld_(u, u_rhs, mbi.il, mbi.iu, mbi.jl, mbi.ju, mbi.kl, mbi.kl);
  }
  if(pmb->pbval->block_bcs[BoundaryFace::outer_x3] == BoundaryFlag::extrapolate_outflow ||
     pmb->pbval->block_bcs[BoundaryFace::outer_x3] == BoundaryFlag::outflow) {
    Z4cSommerfeld_(u, u_rhs, mbi.il, mbi.iu, mbi.jl, mbi.ju, mbi.ku, mbi.ku);
  }
}

//----------------------------------------------------------------------------------------
// \!fn void Z4c::Z4cSommerfeld_(AthenaArray<Real> & u, AthenaArray<Real> & u_rhs,
//      int const is, int const ie, int const js, int const je, int const ks, int const ke);
// \brief apply Sommerfeld BCs to the given set of points
//

void Z4c::Z4cSommerfeld_(AthenaArray<Real> & u, AthenaArray<Real> & u_rhs,
  int const is, int const ie,
  int const js, int const je,
  int const ks, int const ke) {

  Z4c_vars z4c, rhs;
  SetZ4cAliases(u, z4c);
  SetZ4cAliases(u_rhs, rhs);

  for(int k = ks; k <= ke; ++k)
  for(int j = js; j <= je; ++j) {
    // -----------------------------------------------------------------------------------
    // 1st derivatives
    //
    // Scalars
    for(int a = 0; a < NDIM; ++a) {
#pragma omp simd
      for(int i = is; i <= ie; ++i) {
        dKhat_d(a,i) = FD.Ds(a, z4c.Khat(k,j,i));
        dTheta_d(a,i) = FD.Ds(a, z4c.Theta(k,j,i));
      }
    }
    // Vectors
    for(int a = 0; a < NDIM; ++a)
    for(int b = 0; b < NDIM; ++b) {
#pragma omp simd
      for(int i = is; i <= ie; ++i) {
        dGam_du(b,a,i) = FD.Ds(b, z4c.Gam_u(a,k,j,i));
      }
    }
    // Tensors
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b)
    for(int c = 0; c < NDIM; ++c) {
#pragma omp simd
      for(int i = is; i <= ie; ++i) {
        dA_ddd(c,a,b,i) = FD.Ds(c, z4c.A_dd(a,b,k,j,i));
      }
    }

    // -----------------------------------------------------------------------------------
    // Compute pseudo-radial vector
    //
#pragma omp simd
    for(int i = is; i <= ie; ++i) {
      // NOTE: this will need to be changed if the Z4c variables become vertex center
      r(i) = std::sqrt(SQR(mbi.x1(i)) + SQR(mbi.x2(j)) + SQR(mbi.x3(k)));
      s_u(0,i) = mbi.x1(i)/r(i);
      s_u(1,i) = mbi.x2(j)/r(i);
      s_u(2,i) = mbi.x3(k)/r(i);
    }

    // -----------------------------------------------------------------------------------
    // Boundary RHS for scalars
    //
#pragma omp simd
    for(int i = is; i <= ie; ++i) {
      rhs.Theta(k,j,i) = - z4c.Theta(k,j,i)/r(i);
      rhs.Khat(k,j,i) = - std::sqrt(2.) * z4c.Khat(k,j,i)/r(i);
    }
    for(int a = 0; a < NDIM; ++a) {
#pragma omp simd
      for(int i = is; i <= ie; ++i) {
        rhs.Theta(k,j,i) -= s_u(a,i) * dTheta_d(a,i);
        rhs.Khat(k,j,i) -= std::sqrt(2.) * s_u(a,i) * dKhat_d(a,i);
      }
    }

    // -----------------------------------------------------------------------------------
    // Boundary RHS for the Gamma's
    //
    for(int a = 0; a < NDIM; ++a) {
#pragma omp simd
      for(int i = is; i <= ie; ++i) {
        rhs.Gam_u(a,k,j,i) = - z4c.Gam_u(a,k,j,i)/r(i);
      }
      for(int b = 0; b < NDIM; ++b) {
#pragma omp simd
        for(int i = is; i <= ie; ++i) {
          rhs.Gam_u(a,k,j,i) -= s_u(b,i) * dGam_du(b,a,i);
        }
      }
    }

    // -----------------------------------------------------------------------------------
    // Boundary RHS for the A_ab
    //
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
#pragma omp simd
      for(int i = is; i <= ie; ++i) {
        rhs.A_dd(a,b,k,j,i) = - z4c.A_dd(a,b,k,j,i)/r(i);
      }
      for(int c = 0; c < NDIM; ++c) {
#pragma omp simd
        for(int i = is; i <= ie; ++i) {
          rhs.A_dd(a,b,k,j,i) -= s_u(c,i) * dA_ddd(c,a,b,i);
        }
      }
    }
  }
}
