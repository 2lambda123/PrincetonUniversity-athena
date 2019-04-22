# Regression test for self-gravity based on linear Jeans instability
# using FFT gravity + no MPI

# Runs a 3D linear Jeans convergence test and checks L1 errors (which are
# computed by the executable automatically and stored in the temporary file
# jeans-errors.dat)

# Modules
import logging
import numpy as np
import scripts.utils.athena as athena
import sys
sys.path.insert(0, '../../vis/python')
import athena_read                             # noqa
athena_read.check_nan_flag = True
logger = logging.getLogger('athena' + __name__[7:])  # set logger name based on module


# Prepare Athena++
def prepare(**kwargs):
    logger.debug('Running test ' + __name__)
    athena.configure('fft',
                     prob='jeans',
                     grav='fft',
                     **kwargs)
    athena.make()


# Run Athena++
def run(**kwargs):
    # njeans = 1.5
    # period = 0.3
    # 1/omega = 0.046
    # amp 1e-6
    def arg_res(res):
        arguments = [
          'mesh/nx1=64', 'mesh/nx2=32', 'mesh/nx3=32',
          'meshblock/nx1=16',
          'meshblock/nx2=16',
          'meshblock/nx3=16',
          'problem/njeans=1.5',
          'output2/dt=-1', 'time/tlim=0.046', 'problem/compute_error=true',
          'time/ncycle_out=10']
        arguments[0] = 'mesh/nx1='+str(2*res)
        arguments[1] = 'mesh/nx2='+str(res)
        arguments[2] = 'mesh/nx3='+str(res)
        return arguments
    athena.run('hydro/athinput.jeans_3d', arg_res(32))
    athena.run('hydro/athinput.jeans_3d', arg_res(64))


# Analyze outputs
def analyze():
    # read data from error file
    filename = 'bin/jeans-errors.dat'
    data = athena_read.error_dat(filename)
    logger.info(str(data))
    result = True
    # error
    for i in range(len(data)):
        if data[i][4] > 1.e-7:
            logger.warning("FFT Gravity Linear Jeans instability error is too large: %g",
                           data[i][4])
            result = False
    # compute overall convergence slope
    gslope = np.log(data[len(data)-1][4]/data[0][4])/np.log(4.0)
    err_tol = 1.5
    warn_tol = 1.1
    # 2nd order convergence: doubling resolution should decrease error by 4.0
    for i in range(len(data)-1):
        slope = np.log(data[i+1][4]/data[i][4])/np.log(2.0)
        if data[i+1][4] > (err_tol*data[i][4]/(4.0)):
            logger.warning(
                "Linear Jeans instability error is not converging at 2nd order")
            logger.warning("Error tolerance: %g", err_tol)
            logger.warning("Order estimate: %g %g", slope, gslope)
            result = False
        elif data[i+1][4] > (warn_tol*data[i][4]/(4.0)):
            logger.warning("WARNING: Linear Jeans instability error is converging slowly")
            logger.warning("Error tolerance: %g", warn_tol)
            logger.warning("Order estimate: %g %g", slope, gslope)
    return result
