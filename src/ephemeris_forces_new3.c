/** * @file ephemeris_forces.c
 * @brief   A routine for ephemeris-quality force calculations.
 * @author  Matthew Holman <mholman@cfa.harvard.edu>
 * @author  Arya Akmal <akmala@gmail.com>
 *
 *
 * Ephemeris-quality integrations
 *
 * This example uses the IAS15 integrator to integrate
 * the orbits of test particles in the field of the sun, moon, planets
 * and massive asteroids.  The positions and velocities of the
 * massive bodies are taken from JPL ephemeris files.  Solar GR, solar J2,
 * and earth J2/J4 are included.
 *
 * This is being developed to be incorporated into the reboundx package.
 *
 * Contributors:
 *
 * Robert Weryk <weryk@hawaii.edu>
 * Daniel Tamayo <dtamayo@astro.princeton.edu>
 * Matthew Payne <mpayne@cfa.harvard.edu>
 * David Hernandez <dmhernandez@cfa.harvard.edu>
 * Hanno Rein <hanno.rein@utoronto.ca>
 * Davide Farnocchia <davide.farnocchia@jpl.nasa.gov>
 * Jon Giorgini <jon.giorgini@jpl.nasa.gov>
 * 
 * To do:
 * 
 * 0. Write a wrapper function that takes an initial time, the initial positions and
 *    velocities of a set of one or more test particles, an integration time span or 
 *    final time, and time step.  The function should call the appropriate rebound
 *    routines and load the results into arrays that are accessible upon return (through
 *    pointers.  The return value of the function should represent success, failure, etc.
 *    DONE--mostly.
 * 
 * 1. Modify the code so that the initial conditions of the particle, the time
 *    span of the integration, and the time step come from a file.  We probably want to 
 *    allow the user to specific barycentric or geocentric. DONE.
 * 
 * 2. Rearrange the ephem() function so that it returns all the positions in one shot.
 * 
 * 3. Check position of the moon.  DONE.
 * 
 * 4. Separate ephem() and ast_ephem() from the rest of ephemeris_forces code.  DONE.
 * 
 * 5. Streamline ephem() function.  DONE.
 * 
 * 6. Put in earth J2 and J4.  DONE.  Could put in the orientation of the spin 
 *    axis.  Can't include both J2/J4 of earth and sun without this.  DONE.
 * 
 * 7. Put in J2 of the sun.  This will require thinking about the orientation of the spin 
 *    axis.  DONE.
 *
 * 8. Fix loop over particles.  DONE.
 * 
 * 9. Develop sensible code that transitions to and from geocentric system.
 *
 * 10. Document a bunch of tests of the variational equation sections of the code. 
 *     The straight nbody routines for the planets and massive asteroids are likely
 *     ok, because they were copied from other parts of the rebound code.  The Earth J2/J4,
 *     solar J2, and solar GR sections need to be checked carefully.  I am not sure
 *     what kinds of tests to conduct.
 *
 *     Here's an idea: isolate each accelaration calculation and its associated 
 *     variational equation section and test them separately.
 *     
 * 11. Put in the full 4x4 gravitational harmonics and associated variational equations for
 *     the Earth.  This is needed to get a good fit for 2020 CD3, which is an art-sat test case.
 *
 * 12. Work on making the integrations restartable, like the rest of the rebound package.
 */

#define FNAMESIZE 256
#define DEFAULT_JPL_SB_EPHEM "sb441-n16.bsp"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "rebound.h"
#include "reboundx.h"

#include "spk.h"
#include "planets.h"

const int reb_max_messages_length = 1024;   // needs to be constant expression for array size
const int reb_max_messages_N = 10;

enum {
    NO_ERR,        // no error
    ERR_JPL_EPHEM, // JPL ephemeris file not found
    ERR_JPL_AST,   // JPL asteroid file not found
    ERR_NAST,      // asteroid number out of range
    ERR_NEPH,      // planet number out of range
};


int ebody[11] = {
        PLAN_SOL,                       // Sun (in barycentric)
        PLAN_MER,                       // Mercury center
        PLAN_VEN,                       // Venus center
        PLAN_EAR,                       // Earth center
        PLAN_LUN,                       // Moon center
        PLAN_MAR,                       // Mars center
        PLAN_JUP,                       // ...
        PLAN_SAT,
        PLAN_URA,
        PLAN_NEP,
        PLAN_PLU
};

// Added gravitational constant G (2020 Feb 26)
// Added vx, vy, vz for GR stuff (2020 Feb 27)
// Consolidated the routine, removing the if block.
//
static int ephem(const int i, const double jde, double* const GM,
	   double* const x, double* const y, double* const z,
	   double* const vx, double* const vy, double* const vz,
	   double* const ax, double* const ay, double* const az){

    static int initialized = 0;

    static struct _jpl_s *pl;
    struct mpos_s now;

    // The values below are G*mass.
    // Units are solar masses, au, days.
    // TODO: The units should probably be handled elsewhere.
    // DE440/441 units: au^3 day^-2.
    // TODO: These should be moved to an external source that
    // is easily modified.
    const static double JPL_GM[11] =
	{
	    0.2959122082841196e-03, // 0 sun
	    0.4912500194889318e-10, // 1 mercury
	    0.7243452332644119e-09, // 2 venus
	    0.8887692446707102e-09, // 3 earth
	    0.1093189462402435e-10, // 4 moon	    
	    0.9549548829725812e-10, // 5 mars
	    0.2825345825225792e-06, // 6 jupiter
	    0.8459705993376290e-07, // 7 saturn
	    0.1292026564968240e-07, // 8 uranus
	    0.1524357347885194e-07, // 9 neptune
	    0.2175096464893358e-11, // 10 pluto
	};

    if(i<0 || i>10){
	return(ERR_NEPH);
    }

    if (initialized == 0){
      if ((pl = jpl_init()) == NULL) {
	  return(ERR_JPL_EPHEM);	  
      }
      initialized = 1;
    }

    // Get position, velocity, and mass of body i in barycentric coords. 
    
    *GM = JPL_GM[i];

    jpl_calc(pl, &now, jde, ebody[i], PLAN_BAR); 

    // Convert to au/day and au/day^2
    // TODO: Consider making the units more flexible.
    vecpos_div(now.u, pl->cau);
    vecpos_div(now.v, pl->cau/86400.);
    vecpos_div(now.w, pl->cau/(86400.*86400.));

    *x = now.u[0];
    *y = now.u[1];
    *z = now.u[2];
    *vx = now.v[0];
    *vy = now.v[1];
    *vz = now.v[2];
    *ax = now.w[0];
    *ay = now.w[1];
    *az = now.w[2];

    return(NO_ERR);
    
}

static int ast_ephem(const int i, const double jde, double* const GM, double* const x, double* const y, double* const z){

    static int initialized = 0;

    static struct spk_s *spl;
    struct mpos_s pos;

    // The values below are G*mass.
    // Units are solar masses, au, days.
    // DE441
    // GMs were supplied by Davide.
    // TODO: these should be moved to an external
    // source that can be easily updated.
    const static double JPL_GM[16] =    
    {
	    3.2191392075878588e-15, // 107 camilla
	    1.3964518123081070e-13, // 1 ceres
	    2.0917175955133682e-15, // 65 cybele	
	    8.6836253492286545e-15, // 511 davida
	    4.5107799051436795e-15, // 15 eunomia
	    2.4067012218937576e-15, // 31 euphrosyne	    
	    5.9824315264869841e-15, // 52 europa
	    1.2542530761640810e-14, // 10 hygiea
	    6.3110343420878887e-15, // 704 interamnia
	    2.5416014973471498e-15, // 7 iris	    
	    4.2823439677995011e-15, // 3 juno
	    3.0471146330043200e-14, // 2 pallas
	    3.5445002842488978e-15, // 16 psyche
	    4.8345606546105521e-15, // 87 sylvia
	    2.6529436610356353e-15, // 88 thisbe
	    3.8548000225257904e-14, // 4 vesta          

    };

    if(i<0 || i>15){
	return(ERR_NAST);
    }

    if (initialized == 0){

	char buf[FNAMESIZE];

        /** use or environment-specified file, 
	 * or the default filename, in that order
         */
        if (getenv("JPL_SB_EPHEM")!=NULL)
	    strncpy(buf, getenv("JPL_SB_EPHEM"), FNAMESIZE-1);
        else
	    strncpy(buf, DEFAULT_JPL_SB_EPHEM, FNAMESIZE-1);

	printf("%s\n", buf);
	FILE *file;
	file = fopen(buf, "r");
	if(file == NULL){
	    printf("couldn't open asteroid file.\n");
	}
	      

	if ((spl = spk_init(buf)) == NULL) {	
	    //if ((spl = spk_init("sb441-n16.bsp")) == NULL) {
	    return(ERR_JPL_AST);
      }
      
      initialized = 1;

    }

    // TODO: again, the units might be handled more
    // generally

    *GM = JPL_GM[i];
    spk_calc(spl, i, jde, &pos);          
    *x = pos.u[0];
    *y = pos.u[1];
    *z = pos.u[2];

    return(NO_ERR);

}

// TODO: this should be more general.  Perhaps
// this information could come directly from
// the JPL ephemerides.
static int number_bodies(int* N_ephem, int* N_ast){
    *N_ephem = 11;
    *N_ast = 16;

    return(*N_ephem + *N_ast);
}

int all_ephem(const int i, const double t, double* const GM,
		      double* const x, double* const y, double* const z,
		      double* const vx, double* const vy, double* const vz,
		      double* const ax, double* const ay, double* const az
		      ){    

    int number_bodies(int* N_ephem, int* N_ast);
    static int N_ast = -1;
    static int N_ephem = -1;

    if(N_ast == -1 || N_ephem == -1){
	number_bodies(&N_ephem, &N_ast);
    }

    static double xs, ys, zs, vxs, vys, vzs, axs, ays, azs;
    static double GMs;
    static double t_last = -1e99;

    // For any given step, using the IAS15 integrator,
    // all_ephem will need to access positions and GM values
    // for 27 bodies at the times of 8 different substeps.
    // The integrator loops through through the same substeps
    // to convergence.
    // TODO: We can optimize by saving the positions
    // of the perturbers, rather than reevaluating them at
    // each iteration.
    
    // Get position and mass of massive body i.
    if(i < N_ephem){
	int flag = ephem(i, t, GM, x, y, z, vx, vy, vz, ax, ay, az);
	if(flag != NO_ERR) return(flag);
    }else{
	// Get position and mass of asteroid i-N_ephem.
	int flag = ast_ephem(i-N_ephem, t, GM, x, y, z);
	if(flag != NO_ERR) return(flag);	

	if(t != t_last){
	    flag = ephem(0, t, &GMs, &xs, &ys, &zs, &vxs, &vys, &vzs, &axs, &ays, &azs);
	    if(flag != NO_ERR) return(flag);		    
	    t_last = t;
	}

	// Translate massive asteroids from heliocentric to barycentric.
	*x += xs; *y += ys; *z += zs;
	*vx = NAN; *vy = NAN; *vz = NAN;
	*ax = NAN; *ay = NAN; *az = NAN;		
    }

    return(NO_ERR);
}

