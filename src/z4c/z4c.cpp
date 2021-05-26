//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file z4c.cpp
//  \brief implementation of functions in the Z4c class

#include <iostream>
#include <fstream>

// Athena++ headers
#include "z4c.hpp"
#include "z4c_macro.hpp"
#include "../athena.hpp"
#include "../coordinates/coordinates.hpp"
#include "../mesh/mesh.hpp"
#include "../outputs/outputs.hpp"

// constructor, initializes data structures and parameters

char const * const Z4c::Z4c_names[Z4c::N_Z4c] = {
  "z4c.chi",
  "z4c.gxx", "z4c.gxy", "z4c.gxz", "z4c.gyy", "z4c.gyz", "z4c.gzz",
  "z4c.Khat",
  "z4c.Axx", "z4c.Axy", "z4c.Axz", "z4c.Ayy", "z4c.Ayz", "z4c.Azz",
  "z4c.Gamx", "z4c.Gamy", "z4c.Gamz",
  "z4c.Theta",
  "z4c.alpha",
  "z4c.betax", "z4c.betay", "z4c.betaz",
};

char const * const Z4c::ADM_names[Z4c::N_ADM] = {
  "adm.gxx", "adm.gxy", "adm.gxz", "adm.gyy", "adm.gyz", "adm.gzz",
  "adm.Kxx", "adm.Kxy", "adm.Kxz", "adm.Kyy", "adm.Kyz", "adm.Kzz",
  "adm.psi4",
};

char const * const Z4c::Constraint_names[Z4c::N_CON] = {
  "con.C",
  "con.H",
  "con.M",
  "con.Z",
  "con.Mx", "con.My", "con.Mz",
};

char const * const Z4c::Matter_names[Z4c::N_MAT] = {
  "mat.rho",
  "mat.Sx", "mat.Sy", "mat.Sz",
  "mat.Sxx", "mat.Sxy", "mat.Sxz", "mat.Syy", "mat.Syz", "mat.Szz",
};
// WGC wext
#ifdef Z4C_WEXT
char const * const Z4c::Weyl_names[Z4c::N_WEY] = {
  "weyl.rpsi4","weyl.ipsi4",
};
#endif // Z4C_WEXT
//WGC end
Z4c::Z4c(MeshBlock *pmb, ParameterInput *pin) :
  pmy_block(pmb),
#if PREFER_VC
  coarse_u_(N_Z4c, pmb->ncv3, pmb->ncv2, pmb->ncv1,
            (pmb->pmy_mesh->multilevel ? AthenaArray<Real>::DataStatus::allocated :
             AthenaArray<Real>::DataStatus::empty)),
  storage{{N_Z4c, pmb->nverts3, pmb->nverts2, pmb->nverts1}, // u
          {N_Z4c, pmb->nverts3, pmb->nverts2, pmb->nverts1}, // u1
          {},                                                // u2
          {N_Z4c, pmb->nverts3, pmb->nverts2, pmb->nverts1}, // rhs
          {N_ADM, pmb->nverts3, pmb->nverts2, pmb->nverts1}, // adm
          {N_CON, pmb->nverts3, pmb->nverts2, pmb->nverts1}, // con
          {N_MAT, pmb->nverts3, pmb->nverts2, pmb->nverts1}, // mat
//WGC wext
#ifdef Z4C_WEXT
          {N_WEY, pmb->nverts3, pmb->nverts2, pmb->nverts1}, // weyl
#endif // Z4C_WEXT
//WGC end
  },
