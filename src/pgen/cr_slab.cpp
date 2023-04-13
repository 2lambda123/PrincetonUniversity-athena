//======================================================================================
/* Athena++ astrophysical MHD code
 * Copyright (C) 2014 James M. Stone  <jmstone@princeton.edu>
 *
 * This program is free software: you can redistribute and/or modify it under the terms
 * of the GNU General Public License (GPL) as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of GNU GPL in the file LICENSE included in the code
 * distribution.  If not see <http://www.gnu.org/licenses/>.
 *====================================================================================*/

// C++ headers
#include <iostream>   // endl
#include <fstream>
#include <sstream>    // stringstream
#include <stdexcept>  // runtime_error
#include <string>     // c_str()
#include <cmath>      // sqrt
#include <algorithm>  // min

// Athena++ headers
#include "../globals.hpp"
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../mesh/mesh.hpp"
#include "../parameter_input.hpp"
#include "../hydro/hydro.hpp"
#include "../eos/eos.hpp"
#include "../bvals/bvals.hpp"
#include "../hydro/srcterms/hydro_srcterms.hpp"
#include "../field/field.hpp"
#include "../coordinates/coordinates.hpp"
#include "../cr/cr.hpp"
#include "../cr/integrators/cr_integrators.hpp"


//======================================================================================
/*! \file beam.cpp
 *  \brief Beam test for the radiative transfer module
 *
 *====================================================================================*/


//======================================================================================
//! \fn void MeshBlock::ProblemGenerator(ParameterInput *pin)
//  \brief beam test
//======================================================================================

static Real sigma = 1.0e8;
static Real rho_scale, l_scale, temp_scale;
static Real v_max;
static Real rho_hot = 8.936e-3;
static Real rho_c = 1.0;


void Diffusion(MeshBlock *pmb, AthenaArray<Real> &u_cr, 
        AthenaArray<Real> &prim, AthenaArray<Real> &bcc);
void FixMHDLeft(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
     FaceField &b, Real time, Real dt, int is, int ie, int js, int je, 
     int ks, int ke, int ngh);
void FixCRsourceLeft(MeshBlock *pmb, Coordinates *pco, CosmicRay *pcr, 
    const AthenaArray<Real> &w, const AthenaArray<Real> &bcc,
	  AthenaArray<Real> &u_cr, Real time, Real dt, int is, int ie, 
	  int js, int je, int ks, int ke, int ngh);

//void Reflect_ix1(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
//		 FaceField &b, AthenaArray<Real> &u_cr, AthenaArray<Real> &u_ntc,
//     Real time, Real dt, int is, int ie, int js, int je, int ks, int ke);

Real CoolingFunction(const Real temp);

void CoolingTerm(MeshBlock *pmb, const Real time, const Real dt, const AthenaArray<Real> &prim,
		 const AthenaArray<Real> &bcc, AthenaArray<Real> &cons, AthenaArray<Real> &u_cr);

Real EnergyInjection(const Real tnow);

void Mesh::UserWorkAfterLoop(ParameterInput *pin)
{ 
    
}

void Mesh::InitUserMeshData(ParameterInput *pin){

  rho_scale = pin->GetReal("problem","rho_scale");
  l_scale = pin->GetReal("problem","l_scale");
  temp_scale = pin->GetReal("problem","temp_scale");
  v_max = pin->GetReal("cr", "vmax");

  EnrollUserBoundaryFunction(inner_x1, FixMHDLeft);
  if(CR_ENABLED)
    EnrollUserCRBoundaryFunction(inner_x1, FixCRsourceLeft);
}

void MeshBlock::InitUserMeshBlockData(ParameterInput *pin)
{

  if(CR_ENABLED){
    pcr->EnrollOpacityFunction(Diffusion);

  }

}