void rebx_ephemeris_forces(struct reb_simulation* const sim, struct rebx_force* const force, struct reb_particle* const particles, const int N){

    const double G = sim->G;
    const double t = sim->t;
    const int N_real= N;

    int N_ephem, N_ast;
    int number_bodies(int* N_ephem, int* N_ast);

    const int N_tot = number_bodies(&N_ephem, &N_ast);

    // This is only needed if GR is used.
    const double* c = rebx_get_param(sim->extras, force->ap, "c");
    if (c == NULL){
        reb_error(sim, "REBOUNDx Error: Need to set speed of light in gr effect.  See examples in documentation.\n");
        return;
    }

    int* geo = rebx_get_param(sim->extras, force->ap, "geocentric"); // Make sure there is a default setting.
    if (geo == NULL){
        reb_error(sim, "REBOUNDx Error: Need to set geo flag.  See examples in documentation.\n");
        return;
    }

    const double C2 = (*c)*(*c);  // This could be stored as C2.
    
    double GM;
    double x, y, z, vx, vy, vz, ax, ay, az;

    // Get mass, position, velocity, and acceleration of the Earth and Sun
    // for later use.
    // The hard-wired constants should be changed.
    
    // The offset position is used to adjust the particle positions.
    // The options are the barycenter (default) and geocenter.
    double xo, yo, zo, vxo, vyo, vzo, axo, ayo, azo;

    //printf("top\n");
    //fflush(stdout);

    if(*geo == 1){
	// geocentric
	int flag = all_ephem(3, t, &GM, &xo, &yo, &zo, &vxo, &vyo, &vzo, &axo, &ayo, &azo);
	if(flag != NO_ERR){
	    char outstring[50];
	    sprintf(outstring, "%s %d %d\n", "Ephemeris error a ", 3, flag);
	    reb_error(sim, outstring);
	}
    }else{
	// barycentric
	xo = 0.0;  yo = 0.0;  zo = 0.0;
	vxo = 0.0; vyo = 0.0; vzo = 0.0;      
    }

    // TODO: eliminate these output files after testing.
    //FILE *outfile;
    //outfile = fopen("foo.out", "w");

    //FILE *eih;
    //eih = fopen("eih_acc.out", "w");
 
    //FILE *vfile;
    //vfile = fopen("vary_acc.out", "w");

    // Direct forces from massives bodies
    for (int i=0; i<N_tot; i++){
	
        // Get position and mass of massive body i.
	// TOOD: make a version that returns the positions, velocities,
	// and accelerations for all the bodies at a given time.
	int flag = all_ephem(i, t, &GM, &x, &y, &z, &vx, &vy, &vz, &ax, &ay, &az);
	if(flag != NO_ERR){
	    char outstring[50];
	    sprintf(outstring, "%s %d %d\n", "Ephemeris error b ", i, flag);	    
	    reb_error(sim, outstring);
	}

        for (int j=0; j<N_real; j++){

	    // Compute position vector of test particle j relative to massive body i.
	    const double dx = particles[j].x + (xo - x); 
	    const double dy = particles[j].y + (yo - y);
	    const double dz = particles[j].z + (zo - z);
	    const double r2 = dx*dx + dy*dy + dz*dz;
	    const double _r  = sqrt(r2);
	    const double prefac = GM/(_r*_r*_r);

	    //fprintf(outfile, "%3d %25.16le %25.16le %25.16le %25.16le %25.16le %25.16le %25.16le %25.16le\n", i, t, GM, dx, dy, dz, -prefac*dx, -prefac*dy, -prefac*dz);

	    particles[j].ax -= prefac*dx;
	    particles[j].ay -= prefac*dy;
	    particles[j].az -= prefac*dz;

        }

    }

    //fflush(outfile);	    

    // TODO: keep the positions of the sun and planets, to avoid multiple
    // ephemeris calls.
    // Calculate acceleration of variational particles due to sun and planets

    // Variational equations for direct forces from massive bodies
    // Loop over the perturbers
    for (int i=0; i<N_tot; i++){

        // Get position and mass of massive body i.	
	int flag = all_ephem(i, t, &GM, &x, &y, &z, &vx, &vy, &vz, &ax, &ay, &az);
	if(flag != NO_ERR){
	    char outstring[50];
	    sprintf(outstring, "%s %d %d\n", "Ephemeris error c ", i, flag);	    	    
	    reb_error(sim, outstring);
	}
	
        for (int j=0; j<N_real; j++){ //loop over test particles

	    // This stuff was already computed above.
	    const double dx = particles[j].x + (xo - x);
	    const double dy = particles[j].y + (yo - y);
	    const double dz = particles[j].z + (zo - z);
	    const double r2 = dx*dx + dy*dy + dz*dz;
	    const double _r  = sqrt(r2);
	    const double r3inv = 1./(r2*_r);
	    const double r5inv = 3.*r3inv/r2;

	    // Coefficients for variational equations
	    const double dxdx = dx*dx*r5inv - r3inv;
	    const double dydy = dy*dy*r5inv - r3inv;
	    const double dzdz = dz*dz*r5inv - r3inv;
	    const double dxdy = dx*dy*r5inv;
	    const double dxdz = dx*dz*r5inv;
	    const double dydz = dy*dz*r5inv;

	    // Loop over variational particles
	    // Update the accelerations for the variational
	    // particles that are associated with current
	    // real particle.

	    for (int v=0; v < sim->var_config_N; v++){
		struct reb_variational_configuration const vc = sim->var_config[v];
		int tp = vc.testparticle;
		struct reb_particle* const particles_var1 = particles + vc.index;		
		if(tp == j){
	    
		    // Variational particle coords
		    const double ddx = particles_var1[0].x;
		    const double ddy = particles_var1[0].y;
		    const double ddz = particles_var1[0].z;

		    // Matrix multiplication
		    const double dax =   ddx * dxdx + ddy * dxdy + ddz * dxdz;
		    const double day =   ddx * dxdy + ddy * dydy + ddz * dydz;
		    const double daz =   ddx * dxdz + ddy * dydz + ddz * dzdz;

		    // No variational mass contributions for test particles!

		    // Checked
		    // Accumulate acceleration terms
		    particles_var1[0].ax += GM * dax; 
		    particles_var1[0].ay += GM * day; 
		    particles_var1[0].az += GM * daz; 

		}
	    }
        }
    }

    // We might move this into a somewhat separate part of the code,
    // similar to how different extra forces are typically handled in
    // reboundx
    // 
    // Here is the treatment of the Earth's J2 and J4.
    // Borrowed code from gravitational_harmonics example.
    // Assumes the coordinates are geocentric.
    // Also assuming that Earth's pole is along the z
    // axis.  This is only precisely true at the J2000
    // epoch.
    //

    // The geocenter is the reference for the Earth J2/J4 calculations.
    double xe, ye, ze, vxe, vye, vze, axe, aye, aze;    
    all_ephem(3, t, &GM, &xe, &ye, &ze, &vxe, &vye, &vze, &axe, &aye, &aze);

    double xr, yr, zr, vxr, vyr, vzr, axr, ayr, azr;
    xr = xe;  yr = ye;  zr = ze;

    // Hard-coded constants.  BEWARE!
    const double GMearth = 0.888769244512563400E-09;
    const double J2e =  0.0010826253900;
    const double J4e = -0.000001619898;
    const double au = 149597870.700;
    const double Re_eq = 6378.1366/au;
    // Unit vector to equatorial pole at the epoch
    // Note also that the pole orientation is not changing during
    // the integration.

    //double RAe =  359.87123273*M_PI/180.;
    //double Dece =  89.88809752*M_PI/180.;

    double RAe =  359.87123273*M_PI/180.;
    double Dece =  89.88809752*M_PI/180.;

    RAe =  0.0*M_PI/180.;
    Dece =  90.0*M_PI/180.;

    double cosa = cos(RAe);
    double sina = sin(RAe);
    double cosd = cos(Dece);
    double sind = sin(Dece);
    
    // Rearrange this loop for efficiency
    for (int j=0; j<N_real; j++){

        const struct reb_particle p = particles[j];
        double dx = p.x + (xo - xr);
        double dy = p.y + (yo - yr);
        double dz = p.z + (zo - zr);

        const double r2 = dx*dx + dy*dy + dz*dz;
        const double r = sqrt(r2);
	
	// Rotate to Earth equatorial frame
	double dxp =  - dx*sina      + dy*cosa;
	double dyp =  - dx*cosa*sind - dy*sina*sind + dz*cosd;
	double dzp =    dx*cosa*cosd + dy*sina*cosd + dz*sind;

	dx =  dxp;
	dy =  dyp;
	dz =  dzp;
	
	// Calculate acceleration in
	// Earth equatorial frame	

	// J2 terms
        const double costheta2 = dz*dz/r2;
        const double J2e_prefac = 3.*J2e*Re_eq*Re_eq/r2/r2/r/2.;
        const double J2e_fac = 5.*costheta2-1.;

	double resx = GMearth*J2e_prefac*J2e_fac*dx;
	double resy = GMearth*J2e_prefac*J2e_fac*dy;
	double resz = GMearth*J2e_prefac*(J2e_fac-2.)*dz;

	// J4 terms
        const double J4e_prefac = 5.*J4e*Re_eq*Re_eq*Re_eq*Re_eq/r2/r2/r2/r/8.;
        const double J4e_fac = 63.*costheta2*costheta2-42.*costheta2 + 3.;

        resx += GMearth*J4e_prefac*J4e_fac*dx;
        resy += GMearth*J4e_prefac*J4e_fac*dy;
        resz += GMearth*J4e_prefac*(J4e_fac+12.-28.*costheta2)*dz;

	// Rotate back to original frame
	double resxp = - resx*sina      - resy*cosa*sind + resz*cosa*cosd;
	double resyp =   resx*cosa      - resy*sina*sind + resz*sina*cosd;
	double reszp =                  + resy*cosd      + resz*sind;

	resx =  resxp;
	resy =  resyp;
	resz =  reszp;

	//fprintf(outfile, "%3s %25.16le %25.16le %25.16le %25.16le\n", "J24", t, resx, resy, resz);
	//fflush(outfile);	
	
	// Accumulate final acceleration terms
  	particles[j].ax += resx;
        particles[j].ay += resy; 
        particles[j].az += resz;

	// Constants for variational equations
	// J2 terms
        const double J2e_fac2 = 7.*costheta2-1.;
        const double J2e_fac3 = 35.*costheta2*costheta2-30.*costheta2+3.;

	const double dxdx = GMearth*J2e_prefac*(J2e_fac-5.*J2e_fac2*dx*dx/r2);
	const double dydy = GMearth*J2e_prefac*(J2e_fac-5.*J2e_fac2*dy*dy/r2);
	const double dzdz = GMearth*J2e_prefac*(-1.)*J2e_fac3;
	const double dxdy = GMearth*J2e_prefac*(-5.)*J2e_fac2*dx*dy/r2;
	const double dydz = GMearth*J2e_prefac*(-5.)*(J2e_fac2-2.)*dy*dz/r2;
	const double dxdz = GMearth*J2e_prefac*(-5.)*(J2e_fac2-2.)*dx*dz/r2;

	// J4 terms
        const double J4e_fac2= 33.*costheta2*costheta2-18.*costheta2 + 1.;
        const double J4e_fac3= 33.*costheta2*costheta2-30.*costheta2 + 5.;
        const double J4e_fac4= 231.*costheta2*costheta2*costheta2-315.*costheta2*costheta2+105.*costheta2 - 5.;
	
	const double dxdxJ4 = GMearth*J4e_prefac*(J4e_fac-21.*J4e_fac2*dx*dx/r2);
	const double dydyJ4 = GMearth*J4e_prefac*(J4e_fac-21.*J4e_fac2*dy*dy/r2);
	const double dzdzJ4 = GMearth*J4e_prefac*(-3.)*J4e_fac4;
	const double dxdyJ4 = GMearth*J4e_prefac*(-21.)*J4e_fac2*dx*dy/r2;
	const double dydzJ4 = GMearth*J4e_prefac*(-21.)*J4e_fac3*dy*dz/r2;
	const double dxdzJ4 = GMearth*J4e_prefac*(-21.)*J4e_fac3*dx*dz/r2;

	for (int v=0; v < sim->var_config_N; v++){
	    struct reb_variational_configuration const vc = sim->var_config[v];
	    int tp = vc.testparticle;
	    struct reb_particle* const particles_var1 = particles + vc.index;		
	    if(tp == j){
	    
		// Variational particle coords
		const double ddx = particles_var1[0].x;
		const double ddy = particles_var1[0].y;
		const double ddz = particles_var1[0].z;

		// Rotate to Earth equatorial frame
		double ddxp =  - ddx*sina      + ddy*cosa;
		double ddyp =  - ddx*cosa*sind - ddy*sina*sind + ddz*cosd;
		double ddzp =    ddx*cosa*cosd + ddy*sina*cosd + ddz*sind;

		// Matrix multiplication
		// J2 part
		double dax =   ddxp * dxdx + ddyp * dxdy + ddzp * dxdz;
		double day =   ddxp * dxdy + ddyp * dydy + ddzp * dydz;
		double daz =   ddxp * dxdz + ddyp * dydz + ddzp * dzdz;

		// J4 part		
		dax +=   ddxp * dxdxJ4 + ddyp * dxdyJ4 + ddzp * dxdzJ4;
		day +=   ddxp * dxdyJ4 + ddyp * dydyJ4 + ddzp * dydzJ4;
		daz +=   ddxp * dxdzJ4 + ddyp * dydzJ4 + ddzp * dzdzJ4;

		// Rotate back to original frame
		double daxp = - dax*sina      - day*cosa*sind + daz*cosa*cosd;
		double dayp =   dax*cosa      - day*sina*sind + daz*sina*cosd;
		double dazp =                  + day*cosd      + daz*sind;

		// Accumulate acceleration terms
		particles_var1[0].ax += daxp; 
		particles_var1[0].ay += dayp; 
		particles_var1[0].az += dazp; 
	    
	    }
        }
    }
	
    // We might move this into a somewhat separate part of the code,
    // similar to how different extra forces are typically handled in
    // reboundx
    // Here is the treatment of the Sun's J2.
    // Borrowed code from gravitational_harmonics.

    // The Sun center is reference for these calculations.

    //all_ephem(i, t, &GM, &x, &y, &z, &vx, &vy, &vz, &ax, &ay, &az);	    
    all_ephem(0, t, &GM, &xr, &yr, &zr, &vxr, &vyr, &vzr, &axr, &ayr, &azr);	    

    // Hard-coded constants.  BEWARE!
    // Clean up on aisle 3!
    // Mass of sun in solar masses.    
    const double GMsun = GM;
    const double Rs_eq = 696000.0/au;
    const double J2s =   2.196139151652982e-07;

    double RAs = 286.13*M_PI/180.;
    double Decs = 63.87*M_PI/180.;

    //RAs =  0.0*M_PI/180.;
    //Decs =  90.0*M_PI/180.;

    cosa = cos(RAs);
    sina = sin(RAs);
    cosd = cos(Decs);
    sind = sin(Decs);

    for (int j=0; j<N_real; j++){

        const struct reb_particle p = particles[j];
        double dx = p.x + (xo - xr);
        double dy = p.y + (yo - yr);
        double dz = p.z + (zo - zr);

        const double r2 = dx*dx + dy*dy + dz*dz;
        const double r = sqrt(r2);

	// Rotate to solar equatorial frame
	double dxp =  - dx*sina      + dy*cosa;
	double dyp =  - dx*cosa*sind - dy*sina*sind + dz*cosd;
	double dzp =    dx*cosa*cosd + dy*sina*cosd + dz*sind;

	dx =  dxp;
	dy =  dyp;
	dz =  dzp;

	const double costheta2 = dz*dz/r2;
        const double J2s_prefac = 3.*J2s*Rs_eq*Rs_eq/r2/r2/r/2.;
        const double J2s_fac = 5.*costheta2-1.;
        const double J2s_fac2 = 7.*costheta2-1.;
        const double J2s_fac3 = 35.*costheta2*costheta2-30.*costheta2+3.;

	// Calculate acceleration
	double resx = GMsun*J2s_prefac*J2s_fac*dx;
	double resy = GMsun*J2s_prefac*J2s_fac*dy;
	double resz = GMsun*J2s_prefac*(J2s_fac-2.)*dz;

	// Rotate back to original frame
	double resxp = - resx*sina      - resy*cosa*sind + resz*cosa*cosd;
	double resyp =   resx*cosa      - resy*sina*sind + resz*sina*cosd;
	double reszp =                  + resy*cosd      + resz*sind;

	resx =  resxp;
	resy =  resyp;
	resz =  reszp;

	//fprintf(outfile, "%3s %25.16le %25.16le %25.16le %25.16le\n", "J2", t, resx, resy, resz);
	//fflush(outfile);	

        particles[j].ax += resx;
        particles[j].ay += resy;
        particles[j].az += resz;

	// Constants for variational equations
	// Only evaluate if there are variational particles
	const double dxdx = GMsun*J2s_prefac*(J2s_fac-5.*J2s_fac2*dx*dx/r2);
	const double dydy = GMsun*J2s_prefac*(J2s_fac-5.*J2s_fac2*dy*dy/r2);
	const double dzdz = GMsun*J2s_prefac*(-1.)*J2s_fac3;
	const double dxdy = GMsun*J2s_prefac*(-5.)*J2s_fac2*dx*dy/r2;
	const double dydz = GMsun*J2s_prefac*(-5.)*(J2s_fac2-2.)*dy*dz/r2;
	const double dxdz = GMsun*J2s_prefac*(-5.)*(J2s_fac2-2.)*dx*dz/r2;

	for (int v=0; v < sim->var_config_N; v++){
	    struct reb_variational_configuration const vc = sim->var_config[v];
	    int tp = vc.testparticle;
	    struct reb_particle* const particles_var1 = particles + vc.index;		
	    if(tp == j){
	    
		// Variational particle coords
		double ddx = particles_var1[0].x;
		double ddy = particles_var1[0].y;
		double ddz = particles_var1[0].z;
		
		// Rotate to solar equatorial frame
		double ddxp =  - ddx*sina      + ddy*cosa;
		double ddyp =  - ddx*cosa*sind - ddy*sina*sind + ddz*cosd;
		double ddzp =    ddx*cosa*cosd + ddy*sina*cosd + ddz*sind;

		ddx =  ddxp;
		ddy =  ddyp;
		ddz =  ddzp;
	    
		double daxp =   ddx * dxdx + ddy * dxdy + ddz * dxdz;
		double dayp =   ddx * dxdy + ddy * dydy + ddz * dydz;
		double dazp =   ddx * dxdz + ddy * dydz + ddz * dzdz;

		// Rotate back to original frame
		double dax = - daxp*sina      - dayp*cosa*sind + dazp*cosa*cosd;
		double day =   daxp*cosa      - dayp*sina*sind + dazp*sina*cosd;
		double daz =                  + dayp*cosd      + dazp*sind;

		// Accumulate acceleration terms
		particles_var1[0].ax += dax; 
		particles_var1[0].ay += day; 
		particles_var1[0].az += daz; 

	    }

        } 
       
    }


    // We might move this into a somewhat separate part of the code,
    // similar to how different extra forces are typically handled in
    // reboundx
    // Here is the treatment of non-gravitational forces.
    // This needs to be cleaned up so that the constants are not
    // hard-coded and so that this section is only used if the
    // constants are non-zero.

    // The Sun center is reference for these calculations.
    double xs, ys, zs, vxs, vys, vzs, axs, ays, azs;    
    all_ephem(0, t, &GM, &xs, &ys, &zs, &vxs, &vys, &vzs, &axs, &ays, &azs);
    xr = xs;  yr = ys;  zr = zs;
    vxr = vxs; vyr = vys; vzr = vzs;    

    // The non-grav parameters are specific to each object being
    // integrated.

    // Figure out a good way to pass in the non-grav terms
    
    // Normal asteroids
    double A1 = 0.0;
    double A2 = 0.0;
    double A3 = 0.0;

    // 2020 CD3
    //double A1= 1.903810165823E-10;    
    //double A2 = 0.0;
    //double A3 = 0.0;

    // Apophis
    //double A1 = 0.0;
    //double A2 = -5.592839897872E-14;
    //double A3 = 0.0;

    // 2020 SO
    //double A1 = 2.840852439404E-9; //0.0;
    //double A2 = -2.521527931094E-10;
    //double A3= 2.317289821804E-10;
    
    for (int j=0; j<N_real; j++){

        const struct reb_particle p = particles[j];
        double dx = p.x + (xo - xr);
        double dy = p.y + (yo - yr);
        double dz = p.z + (zo - zr);

        const double r2 = dx*dx + dy*dy + dz*dz;
        const double r = sqrt(r2);

	const double g = 1.0/r2;

	double dvx = p.vx + (vxo - vxr);
	double dvy = p.vy + (vyo - vyr);
	double dvz = p.vz + (vzo - vzr);

	double hx = dy*dvz - dz*dvy;
	double hy = dz*dvx - dx*dvz;
	double hz = dx*dvy - dy*dvx;	

	double h2 = hx*hx + hy*hy + hz*hz;
	double h = sqrt(h2);

	double tx = hy*dz - hz*dy;
	double ty = hz*dx - hx*dz;
	double tz = hx*dy - hy*dx;	
	
        const double t2 = tx*tx + ty*ty + tz*tz;
        const double _t = sqrt(t2);

	//particles[j].ax += A1*g*dx/r + A2*g*tx/_t + A3*g*hx/h;
        //particles[j].ay += A1*g*dy/r + A2*g*ty/_t + A3*g*hy/h;
        //particles[j].az += A1*g*dz/r + A2*g*tz/_t + A3*g*hz/h;

//      variational matrix elements
	// Only evaluate the constants if there are variational particles

        const double r3    = r*r*r;
        const double v2    = dvx*dvx + dvy*dvy + dvz*dvz;
        const double rdotv = dx*dvx  + dy*dvy  + dz*dvz;
        const double vdott = dvx*tx  + dvy*ty  + dvz*tz;

	const double dgdr = -2.*g/r;
        const double dgx  = dgdr*dx/r;
        const double dgy  = dgdr*dy/r;
        const double dgz  = dgdr*dz/r;

        const double hxh3 = hx/(h*h*h);
        const double hyh3 = hy/(h*h*h);
        const double hzh3 = hz/(h*h*h);

        const double txt3 = tx/(_t*_t*_t);
        const double tyt3 = ty/(_t*_t*_t);
        const double tzt3 = tz/(_t*_t*_t);

	const double dxdx = A1*(dgx*dx/r + g*(1./r - dx*dx/r3)) 
	    + A2*(dgx*tx/_t + g*((dx*dvx - rdotv)/_t - txt3*(2.*dx*vdott - rdotv*tx)))
	    + A3*(dgx*hx/h + g*(-hxh3)*(v2*dx - rdotv*dvx));

	const double dydy = A1*(dgy*dy/r + g*(1./r - dy*dy/r3)) 
	    + A2*(dgy*ty/_t + g*((dy*dvy - rdotv)/_t - tyt3*(2.*dy*vdott - rdotv*ty)))
	    + A3*(dgy*hy/h + g*(-hyh3)*(v2*dy - rdotv*dvy));

	const double dzdz = A1*(dgz*dz/r + g*(1./r - dz*dz/r3)) 
	    + A2*(dgz*tz/_t + g*((dz*dvz - rdotv)/_t - tzt3*(2.*dz*vdott - rdotv*tz)))
	    + A3*(dgz*hz/h + g*(-hzh3)*(v2*dz - rdotv*dvz));

	const double dxdy = A1*(dgy*dx/r + g*(-dx*dy/r3))
	    + A2*(dgy*tx/_t + g*((2*dy*dvx - dx*dvy)/_t - txt3*(2*dy*vdott - rdotv*ty)))
	    + A3*(dgy*hx/h + g*(dvz/h -hxh3*(v2*dy - rdotv*dvy)));

	const double dydx = A1*(dgx*dy/r + g*(-dy*dx/r3))
	    + A2*(dgx*ty/_t + g*((2*dx*dvy - dy*dvx)/_t - tyt3*(2*dx*vdott - rdotv*tx)))
	    + A3*(dgx*hy/h + g*(-dvz/h -hyh3*(v2*dx - rdotv*dvx)));

	const double dxdz = A1*(dgz*dx/r + g*(-dx*dz/r3))
	    + A2*(dgz*tx/_t + g*((2*dz*dvx - dx*dvz)/_t - txt3*(2*dz*vdott - rdotv*tz)))
	    + A3*(dgz*hx/h + g*(-dvy/h -hxh3*(v2*dz - rdotv*dvz)));

	const double dzdx = A1*(dgx*dz/r + g*(-dz*dx/r3))
	    + A2*(dgx*tz/_t + g*((2*dx*dvz - dz*dvx)/_t - tzt3*(2*dx*vdott - rdotv*tx)))
	    + A3*(dgx*hz/h + g*(dvy/h -hzh3*(v2*dx - rdotv*dvx)));

	const double dydz = A1*(dgz*dy/r + g*(-dy*dz/r3))
	    + A2*(dgz*ty/_t + g*((2*dz*dvy - dy*dvz)/_t - tyt3*(2*dz*vdott - rdotv*tz)))
	    + A3*(dgz*hy/h + g*(dvx/h -hyh3*(v2*dz - rdotv*dvz)));

	const double dzdy = A1*(dgy*dz/r + g*(-dz*dy/r3))
	    + A2*(dgy*tz/_t + g*((2*dy*dvz - dz*dvy)/_t - tzt3*(2*dy*vdott - rdotv*ty)))
	    + A3*(dgy*hz/h + g*(-dvx/h -hzh3*(v2*dy - rdotv*dvy)));

	const double dxdvx = A1*(0.)
	    + A2*g*((dy*dy + dz*dz)/_t - txt3*r2*tx)
	    + A3*g*(-hxh3*(r2*dvx - dx*rdotv));

 	const double dydvy = A1*(0.)
	    + A2*g*((dx*dx + dz*dz)/_t - tyt3*r2*ty)
	    + A3*g*(-hyh3*(r2*dvy - dy*rdotv));

	const double dzdvz = A1*(0.)
	    + A2*g*((dx*dx + dy*dy)/_t - tzt3*r2*tz)
	    + A3*g*(-hzh3*(r2*dvz - dz*rdotv));

	const double dxdvy = A1*(0.)
	    + A2*g*(-dy*dx/_t - tyt3*r2*tx)
	    + A3*g*(-dz/h - hxh3*(r2*dvy - dy*rdotv));

	const double dydvx = A1*(0.)
	    + A2*g*(-dx*dy/_t - txt3*r2*ty)
	    + A3*g*(dz/h - hyh3*(r2*dvx - dx*rdotv));

	const double dxdvz = A1*(0.)
	    + A2*g*(-dz*dx/_t - tzt3*r2*tx)
	    + A3*g*(dy/h - hxh3*(r2*dvz - dz*rdotv));

	const double dzdvx = A1*(0.)
	    + A2*g*(-dx*dz/_t - txt3*r2*tz)
	    + A3*g*(-dy/h - hzh3*(r2*dvx - dx*rdotv));

	const double dydvz = A1*(0.)
	    + A2*g*(-dz*dy/_t - tzt3*r2*ty)
	    + A3*g*(-dx/h - hyh3*(r2*dvz - dz*rdotv));

	const double dzdvy = A1*(0.)
	    + A2*g*(-dy*dz/_t - tyt3*r2*tz)
	    + A3*g*(dx/h - hzh3*(r2*dvy - dy*rdotv));

	for (int v=0; v < sim->var_config_N; v++){
	    struct reb_variational_configuration const vc = sim->var_config[v];
	    int tp = vc.testparticle;
	    struct reb_particle* const particles_var1 = particles + vc.index;		
	    if(tp == j){
	
		// variational particle coords -- transformed to appropriate coord system.
		double ddx = particles_var1[0].x;// + (xo - xr);
		double ddy = particles_var1[0].y;// + (yo - yr);
		double ddz = particles_var1[0].z;// + (zo - zr);
		double ddvx = particles_var1[0].vx;// + (vxo - vxr);
		double ddvy = particles_var1[0].vy;// + (vyo - vyr);
		double ddvz = particles_var1[0].vz;// + (vzo - vzr);

		// Matrix multiplication
		const double dax =   ddx  * dxdx  + ddy  * dxdy  + ddz  * dxdz
		    +   ddvx * dxdvx + ddvy * dxdvy + ddvz * dxdvz;
		const double day =   ddx  * dydx  + ddy  * dydy  + ddz  * dydz
		    +   ddvx * dydvx + ddvy * dydvy + ddvz * dydvz;
		const double daz =   ddx  * dzdx  + ddy  * dzdy  + ddz  * dzdz
		    +   ddvx * dzdvx + ddvy * dzdvy + ddvz * dzdvz;

		// Accumulate acceleration terms
		//particles_var1[0].ax += dax;
		//particles_var1[0].ay += day;
		//particles_var1[0].az += daz;

	    }
	}
	//  variational end
    }

    // We might move this into a somewhat separate part of the code,
    // similar to how different extra forces are typically handled in
    // reboundx
    
    // Damour and Deruelle solar GR treatment
    // The Sun is the reference for these calculations.
    xr  = xs;  yr  = ys;  zr = zs;
    vxr = vxs; vyr = vys; vzr = vzs;
    axr = axs; ayr = ays; azr = azs;    

    for (int j=0; j<N_real; j++){

        struct reb_particle p = particles[j];
        struct reb_vec3d vi;

	p.x += (xo - xr);
	p.y += (yo - yr);
	p.z += (zo - zr);
	p.vx += (vxo - vxr);
	p.vy += (vyo - vyr);
	p.vz += (vzo - vzr);
	
        const double v2 = p.vx*p.vx + p.vy*p.vy + p.vz*p.vz;
        const double r = sqrt(p.x*p.x + p.y*p.y + p.z*p.z);

	const double A = 4.0*GMsun/r - v2;
	const double B = 4.0*(p.x*p.vx + p.y*p.vy + p.z*p.vz);

	const double prefac = GMsun/(r*r*r*C2);

	particles[j].ax += prefac*(A*p.x + B*p.vx);
	particles[j].ay += prefac*(A*p.y + B*p.vy);
	particles[j].az += prefac*(A*p.z + B*p.vz);

	//fprintf(outfile, "%3s %25.16le %25.16le %25.16le %25.16le\n", "GR", t,
	//prefac*(A*p.x + B*p.vx),
	//prefac*(A*p.y + B*p.vy),
	//prefac*(A*p.z + B*p.vz));
	//fflush(outfile);

	// Constants for variational equations
	// Only evaluate if there are variational particles
	const double dpdr = -3.0*prefac/r;

	// This section can be optimized.
	const double dxdx = dpdr*p.x/r * (A*p.x + B*p.vx) + prefac*(A - p.x*(p.x/r)*4.0*GMsun/(r*r) + 4.0*p.vx*p.vx);
	const double dxdy = dpdr*p.y/r * (A*p.x + B*p.vx) + prefac*(  - p.x*(p.y/r)*4.0*GMsun/(r*r) + 4.0*p.vy*p.vx);
	const double dxdz = dpdr*p.z/r * (A*p.x + B*p.vx) + prefac*(  - p.x*(p.z/r)*4.0*GMsun/(r*r) + 4.0*p.vz*p.vx);
	const double dxdvx =                                prefac*(  - 2.0*p.vx*p.x                + 4.0*p.x*p.vx + B);
	const double dxdvy =                                prefac*(  - 2.0*p.vy*p.x                + 4.0*p.y*p.vx    );
	const double dxdvz =                                prefac*(  - 2.0*p.vz*p.x                + 4.0*p.z*p.vx    );

	// This section can be optimized.	
	const double dydx = dpdr*p.x/r * (A*p.y + B*p.vy) + prefac*(  - p.y*(p.x/r)*4.0*GMsun/(r*r) + 4.0*p.vx*p.vy);
	const double dydy = dpdr*p.y/r * (A*p.y + B*p.vy) + prefac*(A - p.y*(p.y/r)*4.0*GMsun/(r*r) + 4.0*p.vy*p.vy);
	const double dydz = dpdr*p.z/r * (A*p.y + B*p.vy) + prefac*(  - p.y*(p.z/r)*4.0*GMsun/(r*r) + 4.0*p.vz*p.vy);
	const double dydvx =                                prefac*(  - 2.0*p.vx*p.y                + 4.0*p.x*p.vy    );
	const double dydvy =                                prefac*(  - 2.0*p.vy*p.y                + 4.0*p.y*p.vy + B);
	const double dydvz =                                prefac*(  - 2.0*p.vz*p.y                + 4.0*p.z*p.vy    );

	// This section can be optimized.		
	const double dzdx = dpdr*p.x/r * (A*p.z + B*p.vz) + prefac*(  - p.z*(p.x/r)*4.0*GMsun/(r*r) + 4.0*p.vx*p.vz);
	const double dzdy = dpdr*p.y/r * (A*p.z + B*p.vz) + prefac*(  - p.z*(p.y/r)*4.0*GMsun/(r*r) + 4.0*p.vy*p.vz);
	const double dzdz = dpdr*p.z/r * (A*p.z + B*p.vz) + prefac*(A - p.z*(p.z/r)*4.0*GMsun/(r*r) + 4.0*p.vz*p.vz);
	const double dzdvx =                                prefac*(  - 2.0*p.vx*p.z                + 4.0*p.x*p.vz    );
	const double dzdvy =                                prefac*(  - 2.0*p.vy*p.z                + 4.0*p.y*p.vz    );
	const double dzdvz =                                prefac*(  - 2.0*p.vz*p.z                + 4.0*p.z*p.vz + B);

	for (int v=0; v < sim->var_config_N; v++){
	    struct reb_variational_configuration const vc = sim->var_config[v];
	    int tp = vc.testparticle;
	    struct reb_particle* const particles_var1 = particles + vc.index;		
	    if(tp == j){
	    
		// variational particle coords
		const double ddx = particles_var1[0].x;
		const double ddy = particles_var1[0].y;
		const double ddz = particles_var1[0].z;
		const double ddvx = particles_var1[0].vx;
		const double ddvy = particles_var1[0].vy;
		const double ddvz = particles_var1[0].vz;

		// Matrix multiplication
		const double dax =   ddx  * dxdx  + ddy  * dxdy  + ddz  * dxdz
		    +   ddvx * dxdvx + ddvy * dxdvy + ddvz * dxdvz;
		const double day =   ddx  * dydx  + ddy  * dydy  + ddz  * dydz
		    +   ddvx * dydvx + ddvy * dydvy + ddvz * dydvz;
		const double daz =   ddx  * dzdx  + ddy  * dzdy  + ddz  * dzdz
		    +   ddvx * dzdvx + ddvy * dzdvy + ddvz * dzdvz;

		// Accumulate acceleration terms
		particles_var1[0].ax += dax;
		particles_var1[0].ay += day;
		particles_var1[0].az += daz;
		
	    }
	}
    }

    // We might move this into a somewhat separate part of the code,
    // similar to how different extra forces are typically handled in
    // reboundx

    // Einstein-Infeld-Hoffman PPN GR treatment
    // Make this one of two options for GR.
    // This one version is only rarely needed.
    double beta = 1.0;
    double gamma = 1.0;

    for (int i=0; i<N_real; i++){

	double GMj, xj, yj, zj, vxj, vyj, vzj, axj, ayj, azj;    
	double GMk, xk, yk, zk, vxk, vyk, vzk, axk, ayk, azk;

	// Declare and initialize variational terms
	// Only do this if the variational terms are needed.
	double dxdx = 0.0;
	double dxdy = 0.0;
	double dxdz = 0.0;    
	double dxdvx = 0.0;
	double dxdvy = 0.0;
	double dxdvz = 0.0;    
	double dydx = 0.0;
	double dydy = 0.0;
	double dydz = 0.0;    
	double dydvx = 0.0;
	double dydvy = 0.0;
	double dydvz = 0.0;    
	double dzdx = 0.0;
	double dzdy = 0.0;
	double dzdz = 0.0;    
	double dzdvx = 0.0;
	double dzdvy = 0.0;
	double dzdvz = 0.0;    

	double term7x_sum = 0.0;
	double dterm7x_sumdx = 0.0;
	double dterm7x_sumdy = 0.0;
	double dterm7x_sumdz = 0.0;		
	double dterm7x_sumdvx = 0.0;
	double dterm7x_sumdvy = 0.0;
	double dterm7x_sumdvz = 0.0;		

	double term7y_sum = 0.0;
	double dterm7y_sumdx = 0.0;
	double dterm7y_sumdy = 0.0;
	double dterm7y_sumdz = 0.0;		
	double dterm7y_sumdvx = 0.0;
	double dterm7y_sumdvy = 0.0;
	double dterm7y_sumdvz = 0.0;		
	
	double term7z_sum = 0.0;
	double dterm7z_sumdx = 0.0;
	double dterm7z_sumdy = 0.0;
	double dterm7z_sumdz = 0.0;		
	double dterm7z_sumdvx = 0.0;
	double dterm7z_sumdvy = 0.0;
	double dterm7z_sumdvz = 0.0;		
	
	double term8x_sum = 0.0;
	double dterm8x_sumdx = 0.0;
	double dterm8x_sumdy = 0.0;
	double dterm8x_sumdz = 0.0;		

	double term8y_sum = 0.0;
	double dterm8y_sumdx = 0.0;
	double dterm8y_sumdy = 0.0;
	double dterm8y_sumdz = 0.0;		
	
	double term8z_sum = 0.0;
	double dterm8z_sumdx = 0.0;
	double dterm8z_sumdy = 0.0;
	double dterm8z_sumdz = 0.0;		

	double grx = 0.0;
	double gry = 0.0;
	double grz = 0.0;		

	for (int j=0; j<1; j++){	
	    //for (int j=0; j<N_ephem; j++){

	    // Get position and mass of massive body j.
	    all_ephem(j, t, &GMj,
		      &xj, &yj, &zj,
		      &vxj, &vyj, &vzj,
		      &axj, &ayj, &azj);

	    // Compute position vector of test particle i relative to massive body j.
	    const double dxij = particles[i].x + (xo - xj); 
	    const double dyij = particles[i].y + (yo - yj);
	    const double dzij = particles[i].z + (zo - zj);
	    const double rij2 = dxij*dxij + dyij*dyij + dzij*dzij;
	    const double _rij  = sqrt(rij2);
	    const double prefacij = GMj/(_rij*_rij*_rij);

	    const double dprefacijdx = -3.0*GMj/(_rij*_rij*_rij*_rij*_rij)*dxij;
	    const double dprefacijdy = -3.0*GMj/(_rij*_rij*_rij*_rij*_rij)*dyij;
	    const double dprefacijdz = -3.0*GMj/(_rij*_rij*_rij*_rij*_rij)*dzij;

	    // This is the place to do all the various i-j dot products
	    
	    const double vi2 = particles[i].vx*particles[i].vx +
		particles[i].vy*particles[i].vy +
		particles[i].vz*particles[i].vz;

	    const double term2 = gamma/C2*vi2;
	    const double dterm2dvx = 2.0*gamma/C2*particles[i].vx;
	    const double dterm2dvy = 2.0*gamma/C2*particles[i].vy;
	    const double dterm2dvz = 2.0*gamma/C2*particles[i].vz;	    

	    const double vj2 = (vxj-vxo)*(vxj-vxo) + (vyj-vyo)*(vyj-vyo) + (vzj-vzo)*(vzj-vzo);

	    const double term3 = (1+gamma)/C2*vj2;
	    // Variational equations do not depend on term3

	    const double vidotvj = particles[i].vx*(vxj-vxo) +
		particles[i].vy*(vyj-vyo) +
		particles[i].vz*(vzj-vzo);

	    const double term4 = -2*(1+gamma)/C2*vidotvj;
	    const double dterm4dvx = -2*(1+gamma)/C2*(vxj-vxo);
	    const double dterm4dvy = -2*(1+gamma)/C2*(vyj-vyo);
	    const double dterm4dvz = -2*(1+gamma)/C2*(vzj-vzo);	    	    
	    

	    const double rijdotvj = dxij*(vxj-vxo) + dyij*(vyj-vyo) + dzij*(vzj-vzo);

	    //fprintf(eih, " EIH_J%12d\n", j);	    
	    //fprintf(eih, "%25.16lE ", rijdotvj/_rij);

	    const double term5 = -1.5/C2*(rijdotvj*rijdotvj)/(_rij*_rij);
	    const double dterm5dx = -3.0/C2*rijdotvj/_rij*((vxj-vxo)/_rij - rijdotvj*dxij/(_rij*_rij*_rij));
	    const double dterm5dy = -3.0/C2*rijdotvj/_rij*((vyj-vyo)/_rij - rijdotvj*dyij/(_rij*_rij*_rij));
	    const double dterm5dz = -3.0/C2*rijdotvj/_rij*((vzj-vzo)/_rij - rijdotvj*dzij/(_rij*_rij*_rij));	    	    

	    double fx = (2+2*gamma)*particles[i].vx - (1+2*gamma)*(vxj-vxo);
	    double fy = (2+2*gamma)*particles[i].vy - (1+2*gamma)*(vyj-vyo);
	    double fz = (2+2*gamma)*particles[i].vz - (1+2*gamma)*(vzj-vzo);
	    double f = dxij*fx + dyij*fy + dzij*fz;

	    double dfdx = fx;
	    double dfdy = fy;
	    double dfdz = fz;	    	    
	    double dfdvx = dxij*(2+2*gamma);
	    double dfdvy = dyij*(2+2*gamma);
	    double dfdvz = dzij*(2+2*gamma);

	    double term7x = prefacij*f*(particles[i].vx-(vxj-vxo));
	    double term7y = prefacij*f*(particles[i].vy-(vyj-vyo));
	    double term7z = prefacij*f*(particles[i].vz-(vzj-vzo));

	    double dterm7xdx = dprefacijdx * f * (particles[i].vx-(vxj-vxo))
		+ prefacij * dfdx * (particles[i].vx-(vxj-vxo));
	    double dterm7xdy = dprefacijdy * f * (particles[i].vx-(vxj-vxo))
		+ prefacij * dfdy * (particles[i].vx-(vxj-vxo));
	    double dterm7xdz = dprefacijdz * f * (particles[i].vx-(vxj-vxo))
		+ prefacij * dfdz * (particles[i].vx-(vxj-vxo));
	    double dterm7xdvx = prefacij * dfdvx * (particles[i].vx-(vxj-vxo))
		+ prefacij * f;
	    double dterm7xdvy = prefacij * dfdvy * (particles[i].vx-(vxj-vxo));

	    double dterm7xdvz = prefacij * dfdvz * (particles[i].vx-(vxj-vxo));	    

	    double dterm7ydx = dprefacijdx * f * (particles[i].vy-(vyj-vyo))
		+ prefacij * dfdx * (particles[i].vy-(vyj-vyo));
	    double dterm7ydy = dprefacijdy * f * (particles[i].vy-(vyj-vyo))
		+ prefacij * dfdy * (particles[i].vy-(vyj-vyo));
	    double dterm7ydz = dprefacijdz * f * (particles[i].vy-(vyj-vyo))
		+ prefacij * dfdz * (particles[i].vy-(vyj-vyo));
	    double dterm7ydvx = prefacij * dfdvx * (particles[i].vy-(vyj-vyo));

	    double dterm7ydvy = prefacij * dfdvy * (particles[i].vy-(vyj-vyo))
		+ prefacij * f;		
	    double dterm7ydvz = prefacij * dfdvz * (particles[i].vy-(vyj-vyo));	    

	    double dterm7zdx = dprefacijdx * f * (particles[i].vz-(vzj-vzo))
		+ prefacij * dfdx * (particles[i].vz-(vzj-vzo));
	    double dterm7zdy = dprefacijdy * f * (particles[i].vz-(vzj-vzo))
		+ prefacij * dfdy * (particles[i].vz-(vzj-vzo));
	    double dterm7zdz = dprefacijdz * f * (particles[i].vz-(vzj-vzo))
		+ prefacij * dfdz * (particles[i].vz-(vzj-vzo));

	    double dterm7zdvx = prefacij * dfdvx * (particles[i].vz-(vzj-vzo));

	    double dterm7zdvy = prefacij * dfdvy * (particles[i].vz-(vzj-vzo));

	    double dterm7zdvz = prefacij * dfdvz * (particles[i].vz-(vzj-vzo))
		+ prefacij * f;
	    
	    term7x_sum += term7x;
	    term7y_sum += term7y;
	    term7z_sum += term7z;

	    dterm7x_sumdx += dterm7xdx;
	    dterm7x_sumdy += dterm7xdy;
	    dterm7x_sumdz += dterm7xdz;	    
	    dterm7x_sumdvx += dterm7xdvx;
	    dterm7x_sumdvy += dterm7xdvy;
	    dterm7x_sumdvz += dterm7xdvz;	    

	    dterm7y_sumdx += dterm7ydx;
	    dterm7y_sumdy += dterm7ydy;
	    dterm7y_sumdz += dterm7ydz;
	    dterm7y_sumdvx += dterm7ydvx;
	    dterm7y_sumdvy += dterm7ydvy;
	    dterm7y_sumdvz += dterm7ydvz;

	    dterm7z_sumdx += dterm7zdx;
	    dterm7z_sumdy += dterm7zdy;
	    dterm7z_sumdz += dterm7zdz;	    
	    dterm7z_sumdvx += dterm7zdvx;
	    dterm7z_sumdvy += dterm7zdvy;
	    dterm7z_sumdvz += dterm7zdvz;	    
	    

	    double term0 = 0.0;
	    double dterm0dx = 0.0;
	    double dterm0dy = 0.0;
	    double dterm0dz = 0.0;	    
	    double dterm0dvx = 0.0;
	    double dterm0dvy = 0.0;
	    double dterm0dvz = 0.0;	    

	    double term1 = 0.0;
	    double dterm1dx = 0.0;
	    double dterm1dy = 0.0;
	    double dterm1dz = 0.0;	    
	    double dterm1dvx = 0.0;
	    double dterm1dvy = 0.0;
	    double dterm1dvz = 0.0;	    

	    axj = 0.0;
	    ayj = 0.0;
	    azj = 0.0;	    
	    
	    for (int k=0; k<N_ephem; k++){

		// Get position and mass of massive body k.
		all_ephem(k, t, &GMk,
			  &xk, &yk, &zk,
			  &vxk, &vyk, &vzk,
			  &axk, &ayk, &azk);

		// Compute position vector of test particle i relative to massive body k.
		const double dxik = particles[i].x + (xo - xk); 
		const double dyik = particles[i].y + (yo - yk);
		const double dzik = particles[i].z + (zo - zk);
		const double rik2 = dxik*dxik + dyik*dyik + dzik*dzik;
		const double _rik  = sqrt(rik2);

		// keep track of GM/rik sum
		term0 += GMk/_rik;

		dterm0dx -= GMk/(_rik*_rik*_rik) * dxik;
		dterm0dy -= GMk/(_rik*_rik*_rik) * dyik;
		dterm0dz -= GMk/(_rik*_rik*_rik) * dzik;				

		if(k != j){
		    // Compute position vector of massive body j relative to massive body k.
		    const double dxjk = xj - xk;
		    const double dyjk = yj - yk;
		    const double dzjk = zj - zk;
		    const double rjk2 = dxjk*dxjk + dyjk*dyjk + dzjk*dzjk;
		    const double _rjk  = sqrt(rjk2);

		    // keep track of GM/rjk sum
		    term1 += GMk/_rjk;

		    axj -= GMk*dxjk/(_rjk*_rjk*_rjk);
		    ayj -= GMk*dyjk/(_rjk*_rjk*_rjk);
		    azj -= GMk*dzjk/(_rjk*_rjk*_rjk);		    		    

		}

	    }

	    term0 *= -2*(beta+gamma)/C2;
	    dterm0dx *= -2*(beta+gamma)/C2;
	    dterm0dy *= -2*(beta+gamma)/C2;
	    dterm0dz *= -2*(beta+gamma)/C2;	    	    
	    
	    term1 *= -(2*beta-1)/C2;

	    const double rijdotaj = dxij*(axj-axo) + dyij*(ayj-ayo) + dzij*(azj-azo);
	    const double term6 = -0.5/C2*rijdotaj;
	    const double dterm6dx = -0.5/C2*(axj-axo);
	    const double dterm6dy = -0.5/C2*(ayj-ayo);	    
	    const double dterm6dz = -0.5/C2*(azj-azo);
	    
	    double term8x = GMj*axj/_rij*(3+4*gamma)/2;
	    double dterm8xdx = -GMj*axj/(_rij*_rij*_rij)*dxij*(3+4*gamma)/2;
	    double dterm8xdy = -GMj*axj/(_rij*_rij*_rij)*dyij*(3+4*gamma)/2;
	    double dterm8xdz = -GMj*axj/(_rij*_rij*_rij)*dzij*(3+4*gamma)/2;	    	    

	    double term8y = GMj*ayj/_rij*(3+4*gamma)/2;
	    double dterm8ydx = -GMj*ayj/(_rij*_rij*_rij)*dxij*(3+4*gamma)/2;
	    double dterm8ydy = -GMj*ayj/(_rij*_rij*_rij)*dyij*(3+4*gamma)/2;
	    double dterm8ydz = -GMj*ayj/(_rij*_rij*_rij)*dzij*(3+4*gamma)/2;	    	    

	    double term8z = GMj*azj/_rij*(3+4*gamma)/2;
	    double dterm8zdx = -GMj*azj/(_rij*_rij*_rij)*dxij*(3+4*gamma)/2;
	    double dterm8zdy = -GMj*azj/(_rij*_rij*_rij)*dyij*(3+4*gamma)/2;
	    double dterm8zdz = -GMj*azj/(_rij*_rij*_rij)*dzij*(3+4*gamma)/2;	    	    

	    term8x_sum += term8x;
	    term8y_sum += term8y;
	    term8z_sum += term8z;

	    dterm8x_sumdx += dterm8xdx;
	    dterm8x_sumdy += dterm8xdy;
	    dterm8x_sumdz += dterm8xdz;	    

	    dterm8y_sumdx += dterm8ydx;
	    dterm8y_sumdy += dterm8ydy;
	    dterm8y_sumdz += dterm8ydz;

	    dterm8z_sumdx += dterm8zdx;
	    dterm8z_sumdy += dterm8zdy;
	    dterm8z_sumdz += dterm8zdz;	    
	    
	    double factor = term0 + term1 + term2 + term3 + term4 + term5 + term6;

	    double dfactordx = dterm0dx + dterm1dx + dterm5dx + dterm6dx;
	    double dfactordy = dterm0dy + dterm1dy + dterm5dy + dterm6dy;
	    double dfactordz = dterm0dz + dterm1dz + dterm5dz + dterm6dz;	    
	    double dfactordvx = dterm1dvx + dterm2dvx + dterm4dvx;
	    double dfactordvy = dterm1dvy + dterm2dvy + dterm4dvy;
	    double dfactordvz = dterm1dvz + dterm2dvz + dterm4dvz;

	    //fprintf(eih, "%24.16lE ", -factor*C2);
	    //fprintf(eih, "%24.16lE %24.16lE %24.16lE %24.16lE ",
	    //-factor*C2*prefacij*dxij,
	    //-factor*C2*prefacij*dyij,
	    //-factor*C2*prefacij*dzij,
	    //f);	    
	    //fprintf(eih, "%24.16lE %24.16lE %24.16lE ",
	    //prefacij*f*(particles[i].vx-(vxj-vxo)),
	    //prefacij*f*(particles[i].vy-(vyj-vyo)),
	    //prefacij*f*(particles[i].vz-(vzj-vzo)));	    
	    //fprintf(eih, "%24.16lE %24.16lE %24.16lE ",
	    //term8x,
	    //term8y,
	    //term8z);
	    //fprintf(eih, "%24.16lE %24.16lE %24.16lE\n",
	    //axj, ayj, azj);

	    //fflush(eih);

	    grx += -prefacij*dxij*factor;
	    gry += -prefacij*dyij*factor;
	    grz += -prefacij*dzij*factor;
	    
	    //particles[i].ax += -prefacij*dxij*factor;
	    //particles[i].ay += -prefacij*dyij*factor;
	    //particles[i].az += -prefacij*dzij*factor;

	    // Variational equation terms go here.

	    dxdx += -dprefacijdx*dxij*factor
		-prefacij*factor
		-prefacij*dxij*dfactordx;
	    
	    dxdy += -dprefacijdy*dxij*factor
		-prefacij*dxij*dfactordy;
	    
	    dxdz += -dprefacijdz*dxij*factor
		-prefacij*dxij*dfactordz;

	    dxdvx += 
		-prefacij*dxij*dfactordvx;

	    dxdvy += 
		-prefacij*dxij*dfactordvy;

	    dxdvz += 
		-prefacij*dxij*dfactordvz;

	    dydx += -dprefacijdx*dyij*factor
		-prefacij*dyij*dfactordx;
	    
	    dydy += -dprefacijdy*dyij*factor
		-prefacij*factor
		-prefacij*dyij*dfactordy;
	    
	    dydz += -dprefacijdz*dyij*factor
		-prefacij*dyij*dfactordz;

	    dydvx += 
		-prefacij*dyij*dfactordvx;

	    dydvy += 
		-prefacij*dyij*dfactordvy;

	    dydvz += 
		-prefacij*dyij*dfactordvz;
	    
	    dzdx += -dprefacijdx*dzij*factor
		-prefacij*dzij*dfactordx;

	    dzdy += -dprefacijdy*dzij*factor
		-prefacij*dzij*dfactordy;
	    
	    dzdz += -dprefacijdz*dzij*factor
		-prefacij*factor
		-prefacij*dzij*dfactordz;

	    dzdvx += 
		-prefacij*dzij*dfactordvx;

	    dzdvy += 
		-prefacij*dzij*dfactordvy;

	    dzdvz += 
		-prefacij*dzij*dfactordvz;

        }

	grx += term7x_sum/C2 + term8x_sum/C2;
	gry += term7y_sum/C2 + term8y_sum/C2;
	grz += term7z_sum/C2 + term8z_sum/C2;

	//fprintf(outfile, "%3s %25.16le %25.16le %25.16le %25.16le\n", "GR", t,
	//grx, gry, grz);
	//fflush(outfile);

	dxdx += dterm7x_sumdx/C2 + dterm8x_sumdx/C2;
	dxdy += dterm7x_sumdy/C2 + dterm8x_sumdy/C2;
	dxdz += dterm7x_sumdz/C2 + dterm8x_sumdz/C2;	
	dxdvx += dterm7x_sumdvx/C2;
	dxdvy += dterm7x_sumdvy/C2;
	dxdvz += dterm7x_sumdvz/C2;

	dydx += dterm7y_sumdx/C2 + dterm8y_sumdx/C2;
	dydy += dterm7y_sumdy/C2 + dterm8y_sumdy/C2;
	dydz += dterm7y_sumdz/C2 + dterm8y_sumdz/C2;	
	dydvx += dterm7y_sumdvx/C2;
	dydvy += dterm7y_sumdvy/C2;
	dydvz += dterm7y_sumdvz/C2;

	dzdx += dterm7z_sumdx/C2 + dterm8z_sumdx/C2;
	dzdy += dterm7z_sumdy/C2 + dterm8z_sumdy/C2;
	dzdz += dterm7z_sumdz/C2 + dterm8z_sumdz/C2;	
	dzdvx += dterm7z_sumdvx/C2;
	dzdvy += dterm7z_sumdvy/C2;
	dzdvz += dterm7z_sumdvz/C2;
	
	//particles[i].ax += term7x_sum/C2 + term8x_sum/C2;
	//particles[i].ay += term7y_sum/C2 + term8y_sum/C2;
	//particles[i].az += term7z_sum/C2 + term8z_sum/C2;

	// Variational equation terms go here.
	for (int v=0; v < sim->var_config_N; v++){
	    struct reb_variational_configuration const vc = sim->var_config[v];
	    int tp = vc.testparticle;
	    struct reb_particle* const particles_var1 = particles + vc.index;		
	    if(tp == i){
	    
		// variational particle coords
		const double ddx = particles_var1[0].x;
		const double ddy = particles_var1[0].y;
		const double ddz = particles_var1[0].z;
		const double ddvx = particles_var1[0].vx;
		const double ddvy = particles_var1[0].vy;
		const double ddvz = particles_var1[0].vz;

		// Matrix multiplication
		const double dax =   ddx  * dxdx  + ddy  * dxdy  + ddz  * dxdz
		    +   ddvx * dxdvx + ddvy * dxdvy + ddvz * dxdvz;
		const double day =   ddx  * dydx  + ddy  * dydy  + ddz  * dydz
		    +   ddvx * dydvx + ddvy * dydvy + ddvz * dydvz;
		const double daz =   ddx  * dzdx  + ddy  * dzdy  + ddz  * dzdz
		    +   ddvx * dzdvx + ddvy * dzdvy + ddvz * dzdvz;

		// Accumulate acceleration terms
		//particles_var1[0].ax += dax;
		//particles_var1[0].ay += day;
		//particles_var1[0].az += daz;
		
	    }
	}
    }

    // This is for testing the variational equations.
    // It could be moved into a separate section of module.
    double delt = 1e-8;
    for (int j=1; j<N_real; j++){
	double dx = particles[j].ax - particles[0].ax;
	double dy = particles[j].ay - particles[0].ay;
	double dz = particles[j].az - particles[0].az;	
	//fprintf(vfile, "%3d %25.16le %25.16le %25.16le %25.16le\n", j, t, dx/delt, dy/delt, dz/delt);
    }
    //fflush(vfile);	        
    
    for (int j=0; j<N_real; j++){ //loop over test particles
	for (int v=0; v < sim->var_config_N; v++){
	    struct reb_variational_configuration const vc = sim->var_config[v];
	    int tp = vc.testparticle;
	    struct reb_particle* const particles_var1 = particles + vc.index;		
	    if(tp == j){
		//fprintf(vfile, "%3d %25.16le %25.16le %25.16le %25.16le\n",
		//j, t, particles_var1[0].ax, particles_var1[0].ay, particles_var1[0].az);		
	    }
	}
    }

    //fclose(eih);
    
    if(*geo == 1){
	// geocentric
	// This part will need work for the variational equations to work.
	all_ephem(3, t, &GM, &xo, &yo, &zo, &vxo, &vyo, &vzo, &axo, &ayo, &azo);

	//printf("%lf %le %le %le geo\n", t, axo, ayo, azo);

	// This is the indirect term for geocentric equations
	// of motion.
	for (int j=0; j<N_real; j++){    

	    printf("%lf %le %le %le\n", t, particles[j].ax, particles[j].ay, particles[j].az);	    
	    particles[j].ax -= axo;
	    particles[j].ay -= ayo;
	    particles[j].az -= azo;

	}
    }
    //printf("here %le\n", t);
    //fflush(stdout);

    //fclose(vfile);    
    
    //fflush(outfile);
    //fclose(outfile);
}

