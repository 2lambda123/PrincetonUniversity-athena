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
#include "../thermal_conduction/tc.hpp"
#include "../cr/cr.hpp"


//======================================================================================
/*! \file thermal_conduction.cpp
 *  \brief Test for thermal conduction
 *
 *====================================================================================*/


//======================================================================================
//! \fn void MeshBlock::ProblemGenerator(ParameterInput *pin)
//  \brief beam test
//======================================================================================


static Real fixedkappa=0.01;
static Real fixedkappa1=1.e-8;

static Real sigma=1.e2;
static Real sigma1=1.e8;

void FixedConductivity(MeshBlock *pmb, AthenaArray<Real> &prim, 
	                                    AthenaArray<Real> &bcc);

void Diffusion(MeshBlock *pmb, AthenaArray<Real> &u_cr, 
        AthenaArray<Real> &prim, AthenaArray<Real> &bcc);


void Mesh::UserWorkAfterLoop(ParameterInput *pin)
{ 
    
}



void MeshBlock::InitUserMeshBlockData(ParameterInput *pin)
{

  if(CR_ENABLED){
    pcr->EnrollOpacityFunction(Diffusion);
  }

  if(TC_ENABLED){
    ptc->EnrollOpacityFunction(FixedConductivity);
  }

}



void MeshBlock::ProblemGenerator(ParameterInput *pin)
{
    
  Real gamma = peos->GetGamma();

  Real vx=0.0;
  // Initialize hydro variable
  for(int k=ks; k<=ke; ++k) {
    for (int j=js; j<=je; ++j) {
      for (int i=is; i<=ie; ++i) {

        Real x1 = pcoord->x1v(i);
        Real x2 = pcoord->x2v(j);

        Real radius=sqrt(x1*x1+x2*x2);
        Real tant=x2/x1;

        Real tgas = 1.0;
        Real rho = 1.0;

        if(radius < 0.7 && radius > 0.5 && fabs(tant) < 0.268 && x1 > 0.0)
        	tgas = 2.0;
      
        phydro->u(IDN,k,j,i) = rho;
        phydro->u(IM1,k,j,i) = vx;
        phydro->u(IM2,k,j,i) = 0.0;
        phydro->u(IM3,k,j,i) = 0.0;
        if (NON_BAROTROPIC_EOS){
          phydro->u(IEN,k,j,i) = 0.5*vx*vx+tgas*rho/(gamma-1.0);
        }
        
        if(CR_ENABLED){
          if(radius < 0.7 && radius > 0.5 && fabs(tant) < 0.268 && x1 > 0.0){
            pcr->u_cr(CRE,k,j,i) = 12.0;
          }else{
            pcr->u_cr(CRE,k,j,i) = 10.0;
          }
          pcr->u_cr(CRF1,k,j,i) = 0.0;
          pcr->u_cr(CRF2,k,j,i) = 0.0;
          pcr->u_cr(CRF3,k,j,i) = 0.0;
        }        

        if(TC_ENABLED){
            ptc->u_tc(0,k,j,i) = tgas*rho/(gamma-1.0);
            ptc->u_tc(1,k,j,i) = 0.0;
            ptc->u_tc(2,k,j,i) = 0.0;
            ptc->u_tc(3,k,j,i) = 0.0;
        }
      }// end i
    }
  }

  // Default values are 1/3
  if(CR_ENABLED){
    int nz1 = block_size.nx1 + 2*(NGHOST);
    int nz2 = block_size.nx2;
    if(nz2 > 1) nz2 += 2*(NGHOST);
    int nz3 = block_size.nx3;
    if(nz3 > 1) nz3 += 2*(NGHOST);
    for(int k=0; k<nz3; ++k){
      for(int j=0; j<nz2; ++j){
        for(int i=0; i<nz1; ++i){
          pcr->sigma_diff(0,k,j,i) = sigma;
          pcr->sigma_diff(1,k,j,i) = 1.e8;
          pcr->sigma_diff(2,k,j,i) = 1.e8;
        }
      }
    }// end k,j,i

  }// End CR


  // Default values are 1/3
  if(TC_ENABLED){
    int nz1 = block_size.nx1 + 2*(NGHOST);
    int nz2 = block_size.nx2;
    if(nz2 > 1) nz2 += 2*(NGHOST);
    int nz3 = block_size.nx3;
    if(nz3 > 1) nz3 += 2*(NGHOST);
    for(int k=0; k<nz3; ++k){
      for(int j=0; j<nz2; ++j){
        for(int i=0; i<nz1; ++i){
          ptc->kappa(0,k,j,i) = fixedkappa;
          ptc->kappa(1,k,j,i) = fixedkappa1;
          ptc->kappa(2,k,j,i) = fixedkappa1;
        }
      }
    }// end k,j,i

  }// End CR



    // Add horizontal magnetic field lines, to show streaming and diffusion 
  // along magnetic field ines
  if(MAGNETIC_FIELDS_ENABLED){

    for (int k=ks; k<=ke; ++k) {
      for (int j=js; j<=je; ++j) {
        for (int i=is; i<=ie+1; ++i) {
          Real x1 = pcoord->x1v(i);
          Real x2 = pcoord->x2v(j);

          Real radius=sqrt(x1*x1+x2*x2);
          pfield->b.x1f(k,j,i) = -x2/radius;
        }
      }
    }

    if(block_size.nx2 > 1){

      for (int k=ks; k<=ke; ++k) {
        for (int j=js; j<=je+1; ++j) {
          for (int i=is; i<=ie; ++i) {

            Real x1 = pcoord->x1v(i);
            Real x2 = pcoord->x2v(j);

            Real radius=sqrt(x1*x1+x2*x2);
            pfield->b.x2f(k,j,i) = x1/radius;
          }
        }
      }

    }

    if(block_size.nx3 > 1){

      for (int k=ks; k<=ke+1; ++k) {
        for (int j=js; j<=je; ++j) {
          for (int i=is; i<=ie; ++i) {
            pfield->b.x3f(k,j,i) = 0.0;
          }
        }
      }
    }// end nx3

    // set cell centerd magnetic field
    // Add magnetic energy density to the total energy
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

  }// end MHD
  
  
  return;
}

