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

/**@file   benderscut_opt.c
 * @brief  Generates a standard Benders' decomposition optimality cut
 * @author Stephen J. Maher
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/benderscut_opt.h"
#include "scip/pub_benders.h"
#include "scip/pub_benderscut.h"
#include "scip/misc_benders.h"

#include "scip/cons_linear.h"
#include "scip/pub_lp.h"


#define BENDERSCUT_NAME             "optimality"
#define BENDERSCUT_DESC             "Standard Benders' decomposition optimality cut"
#define BENDERSCUT_PRIORITY         0
#define BENDERSCUT_LPCUT            TRUE



#define SCIP_DEFAULT_SOLTOL               1e-2  /** The tolerance used to determine optimality of the solution */
#define SCIP_DEFAULT_ADDCUTS             FALSE  /** Should cuts be generated, instead of constraints */

/*
 * Data structures
 */

/** Benders' decomposition cuts data */
struct SCIP_BenderscutData
{
   SCIP_Real             soltol;             /**< the tolerance for the check between the auxiliary var and subprob */
   SCIP_Bool             addcuts;            /**< should cuts be generated instead of constraints */
};


/*
 * Local methods
 */

/* computing as standard Benders' optimality cut from the dual solutions of the LP */
static
SCIP_RETCODE computeStandardOptimalityCut(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP*                 subproblem,         /**< the SCIP instance of the subproblem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   SCIP_CONS*            cons,               /**< the constraint for the generated cut, can be NULL */
   SCIP_ROW*             row,                /**< the row for the generated cut, can be NULL */
   SCIP_Bool             addcut              /**< indicates whether a cut is created instead of a constraint */
   )
{
   SCIP_VAR** vars;
   SCIP_VAR** fixedvars;
   SCIP_CONS** conss;
   int nvars;
   int nfixedvars;
   int nconss;
   SCIP_Real dualsol;
   SCIP_Real addval;
   SCIP_Real lhs;
   int i;

#ifndef NDEBUG
   SCIP_Real verifyobj = 0;
   SCIP_Real checkobj = 0;
#endif

   assert(masterprob != NULL);
   assert(subproblem != NULL);
   assert(benders != NULL);
   assert(cons != NULL || addcut);
   assert(row != NULL || !addcut);

   nvars = SCIPgetNVars(subproblem);
   vars = SCIPgetVars(subproblem);
   nfixedvars = SCIPgetNFixedVars(subproblem);
   fixedvars = SCIPgetFixedVars(subproblem);

   nconss = SCIPgetNConss(subproblem);
   conss = SCIPgetConss(subproblem);


   /* looping over all constraints and setting the coefficients of the cut */
   for( i = 0; i < nconss; i++ )
   {
      addval = 0;

      dualsol = BDconsGetDualsol(subproblem, conss[i]);

      assert( !SCIPisInfinity(subproblem, dualsol) && !SCIPisInfinity(subproblem, -dualsol) );

      if( SCIPisZero(subproblem, dualsol) )
         continue;

      if( addcut )
         lhs = SCIProwGetLhs(row);
      else
         lhs = SCIPgetLhsLinear(masterprob, cons);


      if( SCIPisPositive(subproblem, dualsol) )
         addval = dualsol*BDconsGetLhs(subproblem, conss[i]);
      else if( SCIPisNegative(subproblem, dualsol) )
         addval = dualsol*BDconsGetRhs(subproblem, conss[i]);

      lhs += addval;

      /* Update the lhs of the cut */
      if( addcut )
      {
         SCIP_CALL( SCIPchgRowLhs(masterprob, row, lhs) );
      }
      else
      {
         SCIP_CALL( SCIPchgLhsLinear(masterprob, cons, lhs) );
      }
   }

   /* looping over all variables to update the coefficients in the computed cut. */
   for( i = 0; i < nvars + nfixedvars; i++ )
   {
      SCIP_VAR* var;
      SCIP_VAR* mastervar;
      SCIP_Real redcost;


      if( i < nvars )
         var = vars[i];
      else
         var = fixedvars[i - nvars];

      /* retreiving the master problem variable for the given subproblem variable. */
      SCIP_CALL( SCIPgetBendersMasterVar(masterprob, benders, var, &mastervar) );

      redcost = SCIPgetVarRedcost(subproblem, var);

#ifndef NDEBUG
      checkobj += SCIPvarGetUnchangedObj(var)*SCIPvarGetSol(var, TRUE);
#endif

      /* checking whether the subproblem variable has a corresponding master variable. */
      if( mastervar != NULL )
      {
         SCIP_Real coef;

         coef = -1.0*(SCIPvarGetObj(var) + redcost);

         if( addcut )
         {
            SCIP_CALL( SCIPaddVarToRow(masterprob, row, mastervar, coef) );
         }
         else
         {
            SCIP_CALL( SCIPaddCoefLinear(masterprob, cons, mastervar, coef) );
         }
      }
      else
      {
         if( !SCIPisZero(subproblem, redcost) )
         {
            addval = 0;

            /* get current lhs of the subproblem cut */
            if( addcut )
               lhs = SCIProwGetLhs(row);
            else
               lhs = SCIPgetLhsLinear(masterprob, cons);

            if( SCIPisPositive(subproblem, redcost) )
               addval = redcost*SCIPvarGetLbLocal(var);
            else if( SCIPisNegative(subproblem, redcost) )
               addval = redcost*SCIPvarGetUbLocal(var);

            lhs += addval;

            /* Update lhs */
            if( addcut )
            {
               SCIP_CALL( SCIPchgRowLhs(masterprob, row, lhs) );
            }
            else
            {
               SCIP_CALL( SCIPchgLhsLinear(masterprob, cons, lhs) );
            }
         }
      }
   }

#ifndef NDEBUG
   if( addcut )
      lhs = SCIProwGetLhs(row);
   else
      lhs = SCIPgetLhsLinear(masterprob, cons);
   verifyobj += lhs;

   if( addcut )
      verifyobj -= SCIPgetRowSolActivity(masterprob, row, sol);
   else
      verifyobj -= SCIPgetActivityLinear(masterprob, cons, sol);
#endif

   assert(SCIPisFeasEQ(masterprob, checkobj, verifyobj));

   assert(cons != NULL);

   return SCIP_OKAY;
}


