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
#pragma ident "@(#) $Id: branch_fullstrong.c,v 1.34 2005/01/18 09:26:41 bzfpfend Exp $"

/**@file   branch_fullstrong.c
 * @brief  full strong LP branching rule
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "branch_fullstrong.h"


#define BRANCHRULE_NAME          "fullstrong"
#define BRANCHRULE_DESC          "full strong branching"
#define BRANCHRULE_PRIORITY      0
#define BRANCHRULE_MAXDEPTH      -1
#define BRANCHRULE_MAXBOUNDDIST  1.0


/** branching rule data */
struct BranchruleData
{
   int              lastcand;           /**< last evaluated candidate of last branching rule execution */
};




/*
 * Callback methods
 */

/** destructor of branching rule to free user data (called when SCIP is exiting) */
static
DECL_BRANCHFREE(branchFreeFullstrong)
{  /*lint --e{715}*/
   BRANCHRULEDATA* branchruledata;

   /* free branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   SCIPfreeMemory(scip, &branchruledata);
   SCIPbranchruleSetData(branchrule, NULL);

   return SCIP_OKAY;
}


/** initialization method of branching rule (called after problem was transformed) */
static
DECL_BRANCHINIT(branchInitFullstrong)
{
   BRANCHRULEDATA* branchruledata;

   /* init branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   branchruledata->lastcand = 0;

   return SCIP_OKAY;
}


/** deinitialization method of branching rule (called before transformed problem is freed) */
#define branchExitFullstrong NULL


/** solving process initialization method of branching rule (called when branch and bound process is about to begin) */
#define branchInitsolFullstrong NULL


/** solving process deinitialization method of branching rule (called before branch and bound process data is freed) */
#define branchExitsolFullstrong NULL


/** branching execution method for fractional LP solutions */
static
DECL_BRANCHEXECLP(branchExeclpFullstrong)
{  /*lint --e{715}*/
   BRANCHRULEDATA* branchruledata;
   VAR** lpcands;
   Real* lpcandssol;
   Real* lpcandsfrac;
   Real cutoffbound;
   Real lpobjval;
   Real bestdown;
   Real bestup;
   Real bestscore;
   Real provedbound;
   Bool allcolsinlp;
   Bool exactsolve;
   int nlpcands;
   int npriolpcands;
   int bestcand;

   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   debugMessage("Execlp method of fullstrong branching\n");

   *result = SCIP_DIDNOTRUN;

   /* get branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   /* get current LP objective bound of the local sub problem and global cutoff bound */
   lpobjval = SCIPgetLPObjval(scip);
   cutoffbound = SCIPgetCutoffbound(scip);

   /* check, if we want to solve the problem exactly, meaning that strong branching information is not useful
    * for cutting off sub problems and improving lower bounds of children
    */
   exactsolve = SCIPisExactSolve(scip);

   /* check, if all existing columns are in LP, and thus the strong branching results give lower bounds */
   allcolsinlp = SCIPallColsInLP(scip);

   /* get branching candidates */
   CHECK_OKAY( SCIPgetLPBranchCands(scip, &lpcands, &lpcandssol, &lpcandsfrac, &nlpcands, &npriolpcands) );
   assert(nlpcands > 0);
   assert(npriolpcands > 0);

   /* if only one candidate exists, choose this one without applying strong branching */
   bestcand = 0;
   bestdown = lpobjval;
   bestup = lpobjval;
   bestscore = -SCIPinfinity(scip);
   provedbound = lpobjval;
   if( nlpcands > 1 )
   {
      Real down;
      Real up;
      Real downgain;
      Real upgain;
      Real score;
      Bool lperror;
      Bool downinf;
      Bool upinf;
      Bool downconflict;
      Bool upconflict;
      int i;
      int c;

      /* search the full strong candidate
       * cycle through the candidates, starting with the position evaluated in the last run
       */
      for( i = 0, c = branchruledata->lastcand; i < nlpcands; ++i, ++c )
      {
         c = c % nlpcands;
         assert(lpcands[c] != NULL);

         debugMessage("applying strong branching on variable <%s> with solution %g\n",
            SCIPvarGetName(lpcands[c]), lpcandssol[c]);

         CHECK_OKAY( SCIPgetVarStrongbranch(scip, lpcands[c], INT_MAX, 
               &down, &up, &downinf, &upinf, &downconflict, &upconflict, &lperror) );

         /* display node information line in root node */
         if( SCIPgetDepth(scip) == 0 && SCIPgetNStrongbranchs(scip) % 100 == 0 )
         {
            CHECK_OKAY( SCIPprintDisplayLine(scip, NULL, SCIP_VERBLEVEL_HIGH) );
         }

         /* check for an error in strong branching */
         if( lperror )
         {
            SCIPmessage(scip, SCIP_VERBLEVEL_HIGH,
               "(node %lld) error in strong branching call for variable <%s> with solution %g\n", 
               SCIPgetNNodes(scip), SCIPvarGetName(lpcands[c]), lpcandssol[c]);
            break;
         }

         /* evaluate strong branching */
         down = MAX(down, lpobjval);
         up = MAX(up, lpobjval);
         downgain = down - lpobjval;
         upgain = up - lpobjval;
         assert(!allcolsinlp || exactsolve || downinf == SCIPisGE(scip, down, cutoffbound));
         assert(!allcolsinlp || exactsolve || upinf == SCIPisGE(scip, up, cutoffbound));
         assert(downinf || !downconflict);
         assert(upinf || !upconflict);

         /* check if there are infeasible roundings */
         if( downinf || upinf )
         {
            assert(allcolsinlp);
            assert(!exactsolve);
            
            /* if for both infeasibilities, a conflict clause was created, we don't need to fix the variable by hand,
             * but better wait for the next propagation round to fix them as an inference, and potentially produce a
             * cutoff that can be analyzed
             */
            if( allowaddcons && downinf == downconflict && upinf == upconflict )
            {
               *result = SCIP_CONSADDED;
               break; /* terminate initialization loop, because constraint was added */
            }
            else if( downinf && upinf )
            {
               /* both roundings are infeasible -> node is infeasible */
               *result = SCIP_CUTOFF;
               debugMessage(" -> variable <%s> is infeasible in both directions\n", SCIPvarGetName(lpcands[c]));
               break; /* terminate initialization loop, because node is infeasible */
            }
            else if( downinf )
            {
               /* downwards rounding is infeasible -> change lower bound of variable to upward rounding */
               CHECK_OKAY( SCIPchgVarLb(scip, lpcands[c], SCIPfeasCeil(scip, lpcandssol[c])) );
               *result = SCIP_REDUCEDDOM;
               debugMessage(" -> variable <%s> is infeasible in downward branch\n", SCIPvarGetName(lpcands[c]));
               break; /* terminate initialization loop, because LP was changed */
            }
            else
            {
               /* upwards rounding is infeasible -> change upper bound of variable to downward rounding */
               assert(upinf);
               CHECK_OKAY( SCIPchgVarUb(scip, lpcands[c], SCIPfeasFloor(scip, lpcandssol[c])) );
               *result = SCIP_REDUCEDDOM;
               debugMessage(" -> variable <%s> is infeasible in upward branch\n", SCIPvarGetName(lpcands[c]));
               break; /* terminate initialization loop, because LP was changed */
            }
         }
         else if( allcolsinlp && !exactsolve )
         {
            Real minbound;
            
            /* the minimal lower bound of both children is a proved lower bound of the current subtree */
            minbound = MIN(down, up);
            provedbound = MAX(provedbound, minbound);
         }

         /* check for a better score, if we are within the maximum priority candidates */
         if( c < npriolpcands )
         {
            score = SCIPgetBranchScore(scip, lpcands[c], downgain, upgain);
            if( score > bestscore )
            {
               bestcand = c;
               bestdown = down;
               bestup = up;
               bestscore = score;
            }
         }

         /* update pseudo cost values */
         if( !downinf )
         {
            CHECK_OKAY( SCIPupdateVarPseudocost(scip, lpcands[c], 0.0-lpcandsfrac[c], downgain, 1.0) );
         }
         if( !upinf )
         {
            CHECK_OKAY( SCIPupdateVarPseudocost(scip, lpcands[c], 1.0-lpcandsfrac[c], upgain, 1.0) );
         }

         debugMessage(" -> cand %d/%d (prio:%d) var <%s> (solval=%g, downgain=%g, upgain=%g, score=%g) -- best: <%s> (%g)\n",
            c, nlpcands, npriolpcands, SCIPvarGetName(lpcands[c]), lpcandssol[c], downgain, upgain, score,
            SCIPvarGetName(lpcands[bestcand]), bestscore);
      }

      /* remember last evaluated candidate */
      branchruledata->lastcand = c;
   }

   if( *result != SCIP_CUTOFF && *result != SCIP_REDUCEDDOM && *result != SCIP_CONSADDED )
   {
      NODE* node;
      Real downprio;

      assert(*result == SCIP_DIDNOTRUN);
      assert(0 <= bestcand && bestcand < nlpcands);
      assert(SCIPisLT(scip, provedbound, cutoffbound));

      /* perform the branching */
      debugMessage(" -> %d candidates, selected candidate %d: variable <%s> (solval=%g, down=%g, up=%g, score=%g)\n",
         nlpcands, bestcand, SCIPvarGetName(lpcands[bestcand]), lpcandssol[bestcand], bestdown, bestup, bestscore);

      /* choose preferred branching direction */
      switch( SCIPvarGetBranchDirection(lpcands[bestcand]) )
      {
      case SCIP_BRANCHDIR_DOWNWARDS:
         downprio = 1.0;
         break;
      case SCIP_BRANCHDIR_UPWARDS:
         downprio = -1.0;
         break;
      case SCIP_BRANCHDIR_AUTO:
         downprio = SCIPvarGetRootSol(lpcands[bestcand]) - lpcandssol[bestcand];
         break;
      default:
         errorMessage("invalid preferred branching direction <%d> of variable <%s>\n", 
            SCIPvarGetBranchDirection(lpcands[bestcand]), SCIPvarGetName(lpcands[bestcand]));
         return SCIP_INVALIDDATA;
      }

      /* create child node with x <= floor(x') */
      debugMessage(" -> creating child: <%s> <= %g\n",
         SCIPvarGetName(lpcands[bestcand]), SCIPfeasFloor(scip, lpcandssol[bestcand]));
      CHECK_OKAY( SCIPcreateChild(scip, &node, downprio) );
      CHECK_OKAY( SCIPchgVarUbNode(scip, node, lpcands[bestcand], SCIPfeasFloor(scip, lpcandssol[bestcand])) );
      if( allcolsinlp && !exactsolve )
      {
         CHECK_OKAY( SCIPupdateNodeLowerbound(scip, node, MAX(provedbound, bestdown)) );
      }
      debugMessage(" -> child's lowerbound: %g\n", SCIPnodeGetLowerbound(node));
      
      /* create child node with x >= ceil(x') */
      debugMessage(" -> creating child: <%s> >= %g\n", 
         SCIPvarGetName(lpcands[bestcand]), SCIPfeasCeil(scip, lpcandssol[bestcand]));
      CHECK_OKAY( SCIPcreateChild(scip, &node, -downprio) );
      CHECK_OKAY( SCIPchgVarLbNode(scip, node, lpcands[bestcand], SCIPfeasCeil(scip, lpcandssol[bestcand])) );
      if( allcolsinlp && !exactsolve )
      {
         CHECK_OKAY( SCIPupdateNodeLowerbound(scip, node, MAX(provedbound, bestup)) );
      }
      debugMessage(" -> child's lowerbound: %g\n", SCIPnodeGetLowerbound(node));

      *result = SCIP_BRANCHED;
   }

   return SCIP_OKAY;
}


/** branching execution method for not completely fixed pseudo solutions */
#define branchExecpsFullstrong NULL




/*
 * branching specific interface methods
 */

/** creates the full strong LP braching rule and includes it in SCIP */
RETCODE SCIPincludeBranchruleFullstrong(
   SCIP*            scip                /**< SCIP data structure */
   )
{
   BRANCHRULEDATA* branchruledata;

   /* create fullstrong branching rule data */
   CHECK_OKAY( SCIPallocMemory(scip, &branchruledata) );
   branchruledata->lastcand = 0;

   /* include fullstrong branching rule */
   CHECK_OKAY( SCIPincludeBranchrule(scip, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY, 
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST,
         branchFreeFullstrong, branchInitFullstrong, branchExitFullstrong, 
         branchInitsolFullstrong, branchExitsolFullstrong, 
         branchExeclpFullstrong, branchExecpsFullstrong,
         branchruledata) );

   return SCIP_OKAY;
}
