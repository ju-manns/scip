/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2004 Tobias Achterberg                              */
/*                                                                           */
/*                  2002-2004 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the SCIP Academic Licence.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: sepa_cmir.c,v 1.2 2004/05/28 10:35:20 bzfpfend Exp $"

/**@file   sepa_cmir.c
 * @brief  cmir separator
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>

#include "sepa_cmir.h"


#define SEPA_NAME              "cmir"
#define SEPA_DESC              "complemented mixed integer rounding cuts separator (Marchand's version)"
#define SEPA_PRIORITY             -1000
#define SEPA_FREQ                    10

#define DEFAULT_MAXROUNDS             5 /**< maximal number of cmir separation rounds per node */
#define DEFAULT_MAXROUNDSROOT         9 /**< maximal number of cmir separation rounds in the root node */
#define DEFAULT_MAXSEPACUTS          25 /**< maximal number of cmir cuts separated per separation round */
#define DEFAULT_MAXSEPACUTSROOT      75 /**< maximal number of cmir cuts separated per separation round in root node */
#define DEFAULT_MAXAGGRS              5 /**< maximal number of aggregations for each row per separation round */
#define DEFAULT_MAXAGGRSROOT          8 /**< maximal number of aggreagtions for each row per separation r. in the root node */
#define DEFAULT_DYNAMICCUTS       FALSE /**< should generated cuts be removed from the LP if they are no longer tight? */
#define DEFAULT_MAXSLACK            0.1 /**< max. slack of rows to be used */
#define DEFAULT_MAXROWFAC          1e+4 /**< max. row aggregation factor */
#define DEFAULT_MINROWFAC         -1e+4 /**< min. row aggregation factor */
#define DEFAULT_MAXTESTDELTA        128	/**< max. nr. of different deltas to try */
#define DEFAULT_MAXCONT              10 /**< max. nr. of cont. vars in aggregated row */



/*
 * Data structures
 */

/** separator data */
struct SepaData
{
   int              maxrounds;          /**< maximal number of cmir separation rounds per node */
   int              maxroundsroot;      /**< maximal number of cmir separation rounds in the root node */
   int              maxsepacuts;        /**< maximal number of cmir cuts separated per separation round */
   int              maxsepacutsroot;    /**< maximal number of cmir cuts separated per separation round in root node */
   int              maxaggrs;           /**< maximal number of aggregations for each row per separation round */
   int              maxaggrsroot;       /**< maximal number of aggreagtions for each row per sepa. r. in the root node */
   Bool             dynamiccuts;        /**< should generated cuts be removed from the LP if they are no longer tight? */
   Real             maxslack;         	/**< maximal slack of rows to be used as startrow */
   int              maxrowfac;          /**< maximal row aggregation factor */
   int              minrowfac;          /**< minimal row aggregation factor */
   int              maxtestdelta;	/**< maximal number of different deltas to try */
   int              maxcont;	        /**< maximal number of cont. vars in aggregated row */
};

/*
 * Local methods
 */
