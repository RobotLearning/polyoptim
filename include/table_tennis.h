/*
 * table_tennis.h
 *
 *  Created on: Jun 21, 2016
 *      Author: okoc
 */

#ifndef TABLE_TENNIS_H_
#define TABLE_TENNIS_H_

#include "math.h"
#include "string.h"
#include "SL.h" // if eclipse warns for unresolved inclusion, simply cut and paste again to resolve
#include "SL_user.h"
#include "table.h"

// defines
#define CART 3
#define TSTEP 0.01
#define TPRED 1.0
#define DOF 7

#define LOOKUP_TABLE_SIZE 4002 //769
#define LOOKUP_COLUMN_SIZE 2*DOF + 1 + 2*CART // ball state and optimization parameters (6 + 15)
#define LOOKUP_TABLE_NAME "LookupTable-16-May-2016"

extern Matrix ballMat; // predicted ball pos and vel values for T_pred time seconds
extern Matrix racketMat; // racket strategy: desired positions, velocities and normal saved in matrix

/* Contact Coefficients */
/* coefficient of restitution for the table (i.e. rebound z-velocity multiplier) */
static double 	  CRT = 0.88;//0.91;cannon//0.8; human //0.88 das hatten wir mal experimentell bestimmt, 0.92 fittet aber besser in die beobachteten Daten
/* coefficient of table contact model on Y-direction */
static double 	  CFTY = 0.72;//0.89;cannon//0.78; human //0.6 is eigentlich das richtige, aber da immer ein wenig spin drin sein wird, passt das am besten in die beobachteten Daten
/* coefficient of table contact model on X-direction */
static double 	  CFTX = 0.68; //0.74
/* coefficent of restitution for racket */
static double 	  CRR = 0.78;//0.9;cannon//0.78;human//0.78;

/* Air drag coefficient */
static double Cdrag = 0.1414;

/* for simulating different gravities */
static double   intern_gravity = 9.802;

/* racket computations */
void calc_racket_strategy(double *ballLand, double landTime);
void calc_ball_vel_out(SL_Cstate hitPoint, Vector landPoint, double time2land, Vector velOut);
void calc_racket_normal(Vector bin, Vector bout, Vector normal);
void calc_racket_vel(Vector velBallIn, Vector velBallOut, Vector normalRacket, Vector velRacket);

/* ball related methods */
void set_des_land_param(double *ballLand, double *landTime);
void predict_ball_state(double *b0, double *v0);
void integrate_ball_state(SL_Cstate ballState, SL_Cstate *ballPred, double deltat, int *bounce); //ball prediction
int check_ball_table_contact(SL_Cstate state);

/* first order hold to get racket parameters at time T */
void first_order_hold(double *ballPred, double *racketVel, double *racketNormal, double T);

/* lookup table, i.e. kNN with k = 1 */
int lookup(const Matrix lookupTable, const double* b0, const double* v0, double *x);

#endif /* TABLE_TENNIS_H_ */