#else
  coarse_u_(N_Z4c, pmb->ncc3, pmb->ncc2, pmb->ncc1,
            (pmb->pmy_mesh->multilevel ? AthenaArray<Real>::DataStatus::allocated :
             AthenaArray<Real>::DataStatus::empty)),
  storage{{N_Z4c, pmb->ncells3, pmb->ncells2, pmb->ncells1}, // u
          {N_Z4c, pmb->ncells3, pmb->ncells2, pmb->ncells1}, // u1
          {},                                                // u2
          {N_Z4c, pmb->ncells3, pmb->ncells2, pmb->ncells1}, // rhs
          {N_ADM, pmb->ncells3, pmb->ncells2, pmb->ncells1}, // adm
          {N_CON, pmb->ncells3, pmb->ncells2, pmb->ncells1}, // con
          {N_MAT, pmb->ncells3, pmb->ncells2, pmb->ncells1}, // mat
//WGC wext
#ifdef Z4C_WEXT
          {N_WEY, pmb->ncells3, pmb->ncells2, pmb->ncells1}, // weyl
#endif // Z4C_WEXT
//WGC end
  },
#endif
  empty_flux{AthenaArray<Real>(), AthenaArray<Real>(), AthenaArray<Real>()},
  ubvar(pmb, &storage.u, &coarse_u_, empty_flux)
{
  Mesh *pm = pmy_block->pmy_mesh;
  Coordinates * pco = pmb->pcoord;

  // dimensions required for data allocation
  if (PREFER_VC) {
    mbi.nn1 = pmb->nverts1;
    mbi.nn2 = pmb->nverts2;
    mbi.nn3 = pmb->nverts3;
  } else {
    mbi.nn1 = pmb->ncells1;
    mbi.nn2 = pmb->ncells2;
    mbi.nn3 = pmb->ncells3;
  }
  int nn1 = mbi.nn1, nn2 = mbi.nn2, nn3 = mbi.nn3;

  // convenience for per-block iteration (private Wave scope)
  mbi.il = pmb->is; mbi.jl = pmb->js; mbi.kl = pmb->ks;
  if (PREFER_VC) {
    mbi.iu = pmb->ive; mbi.ju = pmb->jve; mbi.ku = pmb->kve;
  } else {
    mbi.iu = pmb->ie; mbi.ju = pmb->je; mbi.ku = pmb->ke;
  }

  // point to appropriate grid
  if (PREFER_VC) {
    mbi.x1.InitWithShallowSlice(pco->x1f, 1, 0, nn1);
    mbi.x2.InitWithShallowSlice(pco->x2f, 1, 0, nn2);
    mbi.x3.InitWithShallowSlice(pco->x3f, 1, 0, nn3);
  } else {
    mbi.x1.InitWithShallowSlice(pco->x1v, 1, 0, nn1);
    mbi.x2.InitWithShallowSlice(pco->x2v, 1, 0, nn2);
    mbi.x3.InitWithShallowSlice(pco->x3v, 1, 0, nn3);
  }
  //---------------------------------------------------------------------------

  // inform MeshBlock that this array is the "primary" representation
  // Used for:
  // (1) load-balancing
  // (2) (future) dumping to restart file
  pmb->RegisterMeshBlockData(storage.u);

  // "Enroll" in SMR/AMR by adding to vector of pointers in MeshRefinement class
  if (pm->multilevel) {
    refinement_idx = pmy_block->pmr->AddToRefinement(&storage.u, &coarse_u_);
  }


  // If user-requested time integrator is type 3S* allocate additional memory
  std::string integrator = pin->GetOrAddString("time", "integrator", "vl2");
  if (integrator == "ssprk5_4")
    storage.u2.NewAthenaArray(N_Z4c, nn3, nn2, nn1);

  // enroll CellCenteredBoundaryVariable / VertexCenteredBoundaryVariable object
  ubvar.bvar_index = pmb->pbval->bvars.size();
  pmb->pbval->bvars.push_back(&ubvar);
  if (PREFER_VC) {
    pmb->pbval->bvars_main_int_vc.push_back(&ubvar);
  } else {
    pmb->pbval->bvars_main_int.push_back(&ubvar);
  }

  dt1_.NewAthenaArray(nn1);
  dt2_.NewAthenaArray(nn1);
  dt3_.NewAthenaArray(nn1);

  // BD: TODO shift defaults to header [C++11 so can use def. declaration]..
  // Parameters
  opt.chi_psi_power = pin->GetOrAddReal("z4c", "chi_psi_power", -4.0);
  opt.chi_div_floor = pin->GetOrAddReal("z4c", "chi_div_floor", -1000.0);
  opt.diss = pin->GetOrAddReal("z4c", "diss", 0.0);
  opt.eps_floor = pin->GetOrAddReal("z4c", "eps_floor", 1e-12);
  opt.damp_kappa1 = pin->GetOrAddReal("z4c", "damp_kappa1", 0.0);
  opt.damp_kappa2 = pin->GetOrAddReal("z4c", "damp_kappa2", 0.0);
  // Gauge conditions (default to moving puncture gauge)
  opt.lapse_harmonicf = pin->GetOrAddReal("z4c", "lapse_harmonicf", 1.0);
  opt.lapse_harmonic = pin->GetOrAddReal("z4c", "lapse_harmonic", 0.0);
  opt.lapse_oplog = pin->GetOrAddReal("z4c", "lapse_oplog", 2.0);
  opt.lapse_advect = pin->GetOrAddReal("z4c", "lapse_advect", 1.0);
  opt.shift_Gamma = pin->GetOrAddReal("z4c", "shift_Gamma", 1.0);
  opt.shift_advect = pin->GetOrAddReal("z4c", "shift_advect", 1.0);

  opt.shift_alpha2Gamma = pin->GetOrAddReal("z4c", "shift_alpha2Gamma", 0.0);
  opt.shift_H = pin->GetOrAddReal("z4c", "shift_H", 0.0);

  opt.shift_eta = pin->GetOrAddReal("z4c", "shift_eta", 2.0);

#if defined(Z4C_ETA_CONF)
  // Spatially dependent shift damping [based on conf. factor]
  opt.shift_eta_a = pin->GetOrAddReal("z4c", "shift_eta_a", 2.);
  opt.shift_eta_b = pin->GetOrAddReal("z4c", "shift_eta_b", 2.);
  opt.shift_eta_R_0 = pin->GetOrAddReal("z4c", "shift_eta_R_0", 1.31);
#elif defined(Z4C_ETA_TRACK_TP)
  // Spatially dependent shift damping [based on puncture dist.]
  opt.shift_eta_w = pin->GetOrAddReal("z4c", "shift_eta_w", 2.67);
  opt.shift_eta_delta = pin->GetOrAddReal("z4c", "shift_eta_delta", 2.0);
  opt.shift_eta_P = pin->GetOrAddReal("z4c", "shift_eta_P", 3);
  opt.shift_eta_TP_ix = pin->GetOrAddInteger("z4c", "shift_eta_TP_ix", 0);
#endif // Z4C_ETA_CONF, Z4C_ETA_TRACK_TP

  // Problem-specific parameters
  // Two punctures parameters

  // AwA parameters (default to linear wave test)

  opt.AwA_amplitude = pin->GetOrAddReal("z4c", "AwA_amplitude", 1e-10);
  opt.AwA_d_x = pin->GetOrAddReal("z4c", "AwA_d_x", 1.0);
  opt.AwA_d_y = pin->GetOrAddReal("z4c", "AwA_d_y", 1.0);
  opt.AwA_Gaussian_w = pin->GetOrAddReal("z4c", "AwA_Gaussian_w", 0.5);
  opt.AwA_polarised_Gowdy_t0 = pin->GetOrAddReal("z4c",
    "AwA_polarised_Gowdy_t0", 9.8753205829098);

  // sphere-zone refinement test [disabled by default]
  opt.sphere_zone_number = pin->GetOrAddInteger("z4c", "sphere_zone_number", 0);
  if (opt.sphere_zone_number > 0) {
    opt.sphere_zone_levels.NewAthenaArray(opt.sphere_zone_number);
    opt.sphere_zone_radii.NewAthenaArray(opt.sphere_zone_number);
    opt.sphere_zone_puncture.NewAthenaArray(opt.sphere_zone_number);
    opt.sphere_zone_center1.NewAthenaArray(opt.sphere_zone_number);
    opt.sphere_zone_center2.NewAthenaArray(opt.sphere_zone_number);
    opt.sphere_zone_center3.NewAthenaArray(opt.sphere_zone_number);

    for (int i=0; i<opt.sphere_zone_number; ++i) {
      opt.sphere_zone_levels(i) = pin->GetOrAddInteger("z4c",
        "sphere_zone_level_" + std::to_string(i), 0);
      opt.sphere_zone_radii(i) = pin->GetOrAddReal("z4c",
        "sphere_zone_radius_" + std::to_string(i), 0.);
      opt.sphere_zone_puncture(i) = pin->GetOrAddInteger("z4c",
        "sphere_zone_puncture_" + std::to_string(i), -1);
      // populate centers
      opt.sphere_zone_center1(i) = pin->GetOrAddReal("z4c",
        "sphere_zone_center1_" + std::to_string(i), 0.);
      opt.sphere_zone_center2(i) = pin->GetOrAddReal("z4c",
        "sphere_zone_center2_" + std::to_string(i), 0.);
      opt.sphere_zone_center3(i) = pin->GetOrAddReal("z4c",
        "sphere_zone_center3_" + std::to_string(i), 0.);
    }
  }
// #ifdef NPUNCT
//   // for puncture refinement
//   if (NPUNCT > 0) {
//     opt.puncture_levels.NewAthenaArray(NPUNCT);
//     opt.puncture_radii.NewAthenaArray(NPUNCT);

//     for (int i=0; i<opt.sphere_zone_number; ++i) {
//       opt.puncture_levels(i) = pin->GetOrAddInteger("z4c",
//         "puncture_level_" + std::to_string(i),
//         pin->GetOrAddInteger("mesh", "numlevel", 3));
//       opt.puncture_radii(i) = pin->GetOrAddReal("z4c",
//         "puncture_radius_" + std::to_string(i), 1.5);
//     }

//   }
// #endif

  //---------------------------------------------------------------------------
  // Set aliases
  SetADMAliases(storage.adm, adm);
  SetConstraintAliases(storage.con, con);
  SetMatterAliases(storage.mat, mat);
  SetZ4cAliases(storage.rhs, rhs);
  SetZ4cAliases(storage.u, z4c);
//WGC wext
#if defined(Z4C_WEXT)
  SetWeylAliases(storage.weyl, weyl);
//WGC end
#endif
  // Allocate memory for aux 1D vars
  r.NewAthenaTensor(nn1);
  detg.NewAthenaTensor(nn1);
  chi_guarded.NewAthenaTensor(nn1);
  oopsi4.NewAthenaTensor(nn1);
  A.NewAthenaTensor(nn1);
  AA.NewAthenaTensor(nn1);
  R.NewAthenaTensor(nn1);
  Ht.NewAthenaTensor(nn1);
  K.NewAthenaTensor(nn1);
  KK.NewAthenaTensor(nn1);
  Ddalpha.NewAthenaTensor(nn1);
  S.NewAthenaTensor(nn1);
  M_u.NewAthenaTensor(nn1);
  Gamma_u.NewAthenaTensor(nn1);
  DA_u.NewAthenaTensor(nn1);
  s_u.NewAthenaTensor(nn1);
  g_uu.NewAthenaTensor(nn1);
  A_uu.NewAthenaTensor(nn1);
  AA_dd.NewAthenaTensor(nn1);
  R_dd.NewAthenaTensor(nn1);
  Rphi_dd.NewAthenaTensor(nn1);
  Kt_dd.NewAthenaTensor(nn1);
  K_ud.NewAthenaTensor(nn1);
  Ddalpha_dd.NewAthenaTensor(nn1);
  Ddphi_dd.NewAthenaTensor(nn1);
  Gamma_ddd.NewAthenaTensor(nn1);
  Gamma_udd.NewAthenaTensor(nn1);
  DK_ddd.NewAthenaTensor(nn1);
  DK_udd.NewAthenaTensor(nn1);

#if defined(Z4C_ETA_CONF) || defined(Z4C_ETA_TRACK_TP)
  eta_damp.NewAthenaTensor(nn1);
#endif // Z4C_ETA_CONF, Z4C_ETA_TRACK_TP


  dbeta.NewAthenaTensor(nn1);
  dalpha_d.NewAthenaTensor(nn1);
  ddbeta_d.NewAthenaTensor(nn1);
  dchi_d.NewAthenaTensor(nn1);
  dphi_d.NewAthenaTensor(nn1);
  dK_d.NewAthenaTensor(nn1);
  dKhat_d.NewAthenaTensor(nn1);
  dTheta_d.NewAthenaTensor(nn1);
  ddalpha_dd.NewAthenaTensor(nn1);
  dbeta_du.NewAthenaTensor(nn1);
  ddchi_dd.NewAthenaTensor(nn1);
  dGam_du.NewAthenaTensor(nn1);
  dg_ddd.NewAthenaTensor(nn1);
  dg_duu.NewAthenaTensor(nn1);
  dK_ddd.NewAthenaTensor(nn1);
  dA_ddd.NewAthenaTensor(nn1);
  ddbeta_ddu.NewAthenaTensor(nn1);
  ddg_dddd.NewAthenaTensor(nn1);

  Lchi.NewAthenaTensor(nn1);
  LKhat.NewAthenaTensor(nn1);
  LTheta.NewAthenaTensor(nn1);
  Lalpha.NewAthenaTensor(nn1);
  LGam_u.NewAthenaTensor(nn1);
  Lbeta_u.NewAthenaTensor(nn1);
  Lg_dd.NewAthenaTensor(nn1);
  LA_dd.NewAthenaTensor(nn1);

//WGC wext
#if defined(Z4C_WEXT)
  uvec.NewAthenaTensor(nn1);
  vvec.NewAthenaTensor(nn1);
  wvec.NewAthenaTensor(nn1);
  dotp1.NewAthenaTensor(nn1);
  dotp2.NewAthenaTensor(nn1);
  Riem3_dddd.NewAthenaTensor(nn1);
  Riemm4_dddd.NewAthenaTensor(nn1);
  Riemm4_ddd.NewAthenaTensor(nn1);
  Riemm4_dd.NewAthenaTensor(nn1);
#endif // Z4C_WEXT
//WGC end

//
  // Set up finite difference operators
  Real dx1, dx2, dx3;
  if (PREFER_VC) {
    dx1 = pco->dx1f(0); dx2 = pco->dx2f(0); dx3 = pco->dx3f(0);
  } else {
    dx1 = pco->dx1v(0); dx2 = pco->dx2v(0); dx3 = pco->dx3v(0);
  }

  FD.stride[0] = 1;
  FD.stride[1] = 0;
  FD.stride[2] = 0;
  FD.idx[0] = 1.0 / dx1;
  FD.idx[1] = 0.0;
  FD.idx[2] = 0.0;
  if(nn2 > 1) {
    FD.stride[1] = nn1;
    FD.idx[1] = 1.0 / dx2;
  }
  if(nn3 > 1) {
    FD.stride[2] = nn2*nn1;
    FD.idx[2] = 1.0 / dx3;
  }
  FD.diss = opt.diss*pow(2, -2*NGHOST)*(NGHOST % 2 == 0 ? -1 : 1);
}