void FixedConductivity(MeshBlock *pmb, AthenaArray<Real> &prim, 
	                                    AthenaArray<Real> &bcc)
{

  ThermalConduction *ptc=pmb->ptc;
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

        ptc->kappa(0,k,j,i) = fixedkappa;
        ptc->kappa(1,k,j,i) = fixedkappa1;
        ptc->kappa(2,k,j,i) = fixedkappa1;  

      }
    }
  }

  if(MAGNETIC_FIELDS_ENABLED){
    //First, calculate B_dot_grad_Pc
    for(int k=kl; k<=ku; ++k){
      for(int j=jl; j<=ju; ++j){
      // diffusion coefficient is calculated with respect to B direction
      // Use a simple estimate of Grad Pc
        for(int i=il; i<=iu; ++i){
          // Now calculate the angles of B
          Real bxby = sqrt(bcc(IB1,k,j,i)*bcc(IB1,k,j,i) +
                           bcc(IB2,k,j,i)*bcc(IB2,k,j,i));
          Real btot = sqrt(bcc(IB1,k,j,i)*bcc(IB1,k,j,i) +
                           bcc(IB2,k,j,i)*bcc(IB2,k,j,i) +
                           bcc(IB3,k,j,i)*bcc(IB3,k,j,i));

          if(btot > TINY_NUMBER){
            ptc->b_angle(0,k,j,i) = bxby/btot;
            ptc->b_angle(1,k,j,i) = bcc(IB3,k,j,i)/btot;
          }else{
            ptc->b_angle(0,k,j,i) = 1.0;
            ptc->b_angle(1,k,j,i) = 0.0;
          }
          if(bxby > TINY_NUMBER){
            ptc->b_angle(2,k,j,i) = bcc(IB2,k,j,i)/bxby;
            ptc->b_angle(3,k,j,i) = bcc(IB1,k,j,i)/bxby;
          }else{
            ptc->b_angle(2,k,j,i) = 0.0;
            ptc->b_angle(3,k,j,i) = 1.0;
          }


        }//end i        

      }// end j
    }// end k
  }// end MHD


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
          Real dprdx=(u_cr(CRE,k,j,i+1) -  u_cr(CRE,k,j,i-1))/3.0;
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
            Real dprdz=(u_cr(CRE,k+1,j,i) -  u_cr(CRE,k-1,j,i))/3.0;
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
                                   /(sqrt(pb) * va * (4.0/3.0) 
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
           Real grad_pr=(u_cr(CRE,k,j,i+1) -  u_cr(CRE,k,j,i-1))/3.0;
           grad_pr /= distance;

           Real va = 0.0;

           if(va < TINY_NUMBER){
             pcr->sigma_adv(0,k,j,i) = pcr->max_opacity;
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
