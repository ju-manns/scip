/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2005 Tobias Achterberg                              */
/*                                                                           */
/*                  2002-2005 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the SCIP Academic Licence.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: interrupt.h,v 1.5 2005/01/18 09:26:47 bzfpfend Exp $"

/**@file   interrupt.h
 * @brief  methods for catching the user CTRL-C interrupt
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__


#include "def.h"
#include "type_retcode.h"
#include "type_interrupt.h"



/** creates a CTRL-C interrupt data */
extern
RETCODE SCIPinterruptCreate(
   INTERRUPT**      interrupt           /**< pointer to store the CTRL-C interrupt data */
   );

/** frees a CTRL-C interrupt data */
extern
void SCIPinterruptFree(
   INTERRUPT**      interrupt           /**< pointer to the CTRL-C interrupt data */
   );

/** captures the CTRL-C interrupt to call the SCIP's own interrupt handler */
extern
void SCIPinterruptCapture(
   INTERRUPT*       interrupt           /**< CTRL-C interrupt data */
   );

/** releases the CTRL-C interrupt and restores the old interrupt handler */
extern
void SCIPinterruptRelease(
   INTERRUPT*       interrupt           /**< CTRL-C interrupt data */
   );

/** returns whether the user interrupted by pressing CTRL-C */
extern
Bool SCIPinterrupted(
   void
   );


#endif