void MeshBlock::ProblemGenerator(ParameterInput *pin)
{
  Real gamma = peos->GetGamma();
  Real pressure = 1.0;
  Real rho_hot = 8.936e-3;
  Real rho_c = 1.0;

  for (int k=ks; k<=ke; ++k){
    for (int j=js; j<=je; ++j){
      for (int i=is-NGHOST; i<=ie; ++i){

      	Real x1 = pcoord->x1v(i);
      	Real x2 = pcoord->x2v(j); 
      	Real dist = sqrt((x1-20.0)*(x1-20.0));

        phydro->u(IDN,k,j,i) = rho_hot + (rho_c-rho_hot)*(0.5-0.5*tanh((dist - 1.0)/5.0e-3));
      	//phydro->u(IDN,k,j,i) = rho_hot;
      	phydro->u(IM1,k,j,i) = 0.0;
      	phydro->u(IM2,k,j,i) = 0.0;
      	phydro->u(IM3,k,j,i) = 0.0;
      	if (NON_BAROTROPIC_EOS){
         
      	  phydro->u(IEN,k,j,i) = pressure/(gamma-1.0) + 0.5*0.0*0.0;
      	}

      	if (CR_ENABLED){
      	  pcr->u_cr(CRE,k,j,i) = 1.0e-6;
      	  pcr->u_cr(CRF1,k,j,i) = 0.0;
      	  pcr->u_cr(CRF2,k,j,i) = 0.0;
      	  pcr->u_cr(CRF3,k,j,i) = 0.0;
      	}

      }// i
    }// j
  }// k
  
  if(CR_ENABLED){
    int nz1 = block_size.nx1 + 2*(NGHOST);
    int nz2 = block_size.nx2;
    if (nz2 > 1){
      nz2 += 2*(NGHOST);
    }
    int nz3 = block_size.nx3;
    if (nz3 >1){
      nz3 += 2*(NGHOST);
    }
    for(int k=0; k<nz3; ++k){
      for(int j=0; j<nz2; ++j){
	for(int i=0; i<nz1; ++i){
	  pcr->sigma_diff(0,k,j,i) = sigma;
	  pcr->sigma_diff(1,k,j,i) = sigma;
	  pcr->sigma_diff(2,k,j,i) = sigma;
	}
      }
    }
  }

  if(MAGNETIC_FIELDS_ENABLED){
    
    for (int k=ks; k<=ke; ++k){
      for (int j=js; j<=je; ++j){
	for (int i=is; i<=ie+1; ++i){
	  pfield->b.x1f(k,j,i) = 1.7682;
	}
      }
    }
    
    if (block_size.nx2 > 1){
      for (int k=ks; k<=ke; ++k){
	for (int j=js; j<=je+1; ++j){
	  for (int i=is; i<=ie; ++i){
	    pfield->b.x2f(k,j,i) = 0.0;
	  }
	}
      }      
    }
    
    if (block_size.nx3 > 1){
      for (int k=ks; k<=ke+1; ++k){
	for (int j=js; j<=je; ++j){
	  for (int i=is; i<=ie; ++i){
	    pfield->b.x3f(k,j,i) = 0.0;
	  }
	}
      }      
    }

    pfield->CalculateCellCenteredField(pfield->b,pfield->bcc,pcoord,is,ie,js,je,ks,ke);

    for(int k=ks; k<=ke; ++k){
      for(int j=js; j<=je; ++j){
        for(int i=is; i<=ie; ++i){
          phydro->u(IEN,k,j,i) +=
            0.5*(SQR((pfield->bcc(IB1,k,j,i)))
               + SQR((pfield->bcc(IB2,k,j,i)))
               + SQR((pfield->bcc(IB3,k,j,i))));
      
        }
      }
    }
    

  }//end magnetic field

  return;
}