// destructor

Z4c::~Z4c()
{
  storage.u.DeleteAthenaArray();
  storage.u1.DeleteAthenaArray();
  storage.u2.DeleteAthenaArray();
  storage.rhs.DeleteAthenaArray();
  storage.adm.DeleteAthenaArray();
  storage.con.DeleteAthenaArray();
  storage.mat.DeleteAthenaArray();
//WGC wext
#if defined(Z4C_WEXT)
  storage.weyl.DeleteAthenaArray();
#endif
//WGC end
  dt1_.DeleteAthenaArray();
  dt2_.DeleteAthenaArray();
  dt3_.DeleteAthenaArray();

  r.DeleteAthenaTensor();
  detg.DeleteAthenaTensor();
  chi_guarded.DeleteAthenaTensor();
  oopsi4.DeleteAthenaTensor();
  A.DeleteAthenaTensor();
  AA.DeleteAthenaTensor();
  R.DeleteAthenaTensor();
  Ht.DeleteAthenaTensor();
  K.DeleteAthenaTensor();
  KK.DeleteAthenaTensor();
  Ddalpha.DeleteAthenaTensor();
  S.DeleteAthenaTensor();
  M_u.DeleteAthenaTensor();
  Gamma_u.DeleteAthenaTensor();
  DA_u.DeleteAthenaTensor();
  s_u.DeleteAthenaTensor();
  g_uu.DeleteAthenaTensor();
  A_uu.DeleteAthenaTensor();
  AA_dd.DeleteAthenaTensor();
  R_dd.DeleteAthenaTensor();
  Rphi_dd.DeleteAthenaTensor();
  Kt_dd.DeleteAthenaTensor();
  K_ud.DeleteAthenaTensor();
  Ddalpha_dd.DeleteAthenaTensor();
  Ddphi_dd.DeleteAthenaTensor();
  Gamma_ddd.DeleteAthenaTensor();
  Gamma_udd.DeleteAthenaTensor();
  DK_ddd.DeleteAthenaTensor();
  DK_udd.DeleteAthenaTensor();

#if defined(Z4C_ETA_CONF) || defined(Z4C_ETA_TRACK_TP)
  eta_damp.DeleteAthenaTensor();
#endif // Z4C_ETA_CONF, Z4C_ETA_TRACK_TP


  dbeta.DeleteAthenaTensor();
  dalpha_d.DeleteAthenaTensor();
  ddbeta_d.DeleteAthenaTensor();
  dchi_d.DeleteAthenaTensor();
  dphi_d.DeleteAthenaTensor();
  dK_d.DeleteAthenaTensor();
  dKhat_d.DeleteAthenaTensor();
  dTheta_d.DeleteAthenaTensor();
  ddalpha_dd.DeleteAthenaTensor();
  dbeta_du.DeleteAthenaTensor();
  ddchi_dd.DeleteAthenaTensor();
  dGam_du.DeleteAthenaTensor();
  dg_ddd.DeleteAthenaTensor();
  dg_duu.DeleteAthenaTensor();
  dK_ddd.DeleteAthenaTensor();
  dA_ddd.DeleteAthenaTensor();
  ddbeta_ddu.DeleteAthenaTensor();
  ddg_dddd.DeleteAthenaTensor();

  Lchi.DeleteAthenaTensor();
  LKhat.DeleteAthenaTensor();
  LTheta.DeleteAthenaTensor();
  Lalpha.DeleteAthenaTensor();
  LGam_u.DeleteAthenaTensor();
  Lbeta_u.DeleteAthenaTensor();
  Lg_dd.DeleteAthenaTensor();
  LA_dd.DeleteAthenaTensor();
//WGC wext
#if defined(Z4C_WEXT)
  uvec.DeleteAthenaTensor();
  vvec.DeleteAthenaTensor();
  wvec.DeleteAthenaTensor();
  dotp1.DeleteAthenaTensor();
  dotp2.DeleteAthenaTensor();
  Riem3_dddd.DeleteAthenaTensor();
  Riemm4_dddd.DeleteAthenaTensor();
  Riemm4_ddd.DeleteAthenaTensor();
  Riemm4_dd.DeleteAthenaTensor();
#endif
//WGC end

  if (opt.sphere_zone_number > 0) {
    opt.sphere_zone_levels.DeleteAthenaArray();
    opt.sphere_zone_radii.DeleteAthenaArray();
    opt.sphere_zone_puncture.DeleteAthenaArray();
    opt.sphere_zone_center1.DeleteAthenaArray();
    opt.sphere_zone_center2.DeleteAthenaArray();
    opt.sphere_zone_center3.DeleteAthenaArray();
  }


}