/** adds the auxiliary variable to the generated cut. If this is the first optimality cut for the subproblem, then the
 *  auxiliary variable is first created and added to the master problem.
 */
static
SCIP_RETCODE addAuxiliaryVariableToCut(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_CONS*            cons,               /**< the constraint for the generated cut, can be NULL */
   SCIP_ROW*             row,                /**< the row for the generated cut, can be NULL */
   int                   probnumber,         /**< the number of the pricing problem */
   SCIP_Bool             addcut              /**< indicates whether a cut is created instead of a constraint */
   )
{
   SCIP_VAR* auxiliaryvar;

   assert(masterprob != NULL);
   assert(benders != NULL);
   assert(cons != NULL || addcut);
   assert(row != NULL || !addcut);

   auxiliaryvar = SCIPbendersGetAuxiliaryVar(benders, probnumber);

   /* adding the auxiliary variable to the generated cut */
   if( addcut )
   {
      SCIP_CALL( SCIPaddVarToRow(masterprob, row, auxiliaryvar, 1.0) );
   }
   else
   {
      SCIP_CALL( SCIPaddCoefLinear(masterprob, cons, auxiliaryvar, 1.0) );
   }

   return SCIP_OKAY;
}


/** generates and applies Benders' cuts */
static
SCIP_RETCODE generateAndApplyBendersCuts(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP*                 subproblem,         /**< the SCIP instance of the pricing problem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition */
   SCIP_BENDERSCUT*      benderscut,         /**< the benders' decomposition cut method */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   int                   probnumber,         /**< the number of the pricing problem */
   SCIP_BENDERSENFOTYPE  type,               /**< the enforcement type calling this function */
   SCIP_RESULT*          result              /**< the result from solving the subproblems */
   )
{
   SCIP_BENDERSCUTDATA* benderscutdata;
   SCIP_CONSHDLR* consbenders;
   SCIP_CONS* cons;
   SCIP_ROW* row;
   char cutname[SCIP_MAXSTRLEN];
   SCIP_Bool optimal;
   SCIP_Bool addcut;

   assert(masterprob != NULL);
   assert(subproblem != NULL);
   assert(benders != NULL);
   assert(benderscut != NULL);
   assert(result != NULL);
   assert(SCIPgetStatus(subproblem) == SCIP_STATUS_OPTIMAL || SCIPgetLPSolstat(subproblem) == SCIP_LPSOLSTAT_OPTIMAL);

   row = NULL;
   cons = NULL;

   /* retreiving the Benders' cut data */
   benderscutdata = SCIPbenderscutGetData(benderscut);

   /* if the cuts are generated prior to the solving stage, then rows can not be generated. So constraints must be
    * added to the master problem.
    */
   if( SCIPgetStage(masterprob) < SCIP_STAGE_INITSOLVE )
      addcut = FALSE;
   else
      addcut = benderscutdata->addcuts;

   /* retrieving the Benders' decomposition constraint handler */
   consbenders = SCIPfindConshdlr(masterprob, "benders");

   /* checking the optimality of the original problem with a comparison between the auxiliary variable and the
    * objective value of the subproblem */
   SCIP_CALL( SCIPcheckBendersSubprobOptimality(masterprob, benders, sol, probnumber, &optimal) );

   if( optimal )
   {
      SCIPdebugMsg(masterprob, "No cut added for subproblem %d\n", probnumber);
      return SCIP_OKAY;
   }

   /* setting the name of the generated cut */
   (void) SCIPsnprintf(cutname, SCIP_MAXSTRLEN, "optimalitycut_%d_%d", probnumber,
      SCIPbenderscutGetNFound(benderscut) );

   /* creating an empty row or constraint for the Benders' cut */
   if( addcut )
   {
      SCIP_CALL( SCIPcreateEmptyRowCons(masterprob, &row, consbenders, cutname, 0.0, SCIPinfinity(masterprob), FALSE,
            FALSE, TRUE) );
   }
   else
   {
      SCIP_CALL( SCIPcreateConsBasicLinear(masterprob, &cons, cutname, 0, NULL, NULL, 0.0, SCIPinfinity(masterprob)) );
      SCIP_CALL( SCIPsetConsDynamic(masterprob, cons, TRUE) );
      SCIP_CALL( SCIPsetConsRemovable(masterprob, cons, TRUE) );
   }

   /* computing the coefficients of the optimality cut */
   SCIP_CALL( computeStandardOptimalityCut(masterprob, subproblem, benders, sol, cons, row, addcut) );

   /* adding the auxiliary variable to the optimality cut */
   SCIP_CALL( addAuxiliaryVariableToCut(masterprob, benders, cons, row, probnumber, addcut) );

   /* adding the constraint to the master problem */
   if( addcut )
   {
      SCIP_Bool infeasible;

      if( type == SCIP_BENDERSENFOTYPE_LP || type == SCIP_BENDERSENFOTYPE_RELAX )
      {
         SCIP_CALL( SCIPaddRow(masterprob, row, FALSE, &infeasible) );
         assert(!infeasible);
      }
      else
      {
         assert(type == SCIP_BENDERSENFOTYPE_CHECK || type == SCIP_BENDERSENFOTYPE_PSEUDO);
         SCIP_CALL( SCIPaddPoolCut(masterprob, row) );
      }

      /* storing the generated cut */
      SCIP_CALL( SCIPstoreBenderscutCut(masterprob, benderscut, row) );

      /* release the row */
      SCIP_CALL( SCIPreleaseRow(masterprob, &row) );

      (*result) = SCIP_SEPARATED;
   }
   else
   {
      SCIP_CALL( SCIPaddCons(masterprob, cons) );

      SCIPdebugPrintCons(masterprob, cons, NULL);

      /* storing the generated cut */
      SCIP_CALL( SCIPstoreBenderscutCons(masterprob, benderscut, cons) );

      SCIP_CALL( SCIPreleaseCons(masterprob, &cons) );

      (*result) = SCIP_CONSADDED;
   }

   return SCIP_OKAY;
}

