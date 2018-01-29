/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2018 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   table.h
 * @ingroup INTERNALAPI
 * @brief  internal methods for displaying statistics tables
 * @author Tristan Gally
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_TABLE_H__
#define __SCIP_TABLE_H__


#include <stdio.h>

#include "scip/def.h"
#include "blockmemshell/memory.h"
#include "scip/type_retcode.h"
#include "scip/type_set.h"
#include "scip/type_table.h"
#include "scip/type_paramset.h"
#include "scip/pub_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/** copies the given statistics table to a new scip */
extern
SCIP_RETCODE SCIPtableCopyInclude(
   SCIP_TABLE*           table,              /**< statistics table */
   SCIP_SET*             set                 /**< SCIP_SET of SCIP to copy to */
   );

/** creates a statistics table */
extern
SCIP_RETCODE SCIPtableCreate(
   SCIP_TABLE**          table,              /**< pointer to store statistics table */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_MESSAGEHDLR*     messagehdlr,        /**< message handler */
   BMS_BLKMEM*           blkmem,             /**< block memory for parameter settings */
   const char*           name,               /**< name of statistics table */
   const char*           desc,               /**< description of statistics table */
   SCIP_Bool             active,             /**< should the table be activated by default? */
   SCIP_DECL_TABLECOPY   ((*tablecopy)),     /**< copy method of statistics table or NULL if you don't want to copy your plugin into sub-SCIPs */
   SCIP_DECL_TABLEFREE   ((*tablefree)),     /**< destructor of statistics table */
   SCIP_DECL_TABLEINIT   ((*tableinit)),     /**< initialize statistics table */
   SCIP_DECL_TABLEEXIT   ((*tableexit)),     /**< deinitialize statistics table */
   SCIP_DECL_TABLEINITSOL ((*tableinitsol)), /**< solving process initialization method of statistics table */
   SCIP_DECL_TABLEEXITSOL ((*tableexitsol)), /**< solving process deinitialization method of statistics table */
   SCIP_DECL_TABLEOUTPUT ((*tableoutput)),   /**< output method */
   SCIP_TABLEDATA*       tabledata,          /**< statistics table data */
   int                   position,           /**< position of statistics table */
   SCIP_STAGE            earlieststage       /**< output of the statistics table is only printed from this stage onwards */
   );

/** frees memory of statistics table */
extern
SCIP_RETCODE SCIPtableFree(
   SCIP_TABLE**          table,              /**< pointer to statistics table data structure */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** initializes statistics table */
extern
SCIP_RETCODE SCIPtableInit(
   SCIP_TABLE*           table,              /**< statistics table */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** deinitializes statistics table */
extern
SCIP_RETCODE SCIPtableExit(
   SCIP_TABLE*           table,              /**< statistics table */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** informs statistics table that the branch and bound process is being started */
extern
SCIP_RETCODE SCIPtableInitsol(
   SCIP_TABLE*           table,              /**< statistics table */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** informs statistics table that the branch and bound process data is being freed */
extern
SCIP_RETCODE SCIPtableExitsol(
   SCIP_TABLE*           table,              /**< statistics table */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** output statistics table to screen */
extern
SCIP_RETCODE SCIPtableOutput(
   SCIP_TABLE*           table,              /**< statistics table */
   SCIP_SET*             set,                /**< global SCIP settings */
   FILE*                 file                /**< output file (or NULL for standard output) */
   );

#ifdef __cplusplus
}
#endif

#endif