//----------------------------------------------------------------------------------------
// \!fn void Z4c::SetADMAliases(AthenaArray<Real> & u, ADM_vars & z4c)
// \brief Set ADM aliases

void Z4c::SetADMAliases(AthenaArray<Real> & u_adm, Z4c::ADM_vars & adm)
{
  adm.psi4.InitWithShallowSlice(u_adm, I_ADM_psi4);
  adm.g_dd.InitWithShallowSlice(u_adm, I_ADM_gxx);
  adm.K_dd.InitWithShallowSlice(u_adm, I_ADM_Kxx);
}

//----------------------------------------------------------------------------------------
// \!fn void Z4c::SetConstraintAliases(AthenaArray<Real> & u, ADM_vars & z4c)
// \brief Set ADM aliases

void Z4c::SetConstraintAliases(AthenaArray<Real> & u_con, Z4c::Constraint_vars & con)
{
  con.C.InitWithShallowSlice(u_con, I_CON_C);
  con.H.InitWithShallowSlice(u_con, I_CON_H);
  con.M.InitWithShallowSlice(u_con, I_CON_M);
  con.Z.InitWithShallowSlice(u_con, I_CON_Z);
  con.M_d.InitWithShallowSlice(u_con, I_CON_Mx);
}

//----------------------------------------------------------------------------------------
// \!fn void Z4c::SetMatterAliases(AthenaArray<Real> & u_mat, Matter_vars & mat)
// \brief Set matter aliases