void Diffusion(MeshBlock *pmb, AthenaArray<Real> &u_cr, 
        AthenaArray<Real> &prim, AthenaArray<Real> &bcc)
{ 


  // set the default opacity to be a large value in the default hydro case
  CosmicRay *pcr=pmb->pcr;
  int kl=pmb->ks, ku=pmb->ke;
  int jl=pmb->js, ju=pmb->je;
  int il=pmb->is-1, iu=pmb->ie+1;
  
  if(pmb->block_size.nx2 > 1){
    jl -= 1;
    ju += 1;
  }
  if(pmb->block_size.nx3 > 1){
    kl -= 1;
    ku += 1;
  }
  

  for(int k=kl; k<=ku; ++k){
    for(int j=jl; j<=ju; ++j){
#pragma omp simd
      for(int i=il; i<=iu; ++i){

        pcr->sigma_diff(0,k,j,i) = sigma;
        pcr->sigma_diff(1,k,j,i) = sigma;
        pcr->sigma_diff(2,k,j,i) = sigma;  

      }
    }
  }

  Real invlim=1.0/pcr->vmax;

  // The information stored in the array
  // b_angle is
  // b_angle[0]=sin_theta_b
  // b_angle[1]=cos_theta_b
  // b_angle[2]=sin_phi_b
  // b_angle[3]=cos_phi_b




  if(MAGNETIC_FIELDS_ENABLED){
    //First, calculate B_dot_grad_Pc
    for(int k=kl; k<=ku; ++k){
      for(int j=jl; j<=ju; ++j){
    // x component
        pmb->pcoord->CenterWidth1(k,j,il-1,iu+1,pcr->cwidth);
        for(int i=il; i<=iu; ++i){
          Real distance = 0.5*(pcr->cwidth(i-1) + pcr->cwidth(i+1))
                         + pcr->cwidth(i);
          Real dprdx=(u_cr(CRE,k,j,i+1) - u_cr(CRE,k,j,i-1))/3.0;
          dprdx /= distance;
          pcr->b_grad_pc(k,j,i) = bcc(IB1,k,j,i) * dprdx;
        }
    //y component
        if(pmb->block_size.nx2 > 1){
          pmb->pcoord->CenterWidth2(k,j-1,il,iu,pcr->cwidth1);       
          pmb->pcoord->CenterWidth2(k,j,il,iu,pcr->cwidth);
          pmb->pcoord->CenterWidth2(k,j+1,il,iu,pcr->cwidth2);

          for(int i=il; i<=iu; ++i){
            Real distance = 0.5*(pcr->cwidth1(i) + pcr->cwidth2(i))
                           + pcr->cwidth(i);
            Real dprdy=(u_cr(CRE,k,j+1,i) - u_cr(CRE,k,j-1,i))/3.0;
            dprdy /= distance;
            pcr->b_grad_pc(k,j,i) += bcc(IB2,k,j,i) * dprdy;

          }
        }
    // z component
        if(pmb->block_size.nx3 > 1){
          pmb->pcoord->CenterWidth3(k-1,j,il,iu,pcr->cwidth1);       
          pmb->pcoord->CenterWidth3(k,j,il,iu,pcr->cwidth);
          pmb->pcoord->CenterWidth3(k+1,j,il,iu,pcr->cwidth2);

          for(int i=il; i<=iu; ++i){
            Real distance = 0.5*(pcr->cwidth1(i) + pcr->cwidth2(i))
                            + pcr->cwidth(i);
            Real dprdz=(u_cr(CRE,k+1,j,i) - u_cr(CRE,k-1,j,i))/3.0;
            dprdz /= distance;
            pcr->b_grad_pc(k,j,i) += bcc(IB3,k,j,i) * dprdz;

            // now only get the sign
  //          if(pcr->b_grad_pc(k,j,i) > TINY_NUMBER) pcr->b_grad_pc(k,j,i) = 1.0;
  //          else if(-pcr->b_grad_pc(k,j,i) > TINY_NUMBER) pcr->b_grad_pc(k,j,i) 
  //            = -1.0;
  //          else pcr->b_grad_pc(k,j,i) = 0.0;
          }
        }

      // now calculate the streaming velocity
      // streaming velocity is calculated with respect to the current coordinate 
      //  system
      // diffusion coefficient is calculated with respect to B direction
        for(int i=il; i<=iu; ++i){
          Real pb= bcc(IB1,k,j,i)*bcc(IB1,k,j,i)
                  +bcc(IB2,k,j,i)*bcc(IB2,k,j,i)
                  +bcc(IB3,k,j,i)*bcc(IB3,k,j,i);
          Real inv_sqrt_rho = 1.0/sqrt(prim(IDN,k,j,i));
          Real va1 = bcc(IB1,k,j,i)*inv_sqrt_rho;
          Real va2 = bcc(IB2,k,j,i)*inv_sqrt_rho;
          Real va3 = bcc(IB3,k,j,i)*inv_sqrt_rho;

          Real va = sqrt(pb/prim(IDN,k,j,i));

          Real dpc_sign = 0.0;
          if(pcr->b_grad_pc(k,j,i) > TINY_NUMBER) dpc_sign = 1.0;
          else if(-pcr->b_grad_pc(k,j,i) > TINY_NUMBER) dpc_sign = -1.0;
          
          pcr->v_adv(0,k,j,i) = -va1 * dpc_sign;
          pcr->v_adv(1,k,j,i) = -va2 * dpc_sign;
          pcr->v_adv(2,k,j,i) = -va3 * dpc_sign;

          // now the diffusion coefficient

          if(va < TINY_NUMBER){
            pcr->sigma_adv(0,k,j,i) = pcr->max_opacity;
          }else{
            pcr->sigma_adv(0,k,j,i) = fabs(pcr->b_grad_pc(k,j,i))
                                   /(sqrt(pb)*va * (4.0/3.0) 
                                    * invlim * u_cr(CRE,k,j,i)); 
          }

          pcr->sigma_adv(1,k,j,i) = pcr->max_opacity;
          pcr->sigma_adv(2,k,j,i) = pcr->max_opacity;  

          // Now calculate the angles of B
          Real bxby = sqrt(bcc(IB1,k,j,i)*bcc(IB1,k,j,i) +
                           bcc(IB2,k,j,i)*bcc(IB2,k,j,i));
          Real btot = sqrt(pb);
          if(btot > TINY_NUMBER){
            pcr->b_angle(0,k,j,i) = bxby/btot;
            pcr->b_angle(1,k,j,i) = bcc(IB3,k,j,i)/btot;
          }else{
            pcr->b_angle(0,k,j,i) = 1.0;
            pcr->b_angle(1,k,j,i) = 0.0;
          }
          if(bxby > TINY_NUMBER){
            pcr->b_angle(2,k,j,i) = bcc(IB2,k,j,i)/bxby;
            pcr->b_angle(3,k,j,i) = bcc(IB1,k,j,i)/bxby;
          }else{
            pcr->b_angle(2,k,j,i) = 0.0;
            pcr->b_angle(3,k,j,i) = 1.0;            
          }

        }//        

      }// end j
    }// end k

  }// End MHD  
  else{



  for(int k=kl; k<=ku; ++k){
    for(int j=jl; j<=ju; ++j){
  // x component
      pmb->pcoord->CenterWidth1(k,j,il-1,iu+1,pcr->cwidth);
      for(int i=il; i<=iu; ++i){
         Real distance = 0.5*(pcr->cwidth(i-1) + pcr->cwidth(i+1))
                        + pcr->cwidth(i);
         Real grad_pr=( u_cr(CRE,k,j,i+1) -  u_cr(CRE,k,j,i-1))/3.0;
         grad_pr /= distance;

         Real va = 1.0;

         if(va < TINY_NUMBER){
           pcr->sigma_adv(0,k,j,i) = sigma;
           pcr->v_adv(0,k,j,i) = 0.0;
         }else{
           Real sigma2 = fabs(grad_pr)/(va * (4.0/3.0) 
                             * invlim * u_cr(CRE,k,j,i)); 
           if(fabs(grad_pr) < TINY_NUMBER){
             pcr->sigma_adv(0,k,j,i) = 0.0;
             pcr->v_adv(0,k,j,i) = 0.0;
           }else{
             pcr->sigma_adv(0,k,j,i) = sigma2;
             pcr->v_adv(0,k,j,i) = -va * grad_pr/fabs(grad_pr);     
           }
        }

        pcr->sigma_adv(1,k,j,i) = pcr->max_opacity;
        pcr->sigma_adv(2,k,j,i) = pcr->max_opacity;
       
        pcr->v_adv(1,k,j,i) = 0.0;
        pcr->v_adv(2,k,j,i) = 0.0;




      }

    }
  }

  }
}

