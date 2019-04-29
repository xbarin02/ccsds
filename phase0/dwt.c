#include "common.h"
#include "dwt.h"
#include "dwtfloat.h"
#include "dwtint.h"

#include <stddef.h>
#include <assert.h>

int dwt_encode(struct frame *frame, const struct parameters *parameters)
{
	assert( parameters );

	switch (parameters->DWTtype) {
		case 0:
			return dwtfloat_encode(frame);
		case 1:
			return dwtint_encode(frame, parameters->weight);
		default:
			return RET_FAILURE_LOGIC_ERROR;
	}
}

int dwt_decode(struct frame *frame, const struct parameters *parameters)
{
	assert( parameters );

	switch (parameters->DWTtype) {
		case 0:
			return dwtfloat_decode(frame);
		case 1:
			return dwtint_decode(frame, parameters->weight);
		default:
			return RET_FAILURE_LOGIC_ERROR;
	}
}