void Z4c::SetMatterAliases(AthenaArray<Real> & u_mat, Z4c::Matter_vars & mat)
{
  mat.rho.InitWithShallowSlice(u_mat, I_MAT_rho);
  mat.S_d.InitWithShallowSlice(u_mat, I_MAT_Sx);
  mat.S_dd.InitWithShallowSlice(u_mat, I_MAT_Sxx);
}

//----------------------------------------------------------------------------------------
// \!fn void Z4c::SetZ4cAliases(AthenaArray<Real> & u, Z4c_vars & z4c)
// \brief Set Z4c aliases

void Z4c::SetZ4cAliases(AthenaArray<Real> & u, Z4c::Z4c_vars & z4c)
{
  z4c.chi.InitWithShallowSlice(u, I_Z4c_chi);
  z4c.Khat.InitWithShallowSlice(u, I_Z4c_Khat);
  z4c.Theta.InitWithShallowSlice(u, I_Z4c_Theta);
  z4c.alpha.InitWithShallowSlice(u, I_Z4c_alpha);
  z4c.Gam_u.InitWithShallowSlice(u, I_Z4c_Gamx);
  z4c.beta_u.InitWithShallowSlice(u, I_Z4c_betax);
  z4c.g_dd.InitWithShallowSlice(u, I_Z4c_gxx);
  z4c.A_dd.InitWithShallowSlice(u, I_Z4c_Axx);
}
//WGC wext
//----------------------------------------------------------------------------------------
// \!fn void Z4c::SetWeylAliases(AthenaArray<Real> & u, Weyl_vars & weyl)
// \brief Set Weyl aliases
#ifdef Z4C_WEXT
void Z4c::SetWeylAliases(AthenaArray<Real> & u, Z4c::Weyl_vars & weyl)
{
  weyl.rpsi4.InitWithShallowSlice(u, I_WEY_rpsi4);
  weyl.ipsi4.InitWithShallowSlice(u, I_WEY_ipsi4);
}
#endif // Z4C_WEXT
//WGC end