/*

void Reflect_ix1(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
		 FaceField &b, AthenaArray<Real> &u_cr, AthenaArray<Real> &u_ntc,
		 Real time, Real dt, int is, int ie, int js, int je, int ks, int ke){

  for (int n=0; n<(NHYDRO); ++n){
    if (n==(IVX)){
      for (int k=ks; k<=ke; ++k){
	for (int j=js; j<=je; ++j){
	  //#pragma simd
	  for (int i=1; i<=(NGHOST); ++i){
	    prim(IVX,k,j,is-i) = -prim(IVX,k,j,(is+i-1));
	  }
	}
      }  
   }else {
      for (int k=ks; k<=ke; ++k){
	for (int j=js; j<=je; ++j){
	  //#pragma simd
	  for (int i=1; i<=(NGHOST); ++i){
	    prim(n,k,j,is-i) = prim(n,k,j,(is+i-1));
	  }
	}
      }
    }
  }


  if(MAGNETIC_FIELDS_ENABLED){
    for (int k=ks; k<=ke; ++k){
      for (int j=js; j<=je; ++j){
	//#pragma simd
	for (int i=1; i<=(NGHOST); ++i){
	  int ID = Globals::my_rank;
	  
	  b.x1f(k,j,(is-i)) = b.x1f(k,j,(is+i));
	  //if (i==1 && j==je/2 && ID==0){printf("bval, bcc1 = %g\n", b.x1f(k,j,is));}
	}
      }
    }
    for (int k=ks; k<=ke; ++k){
      for (int j=js; j<=je+1; ++j){
	//#pragma simd
	for (int i=1; i<=(NGHOST); ++i){
	  b.x2f(k,j,(is-i)) = b.x2f(k,j,(is+i-1));
	}
      }
    }
    for (int k=ks; k<=ke+1; ++k){
      for (int j=js; j<=je; ++j){
	//#pragma simd
	for (int i=1; i<=(NGHOST); ++i){
	  b.x3f(k,j,(is-i)) = b.x3f(k,j,(is+i-1));
	}
      }
    }
  }

  if(CR_ENABLED){
    for (int n=0; n<(NCR); ++n){
      if (n==(CRF1)){
	for (int k=ks; k<=ke; ++k){
	  for (int j=js; j<=je; ++j){
	    //#pragma simd
	    for (int i=1; i<=(NGHOST); ++i){
	      u_cr(CRF1,k,j,is-i) = -u_cr(CRF1,k,j,(is+i-1));//0.0;
	    }
	  }
	}
      }else{
	for (int k=ks; k<=ke; ++k){
	  for (int j=js; j<=je; ++j){
	    //#pragma simd
	    for (int i=1; i<=(NGHOST); ++i){
	      u_cr(n,k,j,is-i) = u_cr(n,k,j,(is+i-1));
	    }
	  }
	}

      }
      
    }
    
  }


}

*/

