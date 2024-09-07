#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "../../user_io.h"

static constexpr int MAX_DIAG = 69;
static constexpr int MAX_CARDINAL = 85;
static constexpr float WEDGE_BOUNDARY = (float)(MAX_CARDINAL - MAX_DIAG) / MAX_DIAG;
static constexpr float MAX_DIST = hypotf(MAX_DIAG, MAX_DIAG);
static constexpr int sign(int value) {
	return (value > 0) ? 1 
		 : (value < 0) ? -1 
		 : 0;
}

void stick_swap(int* num, int* stick)
{
	uint32_t tv = user_io_status_get("TV", 1);
	bool p2 = tv & 1;
	bool p3 = (tv >> 1) & 1;
	bool swap = (tv >> 2) & 1;

	// Reverse sticks
	*stick = !*stick ? 1 : 0;

	// P1 right stick -> P3
	if (p3) {
		if (*stick) {
			if (*num < 2) {
				*num += 2;
				*stick = 0;
			}
		}
		else if (2 < *num && *num < 5) {
			// Swap sticks to minimize conflict
			*num -= 2;
			*stick = 1;
		}
	}

	// P1 right stick -> P2
	if (p2) {
		const bool num_odd = *num % 2;
		if (*stick) {
			if (!num_odd) {
				(*num)++;
				*stick = 0;
			}
		}
		else if (num_odd) {
			(*num)--;
			*stick = 1;
		}
	}
}

void n64_joy_emu(int* x, int* y, int max_cardinal, float max_range) {
	// Move to top right quadrant to standardize solutions
	const int flip_x = sign(*x);
	const int flip_y = sign(*y);
	const float abs_x = *x * flip_x;
	const float abs_y = *y * flip_y;

	// Either reduce range to radius 97.5807358037f ((69, 69) diagonal of original controller)
	// or reduce cardinals to 85, whichever is less aggressive (smaller reduction in scaling)
	// (subtracts 2 from each to allow for minor outer deadzone)
	// assumes the max range is at least 85 (max cardinal of original controller)
	if (max_cardinal < MAX_CARDINAL) max_cardinal = MAX_CARDINAL;
	if (max_range < MAX_DIST) max_range = MAX_DIST;

	const float scale_cardinal = (float)MAX_CARDINAL / max_cardinal;
	const float scale_range = MAX_DIST / max_range;
	const float scale = scale_cardinal > scale_range ? scale_cardinal : scale_range;
	const float scaled_x = abs_x * scale;
	const float scaled_y = abs_y * scale;

	// Move to octagon's lower wedge in top right quadrant to further standardize solution
	float scaled_max = scaled_x > scaled_y ? scaled_x : scaled_y;
	float scaled_min = scaled_x < scaled_y ? scaled_x : scaled_y;

	// Clamp scaled_min and scaled_max
	// Note: wedge boundary is given by x = 85 - y * ((85 - 69) / 69)
	// If x + y * (16 / 69) > 85, coordinates exceed boundary and need clamped
	const float boundary = scaled_max + scaled_min * WEDGE_BOUNDARY;
	if (boundary > MAX_CARDINAL) {
		// We know target value is on:
		//   1) Boundary line: x = 85 - y * (16 / 69)
		//   2) Observed slope line: y = (scaled_max / scaled_min) * x
		// Solving system of equations yields:
		scaled_min = MAX_CARDINAL * scaled_min / boundary;
		scaled_max = MAX_CARDINAL - scaled_min * WEDGE_BOUNDARY; // Boundary line
	}

	// Move back from wedge to actual coordinates
	if (abs_x > abs_y) {
		*x2 = nearbyintf(scaled_max * flip_x);
		*y2 = nearbyintf(scaled_min * flip_y);
	}
	else {
		*x2 = nearbyintf(scaled_min * flip_x);
		*y2 = nearbyintf(scaled_max * flip_y);
	}
}