/** adds given cut to LP if violated */
static
RETCODE addCut(
   SCIP*            scip,               /**< SCIP data structure */
   SEPADATA*        sepadata,           /**< separator data */
   VAR**            vars,               /**< variables in LP */ 
   int              nvars,              /**< number of variables in LP */
   Real*            varsol,             /**< solution value of variables in LP */
   Real*            cutcoefs,           /**< coefficients of all variables in cut */
   Real             cutrhs,             /**< right side of cut */
   int*             ncuts,              /**< number of cuts added */
   int              ncutssum             /**< number of cuts added so far in this separation round */
   )
{
   COL** cutcols;
   Real* cutvals;
   Real cutact;
   Real cutsqrnorm;
   Real cutnorm;
   Real val;
   int cutlen;
   int v;
   Bool success;
   
   assert(scip != NULL);
   assert(sepadata != NULL);      
   assert(varsol != NULL);
   
   /* get temporary memory for storing the cut as sparse row */
   CHECK_OKAY( SCIPallocBufferArray(scip, &cutcols, nvars) );
   CHECK_OKAY( SCIPallocBufferArray(scip, &cutvals, nvars) );
   
   /* store the cut as sparse row, calculate activity of cut */
   cutlen = 0;
   cutact = 0.0;
   cutsqrnorm = 0.0;
   for( v = 0; v < nvars; ++v )
   {
      val = cutcoefs[v];
      if( !SCIPisZero(scip, val) )
      {
         assert(SCIPvarGetStatus(vars[v]) == SCIP_VARSTATUS_COLUMN);
         cutact += val * varsol[v];
         cutsqrnorm += SQR(val);
         cutcols[cutlen] = SCIPvarGetCol(vars[v]);
         cutvals[cutlen] = val;
         cutlen++;
      }
   }
   cutnorm = SQRT(cutsqrnorm);
   
   if( SCIPisPositive(scip, cutnorm)
      && SCIPisFeasGT(scip, cutact, cutrhs)
      && SCIPisCutViolated(scip, cutact/cutnorm, cutrhs/cutnorm) )
   {
      ROW* cut;
      char cutname[MAXSTRLEN];
      
      /* create the cut */
      sprintf(cutname, "cmir%d_%d", SCIPgetNLPs(scip), ncutssum + (*ncuts));
      CHECK_OKAY( SCIPcreateRow(scip, &cut, cutname, cutlen, cutcols, cutvals, -SCIPinfinity(scip), cutrhs, 
                     (SCIPgetDepth(scip) > 0) /*(depth > 0)*/, FALSE, sepadata->dynamiccuts) );

      debugMessage(" -> found potential c-mir cut <%s>: activity=%f, rhs=%f, norm=%f\n",
         cutname, cutact, cutrhs, cutnorm);
      debug(SCIPprintRow(scip, cut, NULL));
      
#if 0 /*????????????????????*/
      /* try to scale the cut to integral values */
      CHECK_OKAY( SCIPmakeRowRational(scip, cut, 100 /*maxdnom*/, 128 /*maxscale*/, &success) );
#else
      success = TRUE;
#endif

      /* if scaling was successful, add the cut */
      if( success )
      {
         cutact = SCIPgetRowLPActivity(scip, cut);
         cutrhs = SCIProwGetRhs(cut);
         cutnorm = SCIProwGetNorm(cut);
         if( SCIPisPositive(scip, cutnorm)
            && SCIPisFeasGT(scip, cutact, cutrhs)
            && SCIPisCutViolated(scip, cutact/cutnorm, cutrhs/cutnorm) )
         {
            // printf("c-mir cut found after skaling\n");
            debugMessage(" -> found c-mir cut <%s>: act=%f, rhs=%f, norm=%f, viol=%f\n",
               cutname, cutact, cutrhs, cutnorm, (cutact-cutrhs)/cutnorm);
            debug(SCIPprintRow(scip, cut, NULL));
            CHECK_OKAY( SCIPaddCut(scip, cut, (cutact-cutrhs)/cutnorm/(cutlen+1)) );
            /**result = SCIP_SEPARATED;*/
            (*ncuts)++;
         }
         else
         {
            debugMessage(" -> c-mir cut <%s> no longer violated: act=%f, rhs=%f, norm=%f, viol=%f\n",
               cutname, cutact, cutrhs, cutnorm, (cutact-cutrhs)/cutnorm);
            debug(SCIPprintRow(scip, cut, NULL));
         }
      }
      
      /* release the row */
      CHECK_OKAY( SCIPreleaseRow(scip, &cut) );
   }
   
   /* free temporary memory */
   CHECK_OKAY( SCIPfreeBufferArray(scip, &cutvals) );
   CHECK_OKAY( SCIPfreeBufferArray(scip, &cutcols) );

   return SCIP_OKAY;   
}