/**
 * @brief Struct containing pointers to intermediate values
 */
struct reb_dpconst7 {
    double* const restrict p0;  ///< Temporary values at intermediate step 0 
    double* const restrict p1;  ///< Temporary values at intermediate step 1 
    double* const restrict p2;  ///< Temporary values at intermediate step 2 
    double* const restrict p3;  ///< Temporary values at intermediate step 3 
    double* const restrict p4;  ///< Temporary values at intermediate step 4 
    double* const restrict p5;  ///< Temporary values at intermediate step 5 
    double* const restrict p6;  ///< Temporary values at intermediate step 6 
};

static struct reb_dpconst7 dpcast(struct reb_dp7 dp){
    struct reb_dpconst7 dpc = {
        .p0 = dp.p0, 
        .p1 = dp.p1, 
        .p2 = dp.p2, 
        .p3 = dp.p3, 
        .p4 = dp.p4, 
        .p5 = dp.p5, 
        .p6 = dp.p6, 
    };
    return dpc;
}

typedef struct {
  double t, x, y, z, vx, vy, vz, ax, ay, az;
} tstate;

typedef struct {
    double* t;
    double* state;
    tstate* last_state;
    int n_alloc;
    int n_particles;
} timestate;

// Gauss Radau spacings
//static const double h[9]    = { 0.0, 0.0562625605369221464656521910318, 0.180240691736892364987579942780, 0.352624717113169637373907769648, 0.547153626330555383001448554766, 0.734210177215410531523210605558, 0.885320946839095768090359771030, 0.977520613561287501891174488626, 1.0};

