#ifndef __FIVE_WIRE_CALIB_H__
#define __FIVE_WIRE_CALIB_H__

/*
 * five_wire_calib.h
 *
 * This header file declares the five_wire_calibrate()
 * routine, which calculates a set of calibration constants
 * based on three input data points and raw A/D readings.
 *
 * Copyright Boundary Devices, Inc. 2010
 */

struct calibrate_point_t {
	unsigned x, y ;      // screen location
	int i, j ;           // generally the median A/D measurement
};

/* returns non-zero to indicate success */
int five_wire_calibrate
	(struct calibrate_point_t const points[3],
	 int coefficients[]);

#endif

