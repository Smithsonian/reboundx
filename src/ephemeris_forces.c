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
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "rebound.h"
#include "reboundx.h"

#include "spk.h"
#include "planets.h"

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
static void ephem(const int i, const double jde, double* const GM,
	   double* const x, double* const y, double* const z,
	   double* const vx, double* const vy, double* const vz,
	   double* const ax, double* const ay, double* const az){

    static int initialized = 0;

    static struct _jpl_s *pl;
    struct mpos_s now;

    // The values below are G*mass.
    // Units are solar masses, au, days.
    // The units should probably be handled elsewhere.
    const static double JPL_GM[11] =
	{
	    0.295912208285591100E-03, // 0  sun  
	    0.491248045036476000E-10, // 1  mercury
	    0.724345233264412000E-09, // 2  venus
	    0.888769244512563400E-09, // 3  earth
	    0.109318945074237400E-10, // 4  moon
	    0.954954869555077000E-10, // 5  mars
	    0.282534584083387000E-06, // 6  jupiter
	    0.845970607324503000E-07, // 7  saturn
	    0.129202482578296000E-07, // 8  uranus
	    0.152435734788511000E-07, // 9  neptune
	    0.217844105197418000E-11, // 10 pluto
	};

    if(i<0 || i>10){
      fprintf(stderr, "body out of range\n");
      exit(EXIT_FAILURE);
    }

    if (initialized == 0){

      if ((pl = jpl_init()) == NULL) {
	fprintf(stderr, "could not load DE430 file\n");
	exit(EXIT_FAILURE);
      }

      initialized = 1;

    }

    // Get position, velocity, and mass of body i in barycentric coords. 
    
    *GM = JPL_GM[i];

    jpl_calc(pl, &now, jde, ebody[i], PLAN_BAR); 

    // Convert to au/day and au/day^2
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
    
}

static void ast_ephem(const int i, const double jde, double* const GM, double* const x, double* const y, double* const z){

    static int initialized = 0;

    static struct spk_s *spl;
    struct mpos_s pos;

    // 1 Ceres, 4 Vesta, 2 Pallas, 10 Hygiea, 31 Euphrosyne, 704 Interamnia,
    // 511 Davida, 15 Eunomia, 3 Juno, 16 Psyche, 65 Cybele, 88 Thisbe, 
    // 48 Doris, 52 Europa, 451 Patientia, 87 Sylvia

    // The values below are G*mass.
    // Units are solar masses, au, days.      
    const static double JPL_GM[16] =
	{
	    1.400476556172344e-13, // ceres
	    3.854750187808810e-14, // vesta
	    3.104448198938713e-14, // pallas
	    1.235800787294125e-14, // hygiea
	    6.343280473648602e-15, // euphrosyne
	    5.256168678493662e-15, // interamnia
	    5.198126979457498e-15, // davida
	    4.678307418350905e-15, // eunomia
	    3.617538317147937e-15, // juno
	    3.411586826193812e-15, // psyche
	    3.180659282652541e-15, // cybele
	    2.577114127311047e-15, // thisbe
	    2.531091726015068e-15, // doris
	    2.476788101255867e-15, // europa
	    2.295559390637462e-15, // patientia
	    2.199295173574073e-15, // sylvia
	};
    

    if(i<0 || i>15){
      fprintf(stderr, "asteroid out of range\n");
      exit(EXIT_FAILURE);
    }

    if (initialized == 0){
      
      if ((spl = spk_init("sb431-n16s.bsp")) == NULL) {
	fprintf(stderr, "could not load sb431-n16 file\n");
	exit(EXIT_FAILURE);
      }

      
      initialized = 1;

    }

    fflush(stdout);
    *GM = JPL_GM[i];
    spk_calc(spl, i, jde, &pos);          
    *x = pos.u[0];
    *y = pos.u[1];
    *z = pos.u[2];
    fflush(stdout);    
    
}

int number_bodies(int* N_ephem, int* N_ast){
    *N_ephem = 11;
    *N_ast = 16;

    return(*N_ephem + *N_ast);
}

