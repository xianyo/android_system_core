/*
 * five_wire_calib.c
 *
 * This module defines the five_wire_calibrate() routine
 * as declared in five_wire_calib.h
 *
 * Copyright Boundary Devices, Inc. 2010
 */
#include "five_wire_calib.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

enum {
	fixed_divisor = 65536,
};

#define rc(r,c) (r*6+c)
static void SwapRows(double* w1,double* w2)
{
   double t;
   int i=6;
   while (i) {
      t = *w1; 
      *w1++ = *w2;
      *w2++ = t;	//swap
      i--;
   }
}

inline long roundLong( double d ){
   d *= fixed_divisor ;
   return round(d);
}

int five_wire_calibrate
	(struct calibrate_point_t const points[3],
	 int coefficients[7])
{
	int i,j;
	double w[rc(3,6)];
	double t;

	memset( coefficients,0,sizeof(coefficients[0])*7);
	coefficients[6] = fixed_divisor ;
	if( (points[0].x == points[1].x) && (points[1].x == points[2].x) )
		return 0 ; // co-linear in X
	if( (points[0].y == points[1].y) && (points[1].y == points[2].y) )
		return 0 ; // co-linear in Y

	w[rc(0,0)] = points[0].i;
	w[rc(1,0)] = points[0].j;
	w[rc(2,0)] = 1.0;
	
	w[rc(0,1)] = points[1].i;
	w[rc(1,1)] = points[1].j;
	w[rc(2,1)] = 1.0;
	
	w[rc(0,2)] = points[2].i;
	w[rc(1,2)] = points[2].j;
	w[rc(2,2)] = 1.0;
	
	//find Inverse of touched
	for(j=0; j<3; j++) {
		for(i=3; i<6; i++) {
			w[rc(j,i)] = ((j+3)==i) ? 1.0 : 0;  //identity matrix tacked on end
		}
	}

	//	PrintMatrix(w);
	if(w[rc(0,0)] < w[rc(1,0)]) SwapRows(&w[rc(0,0)],&w[rc(1,0)]);
	if(w[rc(0,0)] < w[rc(2,0)]) SwapRows(&w[rc(0,0)],&w[rc(2,0)]);
	t = w[rc(0,0)];
	if(t==0) {printf("error w[0][0]=0\n"); return 0; }
	w[rc(0,0)] = 1.0;
	for(i=1; i<6; i++) w[rc(0,i)] = w[rc(0,i)]/t;
	
	for(j=1; j<3; j++) {
		t = w[rc(j,0)];
		w[rc(j,0)] = 0;
		for(i=1; i<6; i++) w[rc(j,i)] -= w[rc(0,i)] * t;
	}
	
	//	PrintMatrix(w);
	if(w[rc(1,1)] < w[rc(2,1)]) SwapRows(&w[rc(1,0)],&w[rc(2,0)]);
	
	t = w[rc(1,1)];
	if(t==0) {printf("error w[1][1]=0\n"); return 0;}
		w[rc(1,1)] = 1.0;
	for(i=2; i<6; i++) w[rc(1,i)] = w[rc(1,i)]/t;
	
	t = w[rc(2,1)];
	w[rc(2,1)] = 0;
	for(i=2; i<6; i++) w[rc(2,i)] -= w[rc(1,i)] * t;

	//	PrintMatrix(w);
	t = w[rc(2,2)];
	if(t==0) {printf("error w[2][2]=0\n"); return 0;}
	w[rc(2,2)] = 1.0;
	for(i=3; i<6; i++) w[rc(2,i)] = w[rc(2,i)]/t;
	
	for(j=0; j<2; j++) {
		t = w[rc(j,2)];
		w[rc(j,2)] = 0;
		for(i=3; i<6; i++) w[rc(j,i)] -= w[rc(2,i)] * t;
	}
	
	//	PrintMatrix(w);
	t = w[rc(0,1)];
	w[rc(0,1)] = 0;
	for(i=3; i<6; i++) w[rc(0,i)] -= w[rc(1,i)] * t;
	//	PrintMatrix(w);
	
	/*
	| x1  x2  x3 |  | i1  i2  i3 | -1  =  | a1  a2  a3 |
	| y1  y2  y3 |  | j1  j2  j3 |        | b1  b2  b3 |
	  | 1   1    1 |
	*/
	
	coefficients[0] = roundLong((points[0].x * w[rc(0,3)]) + (points[1].x * w[rc(1,3)]) + (points[2].x * w[rc(2,3)]));
	coefficients[1] = roundLong((points[0].x * w[rc(0,4)]) + (points[1].x * w[rc(1,4)]) + (points[2].x * w[rc(2,4)]));
	coefficients[2] = roundLong((points[0].x * w[rc(0,5)]) + (points[1].x * w[rc(1,5)]) + (points[2].x * w[rc(2,5)]));
	coefficients[3] = roundLong((points[0].y * w[rc(0,3)]) + (points[1].y * w[rc(1,3)]) + (points[2].y * w[rc(2,3)]));
	coefficients[4] = roundLong((points[0].y * w[rc(0,4)]) + (points[1].y * w[rc(1,4)]) + (points[2].y * w[rc(2,4)]));
	coefficients[5] = roundLong((points[0].y * w[rc(0,5)]) + (points[1].y * w[rc(1,5)]) + (points[2].y * w[rc(2,5)]));
	return 1 ;
}

#ifdef __SANITY_CHECK__

#define ARRAYSIZE(__arr) (sizeof(__arr)/sizeof(__arr[0]))

static void translate(int i, int j, int const coefficients[], unsigned *x, unsigned *y )
{
      long tmp = coefficients[0]*i + coefficients[1]*j + coefficients[2];
      if( 0 > tmp ){
         tmp = 0 ;
      }
      *x = (unsigned)tmp / coefficients[6];

      tmp = coefficients[3]*i + coefficients[4]*j + coefficients[5];
      if( 0 > tmp )
         tmp = 0 ;
      *y = (unsigned)tmp / coefficients[6];
}

int main(void){
	struct calibrate_point_t const points[3] = {
		{ 320, 240, 429, 310 }
	,	{ 640, 200, 307, 434 }
	,	{ 960, 600, 545, 556 }
	};
	int coefficients[7];
	printf( "Hello, world\n" );
	if (five_wire_calibrate(points,coefficients)) {
		int i ;
		printf( "calibration succeeded\n" );
		for (i = 0 ; i < ARRAYSIZE(points); i++) {
			unsigned x, y ;
                        struct calibrate_point_t const *pt = points+i ;
			translate(pt->i, pt->j, coefficients, &x, &y);
			printf("translating: %u:%u.%d.%d == %u:%u\n", 
			       pt->x, pt->y, pt->i, pt->j, 
			       x, y );
		}
	}
	else
		printf( "calibration failed\n" );
	return 0 ;
}

#endif
