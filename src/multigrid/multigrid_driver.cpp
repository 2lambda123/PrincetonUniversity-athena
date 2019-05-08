//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file multigrid_driver.cpp
//  \brief implementation of functions in class MultigridDriver

// C headers

// C++ headers
#include <algorithm>
#include <cmath>
#include <iostream>   // endl
#include <sstream>    // sstream
#include <stdexcept>  // runtime_error
#include <string>     // c_str()

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../bvals/cc/mg/bvals_mg.hpp"
#include "../coordinates/coordinates.hpp"
#include "../mesh/mesh.hpp"
#include "../parameter_input.hpp"
#include "multigrid.hpp"

#ifdef MPI_PARALLEL
#include <mpi.h>
#endif

// constructor, initializes data structures and parameters

MultigridDriver::MultigridDriver(Mesh *pm, MGBoundaryFunc *MGBoundary, int invar) :
    nvar_(invar), mode_(), // 0: FMG+V(1,1), 1: FMG+F(0,1), 2: V(1,1)
    nrbx1_(pm->nrbx1), nrbx2_(pm->nrbx2), nrbx3_(pm->nrbx3), pmy_mesh_(pm),
    fperiodic_(false), eps_(-1.0) {
  if (pmy_mesh_->mesh_size.nx2==1 || pmy_mesh_->mesh_size.nx3==1) {
    std::stringstream msg;
    msg << "### FATAL ERROR in MultigridDriver::MultigridDriver" << std::endl
        << "Currently the Multigrid solver works only in 3D." << std::endl;
    ATHENA_ERROR(msg);
    return;
  }
  if ( !(pmy_mesh_->use_uniform_meshgen_fn_[X1DIR])
    || !(pmy_mesh_->use_uniform_meshgen_fn_[X2DIR])
    || !(pmy_mesh_->use_uniform_meshgen_fn_[X3DIR])) {
    std::stringstream msg;
    msg << "### FATAL ERROR in MultigridDriver::MultigridDriver" << std::endl
        << "Non-uniform mesh spacing is not supported." << std::endl;
    ATHENA_ERROR(msg);
    return;
  }

  for (int i=0; i<6; i++)
    MGBoundaryFunction_[i]=MGBoundary[i];

  if ( MGBoundaryFunction_[BoundaryFace::inner_x1] == MGPeriodicInnerX1
    && MGBoundaryFunction_[BoundaryFace::outer_x1] == MGPeriodicOuterX1
    && MGBoundaryFunction_[BoundaryFace::inner_x2] == MGPeriodicInnerX2
    && MGBoundaryFunction_[BoundaryFace::outer_x2] == MGPeriodicOuterX2
    && MGBoundaryFunction_[BoundaryFace::inner_x3] == MGPeriodicInnerX3
    && MGBoundaryFunction_[BoundaryFace::outer_x3] == MGPeriodicOuterX3)
    fperiodic_ = true;

  // Setting up the MPI information
  // *** this part should be modified when dedicate processes are allocated ***
  // *** we also need to construct another neighbor list for Multigrid ***
  ranklist_  = new int[pmy_mesh_->nbtotal];
  for (int n=0; n<pmy_mesh_->nbtotal; n++)
    ranklist_[n]=pmy_mesh_->ranklist[n];
  nranks_  = Globals::nranks;
  nslist_  = new int[nranks_];
  nblist_  = new int[nranks_];
  nvlist_  = new int[nranks_];
  nvslist_ = new int[nranks_];
#ifdef MPI_PARALLEL
  MPI_Comm_dup(MPI_COMM_WORLD, &MPI_COMM_MULTIGRID);
  mg_phys_id_ = pmy_mesh_->ReserveTagPhysIDs(1);
#endif
  // assume the same parallelization as hydro
  for (int n=0; n<nranks_; n++) {
    nslist_[n]  = pmy_mesh_->nslist[n];
    nblist_[n]  = pmy_mesh_->nblist[n];
    nvslist_[n] = nslist_[n]*nvar_;
    nvlist_[n]  = nblist_[n]*nvar_;
  }
  rootbuf_=new Real[pm->nbtotal*nvar_];
  mgtlist_ = new MultigridTaskList(this);
  rootsrc_.NewAthenaArray(nvar_, nrbx3_, nrbx2_, nrbx1_);
}

// destructor

MultigridDriver::~MultigridDriver() {
  delete [] ranklist_;
  delete [] nslist_;
  delete [] nblist_;
  delete [] nvslist_;
  delete [] nvlist_;
  delete [] rootbuf_;
  delete mgtlist_;
#ifdef MPI_PARALLEL
  MPI_Comm_free(&MPI_COMM_MULTIGRID);
#endif
}