//----------------------------------------------------------------------------------------
// \!fn Real Z4c::SpatialDet(Real gxx, ... , Real gzz)
// \brief returns determinant of 3-metric

Real Z4c::SpatialDet(Real const gxx, Real const gxy, Real const gxz,
                     Real const gyy, Real const gyz, Real const gzz)
{
  return - SQR(gxz)*gyy + 2*gxy*gxz*gyz - gxx*SQR(gyz) - SQR(gxy)*gzz + gxx*gyy*gzz;
}

//----------------------------------------------------------------------------------------
// \!fn void Z4c::SpatialInv(Real const detginv,
//           Real const gxx, Real const gxy, Real const gxz,
//           Real const gyy, Real const gyz, Real const gzz,
//           Real * uxx, Real * uxy, Real * uxz,
//           Real * uyy, Real * uyz, Real * uzz)
// \brief returns inverse of 3-metric

void Z4c::SpatialInv(Real const detginv,
                     Real const gxx, Real const gxy, Real const gxz,
                     Real const gyy, Real const gyz, Real const gzz,
                     Real * uxx, Real * uxy, Real * uxz,
                     Real * uyy, Real * uyz, Real * uzz)
{
  *uxx = (-SQR(gyz) + gyy*gzz)*detginv;
  *uxy = (gxz*gyz  - gxy*gzz)*detginv;
  *uyy = (-SQR(gxz) + gxx*gzz)*detginv;
  *uxz = (-gxz*gyy + gxy*gyz)*detginv;
  *uyz = (gxy*gxz  - gxx*gyz)*detginv;
  *uzz = (-SQR(gxy) + gxx*gyy)*detginv;
  return;
}