Real EnergyInjection(const Real tnow){
  Real q0 = 5.0e-25;
  Real tramp = 3.0;
  Real tend = 30.0;
  Real t = tnow*4.1856;
  Real qt = q0*(1.0-exp(-t/tramp))*(1.0-exp((t-tend)/tramp));
  if (t >= 30.0){
    qt = 0.0;
  }
  //return in c.g.s
  return qt;

}

Real CoolingFunction(const Real tgas){

  Real x_tgas = log10(tgas/1.0e5);
  Real Theta = 0.4*x_tgas - 3.0 + 5.2/(exp(x_tgas+0.08)+exp(-1.5*(x_tgas+0.08)));
  Real lambda = 1.1e-21*pow(10, Theta);
  //input c.g.s, return c.g.s
  return lambda;
  

}


void MeshBlock::UserWorkInLoop(void)
{


}

void FixCRsourceLeft(MeshBlock *pmb, Coordinates *pco, CosmicRay *pcr, 
    const AthenaArray<Real> &w, const AthenaArray<Real> &bcc,
	  AthenaArray<Real> &u_cr, Real time, Real dt, int is, int ie, 
	  int js, int je, int ks, int ke, int ngh)
{

  Real mH = 1.6735e-24;

  Real v0 = sqrt(1.38e-16*temp_scale/(0.6*mH));
  Real t0 = l_scale/v0;
  Real lambda_scale = rho_scale*v0*v0/t0;


  if(CR_ENABLED){

    for (int k=ks; k<=ke; ++k) {
      for (int j=js; j<=je; ++j) {
#pragma simd
        for (int i=1; i<=ngh; ++i) {

	      Real dens = w(IDN,k,j,is-i);
	      Real temp = w(IEN,k,j,is-i)/w(IDN,k,j,is-i);
	      Real temp_cgs = temp*temp_scale;
	      Real nH = dens*rho_scale/mH;

	      Real cooling_lambda = nH*nH*CoolingFunction(temp_cgs);
	      cooling_lambda = cooling_lambda/lambda_scale;

	      Real heating_gamma = nH*1.0e-25/lambda_scale;
          u_cr(CRE,k,j,is-i) += dt*EnergyInjection(time)/lambda_scale;
          u_cr(CRF1,k,j,is-i) = u_cr(CRF1,k,j,is);
          u_cr(CRF2,k,j,is-i) = u_cr(CRF2,k,j,is);
          u_cr(CRF3,k,j,is-i) = u_cr(CRF3,k,j,is);

        }
      }
    }


  }
}



