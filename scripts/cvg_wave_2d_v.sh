#!/bin/sh
###############################################################################

###############################################################################
# Short script for taking care of (optional) compilation and running a problem
###############################################################################

###############################################################################
# configure here
export RUN_NAME=cvg_wave_2d
export BIN_NAME=wave
export REL_OUTPUT=outputs/wave_2v
export REL_INPUT=scripts/problems
export INPUT_NAME=cvg_wave_2d.inp

# if compilation is chosen
export DIR_INST=$soft/usr  # correct for location of installed libraries

export COMPILE_STR="--prob=wave_2d_cvg_trig -w
                    --cxx g++ -omp -debug -vertex
                    --nghost=2
                    --ncghost=3
                    --nextrapolate=6"

# fill MeshBlock interior with exact solution
# export COMPILE_STR="${COMPILE_STR} -fill_wave_interior"

# fill MeshBlock boundaries [same level] with exact solution
# export COMPILE_STR="${COMPILE_STR} -fill_wave_bnd_sl"

# fill MeshBlock boundaries [from finer] with exact solution
# export COMPILE_STR="${COMPILE_STR} -fill_wave_bnd_frf"

# fill MeshBlock boundaries [from coarser] with exact solution
# export COMPILE_STR="${COMPILE_STR} -fill_wave_bnd_frc"

# fill MeshBlock coarse buffer with exact solution (prior to prolongation)
# export COMPILE_STR="${COMPILE_STR} -fill_wave_coarse_p"

# debug vertex consistency
# export COMPILE_STR="${COMPILE_STR} -dbg_vc_consistency"

###############################################################################

###############################################################################
# ensure paths are adjusted and directory structure exists
. utils/provide_paths.sh

###############################################################################
# compile
. utils/compile_force.sh
###############################################################################

# >:D