//----------------------------------------------------------------------------------------
// \!fn Real Z4c::Trace(Real detginv, Real gxx, ... , Real gzz, Real Axx, ..., Real Azz)
// \brief returns Trace of extrinsic curvature

Real Z4c::Trace(Real const detginv,
                Real const gxx, Real const gxy, Real const gxz,
                Real const gyy, Real const gyz, Real const gzz,
                Real const Axx, Real const Axy, Real const Axz,
                Real const Ayy, Real const Ayz, Real const Azz)
{
  return (detginv*(
       - 2.*Ayz*gxx*gyz + Axx*gyy*gzz +  gxx*(Azz*gyy + Ayy*gzz)
       + 2.*(gxz*(Ayz*gxy - Axz*gyy + Axy*gyz) + gxy*(Axz*gyz - Axy*gzz))
       - Azz*SQR(gxy) - Ayy*SQR(gxz) - Axx*SQR(gyz)
       ));
}

//----------------------------------------------------------------------------------------
// \!fn void Z4c::AlgConstr(AthenaArray<Real> & u)
// \brief algebraic constraints projection
//
// This function operates on all grid points of the MeshBlock

void Z4c::AlgConstr(AthenaArray<Real> & u)
{
  Z4c_vars z4c;
  SetZ4cAliases(u, z4c);

  GLOOP2(k,j) {

    // compute determinant and "conformal conformal factor"
    GLOOP1(i) {
      detg(i) = SpatialDet(z4c.g_dd,k,j,i);
      detg(i) = detg(i) > 0. ? detg(i) : 1.;
      Real eps = detg(i) - 1.;
      oopsi4(i) = (eps < opt.eps_floor) ? (1. - opt.eps_floor/3.) : (pow(1./detg(i), 1./3.));
    }
    // enforce unitary determinant for conformal metric
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      GLOOP1(i) {
        z4c.g_dd(a,b,k,j,i) *= oopsi4(i);
      }
    }

    // compute trace of A
    GLOOP1(i) {
      // note: here we are assuming that det g = 1, which we enforced above
      A(i) = Trace(1.0,
          z4c.g_dd(0,0,k,j,i), z4c.g_dd(0,1,k,j,i), z4c.g_dd(0,2,k,j,i),
          z4c.g_dd(1,1,k,j,i), z4c.g_dd(1,2,k,j,i), z4c.g_dd(2,2,k,j,i),
          z4c.A_dd(0,0,k,j,i), z4c.A_dd(0,1,k,j,i), z4c.A_dd(0,2,k,j,i),
          z4c.A_dd(1,1,k,j,i), z4c.A_dd(1,2,k,j,i), z4c.A_dd(2,2,k,j,i));
    }
    // enforce trace of A to be zero
    for(int a = 0; a < NDIM; ++a)
    for(int b = a; b < NDIM; ++b) {
      GLOOP1(i) {
        z4c.A_dd(a,b,k,j,i) -= (1.0/3.0) * A(i) * z4c.g_dd(a,b,k,j,i);
      }
    }
  }
}
