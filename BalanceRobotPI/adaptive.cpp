#include "adaptive.h"
#include "math.h"

static double r1[hz] = {0};								// param for desired out conductor block
static double r2[hz] = {0};
double s1[hz] = {0};
static double s2[hz] = {0};
static double w = 0;
static unsigned char ini = 0;

Adaptive::Adaptive()
{
}

// Conductor Block Calculate amortiguamiento critico y ganancia unitaria
void Adaptive::conductor_block(void){
    double a1,a2,b1,b2;

    a1 = 2 * exp(-6.0/nCP);
    a2 = -exp(-12.0/nCP);
    b1 = 1 - (1 + 6.0/nCP) * exp(-6.0/nCP);
    b2 = 1 - a1 - a2 - b1;

    r1[0] = a1;	// Vector e1 para cada horizonte a1
    r2[0] = a2;	// vector e2 para cada horizonte a2
    s1[0] = b1;	// vector g1 para cada horizonte b1
    s2[0] = b2;	// vector g2 para cada horizonte b2
    w = 0;

    for (int j = 1; j < hz; j++){
        r1[j] = r1[j-1] * r1[0] + r2[j-1];
        r2[j] = r1[j-1] * r2[0];
        s1[j] = r1[j-1] * s1[0] + s2[j-1];
        s2[j] = r1[j-1] * s2[0];
    }
    for (int j = 0; j < hz; j++){
        w += s1[j];
    }
    // Parameters for conductor block
    // r1[hz-1] * y(k)
    // r2[hz-1] * y(k-1)
    // s2[hz-1] * sp(k-1)
    // w		* sp(k)
}

// Adaptive Control no incremental
void Adaptive::adaptive(double *sp, double *t, double *y, double *u, double *yp, double max_out){
    double y_k = 0;				// Estimated out
    double ek = 0;				// Estimation error
    double q = 0;				// Aux for adaptive process
    double y_dk = 0;			// Incremental Desired Out
    double y_pdk = 0;			// Process Desired out
    unsigned char adap;			// Enable/disable  adaptation

    if (ini <= (PmA+10)) ini++;		// Counter for initialize  input/output vector

    y[0] = yp[0] + Ref_Meters;		// Scaled incremental out process for only positive values.

    if (ini >= (PmA+10)){			// Reload for initialize  input/output vector
        // Adaptive model order 2 with 3 parameters b
        y_k = t[0] * y[PmA] + t[1] * y[PmA+1] + t[2] * u[PmB] + t[3] * u[PmB+1] + t[4] * u[PmB+2];		// Estimated out Delay Process = 1
        ek = (y[0] - y_k);																				// Estimation error
        if (fabs(ek) > NL) adap = 1;																	// Noise Level
            else adap = 0;

        //Adaptive mechanism
        q = (double)adap * ek / (1.0 + (GainA * (pow(y[PmA],2) + pow(y[PmA+1],2)) + GainB * (pow(u[PmB],2) + pow(u[PmB+1],2) + pow(u[PmB+2],2))));
        t[0] += (GainA * q * y[PmA]);
        t[1] += (GainA * q * y[PmA+1]);
        t[2] += (GainB * q * u[PmB]);
        t[3] += (GainB * q * u[PmB+1]);
        t[4] += (GainB * q * u[PmB+2]);

        y_pdk = r1[hz-1] * yp[0] + r2[hz-1] * yp[1] + s2[hz-1] * sp[1] + w * sp[0];		// Desired out calculated. Conductor block. Model ref: a1, a2, b1, b2
        y_dk = y_pdk + Ref_Meters;														// Scaled for always positive values

        // Calculated parameters extended strategy
        double e1[hz] = { t[0] };	// Vector e1 for horizon
        double e2[hz] = { t[1] };	// vector e2 for horizon
        double g1[hz] = { t[2] };	// vector g1 for horizon
        double g2[hz] = { t[3] };	// vector g2 for horizon
        double g3[hz] = { t[4] };	// vector g3 for horizon
        double h = 0;				// h parameter

        for (int j = 1; j < hz; j++){
            e1[j] = e1[j-1] * e1[0] + e2[j-1];
            e2[j] = e1[j-1] * e2[0];
            g1[j] = e1[j-1] * g1[0] + g2[j-1];
            g2[j] = e1[j-1] * g2[0] + g3[j-1];
            g3[j] = e1[j-1] * g3[0];
        }
        for (int j = 0; j < hz; j++){
            h += g1[j];
        }
        u[0] = (y_dk - e1[hz-1] * y[0] - e2[hz-1] * y[1] - g2[hz-1] * u[1] - g3[hz-1] * u[2]) / h;		// Calculated Regulator Out

        if (u[0] > max_out) u[0] = max_out;						// Upper limit out
            else if (u[0] < - max_out) u[0] = - max_out;
    }	// End counter for init output/input vector
    //save_process for K+1
    for (int i = (PmA+1); i > 0; i--){
        y[i] = y[i-1];
    }

    for (int i = (PmB+2); i > 0; i--){
        u[i] = u[i-1];
    }
    sp[1] = sp[0];
    yp[1] = yp[0];
}