static void all_ephem(const int i, const double t, double* const GM,
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
    // for 27 bodies at 8 different times.
    
    // Get position and mass of massive body i.
    if(i < N_ephem){
	ephem(i, t, GM, x, y, z, vx, vy, vz, ax, ay, az);
    }else{
	// Get position and mass of asteroid i-N_ephem.
	ast_ephem(i-N_ephem, t, GM, x, y, z);

	if(t != t_last){
	    ephem(0, t, &GMs, &xs, &ys, &zs, &vxs, &vys, &vzs, &axs, &ays, &azs);
	    t_last = t;
	}

	// Translate massive asteroids from heliocentric to barycentric.
	*x += xs; *y += ys; *z += zs;
	*vx = NAN; *vy = NAN; *vz = NAN;
	*ax = NAN; *ay = NAN; *az = NAN;		
    }
    
}

void rebx_ephemeris_forces(struct reb_simulation* const sim, struct rebx_force* const force, struct reb_particle* const particles, const int N){

    const double G = sim->G;
    const double t = sim->t;
    const int N_var = sim->N_var;
    const int N_real= N;

    const double dt = sim->dt;
    const double last_dt = sim->dt_last_done;

    int N_ephem, N_ast;
    int number_bodies(int* N_ephem, int* N_ast);

    const int N_tot = number_bodies(&N_ephem, &N_ast);

    const double* c = rebx_get_param(sim->extras, force->ap, "c");
    if (c == NULL){
        reb_error(sim, "REBOUNDx Error: Need to set speed of light in gr effect.  See examples in documentation.\n");
        return;
    }

    int* geo = rebx_get_param(sim->extras, force->ap, "geocentric"); // Make sure there is a default set.
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

    if(*geo == 1){
	// geocentric
	all_ephem(3, t, &GM, &xo, &yo, &zo, &vxo, &vyo, &vzo, &axo, &ayo, &azo);

	// This is the indirect term for geocentric equations
	// of motion.
	for (int j=0; j<N_real; j++){    

	    particles[j].ax -= axo;
	    particles[j].ay -= ayo;
	    particles[j].az -= azo;

	}
	
    }else{
	// barycentric
	xo = 0.0;  yo = 0.0;  zo = 0.0;
	vxo = 0.0; vyo = 0.0; vzo = 0.0;      
    }

    for (int i=0; i<N_tot; i++){

        // Get position and mass of massive body i.
	all_ephem(i, t, &GM, &x, &y, &z, &vx, &vy, &vz, &ax, &ay, &az);

        for (int j=0; j<N_real; j++){

	    // Compute position vector of test particle j relative to massive body i.
	    const double dx = particles[j].x + (xo - x); 
	    const double dy = particles[j].y + (yo - y);
	    const double dz = particles[j].z + (zo - z);
	    const double r2 = dx*dx + dy*dy + dz*dz;
	    const double _r  = sqrt(r2);
	    const double prefac = GM/(_r*_r*_r);

	    // Values and cooefficients for variational equations
	    const double r3inv = 1./(r2*_r);
	    const double r5inv = 3.*r3inv/r2;

	    const double dxdx = dx*dx*r5inv - r3inv;
	    const double dydy = dy*dy*r5inv - r3inv;
	    const double dzdz = dz*dz*r5inv - r3inv;
	    const double dxdy = dx*dy*r5inv;
	    const double dxdz = dx*dz*r5inv;
	    const double dydz = dy*dz*r5inv;

	    particles[j].ax -= prefac*dx;
	    particles[j].ay -= prefac*dy;
	    particles[j].az -= prefac*dz;

        }
    }
    
    // Calculate acceleration of variational particles due to sun and planets
    for (int i=0; i<N_tot; i++){

        // Get position and mass of massive body i.	
	all_ephem(i, t, &GM, &x, &y, &z, &vx, &vy, &vz, &ax, &ay, &az);	
	
        for (int j=0; j<N_real; j++){ //loop over test particles

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
		if(tp == j){
	    
		    // Variational particle coords
		    const double ddx = particles[v].x;
		    const double ddy = particles[v].y;
		    const double ddz = particles[v].z;
		    const double Gmi = GM;

		    // Matrix multiplication
		    const double dax =   ddx * dxdx + ddy * dxdy + ddz * dxdz;
		    const double day =   ddx * dxdy + ddy * dydy + ddz * dydz;
		    const double daz =   ddx * dxdz + ddy * dydz + ddz * dzdz;

		    // No variational mass contributions for test particles!

		    // Accumulate acceleration terms
		    particles[v].ax += Gmi * dax; 
		    particles[v].ay += Gmi * day; 
		    particles[v].az += Gmi * daz; 

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
    // Clean up on aisle 3!
    const double GMearth = 0.888769244512563400E-09;
    const double J2e = 0.00108262545;
    const double J4e = -0.000001616;
    const double au = 149597870.700;
    const double Re_eq = 6378.1263/au;
    // Unit vector to equatorial pole at the epoch
    // Clean this up!
    // Note also that the pole orientation is not changing during
    // the integration.

    double RAs =  359.87123273*M_PI/180.;
    double Decs =  89.88809752*M_PI/180.;

    //double xp = cos(Decs)*cos(RAs);
    //double yp = cos(Decs)*sin(RAs);
    //double zp = sin(Decs);

    double xp =  0.0019111736356920146;
    double yp = -1.2513100974355823e-05;
    double zp =   0.9999981736277104;

    //double xp =  0.0;
    //double yp =  0.0;
    //double zp =  1.0;
    
    double incl = acos(zp);
    double longnode;
    if(xp != 0.0 || yp !=0.0) {    
      longnode = atan2(xp, -yp);
    } else {
      longnode = 0.0;
    }

    // Rearrange this loop for efficiency
    for (int j=0; j<N_real; j++){

        const struct reb_particle p = particles[j];
        double dx = p.x + (xo - xr);
        double dy = p.y + (yo - yr);
        double dz = p.z + (zo - zr);

        const double r2 = dx*dx + dy*dy + dz*dz;
        const double r = sqrt(r2);
	
	// Rotate to Earth equatorial frame
	// This could be a single rotation

	// Rotate around z by RA
	double cosr = cos(-longnode);
	double sinr = sin(-longnode);

	double dxp =  dx * cosr - dy * sinr;
	double dyp =  dx * sinr + dy * cosr;
	double dzp =  dz;

	// Rotate around x by Dec
	double cosd = cos(-incl);
	double sind = sin(-incl);
	
	dx =  dxp;
	dy =  dyp * cosd - dzp * sind;
	dz =  dyp * sind + dzp * cosd;

	// Calculate acceleration in
	// Earth equatorial frame	

	// J2 terms
        const double costheta2 = dz*dz/r2;
        const double J2e_prefac = 3.*J2e*Re_eq*Re_eq/r2/r2/r/2.;
        const double J2e_fac = 5.*costheta2-1.;
        const double J2e_fac2 = 7.*costheta2-1.;
        const double J2e_fac3 = 35.*costheta2*costheta2-30.*costheta2+3.;

	double resx = GMearth*J2e_prefac*J2e_fac*dx;
	double resy = GMearth*J2e_prefac*J2e_fac*dy;
	double resz = GMearth*J2e_prefac*(J2e_fac-2.)*dz;	

	// J4 terms
        const double J4e_prefac = 5.*J4e*Re_eq*Re_eq*Re_eq*Re_eq/r2/r2/r2/r/8.;
        const double J4e_fac = 63.*costheta2*costheta2-42.*costheta2 + 3.;
        const double J4e_fac2= 33.*costheta2*costheta2-18.*costheta2 + 1.;
        const double J4e_fac3= 33.*costheta2*costheta2-30.*costheta2 + 5.;
        const double J4e_fac4= 231.*costheta2*costheta2*costheta2-315.*costheta2*costheta2+105.*costheta2 - 5.;

        resx += GMearth*J4e_prefac*J4e_fac*dx;
        resy += GMearth*J4e_prefac*J4e_fac*dy;
        resz += GMearth*J4e_prefac*(J4e_fac+12.-28.*costheta2)*dz;

	// Rotate back to original frame
	// Rotate around x by -Dec
	double resxp =  resx;
	double resyp =  resy * cosd + resz * sind;
	double reszp = -resy * sind + resz * cosd;
	
	// Rotate around z by -RA
	resx =  resxp * cosr + resyp * sinr;
	resy = -resxp * sinr + resyp * cosr;
	resz =  reszp;

	// Accumulate final acceleration terms
  	particles[j].ax += resx;
        particles[j].ay += resy; 
        particles[j].az += resz;

	// Constants for variational equations
	// J2 terms
	const double dxdx = GMearth*J2e_prefac*(J2e_fac-5.*J2e_fac2*dx*dx/r2);
	const double dydy = GMearth*J2e_prefac*(J2e_fac-5.*J2e_fac2*dy*dy/r2);
	const double dzdz = GMearth*J2e_prefac*(-1.)*J2e_fac3;
	const double dxdy = GMearth*J2e_prefac*(-5.)*J2e_fac2*dx*dy/r2;
	const double dydz = GMearth*J2e_prefac*(-5.)*(J2e_fac2-2.)*dy*dz/r2;
	const double dxdz = GMearth*J2e_prefac*(-5.)*(J2e_fac2-2.)*dx*dz/r2;
	// J4 terms
	const double dxdxJ4 = GMearth*J4e_prefac*(J4e_fac-21.*J4e_fac2*dx*dx/r2);
	const double dydyJ4 = GMearth*J4e_prefac*(J4e_fac-21.*J4e_fac2*dy*dy/r2);
	const double dzdzJ4 = GMearth*J4e_prefac*(-3.)*J4e_fac4;
	const double dxdyJ4 = GMearth*J4e_prefac*(-21.)*J4e_fac2*dx*dy/r2;
	const double dydzJ4 = GMearth*J4e_prefac*(-21.)*J4e_fac3*dy*dz/r2;
	const double dxdzJ4 = GMearth*J4e_prefac*(-21.)*J4e_fac3*dx*dz/r2;

	// Looping over variational particles
        for(int v = N_real + 6*j; v < N_real + 6*(j+1); v++){

	    double ddx = particles[v].x;
	    double ddy = particles[v].y;
	    double ddz = particles[v].z;

	    // Rotate to Earth equatorial frame
	    double ddxp =  ddx * cosr - ddy * sinr;
	    double ddyp =  ddx * sinr + ddy * cosr;
	    double ddzp =  ddz;
	    ddx =  ddxp;
	    ddy =  ddyp * cosd - ddzp * sind;
	    ddz =  ddyp * sind + ddzp * cosd;

	    // Matrix multiplication
	    double dax =   ddx * dxdx + ddy * dxdy + ddz * dxdz;
	    double day =   ddx * dxdy + ddy * dydy + ddz * dydz;
	    double daz =   ddx * dxdz + ddy * dydz + ddz * dzdz;

	    dax +=   ddx * dxdxJ4 + ddy * dxdyJ4 + ddz * dxdzJ4;
	    day +=   ddx * dxdyJ4 + ddy * dydyJ4 + ddz * dydzJ4;
	    daz +=   ddx * dxdzJ4 + ddy * dydzJ4 + ddz * dzdzJ4;

	    // Rotate back
	    double daxp =  dax;
	    double dayp =  day * cosd + daz * sind;
	    double dazp = -day * sind + daz * cosd;
	    dax =  daxp * cosr + dayp * sinr;
	    day = -daxp * sinr + dayp * cosr;
	    daz =  dazp;

	    // Accumulate acceleration terms
	    particles[v].ax += dax;
	    particles[v].ay += day;
	    particles[v].az += daz;

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
    const double Msun = 1.0;  // hard-code parameter.
    const double Rs_eq = 696000.0/au;
    const double J2s = 2.1106088532726840e-07;

    RAs = 268.13*M_PI/180.;
    Decs = 63.87*M_PI/180.;

    xp = cos(Decs)*cos(RAs);
    yp = cos(Decs)*sin(RAs);
    zp = sin(Decs);

    incl = acos(zp);
    if(xp != 0.0 || yp !=0.0) {    
      longnode = atan2(xp, -yp);
    } else {
      longnode = 0.0;
    }
    
    for (int j=0; j<N_real; j++){

        const struct reb_particle p = particles[j];
        double dx = p.x + (xo - xr);
        double dy = p.y + (yo - yr);
        double dz = p.z + (zo - zr);

        const double r2 = dx*dx + dy*dy + dz*dz;
        const double r = sqrt(r2);

	// Rotate to solar equatorial frame

	// Rotate around z by RA
	double cosr = cos(-longnode);
	double sinr = sin(-longnode);
	
	// Rotate around z by RA
	double dxp =  dx * cosr - dy * sinr;
	double dyp =  dx * sinr + dy * cosr;
	double dzp =  dz;

	// Rotate around x by Dec
	double cosd = cos(-incl);
	double sind = sin(-incl);

	dx =  dxp;
	dy =  dyp * cosd - dzp * sind;
	dz =  dyp * sind + dzp * cosd;

	const double costheta2 = dz*dz/r2;
        const double J2s_prefac = 3.*J2s*Rs_eq*Rs_eq/r2/r2/r/2.;
        const double J2s_fac = 5.*costheta2-1.;
        const double J2s_fac2 = 7.*costheta2-1.;
        const double J2s_fac3 = 35.*costheta2*costheta2-30.*costheta2+3.;

	// Calculate acceleration
	double resx = G*Msun*J2s_prefac*J2s_fac*dx;
	double resy = G*Msun*J2s_prefac*J2s_fac*dy;
	double resz = G*Msun*J2s_prefac*(J2s_fac-2.)*dz;

        // Variational equations

	// Rotate back to original frame
	// Rotate around x by -Dec
	double resxp =  resx;
	double resyp =  resy * cosd + resz * sind;
	double reszp = -resy * sind + resz * cosd;
	
	// Rotate around z by -RA
	resx =  resxp * cosr + resyp * sinr;
	resy = -resxp * sinr + resyp * cosr;
	resz =  reszp;

        particles[j].ax += resx;
        particles[j].ay += resy;
        particles[j].az += resz;

	// Constants for variational equations
	const double dxdx = G*Msun*J2s_prefac*(J2s_fac-5.*J2s_fac2*dx*dx/r2);
	const double dydy = G*Msun*J2s_prefac*(J2s_fac-5.*J2s_fac2*dy*dy/r2);
	const double dzdz = G*Msun*J2s_prefac*(-1.)*J2s_fac3;
	const double dxdy = G*Msun*J2s_prefac*(-5.)*J2s_fac2*dx*dy/r2;
	const double dydz = G*Msun*J2s_prefac*(-5.)*(J2s_fac2-2.)*dy*dz/r2;
	const double dxdz = G*Msun*J2s_prefac*(-5.)*(J2s_fac2-2.)*dx*dz/r2;

	// Looping over variational particles
        for(int v = N_real + 6*j; v < N_real + 6*(j+1); v++){

	    double ddx = particles[v].x;
	    double ddy = particles[v].y;
	    double ddz = particles[v].z;

	    // Rotate to solar equatorial frame
	    double ddxp =  ddx * cosr - ddy * sinr;
	    double ddyp =  ddx * sinr + ddy * cosr;
	    double ddzp =  ddz;

	    ddx =  ddxp;
	    ddy =  ddyp * cosd - ddzp * sind;
	    ddz =  ddyp * sind + ddzp * cosd;

	    double dax =   ddx * dxdx + ddy * dxdy + ddz * dxdz;
	    double day =   ddx * dxdy + ddy * dydy + ddz * dydz;
	    double daz =   ddx * dxdz + ddy * dydz + ddz * dzdz;

	    // Rotate back to original frame
	    double daxp =  dax;
	    double dayp =  day * cosd + daz * sind;
	    double dazp = -day * sind + daz * cosd;
	    dax =  daxp * cosr + dayp * sinr;
	    day = -daxp * sinr + dayp * cosr;
	    daz =  dazp;

	    // Accumulate acceleration terms
	    particles[v].ax += dax;
	    particles[v].ay += day;
	    particles[v].az += daz;

        } 
       
    }

    // Here is the treatment of non-gravitational forces.

    // The Sun center is reference for these calculations.
    double xs, ys, zs, vxs, vys, vzs, axs, ays, azs;    
    all_ephem(0, t, &GM, &xs, &ys, &zs, &vxs, &vys, &vzs, &axs, &ays, &azs);
    xr = xs;  yr = ys;  zr = zs;
    vxr = vxs; vyr = vys; vzr = vzs;    

    // The non-grav parameters are specific to each object being
    // integrated.

    // Normal asteroids
    double A1 = 0.0;
    double A2 = 0.0;
    double A3 = 0.0;

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
        const double t = sqrt(t2);

	particles[j].ax += A1*g*dx/r + A2*g*tx/t + A3*g*hx/h;
        particles[j].ay += A1*g*dy/r + A2*g*ty/t + A3*g*hy/h;
        particles[j].az += A1*g*dz/r + A2*g*tz/t + A3*g*hz/h;

    }

    // Here is the Solar GR treatment
    // The Sun is the reference for these calculations.
    xr  = xs;  yr  = ys;  zr = zs;
    vxr = vxs; vyr = vys; vzr = vzs;

    const double mu = G*Msun; 
    const int max_iterations = 10; // hard-coded parameter.
    for (int j=0; j<N_real; j++){

        struct reb_particle p = particles[j];
        struct reb_vec3d vi;

	p.x += (xo - xr);
	p.y += (yo - yr);
	p.z += (zo - zr);
	p.vx += (vxo - vxr);
	p.vy += (vyo - vyr);
	p.vz += (vzo - vzr);
	
        vi.x = p.vx;
        vi.y = p.vy;
        vi.z = p.vz;
        double vi2=vi.x*vi.x + vi.y*vi.y + vi.z*vi.z;
        const double ri = sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
	
        int q = 0;
        double A = (0.5*vi2 + 3.*mu/ri)/C2;
        struct reb_vec3d old_v;
        for(q=0; q<max_iterations; q++){
            old_v.x = vi.x;
            old_v.y = vi.y;
            old_v.z = vi.z;
            vi.x = p.vx/(1.-A);
            vi.y = p.vy/(1.-A);
            vi.z = p.vz/(1.-A);
            vi2 =vi.x*vi.x + vi.y*vi.y + vi.z*vi.z;
            A = (0.5*vi2 + 3.*mu/ri)/C2;
            const double dvx = vi.x - old_v.x;
            const double dvy = vi.y - old_v.y;
            const double dvz = vi.z - old_v.z;
            if ((dvx*dvx + dvy*dvy + dvz*dvz)/vi2 < DBL_EPSILON*DBL_EPSILON){
                break;
            }
        }
        const int default_max_iterations = 10;
        if(q==default_max_iterations){
            reb_warning(sim, "REBOUNDx Warning: 10 iterations in ephemeris forces failed to converge. This is typically because the perturbation is too strong for the current implementation.");
        }
  
        const double B = (mu/ri - 1.5*vi2)*mu/(ri*ri*ri)/C2;
        const double rdotrdot = p.x*p.vx + p.y*p.vy + p.z*p.vz;
        
        struct reb_vec3d vidot;
        vidot.x = p.ax + B*p.x;
        vidot.y = p.ay + B*p.y;
        vidot.z = p.az + B*p.z;
        
        const double vdotvdot = vi.x*vidot.x + vi.y*vidot.y + vi.z*vidot.z;
        const double D = (vdotvdot - 3.*mu/(ri*ri*ri)*rdotrdot)/C2;

        particles[j].ax += B*(1.-A)*p.x - A*p.ax - D*vi.x;
        particles[j].ay += B*(1.-A)*p.y - A*p.ay - D*vi.y;
        particles[j].az += B*(1.-A)*p.z - A*p.az - D*vi.z;

	const double prefac = mu/(ri*ri*ri)/C2;
	const double rdotv = p.x*p.vx+p.y*p.vy+p.z*p.vz;
	const double fac1 = mu/ri-vi2;
	const double fac2 = 3.*vi2/ri/ri-4.*mu/ri/ri/ri;;
	const double fac3 = 12.*rdotv/ri/ri;

	const double dxdx = prefac*(fac1+fac2*p.x*p.x+4.*p.vx*p.vx-fac3*p.vx*p.x);
	const double dydy = prefac*(fac1+fac2*p.y*p.y+4.*p.vy*p.vy-fac3*p.vy*p.y);
	const double dzdz = prefac*(fac1+fac2*p.z*p.z+4.*p.vz*p.vz-fac3*p.vz*p.z);

	const double dxdy = prefac*(fac2*p.x*p.y+4.*p.vx*p.vy-fac3*p.vx*p.y);
	const double dydx = prefac*(fac2*p.y*p.x+4.*p.vy*p.vx-fac3*p.vy*p.x);
	const double dxdz = prefac*(fac2*p.x*p.z+4.*p.vx*p.vz-fac3*p.vx*p.z);

	const double dzdx = prefac*(fac2*p.z*p.x+4.*p.vz*p.vx-fac3*p.vz*p.x);
	const double dydz = prefac*(fac2*p.y*p.z+4.*p.vy*p.vz-fac3*p.vy*p.z);
	const double dzdy = prefac*(fac2*p.z*p.y+4.*p.vz*p.vy-fac3*p.vz*p.y);

	const double dxdvx = prefac*(4.*rdotv-2.*p.x*p.vx+4.*p.x*p.vx);
	const double dydvy = prefac*(4.*rdotv-2.*p.y*p.vy+4.*p.y*p.vy);
	const double dzdvz = prefac*(4.*rdotv-2.*p.z*p.vz+4.*p.z*p.vz);

	const double dxdvy = prefac*(-2.*p.x*p.vy+4.*p.y*p.vx);
	const double dydvx = prefac*(-2.*p.y*p.vx+4.*p.x*p.vy);
	const double dxdvz = prefac*(-2.*p.x*p.vz+4.*p.z*p.vx);

	const double dzdvx = prefac*(-2.*p.z*p.vx+4.*p.x*p.vz);
	const double dydvz = prefac*(-2.*p.y*p.vz+4.*p.z*p.vy);
	const double dzdvy = prefac*(-2.*p.z*p.vy+4.*p.y*p.vz);

	// Looping over variational particles
        for(int v = N_real + 6*j; v < N_real + 6*(j+1); v++){

	    // variational particle coords
	    double ddx = particles[v].x;
	    double ddy = particles[v].y;
	    double ddz = particles[v].z;
	    double ddvx = particles[v].vx;
	    double ddvy = particles[v].vy;
	    double ddvz = particles[v].vz;

	    // Matrix multiplication
	    const double dax =   ddx  * dxdx  + ddy  * dxdy  + ddz  * dxdz
		+   ddvx * dxdvx + ddvy * dxdvy + ddvz * dxdvz;
	    const double day =   ddx  * dydx  + ddy  * dydy  + ddz  * dydz
		+   ddvx * dydvx + ddvy * dydvy + ddvz * dydvz;
	    const double daz =   ddx  * dzdx  + ddy  * dzdy  + ddz  * dzdz
		+   ddvx * dzdvx + ddvy * dzdvy + ddvz * dzdvz;

	    // Accumulate acceleration terms
	    particles[v].ax += dax;
	    particles[v].ay += day;
	    particles[v].az += daz;

        }
    }

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
static const double h[9]    = { 0.0, 0.0562625605369221464656521910318, 0.180240691736892364987579942780, 0.352624717113169637373907769648, 0.547153626330555383001448554766, 0.734210177215410531523210605558, 0.885320946839095768090359771030, 0.977520613561287501891174488626, 1.0};

void heartbeat(struct reb_simulation* r);

// integration_function
// tstart: integration start time in tdb
// tstep: suggested initial time step (days)
// trange: amount of time to integrate (days)
// geocentric: 1==geocentric equations of motion, 0==heliocentric
// n_particles: number of input test particles
// instate: input states of test particles
// ts: output times and states.

/*
int integration_function(double tstart, double tstep, double trange,
			 int geocentric,
			 int n_particles,
			 double* instate,
			 timestate *ts){
*/
int integration_function(double tstart, double tstep, double trange,
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
    r->heartbeat = heartbeat;
    r->display_data = NULL;
    r->collision = REB_COLLISION_NONE;  // This is important and needs to be considered carefully.
    r->collision_resolve = reb_collision_resolve_merge; // Not sure what this is for.
    r->gravity = REB_GRAVITY_NONE;

    // These quantities are specific to IAS15.  Perhaps something more flexible could
    // be done so that other REBOUND integration routines could be explored.
    r->ri_ias15.min_dt = 1e-2;  // to avoid very small time steps
    //r->ri_ias15.epsilon = 1e-8; // to avoid convergence issue with geocentric orbits
    r->ri_ias15.epsilon = epsilon; // to avoid convergence issue with geocentric orbits    

    r->exact_finish_time = 1;
    
    struct rebx_extras* rebx = rebx_attach(r);

    // Also add "ephemeris_forces" 
    struct rebx_force* ephem_forces = rebx_load_force(rebx, "ephemeris_forces");
    rebx_add_force(rebx, ephem_forces);

    rebx_set_param_int(rebx, &ephem_forces->ap, "geocentric", geocentric);

    // Set speed of light in right units (set by G & initial conditions).
    // The speed of light should be set using the ephemeris routines,
    // so that it is ensured to consistent with the units used in those routines.
    rebx_set_param_double(rebx, &ephem_forces->ap, "c", 173.144632674);

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

    //rebx_set_param_pointer(rebx, &ephem_forces->ap, "last_state", last_state);

    double tmax = tstart+trange;


    /*
    const double dtsign = copysign(1.,r->dt);   // Used to determine integration direction
    while((r->t)*dtsign<tmax*dtsign){ 
	reb_integrate(r, r->t + r->dt);
    }
    */

    reb_integrate(r, tmax);

    //ts->n_particles = n_particles;

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
    
    //struct rebx_force* ephem_forces = rebx_get_force(r->extras, "ephemeris_forces");
    //timestate* ts = rebx_get_param(r->extras, ephem_forces->ap, "timestate");
    //double* outtime;
    //double* outstate;

    //outtime = ts->t;
    //outstate = ts->state;

    //tstate* last_state = rebx_get_param(r->extras, ephem_forces->ap, "last_state");    

    store_function(r);

    reb_update_acceleration(r);

    store_last_state(r);

}

void store_last_state(struct reb_simulation* r){

    struct rebx_force* ephem_forces = rebx_get_force(r->extras, "ephemeris_forces");
    timestate* ts = rebx_get_param(r->extras, ephem_forces->ap, "timestate");
    tstate* last_state = ts->last_state;
    //rebx_get_param(r->extras, ephem_forces->ap, "last_state");

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
    //tstate* last_state = rebx_get_param(r->extras, ephem_forces->ap, "last_state");
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

	int time_offset = (step-1)*8+1;

	// Loop over intervals using Gauss-Radau spacings      
	for(int n=1;n<9;n++) {

	    // The h[n] values here define the substeps used in the
	    // the integration, but they could be altered at this point.
	    s[0] = r->dt_last_done * h[n];

	    s[1] = s[0] * s[0] / 2.;
	    s[2] = s[1] * h[n] / 3.;
	    s[3] = s[2] * h[n] / 2.;
	    s[4] = 3. * s[3] * h[n] / 5.;
	    s[5] = 2. * s[4] * h[n] / 3.;
	    s[6] = 5. * s[5] * h[n] / 7.;
	    s[7] = 3. * s[6] * h[n] / 4.;
	    s[8] = 7. * s[7] * h[n] / 9.;

	    double t = r->t + r->dt_last_done * (-1.0 + h[n]);

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
		int offset = ((step-1)*8 + n)*6*N + 6*j;
		outstate[offset+0] = xx0;
		outstate[offset+1] = xy0;	  	  
		outstate[offset+2] = xz0;
	    }

	    s[0] = r->dt_last_done * h[n];
	    s[1] =      s[0] * h[n] / 2.;
	    s[2] = 2. * s[1] * h[n] / 3.;
	    s[3] = 3. * s[2] * h[n] / 4.;
	    s[4] = 4. * s[3] * h[n] / 5.;
	    s[5] = 5. * s[4] * h[n] / 6.;
	    s[6] = 6. * s[5] * h[n] / 7.;
	    s[7] = 7. * s[6] * h[n] / 8.;

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
		int offset = ((step-1)*8 + n)*6*N + 6*j;		
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