//static const double hg[9]   =   { 0.0, 0.0125360439090882, 0.1090842587659852, 0.2830581304412210, 0.5000000000000000, 0.7169418695587790, 0.8909157412340150, 0.9874639560909118, 1.0};

//static const double hg[9]   =   { 0.0, 0.125, 0.250, 0.375, 0.500, 0.625, 0.750, 0.875, 1.0};

static const double hg[11]   =   { 0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};


void heartbeat(struct reb_simulation* r);

// integration_function
// tstart: integration start time in tdb
// tstep: suggested initial time step (days)
// trange: amount of time to integrate (days)
// geocentric: 1==geocentric equations of motion, 0==heliocentric
// n_particles: number of input test particles
// instate: input states of test particles
// ts: output times and states.

int integration_function(double tstart, double tend, double tstep,
			 int geocentric,
			 double epsilon,
			 int n_particles,
			 double* instate,
			 int n_var,
			 int* invar_part,			 
			 double* invar,
			 int n_alloc,			 
			 int *n_out,
			 double* outtime,
			 double* outstate){			 

    struct reb_simulation* r = reb_create_simulation();

    // Set up simulation constants
    // The gravitational constant should be set using the ephemeris routines,
    // so that it is ensured to consistent with the units used in those routines.
    r->G = 0.295912208285591100E-03; // Gravitational constant (AU, solar masses, days)

    r->integrator = REB_INTEGRATOR_IAS15;
    r->save_messages = 1;
    r->heartbeat = heartbeat;
    r->display_data = NULL;
    r->collision = REB_COLLISION_NONE;  // This is important and needs to be considered carefully.
    r->collision_resolve = reb_collision_resolve_merge; // Not sure what this is for.
    r->gravity = REB_GRAVITY_NONE;

    // These quantities are specific to IAS15.  Perhaps something more flexible could
    // be done so that other REBOUND integration routines could be explored.

    // Don't hard code this.
    r->ri_ias15.min_dt = 1e-2;  // to avoid very small time steps
    //r->ri_ias15.min_dt = 1e-4;  // to avoid very small time steps    
    r->ri_ias15.epsilon = epsilon; // to avoid convergence issue with geocentric orbits    

    // This should be flexible.
    r->exact_finish_time = 1;
    //r->exact_finish_time = 0;
    
    struct rebx_extras* rebx = rebx_attach(r);

    // Here is where the components (direct n-body, GR, Earth J2/J4, Solar J2,
    // non-gravs, etc) could be separately loaded.

    // Also add "ephemeris_forces" 
    struct rebx_force* ephem_forces = rebx_load_force(rebx, "ephemeris_forces");
    rebx_add_force(rebx, ephem_forces);

    rebx_set_param_int(rebx, &ephem_forces->ap, "geocentric", geocentric);

    // Set speed of light in right units (set by G & initial conditions).
    // The speed of light should be set using the ephemeris routines,
    // so that it is ensured to consistent with the units used in those routines.

    rebx_set_param_double(rebx, &ephem_forces->ap, "c", 173.14463267424031);

    timestate *ts = (timestate*) malloc(sizeof(timestate)); 

    ts->t = outtime;
    ts->state = outstate;

    rebx_set_param_pointer(rebx, &ephem_forces->ap, "timestate", ts);

    // Add and initialize particles    
    for(int i=0; i<n_particles; i++){

	struct reb_particle tp = {0};

	tp.x  =  instate[6*i+0];
	tp.y  =  instate[6*i+1];
	tp.z  =  instate[6*i+2];
	tp.vx =  instate[6*i+3];
	tp.vy =  instate[6*i+4];
	tp.vz =  instate[6*i+5];

	reb_add(r, tp);
    }

    // Add and initialize variational particles

    for(int i=0; i<n_var; i++){

	// invar_part[i] contains the index of the test particle that we vary.	
        int var_i = reb_add_var_1st_order(r, invar_part[i]);
	
        r->particles[var_i].x =  invar[6*i+0]; 
        r->particles[var_i].y =  invar[6*i+1]; 
        r->particles[var_i].z =  invar[6*i+2]; 
        r->particles[var_i].vx = invar[6*i+3]; 
        r->particles[var_i].vy = invar[6*i+4]; 
        r->particles[var_i].vz = invar[6*i+5]; 
 
    }

    int N = r->N; // N includes real+variational particles

    r->t = tstart;    // set simulation internal time to the time of test particle initial conditions.
    r->dt = tstep;    // time step in days, this is just an initial value.  It probably does not need
                      // to be passed in.

    ts->n_particles = n_particles;
    ts->n_alloc = n_alloc;

    tstate* last_state = (tstate*) malloc(N*sizeof(tstate));

    ts->last_state = last_state;

    /*
    const double dtsign = copysign(1.,r->dt);   // Used to determine integration direction
    while((r->t)*dtsign<tend*dtsign){ 
	reb_integrate(r, r->t + r->dt);
    }
    */

    reb_integrate(r, tend);

    if (r->messages){
	printf("error\n");
	fflush(stdout);
	for(int i=0; i<reb_max_messages_N; i++){
	    printf("mess: %d", i);
	    fflush(stdout);
	    if(r->messages[i] != NULL){
		printf("%d %s", i, r->messages[i]);
		printf("blah\n");
		fflush(stdout);
	    }
	}
    }

    *n_out = r->steps_done;

    int status = r->status;

    // explicitly free all the memory allocated by REBOUNDx
    rebx_set_param_pointer(rebx, &ephem_forces->ap, "timestate", NULL);
    //rebx_set_param_pointer(rebx, &ephem_forces->ap, "last_state", NULL);
    ts->t = NULL;
    ts->state = NULL;
    ts->last_state = NULL;    
    free(ts);
    free(last_state);
    reb_free_simulation(r);
    rebx_free(rebx);

    return(status);
}

