"""This is a python wrapper to an ephemeris-quality integrator function.
This wrapper uses ctypes to access a reboundx c library that contains an
extension for carrying out highly accurate integrations of test
particles moving in the field of the sun, planets, moon, and massive
asteroids, with positions and velocities supplied by JPL ephemeris 
files.  
"""

import ctypes
from ctypes import *
import numpy as np
from os import path as osp

rebx_location = osp.join(osp.dirname(osp.realpath(__file__)), 'libreboundx.so')
rebx_lib = CDLL(rebx_location)

class TimeState(Structure):
    """
    A ctypes mapping to the structure populated by integration_function.
    """
    _fields_ = [
        ('t', POINTER(c_double)),
        ('state', POINTER(c_double)),
        ('n_alloc', c_int),        
        ('n_particles', c_int)
    ]

"""
static int all_ephem(const int i, const double t, double* const GM,
		      double* const x, double* const y, double* const z,
		      double* const vx, double* const vy, double* const vz,
		      double* const ax, double* const ay, double* const az
"""

def all_ephem(i, t):
    """
    Gets the position, velocity, and acceleration of a body from the 
    ephemeris.
    
    *Returns*
        (GM, (x, y, z, vx, vy, vz, ax, ay, az)) : tuple of floats
    
    """
    
    # Set up call to integration_function
    _all_ephem = rebx_lib.all_ephem
    _all_ephem.restype = c_int
    _all_ephem.argtypes = (c_int, c_double, 
                                      POINTER(c_double),
                                      POINTER(c_double),
                                      POINTER(c_double),
                                      POINTER(c_double),
                                      POINTER(c_double),
                                      POINTER(c_double),
                                      POINTER(c_double),
                                      POINTER(c_double),
                                      POINTER(c_double))

    GM = c_double()    
    x = c_double()
    y = c_double()
    z = c_double()
    vx = c_double()
    vy = c_double()
    vz = c_double()
    ax = c_double()
    ay = c_double()
    az = c_double()

    return_value = _all_ephem(i, t, byref(GM),
                              byref(x), byref(y), byref(z),
                              byref(vx), byref(vy), byref(vz),
                              byref(ax), byref(ay), byref(az))

    return GM.value, np.array((x.value, y.value, z.value, vx.value, vy.value, vz.value, ax.value, ay.value, az.value))



def integration_function(tstart, tend, tstep,
                         geocentric,
                         n_particles,
                         instate_arr,
                         n_var,
                         invar_part,                         
                         invar,
                         epsilon = 1e-8,
                         min_dt = 0.01,
                         max_dt = 32.):

    # Set up call to integration_function
    _integration_function = rebx_lib.integration_function

    _integration_function.argtypes = (c_double, c_double, c_double,
                                      c_int,
                                      c_double,
                                      c_int,
                                      POINTER(c_double),
                                      c_int,
                                      POINTER(c_int),
                                      POINTER(c_double),
                                      c_int,
                                      POINTER(c_int),
                                      POINTER(c_double),
                                      POINTER(c_double),
                                      c_double,
                                      c_double)

    _integration_function.restype = c_int

    n_alloc = int(abs((tend-tstart)/tstep)*1.2)
    return_value = 5
    fac = 1.0
    iters = 0

    if (tend-tstart)/tstep < 0.0:
        print('tstep is in the wrong direction')
        return None, None, None, None, -10

    substeps = 10
    while(return_value == 5 and iters<5):

        n_alloc = int(fac*n_alloc)
        tsize = (n_alloc*substeps+1)    
        ssize = (n_alloc*substeps+1)*6*(n_particles+n_var)

        outtime = np.zeros((tsize), dtype=np.double)
        outstate = np.zeros((ssize), dtype=np.double)

        n_out = c_int()
        
        return_value = _integration_function(tstart, tend, tstep,
                                             geocentric,
                                             epsilon,
                                             n_particles,
                                             instate_arr.ctypes.data_as(POINTER(c_double)),
                                             n_var,
                                             invar_part.ctypes.data_as(POINTER(c_int)),                                             
                                             invar.ctypes.data_as(POINTER(c_double)),
                                             n_alloc,
                                             byref(n_out),
                                             outtime.ctypes.data_as(POINTER(c_double)),
                                             outstate.ctypes.data_as(POINTER(c_double)),
                                             min_dt,
                                             max_dt)

        #print('integration_function: ', iters)
        #import sys
        #sys.exit()

        if len(outtime)>1:
            fac = int(1.5*abs((tend-tstart)/(outtime[-1]-outtime[0])))

        iters += 1

    outstate = np.reshape(outstate, (-1, (n_particles+n_var), 6))
    outstate = outstate[:substeps*n_out.value+1]
    outtime = outtime[:substeps*n_out.value+1]

    states = outstate[:,0:n_particles,:]
    var_state = outstate[:,n_particles:,:]
    var_ng = None

    return outtime, states, var_state, var_ng, return_value    
    

