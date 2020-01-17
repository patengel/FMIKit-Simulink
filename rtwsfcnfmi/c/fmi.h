/*****************************************************************
 *  Copyright (c) Dassault Systemes. All rights reserved.        *
 *  This file is part of FMIKit. See LICENSE.txt in the project  *
 *  root for license information.                                *
 *****************************************************************/

/*
-----------------------------------------------------------
	Common includes and macros for FMI 1.0 and FMI 2.0
	S-function wrappers
-----------------------------------------------------------
*/

#ifndef FMI__H
#define FMI__H

/*
 * Generated header to configure MATLAB release
*/
#include "sfcn_fmi_rel_conf.h"


#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include <math.h>

#if defined(_MSC_VER)
#include "windows.h"

#else

#include <sys/stat.h>
/* might need access to non-standard function dladdr */
#define __USE_GNU
#include <dlfcn.h>
#undef __USE_GNU

#endif

#include <simstruc.h>

#undef SFCN_FMI_VERBOSITY /* Define to add debug logging */

#define SFCN_FMI_MAX_TIME	1e100
#define SFCN_FMI_EPS		2e-13	/* Not supported with discrete sample times smaller than this */

#if defined(_MSC_VER)
#if _MSC_VER > 1350
/* avoid warnings from Visual Studio */
#define strncpy(dest, src, len) strncpy_s(dest, (len) + 1, src, len)
#endif
#endif

/* Variable categories */
typedef enum {
  SFCN_FMI_PARAMETER  = 1,
  SFCN_FMI_STATE      = 2,
  SFCN_FMI_DERIVATIVE = 3,
  SFCN_FMI_OUTPUT     = 4,
  SFCN_FMI_INPUT      = 5,
  SFCN_FMI_BLOCKIO    = 6,
  SFCN_FMI_DWORK	  = 7
} sfcn_fmi_category_T;

/* Create a 32-bit value reference on format: */
/*  category: 31-28  datatype: 27-24  index: 23-0 */
#define SFCN_FMI_VALUE_REFERENCE(category, datatype, index) ((unsigned int)(category) << 28 | \
															 (unsigned int)(datatype) << 24 | \
														     (unsigned int)(index))

#define SFCN_FMI_CATEGORY(valueRef) ((valueRef) >> 28)
#define SFCN_FMI_DATATYPE(valueRef)	(((valueRef) >> 24) & 0xf)
#define SFCN_FMI_INDEX(valueRef)	((valueRef) & 0xffffff)

/* Function for double precision comparison */
int isEqual(double a, double b)
{
	double A, B, largest;
	double diff = fabs(a-b);

	A = fabs(a);
	B = fabs(b);
	largest = (B > A) ? B : A;

	if (diff <= (1.0+largest)*SFCN_FMI_EPS)
		return 1;
	return 0;
}

#endif