/*
// Adaptive Control incremental
void adaptive(double *sp, double *t, double *y, double *u, double *yp, double max_out){
    double y_k = 0;				// Estimated out
    double ek = 0;				// Estimation error
    double q = 0;				// Aux for adaptive process
    double y_dk = 0;			// Incremental Desired Out
    double y_pdk = 0;			// Process Desired out
    unsigned char adap;			// Enable/disable  adaptation

    if (ini <= (PmA+8)) ini++;		// Counter for initialize  input/output vector

    y[0] = yp[0] - yp[2];			// Scaled incremental out process for only positive values.
    y[0] += Ref_Meters;

    if (ini >= (PmA+8)){			// Control for initialize  input/output vector
        // Adaptive model order 2 with 3 parameters b
        y_k = t[0] * y[PmA] + t[1] * y[PmA+1] + t[2] * u[PmB] + t[3] * u[PmB+1] + t[4] * u[PmB+2];		// Estimated out Delay Process = 1
        ek = (y[0] - y_k);																				// Estimation error
        if (fabs(ek) > NL) adap = 1;																	// Noise Level
            else adap = 0;

        //Adaptive mechanism
        q = (double)adap * ek / (1.0 + (GainA * (pow(y[PmA],2) + pow(y[PmA+1],2)) + GainB * (pow(u[PmB],2) + pow(u[PmB+1],2) + pow(u[PmB+2],2))));
        t[0] += (GainA * q * y[PmA]);
        t[1] += (GainA * q * y[PmA+1]);
        t[2] += (GainB * q * u[PmB]);
        t[3] += (GainB * q * u[PmB+1]);
        t[4] += (GainB * q * u[PmB+2]);

        y_pdk = r1[hz-1] * yp[0] + r2[hz-1] * yp[1] + s2[hz-1] * sp[1] + w * sp[0];		// Desired out calculated. Conductor block. Model ref: a1, a2, b1, b2
        y_dk = y_pdk - yp[0];
        y_dk += Ref_Meters;														// Scaled for always positive values

        // Calculated parameters extended strategy
        double e1[hz] = { t[0] };	// Vector e1 for horizon
        double e2[hz] = { t[1] };	// vector e2 for horizon
        double g1[hz] = { t[2] };	// vector g1 for horizon
        double g2[hz] = { t[3] };	// vector g2 for horizon
        double g3[hz] = { t[4] };	// vector g3 for horizon
        double h = 0;				// h parameter

        for (int j = 1; j < hz; j++){
            e1[j] = e1[j-1] * e1[0] + e2[j-1];
            e2[j] = e1[j-1] * e2[0];
            g1[j] = e1[j-1] * g1[0] + g2[j-1];
            g2[j] = e1[j-1] * g2[0] + g3[j-1];
            g3[j] = e1[j-1] * g3[0];
        }
        for (int j = 0; j < hz; j++){
            h += g1[j];
        }
        u[0] = (y_dk - e1[hz-1] * y[0] - e2[hz-1] * y[1] - g2[hz-1] * u[1] - g3[hz-1] * u[2]) / h;		// Calculated Regulator Out

        if (u[0] > max_out) u[0] = max_out;						// Upper limit out
            else if (u[0] < - max_out) u[0] = - max_out;
    }	// End counter for init output/input vector
    //save_process for K+1
    for (int i = (PmA+1); i > 0; i--){
        y[i] = y[i-1];
    }

    for (int i = (PmB+2); i > 0; i--){
        u[i] = u[i-1];
    }
    yp[2] = yp[1];
    yp[1] = yp[0];
    sp[1] = sp[0];
}
*/