/*
 * Callback methods of Benders' decomposition cuts
 */

/** destructor of Benders' decomposition cuts to free user data (called when SCIP is exiting) */
static
SCIP_DECL_BENDERSCUTFREE(benderscutFreeOpt)
{  /*lint --e{715}*/
   SCIP_BENDERSCUTDATA* benderscutdata;

   assert( benderscut != NULL );
   assert( strcmp(SCIPbenderscutGetName(benderscut), BENDERSCUT_NAME) == 0 );

   /* free Benders' cut data */
   benderscutdata = SCIPbenderscutGetData(benderscut);
   assert( benderscutdata != NULL );

   SCIPfreeBlockMemory(scip, &benderscutdata);

   SCIPbenderscutSetData(benderscut, NULL);

   return SCIP_OKAY;
}


/** execution method of Benders' decomposition cuts */
static
SCIP_DECL_BENDERSCUTEXEC(benderscutExecOpt)
{  /*lint --e{715}*/
   SCIP* subproblem;

   assert(scip != NULL);
   assert(benders != NULL);
   assert(benderscut != NULL);
   assert(result != NULL);
   assert(probnumber >= 0 && probnumber < SCIPbendersGetNSubproblems(benders));

   subproblem = SCIPbendersSubproblem(benders, probnumber);

   /* only generate optimality cuts if the subproblem is optimal */
   if( SCIPgetStatus(subproblem) == SCIP_STATUS_OPTIMAL ||
    (SCIPgetStage(subproblem) == SCIP_STAGE_SOLVING && SCIPgetLPSolstat(subproblem) == SCIP_LPSOLSTAT_OPTIMAL) )
   {
      /* generating a cut for a given subproblem */
      SCIP_CALL( generateAndApplyBendersCuts(scip, subproblem, benders, benderscut,
            sol, probnumber, type, result) );
   }

   return SCIP_OKAY;
}


