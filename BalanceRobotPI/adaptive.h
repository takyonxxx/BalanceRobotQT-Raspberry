#ifndef ADAPTIVE_H
#define ADAPTIVE_H

#define Ref_Meters		180.0		// Reference for meters in adaptive predictive control
#define NL				0.005		// Noise Level for Adaptive Mechanism.
#define GainA 			0.6			// Gain for Adaptive Mechanism A
#define GainB 			0.6			// Gain for Adaptive Mechanism B
#define PmA				2			// Delay Parameters a
#define PmB				2			// Delay Parameters b
#define nCP				9.0			// Conductor block periods control for rise to set point ts = n * CP
#define hz				5			// Prediction Horizon (Horizon max = n + 2)
#define UP_Roll			800.0		// Upper limit out
#define UP_Yaw			150.0		// Upper limit out
#define GainT_Roll		12.0		// Total Gain Roll Out Controller
#define GainT_Yaw		5.0			// Total Gain Yaw Out Controller
#define MaxOut_Roll		UP_Roll/GainT_Roll
#define MaxOut_Yaw		UP_Yaw/GainT_Yaw

#define Kp_ROLLPITCH 0.2		// Pitch&Roll Proportional Gain
#define Ki_ROLLPITCH 0.000001	// Pitch&Roll Integrator Gain

class Adaptive
{
public:
    Adaptive();

    void conductor_block(void);
    void adaptive(double *sp, double *t, double *y, double *u, double *yp, double max_out);

};

#endif // ADAPTIVE_H