//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::SetupMultigrid()
//  \brief initialize the source assuming that the source terms are already loaded

void MultigridDriver::SetupMultigrid() {
  nrootlevel_=mgroot_->GetNumberOfLevels();
  nmblevel_=vmg_[0]->GetNumberOfLevels();
  ntotallevel_=nrootlevel_+nmblevel_-1;
  current_level_=ntotallevel_-1;

  for (auto itr = vmg_.begin(); itr<vmg_.end(); itr++) {
    Multigrid *pmg = *itr;
    pmg->pmgbval->CopyNeighborInfoFromMeshBlock();
  }
  if (fperiodic_)
    SubtractAverage(0);
  if (mode_<=1) { // FMG
    for (auto itr = vmg_.begin(); itr<vmg_.end(); itr++) {
      Multigrid *pmg = *itr;
      pmg->RestrictFMGSource();
    }
    FillRootGridSource();
    mgroot_->RestrictFMGSource();
    current_level_=0;
  } else {
    current_level_=ntotallevel_-1;
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::SubtractAverage(int type)
//  \brief Calculate the global average and subtract it

void MultigridDriver::SubtractAverage(int type) {
  for (auto itr = vmg_.begin(); itr<vmg_.end(); itr++) {
    Multigrid *pmg = *itr;
    for (int v=0; v<nvar_; v++)
      rootbuf_[pmg->pmy_block_->gid*nvar_+v]=pmg->CalculateTotal(type, v);
  }
#ifdef MPI_PARALLEL
  MPI_Allgatherv(MPI_IN_PLACE, nblist_[Globals::my_rank]*nvar_, MPI_ATHENA_REAL,
                 rootbuf_, nvlist_, nvslist_, MPI_ATHENA_REAL, MPI_COMM_MULTIGRID);
#endif
  Real vol=(pmy_mesh_->mesh_size.x1max-pmy_mesh_->mesh_size.x1min)
           *(pmy_mesh_->mesh_size.x2max-pmy_mesh_->mesh_size.x2min)
           *(pmy_mesh_->mesh_size.x3max-pmy_mesh_->mesh_size.x3min);
  for (int v=0; v<nvar_; v++) {
    Real total=0.0;
    for (int n=0; n<pmy_mesh_->nbtotal; n++)
      total+=rootbuf_[n*nvar_+v];
    last_ave_=total/vol;
    for (auto itr = vmg_.begin(); itr<vmg_.end(); itr++) {
      Multigrid *pmg = *itr;
      pmg->SubtractAverage(type, v, last_ave_);
    }
  }

  return;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::FillRootGridSource()
//  \brief collect the coarsest data and fill the root grid

void MultigridDriver::FillRootGridSource() {
  for (auto itr = vmg_.begin(); itr<vmg_.end(); itr++) {
    Multigrid *pmg = *itr;
    for (int v=0; v<nvar_; v++)
      rootbuf_[pmg->pmy_block_->gid*nvar_+v]=pmg->GetRootSource(v);
  }
#ifdef MPI_PARALLEL
  MPI_Allgatherv(MPI_IN_PLACE, nblist_[Globals::my_rank]*nvar_, MPI_ATHENA_REAL,
                 rootbuf_, nvlist_, nvslist_, MPI_ATHENA_REAL, MPI_COMM_MULTIGRID);
#endif
  if (pmy_mesh_->multilevel) {
    // *** implement later
  } else { // uniform
    for (int n=0; n<pmy_mesh_->nbtotal; n++) {
      LogicalLocation &loc=pmy_mesh_->loclist[n];
      for (int v=0; v<nvar_; v++)
        // KGF: possible std::int64_t overflow. Unlikely to occur with Multigrid
        rootsrc_(v, static_cast<int>(loc.lx3), static_cast<int>(loc.lx2),
                 static_cast<int>(loc.lx1))=rootbuf_[n*nvar_+v];
    }
    mgroot_->LoadSource(rootsrc_, 0, 0, 1.0);
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::FMGProlongate()
//  \brief Prolongation for FMG Cycle

void MultigridDriver::FMGProlongate() {
  int flag=0;
  if (current_level_ == nrootlevel_-1) {
    TransferFromRootToBlocks();
    flag=1;
  }
  if (current_level_ >= nrootlevel_-1) {
    mgtlist_->SetMGTaskListFMGProlongate(flag);
    mgtlist_->DoTaskListOneStage(this);
  } else { // root grid
    mgroot_->pmgbval->ApplyPhysicalBoundaries();
    mgroot_->FMGProlongate();
  }
  current_level_++;
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::TransferFromRootToBlocks()
//  \brief Transfer the data from the root grid to the coarsest level of each MeshBlock

void MultigridDriver::TransferFromRootToBlocks() {
  AthenaArray<Real> &src=mgroot_->GetCurrentData();
  mgroot_->pmgbval->ApplyPhysicalBoundaries();
  if (pmy_mesh_->multilevel) {
    // *** implement later ***
  } else {
    for (auto itr = vmg_.begin(); itr<vmg_.end(); itr++) {
      Multigrid *pmg = *itr;
      LogicalLocation &loc=pmg->pmy_block_->loc;
      // KGF: possible std::int64_t overflow. Practically never occur with Multigrid
      pmg->SetFromRootGrid(src, static_cast<int>(loc.lx1), static_cast<int>(loc.lx2),
                           static_cast<int>(loc.lx3));
    }
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::OneStepToFiner(int nsmooth)
//  \brief smoothing and restriction one level

void MultigridDriver::OneStepToFiner(int nsmooth) {
  int ngh=mgroot_->ngh_;
  int flag=0;
  if (current_level_ == nrootlevel_-1) {
    TransferFromRootToBlocks();
    flag=1;
  }
  if (current_level_ >= nrootlevel_-1) {
    if (current_level_ == ntotallevel_-2) flag=2;
    mgtlist_->SetMGTaskListToFiner(nsmooth, ngh, flag);
    mgtlist_->DoTaskListOneStage(this);
  } else { // root grid
    mgroot_->pmgbval->ApplyPhysicalBoundaries();
    mgroot_->ProlongateAndCorrect();
    for (int n=0; n<nsmooth; n++) {
      mgroot_->pmgbval->ApplyPhysicalBoundaries();
      mgroot_->Smooth(0);
      mgroot_->pmgbval->ApplyPhysicalBoundaries();
      mgroot_->Smooth(1);
    }
  }
  current_level_++;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::OneStepToCoarser(int nsmooth)
//  \brief smoothing and restriction one level

void MultigridDriver::OneStepToCoarser(int nsmooth) {
  int ngh=mgroot_->ngh_;
  if (current_level_ >= nrootlevel_) {
    mgtlist_->SetMGTaskListToCoarser(nsmooth, ngh);
    mgtlist_->DoTaskListOneStage(this);
    if (current_level_ == nrootlevel_) {
      FillRootGridSource();
      mgroot_->ZeroClearData();
    }
  } else { // root grid
    for (int n=0; n<nsmooth; n++) {
      mgroot_->pmgbval->ApplyPhysicalBoundaries();
      mgroot_->Smooth(0);
      mgroot_->pmgbval->ApplyPhysicalBoundaries();
      mgroot_->Smooth(1);
    }
    mgroot_->pmgbval->ApplyPhysicalBoundaries();
    mgroot_->Restrict();
  }
  current_level_--;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::SolveVCycle(int npresmooth, int npostsmooth)
//  \brief Solve the V-cycle starting from the current level

void MultigridDriver::SolveVCycle(int npresmooth, int npostsmooth) {
  int startlevel=current_level_;
  while (current_level_>0)
    OneStepToCoarser(npresmooth);
  SolveCoarsestGrid();
  while (current_level_<startlevel)
    OneStepToFiner(npostsmooth);
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::SolveFCycle(int npresmooth, int npostsmooth)
//  \brief Solve the F-cycle starting from the current level

void MultigridDriver::SolveFCycle(int npresmooth, int npostsmooth) {
  int startlevel=current_level_;
  int turnlevel;
  if (startlevel == 0)
    turnlevel=0;
  else
    turnlevel=1;
  for (; turnlevel<=startlevel; turnlevel++) {
    while (current_level_>0)
      OneStepToCoarser(npresmooth);
    SolveCoarsestGrid();
    while (current_level_<turnlevel)
      OneStepToFiner(npostsmooth);
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::SolveFMGCycle()
//  \brief Solve the FMG Cycle using the V(1,1) or F(0,1) cycle

void MultigridDriver::SolveFMGCycle() {
  for (int lev=0; lev<ntotallevel_; lev++) {
    if (mode_ == 0)
      SolveVCycle(1, 1);
    else if (mode_ == 1)
      SolveFCycle(0, 1);
    if (lev!=ntotallevel_-1) FMGProlongate();
  }
  if (fperiodic_)
    SubtractAverage(1);
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::SolveIterative()
//  \brief Solve iteratively until the convergence is achieved

void MultigridDriver::SolveIterative() {
  Real def=eps_+1e-10;
  int niter=0;
  std::cout << std::scientific;
  while (def>eps_) {
    SolveVCycle(1,1);
    Real olddef=def;
    def=0.0;
    for (int n=0; n<nvar_; n++)
      def+=CalculateDefectNorm(n, 2);
    if (niter > 0 && def/olddef > 0.5) {
      if (eps_ == 0.0) break;
      if (Globals::my_rank == 0)
        std::cout << "### Warning in MultigridDriver::SolveIterative" << std::endl
                  << "Slow multigrid convergence : defect norm = " << def
                  << ", convergence factor = " << def/olddef << "." << std::endl;
    }
    if (niter>100) {
      if (Globals::my_rank == 0) {
        std::cout
            << "### Warning in MultigridDriver::SolveIterative" << std::endl
            << "Aborting because the # iterations is too large, niter > 100." << std::endl
            << "Check the solution as it may not be accurate enough." << std::endl;
      }
      break;
    }
    niter++;
  }
  if (fperiodic_)
    SubtractAverage(1);
  return;
}


//----------------------------------------------------------------------------------------
//! \fn void MultigridDriver::SolveCoarsestGrid()
//  \brief Solve the coarsest root grid

void MultigridDriver::SolveCoarsestGrid() {
  int ni = (std::max(nrbx1_, std::max(nrbx2_, nrbx3_))
            >> (nrootlevel_-1));
  if (fperiodic_ && ni == 1) { // trivial case - all zero
    mgroot_->ZeroClearData();
    return;
  } else {
    if (fperiodic_) {
      Real vol=(mgroot_->size_.x1max-mgroot_->size_.x1min)
              *(mgroot_->size_.x2max-mgroot_->size_.x2min)
              *(mgroot_->size_.x3max-mgroot_->size_.x3min);
      for (int v=0; v<nvar_; v++) {
        Real ave=mgroot_->CalculateTotal(1, v)/vol;
        mgroot_->SubtractAverage(1, v, ave);
      }
    }
    for (int i=0; i<ni; i++) { // iterate ni times
      mgroot_->pmgbval->ApplyPhysicalBoundaries();
      mgroot_->Smooth(0);
      mgroot_->pmgbval->ApplyPhysicalBoundaries();
      mgroot_->Smooth(1);
    }
    mgroot_->pmgbval->ApplyPhysicalBoundaries();
    if (fperiodic_) {
      Real vol=(mgroot_->size_.x1max-mgroot_->size_.x1min)
              *(mgroot_->size_.x2max-mgroot_->size_.x2min)
              *(mgroot_->size_.x3max-mgroot_->size_.x3min);
      for (int v=0; v<nvar_; v++) {
        Real ave=mgroot_->CalculateTotal(1, v)/vol;
        mgroot_->SubtractAverage(1, v, ave);
      }
    }
  }
  return;
}


//----------------------------------------------------------------------------------------
//! \fn Real MultigridDriver::CalculateDefectNorm(int n, int nrm)
//  \brief calculate the defect norm

Real MultigridDriver::CalculateDefectNorm(int n, int nrm) {
  Real norm=0.0;
  for (auto itr = vmg_.begin(); itr < vmg_.end(); itr++) {
    Multigrid *pmg = *itr;
    if (nrm == 0)
      norm=std::max(norm, pmg->CalculateDefectNorm(n, nrm));
    else
      norm+=pmg->CalculateDefectNorm(n, nrm);
  }
#ifdef MPI_PARALLEL
  if (nrm == 0)
    MPI_Allreduce(MPI_IN_PLACE,&norm,1,MPI_ATHENA_REAL,MPI_MAX,MPI_COMM_MULTIGRID);
  else
    MPI_Allreduce(MPI_IN_PLACE,&norm,1,MPI_ATHENA_REAL,MPI_SUM,MPI_COMM_MULTIGRID);
#endif
  if (nrm == 2)
    norm=std::sqrt(norm);

  return norm;
}


//----------------------------------------------------------------------------------------
//! \fn Multigrid* MultigridDriver::FindMultigrid(int tgid)
//  \brief return the Multigrid whose gid is tgid

Multigrid* MultigridDriver::FindMultigrid(int tgid) {
  for (auto itr = vmg_.begin(); itr < vmg_.end(); itr++) {
    Multigrid *pmg = *itr;
    if (pmg->pmy_block_->gid == tgid)
      return pmg;
  }
  return nullptr;
}