/*
 * Benders' decomposition cuts specific interface methods
 */

/** creates the opt Benders' decomposition cuts and includes it in SCIP */
SCIP_RETCODE SCIPincludeBenderscutOpt(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   SCIP_BENDERSCUTDATA* benderscutdata;
   SCIP_BENDERSCUT* benderscut;
   char paramname[SCIP_MAXSTRLEN];

   assert(benders != NULL);

   /* create opt Benders' decomposition cuts data */
   SCIP_CALL( SCIPallocBlockMemory(scip, &benderscutdata) );
   benderscutdata->soltol = 1e-04;

   benderscut = NULL;

   /* include Benders' decomposition cuts */
   SCIP_CALL( SCIPincludeBenderscutBasic(scip, benders, &benderscut, BENDERSCUT_NAME, BENDERSCUT_DESC,
         BENDERSCUT_PRIORITY, BENDERSCUT_LPCUT, benderscutExecOpt, benderscutdata) );

   assert(benderscut != NULL);

   /* setting the non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetBenderscutFree(scip, benderscut, benderscutFreeOpt) );

   /* add opt Benders' decomposition cuts parameters */
   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/benderscut/%s/solutiontol",
      SCIPbendersGetName(benders), BENDERSCUT_NAME);
   SCIP_CALL( SCIPaddRealParam(scip, paramname,
         "the tolerance used for the comparison between the auxiliary variable and the subproblem objective.",
         &benderscutdata->soltol, FALSE, SCIP_DEFAULT_SOLTOL, 0.0, 1.0, NULL, NULL) );

   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/benderscut/%s/addcuts",
      SCIPbendersGetName(benders), BENDERSCUT_NAME);
   SCIP_CALL( SCIPaddBoolParam(scip, paramname,
         "should cuts be generated and added to the cutpool instead of global constraints directly added to the problem.",
         &benderscutdata->addcuts, FALSE, SCIP_DEFAULT_ADDCUTS, NULL, NULL) );

   return SCIP_OKAY;
}
