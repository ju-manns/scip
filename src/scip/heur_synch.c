/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2016 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   heur_synch.c
 * @brief  primal heuristic that adds solutions from synchronization
 * @author Robert Lion Gottwald
 *
 * This heuristic takes solutions during synchronization and then adds them.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/heur_synch.h"
#include "scip/scip.h"


#define HEUR_NAME             "synch"
#define HEUR_DESC             "synch solution heuristic"
#define HEUR_DISPCHAR         '$'
#define HEUR_PRIORITY         -3000000     /* should process after all other heuristics */
#define HEUR_FREQ             -1
#define HEUR_FREQOFS          0
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           SCIP_HEURTIMING_DURINGLPLOOP | SCIP_HEURTIMING_BEFOREPRESOL | SCIP_HEURTIMING_BEFORENODE
#define HEUR_USESSUBSCIP      FALSE  /**< does the heuristic use a secondary SCIP instance? */


/*
 * Data structures
 */


/** primal heuristic data */
struct SCIP_HeurData
{
   SCIP_SOL**            sols;               /**< storing solutions passed to heuristic sorted by objective value */
   int                   nsols;              /**< number of soluions stored */
   int                   maxnsols;           /**< maximum number of solutions that can be stored */
};


/*
 * Callback methods of primal heuristic
 */

/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
static
SCIP_DECL_HEURFREE(heurFreeSynch)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert( heur != NULL );
   assert( strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0 );
   assert( scip != NULL );

   SCIPdebugMessage("free method of synch primal heuristic.\n");

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);
   assert(heurdata->nsols == 0);
   SCIPfreeMemoryArray(scip, &heurdata->sols);
   SCIPfreeMemory(scip, &heurdata);

   return SCIP_OKAY;
}


/** deinitialization method of primal heuristic (called before transformed problem is freed) */
static
SCIP_DECL_HEUREXITSOL(heurExitSynch)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;
   int            i;

   assert( heur != NULL );
   assert( strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0 );
   assert( scip != NULL );

   SCIPdebugMessage("exit method of synch primal heuristic.\n");

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* free solution if one is still present */
   for( i = 0; i < heurdata->nsols; ++i )
   {
      SCIP_CALL( SCIPfreeSol(scip, &heurdata->sols[i]) );
   }
   heurdata->nsols = 0;
   return SCIP_OKAY;
}


/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecSynch)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;
   SCIP_Bool stored;
   int i;

   assert( heur != NULL );
   assert( strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0 );
   assert( scip != NULL );
   assert( result != NULL );

   SCIPheurSetFreq(heur, -1);

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);
   assert(heurdata->nsols > 0);

   SCIPdebugMessage("exec method of synch primal heuristic.\n");
   *result = SCIP_DIDNOTFIND;
   for( i = 0; i < heurdata->nsols; ++i )
   {
      SCIP_CALL( SCIPtrySolFree(scip, &heurdata->sols[i], FALSE, FALSE, FALSE, FALSE, FALSE, &stored) );
      if( stored )
         *result = SCIP_FOUNDSOL;

   }

   heurdata->nsols = 0;

   return SCIP_OKAY;
}

/*
 * primal heuristic specific interface methods
 */

/** creates the synch primal heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurSynch(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_HEURDATA* heurdata;
   SCIP_HEUR*     heur;

   /* create heuristic data */
   SCIP_CALL( SCIPallocMemory(scip, &heurdata) );
   SCIP_CALL( SCIPgetIntParam(scip, "concurrent/sync/maxnsols", &heurdata->maxnsols) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &heurdata->sols, heurdata->maxnsols) );
   heurdata->nsols = 0;

   /* include primal heuristic */
   SCIP_CALL( SCIPincludeHeurBasic(scip, &heur,
         HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING, HEUR_USESSUBSCIP, heurExecSynch, heurdata) );

   assert(heur != NULL);

   /* set non-NULL pointers to callback methods */
   SCIP_CALL( SCIPsetHeurFree(scip, heur, heurFreeSynch) );
   SCIP_CALL( SCIPsetHeurExit(scip, heur, heurExitSynch) );

   return SCIP_OKAY;
}


/** pass solution to synch heuristic */
SCIP_RETCODE SCIPheurSynchPassSol(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< synch heuristic */
   SCIP_SOL*             sol                 /**< solution to be passed */
   )
{
   SCIP_HEURDATA* heurdata;
   SCIP_Real      solobj;
   int            i;
   assert(scip != NULL);
   assert(heur != NULL);
   assert(sol != NULL);
   assert(strcmp(HEUR_NAME, SCIPheurGetName(heur)) == 0);
   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   SCIPsolSetHeur(sol, heur);
   solobj = SCIPgetSolTransObj(scip, sol);
   /* check if we have an empty space in the solution array or
    * if we need to discard the worst solution */
   if( heurdata->nsols < heurdata->maxnsols )
   {
      /* if the maximum number of solutions is not yet reached just
       * insert the solution sorted by its objective value */
      i = heurdata->nsols++;
      while( i > 0 && solobj > SCIPgetSolTransObj(scip, heurdata->sols[i - 1]) )
      {
         heurdata->sols[i] =  heurdata->sols[i - 1];
         --i;
      }
      heurdata->sols[i] = sol;
   }
   else
   {
      /* already have reached the maximum number of solutions so
       * we need to check if the solution is better than a previous
       * one and free the worst solution to make room for it if that
       * is the case */
      i = 0;
      while( i < heurdata->nsols && solobj < SCIPgetSolTransObj(scip, heurdata->sols[i]) )
      {
         if( i > 0 )
         {
            heurdata->sols[i - 1] = heurdata->sols[i];
         }
         else
         {
            SCIP_CALL( SCIPfreeSol(scip, &heurdata->sols[i]) );
         }

         ++i;
      }
      /* check if solution must be inserted or discarded */
      if( i > 0)
      {
         /* found position to insert the solution sorted by objective value */
         heurdata->sols[i-1] = sol;
      }
      else
      {
         /* solution is not better just discard it */
         SCIPfreeSol(scip, &sol);
      }
   }

   assert(heurdata->nsols <= heurdata->maxnsols);
   SCIPheurSetFreq(heur, 1);
   return SCIP_OKAY;
}