def production_integration_function_wrapper(
        tstart,
        tend,
        epoch,
        instate_arr,
        non_grav_dict_list = None,
        tstep=32,
        geocentric=0,
        epsilon=1e-8,
        tstep_min = 0.01,
        tstep_max = 32):       

    """
    Standardized wrapper for calling integration_function
    Intended for "production" usage by MPC
     - Sets a bunch of standard defaults (e.g. tangent-eqn evolution vector specificaton)
    
    In the future it will handle non-gravs, but for now it does *** NOT ***
    Non-grav dicts will look like ...
        "nongrav_data": {
            "non_gravs": false,
            "booleans": {
                "yarkovski": false,
                "srp": false,
                "marsden": false,
                "yc": false,
                "yabushita": false,
                "A1": false,
                "A2": false,
                "A3": false,
                "DT": false
            },
            "coefficients": {
                "yarkovski": null,
                "srp": null,
                "A1": null,
                "A2": null,
                "A3": null,
                "DT": null
            }
        }
    """

    n_particles = len(instate_arr)

    # call the integrator
    # For non-gravs:
    # pass in an identifier for the model
    # pass in the parameters for that model
    # pass in variational particle states for the
    # non-grav parameters, in addition to the usual
    # variational particles
    
    # We may have non-gravs ...
    if isinstance(non_grav_dict_list, list):

        pass
    
        # *** FOR NOW LET'S NOT WORRY ABOUT IMPLEMENTING THIS !!! ***
        #assert len(non_grav_dict_list) == n_particles
        #assert np.all( [isinstance(_, dict) for _ in non_grav_dict_list ] )
        ###do something to interpret each dict
        ###do they all need to be the same model?
        
    # variational eqn stuff

    n_var       = 6*n_particles
    invar_part  = np.repeat(np.arange(n_particles),6)
    invar       = np.concatenate([np.identity(6) for i in np.arange(n_particles)])

    if tstart == tend:
         return np.array((tstart)), instate_arr, invar, var_state, None, 0
        
    f = (epoch-tstart)/(tend-tstart)

    #
    # if f<=0 then epoch is outside of the range, on the tstart side
    #
    # 1. epoch tstart tend
    # 6. tend tstart epoch
    # integrate from the epoch to tstart, then continue to tend
    #
    # This means that the instate_arr and invar arrays need to
    # to saved at tstart so that they can be fed into the next
    # integration.  To ensure that the precise time is met, this
    # flag is set in the C code.
    #     r->exact_finish_time = 1;
    # output is enabled in the second portion

    if tstart > tend:
        tstep = -tstep

    if f <= 0.:

        outtime, states, var_state, var_ng, return_value = integration_function(epoch,
                                                                                tstart,
                                                                                tstep,
                                                                                geocentric,
                                                                                n_particles,
                                                                                instate_arr,
                                                                                n_var,
                                                                                invar_part,
                                                                                invar,
                                                                                epsilon = epsilon,
                                                                                min_dt = tstep_min,
                                                                                max_dt = tstep_max)

        outtime, states, var_state, var_ng, return_value = integration_function(tstart,
                                                                                tend,
                                                                                tstep,
                                                                                geocentric,
                                                                                n_particles,
                                                                                states[-1],
                                                                                n_var,
                                                                                invar_part,
                                                                                var_state[-1],
                                                                                epsilon = epsilon,
                                                                                min_dt = tstep_min,
                                                                                max_dt = tstep_max)

        return_value = (return_value,)         
         
    #
    # if f>=1 then epoch is outside of the range, on the tend side
    #
    # 2. epoch tend tstart
    # 5. tstart tend epoch
    # integrate from the epoch to tend, then continue to tstart
    # output is enabled in the second portion

    elif f >= 1.:

        outtime, states, var_state, var_ng, return_value = integration_function(epoch,
                                                                                tend,
                                                                                -tstep,
                                                                                geocentric,
                                                                                n_particles,
                                                                                instate_arr,
                                                                                n_var,
                                                                                invar_part,
                                                                                invar,
                                                                                epsilon = epsilon,
                                                                                min_dt = tstep_min,
                                                                                max_dt = tstep_max)
        

        outtime, states, var_state, var_ng, return_value = integration_function(tend,
                                                                                tstart,
                                                                                -tstep,
                                                                                geocentric,
                                                                                n_particles,
                                                                                states[-1],
                                                                                n_var,
                                                                                invar_part,
                                                                                var_state[-1],
                                                                                epsilon = epsilon,
                                                                                min_dt = tstep_min,
                                                                                max_dt = tstep_max)


        # Reverse the order of the integration

        outtime = np.flip(outtime, axis=0)[0:-1]
        states = np.flip(states, axis=0)[0:-1]
        var_state = np.flip(var_state, axis=0)[0:-1]

        return_value = (return_value,)
        
    #
    # if that 0<f<1 then epoch is within the span of tstart to tend
    #
    # 3. tstart epoch tend
    # 4. tend epoch tstart
    # integrate from the epoch to tstart, then integrate from the epoch to tend
    # output is enabled in both portions

    else:

         outtime0, states0, var_state0, var_ng0, return_value0 = integration_function(epoch,
                                                                                      tstart,
                                                                                      -tstep,
                                                                                      geocentric,
                                                                                      n_particles,
                                                                                      instate_arr,
                                                                                      n_var,
                                                                                      invar_part,
                                                                                      invar,
                                                                                      epsilon = epsilon,
                                                                                      min_dt = tstep_min,
                                                                                      max_dt = tstep_max)
        

         outtime1, states1, var_state1, var_ng1, return_value1 = integration_function(epoch,
                                                                                      tend,
                                                                                      tstep,
                                                                                      geocentric,
                                                                                      n_particles,
                                                                                      instate_arr,
                                                                                      n_var,
                                                                                      invar_part,
                                                                                      invar,
                                                                                      epsilon = epsilon,
                                                                                      min_dt = tstep_min,
                                                                                      max_dt = tstep_max)
         

         # Reverse the order of the first integration
         # and concatenate the results of the second integration
         # onto it.

         timesf = np.flip(outtime0, axis=0)[0:-1]
         outtime = np.concatenate((timesf, outtime1), axis=0)

         statesf = np.flip(states0, axis=0)[0:-1]
         states = np.concatenate((statesf, states1), axis=0)         

         var_statef = np.flip(var_state0, axis=0)[0:-1]
         var_state = np.concatenate((var_statef, var_state1), axis=0)         

         #var_ngf = np.flip(var_ng0, axis=0)[0:-1]
         #var_ng = np.concatenate((var_ngf, var_ng1), axis=0)

         var_ng = var_ng0

         return_value = (return_value0, return_value1)

    return outtime, states, var_state, var_ng, return_value             