void heartbeat(struct reb_simulation* r){

    void store_function(struct reb_simulation* r);
    void store_last_state(struct reb_simulation* r);    

    store_function(r);

    reb_update_acceleration(r);

    store_last_state(r);

}

void store_last_state(struct reb_simulation* r){

    struct rebx_force* ephem_forces = rebx_get_force(r->extras, "ephemeris_forces");
    timestate* ts = rebx_get_param(r->extras, ephem_forces->ap, "timestate");
    tstate* last_state = ts->last_state;

    int N = r->N;    
    for(int j=0; j<N; j++){ 
	last_state[j].t = r->t;	
	last_state[j].x = r->particles[j].x;
	last_state[j].y = r->particles[j].y;
	last_state[j].z = r->particles[j].z;
	last_state[j].vx = r->particles[j].vx;
	last_state[j].vy = r->particles[j].vy;
	last_state[j].vz = r->particles[j].vz;
	last_state[j].ax = r->particles[j].ax;
	last_state[j].ay = r->particles[j].ay;
	last_state[j].az = r->particles[j].az;
    }
}

// This function is doing two related things:
// 1. Calculating the positions and velocities at the substeps
// 2. Storing the times and positions/velocities in the arrays
//    that are provided.
// For this to work, we need:
// * the last valid state for all particles,
// * the b coefficients for all the particles,
// * the last time step
//
// We need to adjust this so that it stores the positions
// and velocities at the substeps and the final computed
// state, rather than the previous computed state and
// the values at the substeps.

