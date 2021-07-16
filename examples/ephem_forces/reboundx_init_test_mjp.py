'''
MJP : 2021-07-16

Copying some simple run commands from MJH's "integrator_example.ipynb" notebook

I just want to have a very simple test/demo that I can use to see whether a basic installation
has worked or not ...

'''

# Do basic required imports
import numpy as np
import ephem_forces

# MJP: I have not examined the all_ephem code, so I don't know what this is doing
#      However, it seems it should return 10 numbers ...
#      For now I will content myself with checking for that...
_ = ephem_forces.all_ephem(3, 2458849.500000000)
assert len(_) == 10 , \
    f'The return from calling ephem_forces.all_ephem does not have the expected length: {_}'

# Set up the ICs for an integration
row = [3.338875349745594E+00, -9.176518281675284E-01, -5.038590682977396E-01, 2.805663319000732E-03, 7.550408687780768E-03, 2.980028206579994E-03]

# Set up the parameters for an integration
instates = np.array([row])
n_particles = 1
tstart, tstep, trange = 2458849.5, 20.0, 10000
epoch = tstart
tend = tstart + trange

# Run integration
times, states, var, var_ng, status = ephem_forces.production_integration_function_wrapper(tstart, tend, epoch, instates)

# Examine output types
for _ in (times, states, var):
    assert isinstance(_, np.ndarray )
assert isinstance( var_ng, type(None))
assert isinstance( status, tuple )
print(f"times.shape={times.shape} , states.shape={states.shape} , var.shape={var.shape}" )
n = 10
print(f"Printing first {n} steps of output...")
for i in range(n):
    print("\n",i)
    print("times[i]=",times[i])
    print("states[i]=",states[i])
    print("var[i]=",var[i])

print("Integration Successful!")