/** aggregates different single mixed integer constraints by taking linear combinations of the rows of the LP  */
static
RETCODE aggregation(
   SCIP*            scip,               /**< SCIP data structure */ 
   SEPADATA*        sepadata,           /**< separator data */
   ROW**            rows,               /**< rows in LP */ 
   int              nrows,              /**< number of rows in LP */
   int              startrow,           /**< index of row to start aggregation */ 
   VAR**            vars,               /**< variables in LP */
   int              nvars,              /**< number of variables in LP */
   Real*            varsol,             /**< LP solution value of all variables in LP */
   int*             ncuts,              /**< pointer to store number of cuts generated */
   int              ncutssum,           /**< number of cuts added so far in this separation round */
   int              maxaggrs            /**< maximal number of aggregations */
   )
{
   Real* aggrcoefs;       /* coefficients of all variables in aggregated row */
   Real* rowweights;      /* weight of rows in all aggregations */ 

   Real* upperbounds;     /* global upper bound of all variables */
   Real* lowerbounds;     /* global lower bound of all variables */

   int nstartnonzcols;    /* number of nonzero columns of startrow */
   COL** startnonzcols;   /* columns with nonzero coefficients of startrow */
   Real* startnonzcoefs;  /* nonzero coefficients of startrow */    
   Real startrowact;      /* activity of startrow */
      
   Real* cutcoefs;         /* coefficients of variables in cut */
   Real cutrhs;            /* right hand side of the cut */
   Bool success;

   int naggrs;
   int numcont;
   COL* bestcol;          

   int ncols;
   COL** cols;

   int var;
   int col;
   int row;

   int maxrowfac;
   int minrowfac;
   int maxtestdelta;
   int maxcont;

   assert(scip != NULL);
   assert(sepadata != NULL);      
   assert(0 <= startrow && startrow < nrows);
   
   debugMessage("start c-MIR aggregation with row <%s> (%d/%d)\n", SCIProwGetName(rows[startrow]), startrow, nrows);

    /* get variables */
   CHECK_OKAY( SCIPgetLPColsData(scip, &cols, &ncols) );

   ncols = SCIPgetNLPCols(scip);

   /* set datastructures */
   CHECK_OKAY( SCIPallocBufferArray(scip, &aggrcoefs, ncols) );
   CHECK_OKAY( SCIPallocBufferArray(scip, &rowweights, nrows) );
   CHECK_OKAY( SCIPallocBufferArray(scip, &cutcoefs, nvars) );
   CHECK_OKAY( SCIPallocBufferArray(scip, &upperbounds, ncols) );
   CHECK_OKAY( SCIPallocBufferArray(scip, &lowerbounds, ncols) );
   
   /* set weights of rows in aggregations done so far */
   for( row = 0; row < nrows; row++ )
      rowweights[row] = 0.0;
   
   startrowact = SCIPgetRowActivity(scip, rows[startrow]);
   if( startrowact <= 0.5 * SCIProwGetLhs(rows[startrow]) + 0.5 * SCIProwGetRhs(rows[startrow]) )
      rowweights[startrow] = -1.0;
   else 
      rowweights[startrow] = 1.0;

   /* get nonzero columns and coefficients of startrow */
   startnonzcols =  SCIProwGetCols(rows[startrow]);
   nstartnonzcols = SCIProwGetNLPNonz(rows[startrow]);
   startnonzcoefs = SCIProwGetVals(rows[startrow]);

   for( col = 0; col < ncols; col++ )
   {
#if 0 /*?????????????????????*/
      lowerbounds[col] = SCIPvarGetLbGlobal(SCIPcolGetVar(cols[col]));
      upperbounds[col] = SCIPvarGetUbGlobal(SCIPcolGetVar(cols[col]));
#else
      lowerbounds[col] = SCIPcolGetLb(cols[col]);
      upperbounds[col] = SCIPcolGetUb(cols[col]);
#endif      
   }
                     
   /* for all columns of startrow store coefficient as coefficient in aggregated row */ 
   clearMemoryArray(aggrcoefs, ncols);
   numcont = 0;
   for( col = 0; col < nstartnonzcols; col++ )
   {
      VAR* var;
      var = SCIPcolGetVar(startnonzcols[col]);
      int pos = SCIPcolGetLPPos(startnonzcols[col]);
      assert(pos >= 0); 
      aggrcoefs[pos] = rowweights[startrow] * startnonzcoefs[col];
      if( SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS )
         numcont++;
   }


   maxrowfac = sepadata->maxrowfac;
   minrowfac = sepadata->minrowfac;
   maxtestdelta = sepadata->maxtestdelta;
   maxcont = sepadata->maxcont;

   (*ncuts) = 0;
   naggrs = 0;

   /* try to generate cut from the current aggregated row 
    * add cut if found, otherwise add another row to aggregated row 
    * in order to get ride of a continuous variabel */  
   do
   {
      if( numcont > maxcont )
         break;

      Real bestdelta;
      Real bestviolation; 

      Real* deltatested;
      int ndeltatested;

      bestcol = NULL;
      
      CHECK_OKAY( SCIPallocBufferArray(scip, &deltatested, ncols) );
      clearMemoryArray(deltatested, ncols);
      ndeltatested = 0;

#ifdef DEBUG
      {
         int i;
         printf("aggregation of %d rows:\n", naggrs);
         for( i = 0; i < nrows; ++i )
         {
            if( rowweights[i] != 0.0 )
            {
               printf(" %g<%s>", rowweights[i], SCIProwGetName(rows[i]));
               SCIProwPrint(rows[i], NULL);
            }
         }
         printf("\n");
         printf("cont. cols:");
         for( i = 0; i < ncols; ++i )
            if( aggrcoefs[i] != 0.0 )
               printf(" %g<%s>", aggrcoefs[i], SCIPvarGetName(SCIPcolGetVar(cols[i])));
         printf("\n");
      }
#endif

      /* search delta for generating a cut with maximum violation: 
       * delta = coefficient of integer variable, which lies between its bounds
       */ 
      bestdelta = 0.0;
      bestviolation = 0.0;
      for( col = 0; col < ncols && ndeltatested < maxtestdelta; col++ )   /*!!!!!!!!!!!!!*/
      {
         VAR* var;
         Real primsol;
         Real cutact;
         Real violation;
         Real delta;
         int i;
         Bool tested;

         tested = FALSE;

         var = SCIPcolGetVar(cols[col]);
         primsol = SCIPcolGetPrimsol(cols[col]);

         /* coefficient of column is candidate for bestdelta */
         if( !SCIPisZero(scip, aggrcoefs[col]) && SCIPvarGetType(var) != SCIP_VARTYPE_CONTINUOUS 
             && SCIPisLT(scip, lowerbounds[col], primsol) && SCIPisLT(scip, primsol, upperbounds[col]) ) 
         {
            delta = 1 / ABS(aggrcoefs[col]);

            /* only not tested delta */
            for( i = 0; !tested && i < ndeltatested; i++ )
               tested = SCIPisEQ(scip, deltatested[i], delta);
            
            if( !tested )
            {

               /* create a MIR cut out of the weighted LP rows */
               CHECK_OKAY( SCIPcalcMIR(scip, 0.05, rowweights, delta, cutcoefs, &cutrhs, &cutact, &success) );
               debugMessage("delta = %g -> success: %d\n", delta, success);
               /* delta generates cut which is more violated */
               if( success )
               {
                  violation = cutact - cutrhs;
                  debugMessage("act = %g  rhs = %g  viol = %g, old bestviol = %g\n", cutact, cutrhs, violation, bestviolation);
                  if( violation > bestviolation )
                  {
                     bestdelta = delta;
                     bestviolation = violation;
                  }  
               }

               deltatested[ndeltatested] = delta;
               ndeltatested++;

            }
         }
      }
  
      CHECK_OKAY( SCIPfreeBufferArray(scip, &deltatested) );

      /* delta found */
      if( bestdelta != 0.0 && bestviolation >= 0.2 ) /* !!!!!!!!!!!!!!!! */
      {
         int i;
         int besti;
         Real cutact;
         Real violation;
      
         besti = 1;
         /* Try to improve violation by multiplying delta with 2, 4 and 8 */
         for( i = 2; i <= 8; i *= 2 )
         {
            /* create a MIR cut out of the weighted LP rows */
            CHECK_OKAY( SCIPcalcMIR(scip, 0.05, rowweights, bestdelta * i, cutcoefs, &cutrhs, &cutact, &success) );
            if( success )
            {
               violation = cutact - cutrhs;
               /* i * bestdelta generates cut which is more violated */
               if( violation > bestviolation )
               {
                  besti = i;
                  bestviolation = violation;
               }
            }
         }
         bestdelta *= besti;
         
         /* generate cut with bestdelta */
         CHECK_OKAY( SCIPcalcMIR(scip, 0.05, rowweights, bestdelta, cutcoefs, &cutrhs, &cutact, &success) );
         CHECK_OKAY( addCut(scip, sepadata, vars, nvars, varsol, cutcoefs, cutrhs, ncuts, ncutssum) );

      }
    
      /* no cut generated, or maximum number of cuts allowed not reached yet 
       * Step 2: Add one of the next rows in LP to startrow to get rid of a continuous variable */
      if( naggrs < maxaggrs)
      {
         Real maxbounddist;
         
         ROW* bestrow;          
         COL** bestrownonzcols;     /* columns with nonzero coefficients in best row to add */
         Real* bestrownonzcoefs;    /* nonzero coefficients of columns in best row to add */
         int nbestrownonzcols;      /* number of columns with nonzero coefficients in best row to add */
         Real bestrowact;           /* activity of best row to add */
         Real aggrfact;         
         
         debugMessage(" -> search column to eliminate\n");

         /* search for "best" continuous variable in aggregated row */
         maxbounddist = 0.0;
         bestrow = NULL;
         for( col = 0; col < ncols; col++ )
         {
            if( aggrcoefs[col] != 0.0 )
            {
               VAR* var;
               COL* column;
               Real distlower;
               Real distupper;
               Real bounddist;
               Real primsol;

               column = cols[col];
               var = SCIPcolGetVar(column);
                        
               if( SCIPvarGetType(var) != SCIP_VARTYPE_CONTINUOUS )
                  continue;

               /* get minimum distance of LP solution value of variable to its bounds */
               primsol = SCIPcolGetPrimsol(column);
               distlower = primsol - lowerbounds[col]; 
               distupper = upperbounds[col] - primsol;
               bounddist = MIN(distlower, distupper);
               
               debugMessage("     -> col <%s>[%g,%g]: sol=%g, dist=%g\n", 
                  SCIPvarGetName(var), lowerbounds[col], upperbounds[col], primsol, bounddist);

               /* only columns/variables: 
                * - representing continuous variables 
                * - with nonzero aggregation coefficient
                * - solution value is strictly between lower and upper bound (actually bounddist > maxbounddist)
                * - it exists a row with nonzero coefficient in this column */
               if( bounddist > maxbounddist )
               {
                  ROW** nonzrows;
                  Real* nonzcoefs;
                  int nnonzrows;
                  Real minslack;

                  minslack = SCIPinfinity(scip);
                  
                  /* look for "best" row to add (minimal slack) 
                   * no row, that has been a startrow before
                   * no row, that was allready involved in aggregation */
                  nnonzrows = SCIPcolGetNLPNonz(column);
                  nonzrows = SCIPcolGetRows(column);
                  nonzcoefs = SCIPcolGetVals(column);
                  
                  for( row = 0; row < nnonzrows; row++ )
                  {
                     Real slack;
                     Real fact;
                     int lppos;

                     lppos = SCIProwGetLPPos(nonzrows[row]);
                     assert(0 <= lppos && lppos < nrows);

                     debugMessage("        -> row <%s>: weight=%g, pos=%d, fact=%g, %g <= %g <= %g\n",
                        SCIProwGetName(nonzrows[row]), rowweights[lppos], lppos, - aggrcoefs[col] / nonzcoefs[row],
                        SCIProwGetLhs(nonzrows[row]), SCIPgetRowLPActivity(scip, nonzrows[row]), SCIProwGetRhs(nonzrows[row]));

                     /* take only LP rows, that are below the starting row and not yet aggregated */
                     if( lppos <= startrow || rowweights[lppos] != 0.0 || SCIProwIsModifiable(nonzrows[row]) )
                        continue;
                     
                     slack = SCIPinfinity(scip);
                     fact = - aggrcoefs[col] / nonzcoefs[row]; 
                     
                     if( fact < 0.0 && fact >= minrowfac )    /* !!!!!!!!!!!!!!!!! */
                     {
                        /* negative aggregation factor => lhs > -INF! */
                        Real lhs = SCIProwGetLhs(nonzrows[row]);
                        if( !SCIPisInfinity(scip, -lhs) )
                           slack = (lhs - SCIPgetRowLPActivity(scip, nonzrows[row])) * fact;
                     }
                     else if( fact > 0.0 && fact <= maxrowfac )    /* !!!!!!!!!!!!!!!!! */
                     {
                        /* positive aggregation factor => rhs < INF! */
                        Real rhs = SCIProwGetRhs(nonzrows[row]);
                        if( !SCIPisInfinity(scip, rhs) )
                           slack = (rhs - SCIPgetRowLPActivity(scip, nonzrows[row])) * fact;
                     }
                     
                     /* row has better slack */
                     if( slack < minslack )
                     {
                        maxbounddist = bounddist;
                        bestcol = column;
                        bestrow = nonzrows[row];
                        aggrfact = fact;
                        minslack = slack; 
                        debugMessage("     -> column <%s>: %g * row <%s>, bounddist=%g, slack=%g\n",
                           SCIPvarGetName(SCIPcolGetVar(bestcol)), aggrfact, SCIProwGetName(bestrow), maxbounddist, slack);
                     }
                  }
               }
            }
         }
         assert((bestcol == NULL) == (bestrow == NULL));
         
         /* row to add found */
         if( bestcol != NULL )
         {
            assert(rowweights[SCIProwGetLPPos(bestrow)] == 0.0);
            
            /* Step 3: add row found to aggregated row */
            /* change row weight for row to add */
            rowweights[SCIProwGetLPPos(bestrow)] = aggrfact;
               
            /* change coefficients of aggregated row */
            bestrownonzcols = SCIProwGetCols(bestrow);
            bestrownonzcoefs = SCIProwGetVals(bestrow);
            nbestrownonzcols = SCIProwGetNLPNonz(bestrow);
            for( col = 0; col < nbestrownonzcols; col++ )
            {
               VAR* var;
               var = SCIPcolGetVar(bestrownonzcols[col]);
               int pos = SCIPcolGetLPPos(bestrownonzcols[col]);
               assert(pos >= 0);
               if( aggrcoefs[pos] != 0.0 && SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS )
                  numcont--;
               aggrcoefs[pos] += bestrownonzcoefs[col] * aggrfact;
               if( SCIPisZero(scip, aggrcoefs[pos]) )
                  aggrcoefs[pos] = 0.0;
               else if( SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS )
                  numcont++;
            }
         }
         naggrs++;
      }
   }
   while( bestcol != NULL );
      
   /* free datastructures */
   CHECK_OKAY( SCIPfreeBufferArray(scip, &lowerbounds) );
   CHECK_OKAY( SCIPfreeBufferArray(scip, &upperbounds) );
   CHECK_OKAY( SCIPfreeBufferArray(scip, &cutcoefs) );
   CHECK_OKAY( SCIPfreeBufferArray(scip, &rowweights) );
   CHECK_OKAY( SCIPfreeBufferArray(scip, &aggrcoefs) );

   return SCIP_OKAY; 
}