void store_function(struct reb_simulation* r){
    int N = r->N;
    int N3 = 3*N;

    static int last_steps_done = 0;

    double s[9]; // Summation coefficients

    struct rebx_force* ephem_forces = rebx_get_force(r->extras, "ephemeris_forces");
    timestate* ts = rebx_get_param(r->extras, ephem_forces->ap, "timestate");
    tstate* last_state = ts->last_state;    

    static double* outtime;
    static double* outstate;

    int n_alloc;

    int step = r->steps_done;

    outtime = ts->t;
    outstate = ts->state;
    n_alloc= ts->n_alloc;

    if(step==0){

	int state_offset = 0;
	int time_offset = 0;
	
	outtime[time_offset++] = r->t;

	for(int j=0; j<N; j++){
	    last_state[j].t = r->t;	
	    outstate[state_offset++] = r->particles[j].x;
	    outstate[state_offset++] = r->particles[j].y;
	    outstate[state_offset++] = r->particles[j].z;
	    outstate[state_offset++] = r->particles[j].vx;
	    outstate[state_offset++] = r->particles[j].vy;
	    outstate[state_offset++] = r->particles[j].vz;
	}

    }else if(r->steps_done > last_steps_done){

	// Convenience variable.  The 'br' field contains the 
	// set of coefficients from the last completed step.
	const struct reb_dpconst7 b  = dpcast(r->ri_ias15.br);

	double* x0 = malloc(sizeof(double)*N3);
	double* v0 = malloc(sizeof(double)*N3);
	double* a0 = malloc(sizeof(double)*N3);

	for(int j=0;j<N;j++) {

	    const int k0 = 3*j+0;
	    const int k1 = 3*j+1;
	    const int k2 = 3*j+2;

	    x0[k0] = last_state[j].x;
	    x0[k1] = last_state[j].y;
	    x0[k2] = last_state[j].z;

	    v0[k0] = last_state[j].vx;
	    v0[k1] = last_state[j].vy;
	    v0[k2] = last_state[j].vz;	

	    a0[k0] = last_state[j].ax;
	    a0[k1] = last_state[j].ay;
	    a0[k2] = last_state[j].az;

	}

	//int time_offset = (step-1)*8+1;
	//int time_offset = (step-1)*10+1;
	int nsub = 10;
	int time_offset = (step-1)*nsub+1;		

	// Loop over intervals using Gauss-Radau spacings      
	//for(int n=1;n<9;n++) {
	//for(int n=1;n<11;n++) {
	for(int n=1;n<(nsub+1);n++) {	    

	    // The hg[n] values here define the substeps used in the
	    // the integration, but they could be any values.
	    // A natural alternative would be the Chebyshev nodes.
	    // The degree would be altered, but the value would need
	    // to be included in the output.
	    // Another approach would be to output the ingredients for
	    // the evaluations below.
	    // x0, v0, a0, and 7 coefficients for each component
	    // This has the advantage of providing a complete state
	    // plus the accelerations at intervals.
	    
	    s[0] = r->dt_last_done * hg[n];

	    s[1] = s[0] * s[0] / 2.;
	    s[2] = s[1] * hg[n] / 3.;
	    s[3] = s[2] * hg[n] / 2.;
	    s[4] = 3. * s[3] * hg[n] / 5.;
	    s[5] = 2. * s[4] * hg[n] / 3.;
	    s[6] = 5. * s[5] * hg[n] / 7.;
	    s[7] = 3. * s[6] * hg[n] / 4.;
	    s[8] = 7. * s[7] * hg[n] / 9.;

	    double t = r->t + r->dt_last_done * (-1.0 + hg[n]);

	    outtime[time_offset++] = t;	

	    // Predict positions at interval n using b values
	    // for all the particles
	    for(int j=0;j<N;j++) {  
		//int mj = j;
		const int k0 = 3*j+0;
		const int k1 = 3*j+1;
		const int k2 = 3*j+2;

		double xx0 = x0[k0] + (s[8]*b.p6[k0] + s[7]*b.p5[k0] + s[6]*b.p4[k0] + s[5]*b.p3[k0] + s[4]*b.p2[k0] + s[3]*b.p1[k0] + s[2]*b.p0[k0] + s[1]*a0[k0] + s[0]*v0[k0] );
		double xy0 = x0[k1] + (s[8]*b.p6[k1] + s[7]*b.p5[k1] + s[6]*b.p4[k1] + s[5]*b.p3[k1] + s[4]*b.p2[k1] + s[3]*b.p1[k1] + s[2]*b.p0[k1] + s[1]*a0[k1] + s[0]*v0[k1] );
		double xz0 = x0[k2] + (s[8]*b.p6[k2] + s[7]*b.p5[k2] + s[6]*b.p4[k2] + s[5]*b.p3[k2] + s[4]*b.p2[k2] + s[3]*b.p1[k2] + s[2]*b.p0[k2] + s[1]*a0[k2] + s[0]*v0[k2] );

		// Store the results
		//int offset = ((step-1)*8 + n)*6*N + 6*j;
		//int offset = ((step-1)*10 + n)*6*N + 6*j;
		int offset = ((step-1)*nsub + n)*6*N + 6*j;
		outstate[offset+0] = xx0;
		outstate[offset+1] = xy0;	  	  
		outstate[offset+2] = xz0;
	    }

	    s[0] = r->dt_last_done * hg[n];
	    s[1] =      s[0] * hg[n] / 2.;
	    s[2] = 2. * s[1] * hg[n] / 3.;
	    s[3] = 3. * s[2] * hg[n] / 4.;
	    s[4] = 4. * s[3] * hg[n] / 5.;
	    s[5] = 5. * s[4] * hg[n] / 6.;
	    s[6] = 6. * s[5] * hg[n] / 7.;
	    s[7] = 7. * s[6] * hg[n] / 8.;

	    // Predict velocities at interval n using b values
	    // for all the particles
	    for(int j=0;j<N;j++) {

		const int k0 = 3*j+0;
		const int k1 = 3*j+1;
		const int k2 = 3*j+2;

		double vx0 = v0[k0] + s[7]*b.p6[k0] + s[6]*b.p5[k0] + s[5]*b.p4[k0] + s[4]*b.p3[k0] + s[3]*b.p2[k0] + s[2]*b.p1[k0] + s[1]*b.p0[k0] + s[0]*a0[k0];
		double vy0 = v0[k1] + s[7]*b.p6[k1] + s[6]*b.p5[k1] + s[5]*b.p4[k1] + s[4]*b.p3[k1] + s[3]*b.p2[k1] + s[2]*b.p1[k1] + s[1]*b.p0[k1] + s[0]*a0[k1];
		double vz0 = v0[k2] + s[7]*b.p6[k2] + s[6]*b.p5[k2] + s[5]*b.p4[k2] + s[4]*b.p3[k2] + s[3]*b.p2[k2] + s[2]*b.p1[k2] + s[1]*b.p0[k2] + s[0]*a0[k2];

		// Store the results
		//int offset = ((step-1)*8 + n)*6*N + 6*j;
		//int offset = ((step-1)*10 + n)*6*N + 6*j;
		int offset = ((step-1)*nsub + n)*6*N + 6*j;				
		outstate[offset+3] = vx0;
		outstate[offset+4] = vy0;	  	  
		outstate[offset+5] = vz0;

	    }
	}

	free(x0);
	free(v0);
	free(a0);
    }
    last_steps_done = r->steps_done;

    if((ts->n_alloc-step) < 1){
	r->status = REB_EXIT_USER;
	return;
    }

}


