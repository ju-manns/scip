/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2003 Tobias Achterberg                              */
/*                            Thorsten Koch                                  */
/*                  2002-2003 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the SCIP Academic Licence.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   cmain.c
 * @brief  main file for C compilation
 * @author Tobias Achterberg
 */

/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "scip.h"
#include "reader_mps.h"
#include "disp_default.h"
#include "cons_integral.h"
#include "cons_linear.h"
#include "cons_setcover.h"
#include "cons_setpack.h"
#include "cons_setpart.h"
#include "nodesel_bfs.h"
#include "nodesel_dfs.h"
#include "branch_fullstrong.h"
#include "branch_mostinf.h"
#include "branch_leastinf.h"
#include "heur_diving.h"
#include "heur_rounding.h"
#include "sepa_gomory.h"


static
RETCODE runSCIP(
   int              argc,
   char**           argv
   )
{
   SCIP* scip = NULL;

   SCIPprintVersion(NULL);

   /*********
    * Setup *
    *********/

   printf("\nsetup SCIP\n");
   printf("==========\n\n");

   /* initialize SCIP */
   CHECK_OKAY( SCIPcreate(&scip) );

   /* include user defined callbacks */
   CHECK_OKAY( SCIPincludeReaderMPS(scip) );
   CHECK_OKAY( SCIPincludeDispDefault(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrIntegral(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrLinear(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrSetcover(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrSetpack(scip) );
   CHECK_OKAY( SCIPincludeConsHdlrSetpart(scip) );
   CHECK_OKAY( SCIPincludeNodeselBfs(scip) );
   CHECK_OKAY( SCIPincludeNodeselDfs(scip) );
   /*CHECK_OKAY( SCIPincludeBranchruleFullstrong(scip) );*/
   CHECK_OKAY( SCIPincludeBranchruleMostinf(scip) );
   CHECK_OKAY( SCIPincludeBranchruleLeastinf(scip) );
   CHECK_OKAY( SCIPincludeHeurDiving(scip) );
   CHECK_OKAY( SCIPincludeHeurRounding(scip) );
   CHECK_OKAY( SCIPincludeSepaGomory(scip) );
   
   /*CHECK_OKAY( includeTestEventHdlr(scip) );*/


   /**************
    * Parameters *
    **************/

   /*CHECK_OKAY( SCIPwriteParams(scip, "scip.set", TRUE) );*/
   if( SCIPfileExists("scip.set") )
   {
      printf("reading parameter file <scip.set>\n");
      CHECK_OKAY( SCIPreadParams(scip, "scip.set") );
   }
   else
      printf("parameter file <scip.set> not found - using default parameters\n");



   /********************
    * Problem Creation *
    ********************/

   if( argc < 2 )
   {
      printf("syntax: %s <problem>\n", argv[0]);
      return SCIP_OKAY;
   }

   printf("\nread problem <%s>\n", argv[1]);
   printf("============\n\n");
   CHECK_OKAY( SCIPreadProb(scip, argv[1]) );



   /*******************
    * Problem Solving *
    *******************/

   /* solve problem */
   printf("\nsolve problem\n");
   printf("=============\n\n");
   CHECK_OKAY( SCIPsolve(scip) );

#if 1
   printf("\ntransformed primal solution:\n");
   printf("============================\n\n");
   CHECK_OKAY( SCIPprintBestTransSol(scip, NULL) );
#endif

#if 1
   printf("\nprimal solution:\n");
   printf("================\n\n");
   CHECK_OKAY( SCIPprintBestSol(scip, NULL) );
#endif

#ifndef NDEBUG
   /*SCIPdebugMemory(scip);*/
#endif


   /**************
    * Statistics *
    **************/

   printf("\nStatistics\n");
   printf("==========\n\n");

   CHECK_OKAY( SCIPprintStatistics(scip, NULL) );


   /********************
    * Deinitialization *
    ********************/

   printf("\nfree SCIP\n");
   printf("=========\n\n");

   /* free SCIP */
   CHECK_OKAY( SCIPfree(&scip) );


   /*****************************
    * Local Memory Deallocation *
    *****************************/

#ifndef NDEBUG
   memoryCheckEmpty();
#endif

   return SCIP_OKAY;
}

int
main(
   int              argc,
   char**           argv
   )
{
   RETCODE retcode;

   todoMessage("implement remaining events");
   todoMessage("avoid addition of identical rows");
   todoMessage("avoid addition of identical constraints");
   todoMessage("pricing for pseudo solutions");
   todoMessage("integrality check on objective function, abort if gap is below 1.0");
   todoMessage("numerical problems in tree->actpseudoobjval if variable's bounds are infinity");
   todoMessage("implement reduced cost fixing");
   todoMessage("statistics: count domain reductions and constraint additions of constraint handlers");
   todoMessage("it's a bit ugly, that user call backs may be called before the nodequeue was processed");
   todoMessage("information method if parameter changed");

   retcode = runSCIP(argc, argv);
   if( retcode != SCIP_OKAY )
   {
      SCIPprintError(retcode, stderr);
      return -1;
   }

   return 0;
}