void FixMHDLeft(MeshBlock *pmb, Coordinates *pco, AthenaArray<Real> &prim,
     FaceField &b, Real time, Real dt, int is, int ie, int js, int je, 
     int ks, int ke, int ngh)
{

  // copy hydro variables into ghost zones, reflecting v1

  for (int k=ks; k<=ke; ++k) {
    for (int j=js; j<=je; ++j) {
      for (int i=1; i<=ngh; ++i) {

        prim(IDN,k,j,is-i) = prim(IDN,k,j,is);
        prim(IVX,k,j,is-i) = prim(IVX,k,j,is); // reflect 1-velocity
        prim(IVY,k,j,is-i) = prim(IVY,k,j,is);
        prim(IVZ,k,j,is-i) = prim(IVZ,k,j,is);
        if(NON_BAROTROPIC_EOS)
          prim(IEN,k,j,is-i) = 10.0;
//         prim(IEN,k,j,is-i) = prim(IEN,k,j,is+i-1);
      }
    }
  }

  

  // copy face-centered magnetic fields into ghost zones, reflecting b1
  if (MAGNETIC_FIELDS_ENABLED) {
    for (int k=ks; k<=ke; ++k) { 
    for (int j=js; j<=je; ++j) { 
#pragma simd
      for (int i=1; i<=(NGHOST); ++i) { 
//        b.x1f(k,j,(is-i)) = sqrt(2.0*const_pb);  // reflect 1-field
          b.x1f(k,j,(is-i)) =  b.x1f(k,j,is);
      } 
    }}
    if(je > js){ 
     for (int k=ks; k<=ke; ++k) {
     for (int j=js; j<=je+1; ++j) {
#pragma simd
      for (int i=1; i<=(NGHOST); ++i) {
        b.x2f(k,j,(is-i)) =  b.x2f(k,j,is);
      }
     }}  
    }
    if(ke > ks){        
     for (int k=ks; k<=ke+1; ++k) {
      for (int j=js; j<=je; ++j) {
#pragma simd
       for (int i=1; i<=(NGHOST); ++i) {
         b.x3f(k,j,(is-i)) =  b.x3f(k,j,is);
       }
      }}
    }
  }


}