/*
 * Callback methods of separator
 */

/* TODO: Implement all necessary separator methods. The methods with an #if 0 ... #else #define ... are optional */

/** destructor of separator to free user data (called when SCIP is exiting) */
static
DECL_SEPAFREE(sepaFreeCmir)
{  /*lint --e{715}*/
   SEPADATA* sepadata;

   /* free separator data */
   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);

   SCIPfreeMemory(scip, &sepadata);

   SCIPsepaSetData(sepa, NULL);

   return SCIP_OKAY;

}


/** initialization method of separator (called when problem solving starts) */
#if 0
static
DECL_SEPAINIT(sepaInitCmir)
{  /*lint --e{715}*/
   errorMessage("method of cmir separator not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define sepaInitCmir NULL
#endif


/** deinitialization method of separator (called when problem solving exits) */
#if 0
static
DECL_SEPAEXIT(sepaExitCmir)
{  /*lint --e{715}*/
   errorMessage("method of cmir separator not implemented yet\n");
   abort(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define sepaExitCmir NULL
#endif


/** execution method of separator */
static
DECL_SEPAEXEC(sepaExecCmir)
{  /*lint --e{715}*/
   SEPADATA* sepadata;
   int nrows;
   ROW** rows;     
   int nvars;
   VAR** vars;

   Real* rowslack;
   int* roworder;

   Real* varsol;
   int row;

   int depth;
   int ncalls;
   int maxsepacuts;
   int maxaggrs;
   Real maxslack;

   int ncutssum;
   int ncuts;

   assert(sepa != NULL);
   assert(scip != NULL);
 
   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);

   depth = SCIPgetDepth(scip);
   ncalls = SCIPsepaGetNCallsAtNode(sepa);

   /* only call the cmir cut separator a given number of times at each node */
   if( (depth == 0 && ncalls >= sepadata->maxroundsroot)
      || (depth > 0 && ncalls >= sepadata->maxrounds) )
      return SCIP_OKAY;

   *result = SCIP_DIDNOTFIND;

   /* get all rows and number of columns */
   CHECK_OKAY( SCIPgetLPRowsData(scip, &rows, &nrows) ); 
   assert(rows != NULL);

   /* get all COLUMN variables and number of variables */
   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);
   assert(vars != NULL);

   /* get data structure */
   CHECK_OKAY( SCIPallocBufferArray(scip, &rowslack, nrows) );
   CHECK_OKAY( SCIPallocBufferArray(scip, &roworder, nrows) );
   CHECK_OKAY( SCIPallocBufferArray(scip, &varsol, nvars) );
  
   /* get the LP solution for all COLUMN variables */
   CHECK_OKAY( SCIPgetVarSols(scip, nvars, vars, varsol) );

  /* get the maximal number of cuts allowed in a separation round */
   if( depth == 0 )
   {
      maxsepacuts = sepadata->maxsepacutsroot;
      maxaggrs = sepadata->maxaggrsroot;
   }   
   else
   {
      maxsepacuts = sepadata->maxsepacuts;
      maxaggrs = sepadata->maxaggrs;

   }

   maxslack = sepadata->maxslack;
   /* calculate slack of all rows and sort rows by nondecreasing slack */
   for( row = 0; row < nrows; row++ )
   {
      int i;

      rowslack[row] = SCIPgetRowLPFeasibility(scip, rows[row]);
      for( i = row - 1; i >= 0 && rowslack[row] < rowslack[roworder[i]]; --i )
         roworder[i + 1] = roworder[i];
      assert(i + 1 >= 0 && i + 1 <= row);
      roworder[i + 1] = row;
   }
 
   /* start aggregation heuristic for each row in the LP */
   ncutssum = 0;
   for( row = 0; row < nrows && ncutssum < maxsepacuts && rowslack[roworder[row]] <= maxslack; row++ )
   {
         CHECK_OKAY( aggregation(scip, sepadata, rows, nrows, roworder[row], vars, nvars, varsol, 
                        &ncuts, ncutssum, maxaggrs) );
         ncutssum += ncuts;
   }

   /* free data structure */
   CHECK_OKAY( SCIPfreeBufferArray(scip, &varsol) );
   CHECK_OKAY( SCIPfreeBufferArray(scip, &roworder) );
   CHECK_OKAY( SCIPfreeBufferArray(scip, &rowslack) );

   if( ncutssum > 0 )
      *result = SCIP_SEPARATED;
    
   return SCIP_OKAY;

}





/*
 * separator specific interface methods
 */

/** creates the cmir separator and includes it in SCIP */
RETCODE SCIPincludeSepaCmir(
   SCIP*            scip                /**< SCIP data structure */
   )
{
   SEPADATA* sepadata;

   /* create cmir separator data */
   CHECK_OKAY( SCIPallocMemory(scip, &sepadata) );
   sepadata->maxrounds = DEFAULT_MAXROUNDS;
   sepadata->maxroundsroot = DEFAULT_MAXROUNDSROOT;

   /* TODO: (optional) create separator specific data here */

   /* include separator */
   CHECK_OKAY( SCIPincludeSepa(scip, SEPA_NAME, SEPA_DESC, SEPA_PRIORITY, SEPA_FREQ,
                  sepaFreeCmir, sepaInitCmir, sepaExitCmir, sepaExecCmir,
                  sepadata) );

   /* add cmir separator parameters */
   /* TODO: (optional) add separator specific parameters with SCIPaddTypeParam() here */
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/maxrounds",
                  "maximal number of cmir separation rounds per node",
                  &sepadata->maxrounds, DEFAULT_MAXROUNDS, 0, INT_MAX, NULL, NULL) );
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/maxroundsroot",
                  "maximal number of cmir separation rounds in the root node",
                  &sepadata->maxroundsroot, DEFAULT_MAXROUNDSROOT, 0, INT_MAX, NULL, NULL) );
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/maxsepacuts",
                  "maximal number of cmir cuts separated per separation round",
                  &sepadata->maxsepacuts, DEFAULT_MAXSEPACUTS, 0, INT_MAX, NULL, NULL) );
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/maxsepacutsroot",
                  "maximal number of cmir cuts separated per separation round in the root node",
                  &sepadata->maxsepacutsroot, DEFAULT_MAXSEPACUTSROOT, 0, INT_MAX, NULL, NULL) );
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/maxaggrs",
                  "maximal number of aggregations for each row per separation round",
                  &sepadata->maxaggrs, DEFAULT_MAXAGGRS, 0, INT_MAX, NULL, NULL) );
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/maxaggrsroot",
                  "maximal number of aggregations for each row per separation round in the root node",
                  &sepadata->maxaggrsroot, DEFAULT_MAXAGGRSROOT, 0, INT_MAX, NULL, NULL) );
   CHECK_OKAY( SCIPaddBoolParam(scip,
                  "separating/cmir/dynamiccuts",
                  "should generated cuts be removed from the LP if they are no longer tight?",
                  &sepadata->dynamiccuts, DEFAULT_DYNAMICCUTS, NULL, NULL) );
   CHECK_OKAY( SCIPaddRealParam(scip,
                  "separating/cmir/maxslack",
                  "maximal slack of rows to be used",
                  &sepadata->maxslack, DEFAULT_MAXSLACK, 0, REAL_MAX, NULL, NULL) );
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/maxrowfac",
                  "maximal row aggregation factor",
                  &sepadata->maxrowfac, DEFAULT_MAXROWFAC, 0, INT_MAX, NULL, NULL) );
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/minrowfac",
                  "minimal row aggregation factor",
                  &sepadata->minrowfac, DEFAULT_MINROWFAC, INT_MIN, 0, NULL, NULL) );
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/maxtestdelta",
                  "maximal number of different deltas tested",
                  &sepadata->maxtestdelta, DEFAULT_MAXTESTDELTA, 0, INT_MAX, NULL, NULL) );
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "separating/cmir/maxcont",
                  "maximal number of cont. vars in aggregated row", 
                  &sepadata->maxcont, DEFAULT_MAXCONT, 0, INT_MAX, NULL, NULL) );
   return SCIP_OKAY;
}
