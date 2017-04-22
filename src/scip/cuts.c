/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2017 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   cuts.c
 * @brief  methods for aggregation of rows
 *
 * @author Robert Lion Gottwald
 * @author Jakob Witzig
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "scip/scip.h"
#include "scip/cuts.h"
#include "scip/struct_lp.h"
#include "scip/lp.h"
#include "scip/struct_cuts.h"
#include "scip/cons_knapsack.h"
#include "scip/struct_scip.h"
#include "scip/dbldblarith.h"

/* =========================================== general static functions =========================================== */
#ifdef SCIP_DEBUG
static
void printCut(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Real*            cutcoefs,           /**< non-zero coefficients of cut */
   SCIP_Real             cutrhs,             /**< right hand side of the MIR row */
   int*                  cutinds,            /**< indices of problem variables for non-zero coefficients */
   int                   cutnnz,             /**< number of non-zeros in cut */
   SCIP_Bool             ignorsol,
   SCIP_Bool             islocal
   )
{
   SCIP_Real activity;
   SCIP_VAR** vars;
   int i;

   assert(scip != NULL);
   vars = SCIPgetVars(scip);

   SCIPdebugMessage("CUT:");
   activity = 0.0;
   for( i = 0; i < cutnnz; ++i )
   {
      SCIPdebugPrintf(" %+g<%s>", cutcoefs[i], SCIPvarGetName(vars[cutinds[i]]));

      if( !ignorsol )
         activity += cutcoefs[i] * (sol == NULL ? SCIPvarGetLPSol(vars[cutinds[i]]) : SCIPgetSolVal(scip, sol, vars[cutinds[i]]));
      else
      {
         if( cutcoefs[i] > 0.0 )
         {
            activity += cutcoefs[i] * (islocal ? SCIPvarGetLbLocal(vars[cutinds[i]]) : SCIPvarGetLbGlobal(vars[cutinds[i]]));
         }
         else
         {
            activity += cutcoefs[i] * (islocal ? SCIPvarGetUbLocal(vars[cutinds[i]]) : SCIPvarGetUbGlobal(vars[cutinds[i]]));
         }
      }
   }
   SCIPdebugPrintf(" <= %.6f (activity: %g)\n", cutrhs, activity);
}
#endif

static
SCIP_RETCODE varVecAddScaledRowCoefs(
   SCIP*                 scip,               /**< SCIP datastructure */
   int**                 indsptr,            /**< pointer to array with variable problem indices of non-zeros in variable vector */
   SCIP_Real**           valsptr,            /**< pointer to array with non-zeros values of variable vector */
   int*                  nnz,                /**< number of non-zeros coefficients of variable vector */
   int*                  size,               /**< if not NULL ensures the size of the index and value arrays using block memory */
   SCIP_ROW*             row,                /**< row coefficients to add to variable vector */
   SCIP_Real             scale               /**< scale for adding given row to variable vector */
   )
{
   /* add up coefficients */
   int i;
   int* varpos;
   int nvars;
   int ncommon;
   int newsize;
   int* inds;
   SCIP_Real* vals;

   assert(indsptr != NULL);
   assert(valsptr != NULL);

   inds = *indsptr;
   vals = *valsptr;

   /* if the row is currently empty just scale it and add it directly */
   if( *nnz == 0 )
   {
      if( size != NULL && *size < row->len )
      {
         newsize = SCIPcalcMemGrowSize(scip, row->len);
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, valsptr, *size, newsize) );
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, indsptr, *size, newsize) );
         *size = newsize;
         vals = *valsptr;
         inds = *indsptr;
      }

      /* add the non-zeros to the aggregation row */
      if( scale == 1.0 )
      {
         BMScopyMemoryArray(vals, row->vals, row->len);
      }
      else
      {
         for( i = 0; i < row->len; ++i )
            vals[i] = row->vals[i] * scale;
      }

      for( i = 0; i < row->len; ++i )
         inds[i] = row->cols[i]->var_probindex;

      *nnz = row->len;

      return SCIP_OKAY;
   }

   nvars = SCIPgetNVars(scip);
   SCIP_CALL( SCIPallocCleanBufferArray(scip, &varpos, nvars) );

   /* remember positions of non-zeros in the given row */
   for( i = 0; i < row->len; ++i )
      varpos[row->cols[i]->var_probindex] = i + 1;

   /* loop over the aggregation row's current non-zeros and add all values that the aggregation row has
    * in common with the given row
    */
   ncommon = 0;
   for( i = 0; i < *nnz; ++i )
   {
      int j = inds[i];

      if( varpos[j] != 0 )
      {
         assert(row->cols[varpos[j] - 1]->var_probindex == j);
         vals[i] += scale * row->vals[varpos[j] - 1];
         varpos[j] = 0;
         ++ncommon;
      }
   }

   /* ensure that the memory is big enough to hold the remaining non-zeros of the given row */
   if( size != NULL )
   {
      newsize = *nnz + row->len - ncommon;
      if( newsize > *size )
      {
         newsize = SCIPcalcMemGrowSize(scip, newsize);
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, valsptr, *size, newsize) );
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, indsptr, *size, newsize) );
         *size = newsize;
         vals = *valsptr;
         inds = *indsptr;
      }
   }

   /* add the remaining non-zeros to the aggregation row */
   for( i = 0; i < row->len; ++i )
   {
      int probindex = row->cols[i]->var_probindex;

      if( varpos[probindex] != 0 )
      {
         int j = (*nnz)++;

         assert(varpos[probindex] == i + 1);
         assert(size == NULL || j < *size);

         vals[j] = row->vals[i] * scale;
         inds[j] = probindex;
         varpos[probindex] = 0;
      }
   }

   /* the varpos array was cleaned during the addition of the elements from the given row */
   SCIPfreeCleanBufferArray(scip, &varpos);

   return SCIP_OKAY;
}

/* calculates the cuts efficacy for the given solution */
static
SCIP_Real calcEfficacy(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_SOL*             sol,                /**< solution to calculate the efficacy for (NULL for LP solution) */
   SCIP_Real*            cutcoefs,           /**< array of the non-zero coefficients in the cut */
   SCIP_Real             cutrhs,             /**< the right hand side of the cut */
   int*                  cutinds,            /**< array of the problem indices of variables with a non-zero coefficient in the cut */
   int                   cutnnz              /**< the number of non-zeros in the cut */
   )
{
   SCIP_VAR** vars;
   SCIP_Real norm;
   SCIP_Real activity;
   int i;

   assert(scip != NULL);
   assert(cutcoefs != NULL);
   assert(cutinds != NULL);

   norm = MAX(1e-6, SCIPgetVectorEfficacyNorm(scip, cutcoefs, cutnnz));
   vars = SCIPgetVars(scip);

   activity = 0.0;
   for( i = 0; i < cutnnz; ++i )
      activity += cutcoefs[i] * SCIPgetSolVal(scip, sol, vars[cutinds[i]]);

   return (activity - cutrhs) / norm;
}

/* =========================================== aggregation row =========================================== */

/** create an empty aggregation row */
SCIP_RETCODE SCIPaggrRowCreate(
   SCIP*                 scip,               /**< SCIP datastructure */
    SCIP_AGGRROW**         aggrrow              /**< pointer to return aggregation row */
   )
{
   assert(scip != NULL);
   assert(aggrrow != NULL);

   SCIP_CALL( SCIPallocBlockMemory(scip, aggrrow) );

   (*aggrrow)->vals = NULL;
   (*aggrrow)->inds = NULL;
   (*aggrrow)->local = FALSE;
   (*aggrrow)->nnz = 0;
   (*aggrrow)->valssize = 0;
   (*aggrrow)->rank = 0;
   (*aggrrow)->rhs = 0.0;
   (*aggrrow)->rowsinds = NULL;
   (*aggrrow)->slacksign = NULL;
   (*aggrrow)->rowweights = NULL;
   (*aggrrow)->nrows = 0;
   (*aggrrow)->rowssize = 0;

   return SCIP_OKAY;
}

/** free a aggregation row */
void SCIPaggrRowFree(
   SCIP*                 scip,               /**< SCIP datastructure */
    SCIP_AGGRROW**         aggrrow              /**< pointer to aggregation row that should be freed */
   )
{
   assert(scip != NULL);
   assert(aggrrow != NULL);

   SCIPfreeBlockMemoryArrayNull(scip, &(*aggrrow)->vals, (*aggrrow)->valssize);
   SCIPfreeBlockMemoryArrayNull(scip, &(*aggrrow)->inds, (*aggrrow)->valssize);
   SCIPfreeBlockMemoryArrayNull(scip, &(*aggrrow)->rowsinds, (*aggrrow)->rowssize);
   SCIPfreeBlockMemoryArrayNull(scip, &(*aggrrow)->slacksign, (*aggrrow)->rowssize);
   SCIPfreeBlockMemoryArrayNull(scip, &(*aggrrow)->rowweights, (*aggrrow)->rowssize);
   SCIPfreeBlockMemory(scip, aggrrow);
}


/** copy a aggregation row */
SCIP_RETCODE SCIPaggrRowCopy(
   SCIP*                 scip,               /**< SCIP datastructure */
    SCIP_AGGRROW**         aggrrow,             /**< pointer to return aggregation row */
    SCIP_AGGRROW*          source              /**< source aggregation row */
   )
{
   assert(scip != NULL);
   assert(aggrrow != NULL);
   assert(source != NULL);

   SCIP_CALL( SCIPallocBlockMemory(scip, aggrrow) );

   SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*aggrrow)->vals, source->vals, source->nnz) );
   SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*aggrrow)->inds, source->inds, source->nnz) );
   (*aggrrow)->nnz = source->nnz;
   (*aggrrow)->valssize = source->nnz;
   (*aggrrow)->rhs = source->rhs;
   SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*aggrrow)->rowsinds, source->vals, source->nrows) );
   SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*aggrrow)->slacksign, source->slacksign, source->nrows) );
   SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*aggrrow)->rowweights, source->rowweights, source->nrows) );
   (*aggrrow)->nrows = source->nrows;
   (*aggrrow)->rowssize = source->nrows;
   (*aggrrow)->rank = source->rank;
   (*aggrrow)->local = source->local;

   return SCIP_OKAY;
}


/** add scaled row to aggregation row */
SCIP_RETCODE SCIPaggrRowAddRow(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_AGGRROW*         aggrrow,            /**< aggregation row */
   SCIP_ROW*             row,                /**< row to add to aggregation row */
   SCIP_Real             scale,              /**< scale for adding given row to aggregation row */
   int                   sidetype            /**< specify row side type (-1 = lhs, 0 = automatic, 1 = rhs) */
   )
{
   int i;

   assert(row->lppos >= 0);

   /* update local flag */
   aggrrow->local = aggrrow->local || row->local;

   /* update rank */
   aggrrow->rank = MAX(row->rank, aggrrow->rank);

   {
      SCIP_Real sideval;
      SCIP_Bool uselhs;

      i = aggrrow->nrows++;

      if( aggrrow->nrows > aggrrow->rowssize )
      {
         int newsize = SCIPcalcMemGrowSize(scip, aggrrow->nrows);
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &aggrrow->rowsinds, aggrrow->rowssize, newsize) );
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &aggrrow->slacksign, aggrrow->rowssize, newsize) );
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &aggrrow->rowweights, aggrrow->rowssize, newsize) );
         aggrrow->rowssize = newsize;
      }
      aggrrow->rowsinds[i] = SCIProwGetLPPos(row);
      aggrrow->rowweights[i] = scale;

      if ( sidetype == -1 )
      {
         assert( ! SCIPisInfinity(scip, -row->lhs) );
         uselhs = TRUE;
      }
      else if ( sidetype == 1 )
      {
         assert( ! SCIPisInfinity(scip, row->rhs) );
         uselhs = FALSE;
      }
      else
      {
         /* Automatically decide, whether we want to use the left or the right hand side of the row in the summation.
          * If possible, use the side that leads to a positive slack value in the summation.
          */
         if( SCIPisInfinity(scip, row->rhs) || (!SCIPisInfinity(scip, -row->lhs) && scale < 0.0) )
            uselhs = TRUE;
         else
            uselhs = FALSE;
      }

      if( uselhs )
      {
         aggrrow->slacksign[i] = -1;
         sideval = row->lhs - row->constant;
         if( row->integral )
            sideval = SCIPfeasCeil(scip, sideval); /* row is integral: round left hand side up */
      }
      else
      {
         aggrrow->slacksign[i] = +1;
         sideval = row->rhs - row->constant;
         if( row->integral )
            sideval = SCIPfeasFloor(scip, sideval); /* row is integral: round right hand side up */
      }
      aggrrow->rhs += scale * sideval;
   }

   /* add up coefficients */
   SCIP_CALL( varVecAddScaledRowCoefs(scip, &aggrrow->inds, &aggrrow->vals, &aggrrow->nnz, &aggrrow->valssize, row, scale) );

   return SCIP_OKAY;
}

/** clear all entries int the aggregation row but don't free memory */
void SCIPaggrRowClear(
   SCIP_AGGRROW*         aggrrow             /**< the aggregation row */
   )
{
   aggrrow->nnz = 0;
   aggrrow->nrows = 0;
   aggrrow->rank = 0;
   aggrrow->rhs = 0.0;
   aggrrow->local = FALSE;
}

/* static function to add one row without clearing the varpos array
 * so that multiple rows can be added using the same varpos array
 * which is only cleared in the end
 */
static
SCIP_RETCODE addOneRow(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_AGGRROW*         aggrrow,            /**< the aggregation row */
   SCIP_ROW*             row,                /**< the row to add */
   SCIP_Real             weight,             /**< weight of row to add */
   SCIP_Real             maxweightrange,     /**< maximal valid range max(|weights|)/min(|weights|) of row weights */
   SCIP_Real             minallowedweight,   /**< minimum magnitude of weight for rows that are used in the summation */
   SCIP_Bool             sidetypebasis,      /**< choose sidetypes of row (lhs/rhs) based on basis information? */
   SCIP_Bool             allowlocal,         /**< should local rows allowed to be used? */
   int                   negslack,           /**< should negative slack variables allowed to be used? (0: no, 1: only for integral rows, 2: yes) */
   int                   maxaggrlen,         /**< maximal length of aggregation row */
   SCIP_Real*            minabsweight,       /**< pointer to update minabsweight */
   SCIP_Real*            maxabsweight,       /**< pointer to update maxabsweight */
   int*                  varpos,             /**< array with positions of variables in current aggregation row */
   SCIP_Bool*            rowtoolong          /**< is the aggregated row too long */
   )
{
   SCIP_Real absweight;
   SCIP_Real sideval;
   SCIP_Bool uselhs;
   int i;

   *rowtoolong = FALSE;
   absweight = REALABS(weight);

   if( SCIProwIsModifiable(row) ||
       (SCIProwIsLocal(row) && !allowlocal) ||
       absweight > maxweightrange * (*minabsweight) ||
       (*maxabsweight) > maxweightrange * absweight ||
       absweight < minallowedweight
      )
   {
      return SCIP_OKAY;
   }

   *minabsweight = MIN(*minabsweight, absweight);
   *maxabsweight = MAX(*maxabsweight, absweight);

   if( sidetypebasis && !SCIPisEQ(scip, SCIProwGetLhs(row), SCIProwGetRhs(row)) )
   {
      SCIP_BASESTAT stat = SCIProwGetBasisStatus(row);

      if( stat == SCIP_BASESTAT_LOWER )
      {
         assert( ! SCIPisInfinity(scip, -SCIProwGetLhs(row)) );
         uselhs = TRUE;
      }
      else if( stat == SCIP_BASESTAT_UPPER )
      {
         assert( ! SCIPisInfinity(scip, SCIProwGetRhs(row)) );
         uselhs = FALSE;
      }
      else if( weight < 0.0 && !SCIPisInfinity(scip, -row->lhs) )
      {
         uselhs = TRUE;
      }
      else
      {
         uselhs = FALSE;
      }
   }
   else if( weight < 0.0 && !SCIPisInfinity(scip, -row->lhs) )
   {
      uselhs = TRUE;
   }
   else
   {
      uselhs = FALSE;
   }

   if( uselhs )
   {
      if( weight > 0.0 && ((negslack == 0) || (negslack == 1 && !row->integral)) )
         return SCIP_OKAY;

      sideval = row->lhs - row->constant;
      /* row is integral? round left hand side up */
      if( row->integral )
         sideval = SCIPfeasCeil(scip, sideval);
   }
   else
   {
      if( weight < 0.0 && ((negslack == 0) || (negslack == 1 && !row->integral)) )
         return SCIP_OKAY;

      sideval = row->rhs - row->constant;
      /* row is integral? round right hand side down */
      if( row->integral )
         sideval = SCIPfeasFloor(scip, sideval);
   }

   /* add right hand side, update rank and local flag */
   aggrrow->rhs += sideval * weight;
   aggrrow->rank = MAX(aggrrow->rank, row->rank);
   aggrrow->local = aggrrow->local || row->local;

   /* ensure the array for storing the row information is large enough */
   i = aggrrow->nrows++;
   if( aggrrow->nrows > aggrrow->rowssize )
   {
      int newsize = SCIPcalcMemGrowSize(scip, aggrrow->nrows);
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &aggrrow->rowsinds, aggrrow->rowssize, newsize) );
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &aggrrow->slacksign, aggrrow->rowssize, newsize) );
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &aggrrow->rowweights, aggrrow->rowssize, newsize) );
      aggrrow->rowssize = newsize;
   }

   /* add information of addditional row */
   aggrrow->rowsinds[i] = row->lppos;
   aggrrow->rowweights[i] = weight;
   aggrrow->slacksign[i] = uselhs ? -1 : 1;

   /* ensure the aggregation row can hold all non-zero entries from the additional row */
   {
      int newsize = aggrrow->nnz + row->len;
      if( newsize > aggrrow->valssize )
      {
         newsize = SCIPcalcMemGrowSize(scip, newsize);
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &aggrrow->vals, aggrrow->valssize, newsize) );
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &aggrrow->inds, aggrrow->valssize, newsize) );
         aggrrow->valssize = newsize;
      }
   }

   /* add coefficients */
   for( i = 0; i < row->len; ++i )
   {
      int k = varpos[row->cols[i]->var_probindex];
      if( k == 0 )
      {
         k = aggrrow->nnz++;
         aggrrow->vals[k] = weight * row->vals[i];
         aggrrow->inds[k] = row->cols[i]->var_probindex;
         varpos[aggrrow->inds[k]] = k + 1;
      }
      else
      {
         aggrrow->vals[k - 1] += weight * row->vals[i];
      }
   }

   /* check if row is too long now */
   if( aggrrow->nnz > maxaggrlen )
      *rowtoolong = TRUE;

   return SCIP_OKAY;
}


/** aggregate rows using the given weights; the current content of the aggregation
 *  row gets overwritten
 */
SCIP_RETCODE SCIPaggrRowSumRows(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_AGGRROW*         aggrrow,            /**< the aggregation row */
   SCIP_Real*            weights,            /**< row weights in row summation */
   int*                  rowinds,            /**< array to store indices of non-zero entries of the weights array, or
                                              *   NULL */
   int                   nrowinds,           /**< number of non-zero entries in weights array, -1 if rowinds is NULL */
   SCIP_Real             maxweightrange,     /**< maximal valid range max(|weights|)/min(|weights|) of row weights */
   SCIP_Real             minallowedweight,   /**< minimum magnitude of weight for rows that are used in the summation */
   SCIP_Bool             sidetypebasis,      /**< choose sidetypes of row (lhs/rhs) based on basis information? */
   SCIP_Bool             allowlocal,         /**< should local rows allowed to be used? */
   int                   negslack,           /**< should negative slack variables allowed to be used? (0: no, 1: only for integral rows, 2: yes) */
   int                   maxaggrlen,         /**< maximal length of aggregation row */
   SCIP_Bool*            valid               /**< is the aggregation valid */
   )
{
   SCIP_ROW** rows;
   SCIP_VAR** vars;
   int nrows;
   int* varpos;
   int nvars;
   int k;
   SCIP_Bool rowtoolong;
   SCIP_Real minabsweight;
   SCIP_Real maxabsweight;

   SCIP_CALL( SCIPgetVarsData(scip, &vars, &nvars, NULL, NULL, NULL, NULL) );
   SCIP_CALL( SCIPgetLPRowsData(scip, &rows, &nrows) );

   SCIP_CALL( SCIPallocCleanBufferArray(scip, &varpos, nvars) );

   minabsweight = SCIPinfinity(scip);
   maxabsweight = -SCIPinfinity(scip);

   SCIPaggrRowClear(aggrrow);

   if( rowinds != NULL && nrowinds > -1 )
   {
      for( k = 0; k < nrowinds; ++k )
      {
         SCIP_CALL( addOneRow(scip, aggrrow, rows[rowinds[k]], weights[rowinds[k]], maxweightrange, minallowedweight, sidetypebasis, allowlocal,
                              negslack, maxaggrlen, &minabsweight, &maxabsweight, varpos, &rowtoolong) );

         if( rowtoolong )
         {
            *valid = FALSE;
            goto TERMINATE;
         }
      }
   }
   else
   {
      for( k = 0; k < nrows; ++k )
      {
         SCIP_CALL( addOneRow(scip, aggrrow, rows[k], weights[k], maxweightrange, minallowedweight, sidetypebasis, allowlocal,
                              negslack, maxaggrlen, &minabsweight, &maxabsweight, varpos, &rowtoolong) );

         if( rowtoolong )
         {
            *valid = FALSE;
            goto TERMINATE;
         }
      }
   }

   *valid = aggrrow->nnz > 0;

TERMINATE:

   if( *valid )
   {
      k = 0;
      while( k < aggrrow->nnz )
      {
         varpos[aggrrow->inds[k]] = 0;
         if( SCIPisZero(scip, aggrrow->vals[k]) )
         {
            /* remove zero entry */
            --aggrrow->nnz;
            if( k < aggrrow->nnz )
            {
               aggrrow->vals[k] = aggrrow->vals[aggrrow->nnz];
               aggrrow->inds[k] = aggrrow->inds[aggrrow->nnz];
            }
         }
         else
            ++k;
      }
   }
   else
   {
      for( k = 0; k < aggrrow->nnz; ++k )
         varpos[aggrrow->inds[k]] = 0;
   }

   SCIPfreeCleanBufferArray(scip, &varpos);

   return SCIP_OKAY;
}

/** removes all zero entries in the aggregation row */
void SCIPaggrRowRemoveZeros(
   SCIP_AGGRROW*         aggrrow,            /**< the aggregation row */
   SCIP_Real             epsilon             /**< value to consider zero */
   )
{
   int i;

   assert(aggrrow != NULL);

   for( i = 0; i < aggrrow->nnz; )
   {
      if( EPSZ(aggrrow->vals[i], epsilon) )
      {
         --aggrrow->nnz;
         if( i < aggrrow->nnz )
         {
            aggrrow->vals[i] = aggrrow->vals[aggrrow->nnz];
            aggrrow->inds[i] = aggrrow->inds[aggrrow->nnz];
         }
      }
      else
         ++i;
   }
}

/** checks whether a given row has been added to the aggregation row */
SCIP_Bool SCIPaggrRowHasRowBeenAdded(
   SCIP_AGGRROW*         aggrrow,            /**< the aggregation row */
   SCIP_ROW*             row                 /**< row for which it is checked whether it has been added to the aggregation */
   )
{
   int i;
   int rowind;

   assert(aggrrow != NULL);
   assert(row != NULL);

   rowind = SCIProwGetLPPos(row);

   for( i = 0; i < aggrrow->nrows; ++i )
      if( aggrrow->rowsinds[i] == rowind )
         return TRUE;

   return FALSE;
}

/** gets the range of the absolute values of weights that have been used to aggregate a row into this aggregation row */
void SCIPaggrRowGetAbsWeightRange(
   SCIP_AGGRROW*         aggrrow,            /**< the aggregation row */
   SCIP_Real*            minabsrowweight,    /**< pointer to store smallest absolute value of weights used for rows aggregated
                                              *   into the given aggregation row */
   SCIP_Real*            maxabsrowweight     /**< pointer to store largest absolute value of weights used for rows aggregated
                                              *   into the given aggregation row */
   )
{
   int i;

   assert(aggrrow != NULL);
   assert(aggrrow->nrows > 0);

   *minabsrowweight = REALABS(aggrrow->rowweights[0]);
   *maxabsrowweight = *minabsrowweight;

   for( i = 1; i < aggrrow->nrows; ++i )
   {
      SCIP_Real absweight = REALABS(aggrrow->rowweights[i]);
      if( absweight < *minabsrowweight )
         *minabsrowweight = absweight;
      else if( absweight > *maxabsrowweight )
         *maxabsrowweight = absweight;
   }
}

/** removes almost zero entries and relaxes the sides of the row accordingly */
static
void cleanupCut(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_Bool             cutislocal,         /**< is the cut a local cut */
   int*                  cutinds,            /**< variable problem indices of non-zeros in cut */
   SCIP_Real*            cutcoefs,           /**< non-zeros coefficients of cut */
   int*                  nnz,                /**< number non-zeros coefficients of cut */
   SCIP_Real*            cutrhs              /**< right hand side of cut */
   )
{
   int i;
   SCIP_VAR** vars;

   assert(scip != NULL);
   assert(cutinds != NULL);
   assert(cutcoefs != NULL);
   assert(cutrhs != NULL);

   i = 0;
   vars = SCIPgetVars(scip);

   while( i < *nnz )
   {
      if( SCIPisSumZero(scip, cutcoefs[i]) )
      {
         /* relax left and right hand sides if necessary */
         if( !SCIPisInfinity(scip, *cutrhs) && !SCIPisZero(scip, cutcoefs[i]) )
         {
            int v = cutinds[i];

            if( cutcoefs[i] < 0.0 )
            {
               SCIP_Real ub = cutislocal ? SCIPvarGetUbLocal(vars[v]) : SCIPvarGetUbGlobal(vars[v]);
               if( SCIPisInfinity(scip, ub) )
                  *cutrhs = SCIPinfinity(scip);
               else
                  *cutrhs -= cutcoefs[i] * ub;
            }
            else
            {
               SCIP_Real lb = cutislocal ? SCIPvarGetLbLocal(vars[v]) : SCIPvarGetLbGlobal(vars[v]);
               if( SCIPisInfinity(scip, -lb) )
                  *cutrhs = SCIPinfinity(scip);
               else
                  *cutrhs -= cutcoefs[i] * lb;
            }
         }

         /* remove non-zero entry */
         --(*nnz);
         if( i < *nnz )
         {
            cutcoefs[i] = cutcoefs[*nnz];
            cutinds[i] = cutinds[*nnz];
         }
      }
      else
         ++i;
   }
}

/** gets the array of corresponding variable problem indices for each non-zero in the aggregation row */
int* SCIPaggrRowGetInds(
    SCIP_AGGRROW*          aggrrow              /**< aggregation row */
   )
{
   assert(aggrrow != NULL);

   return aggrrow->inds;
}

/** gets the array of non-zero values in the aggregation row */
SCIP_Real* SCIPaggrRowGetVals(
    SCIP_AGGRROW*          aggrrow              /**< aggregation row */
   )
{
   assert(aggrrow != NULL);

   return aggrrow->vals;
}

/** gets the number of non-zeros in the aggregation row */
int SCIPaggrRowGetNNz(
    SCIP_AGGRROW*          aggrrow              /**< aggregation row */
   )
{
   assert(aggrrow != NULL);

   return aggrrow->nnz;
}

/** gets the rank of the aggregation row */
int SCIPaggrRowGetRank(
    SCIP_AGGRROW*          aggrrow              /**< aggregation row */
   )
{
   assert(aggrrow != NULL);

   return aggrrow->rank;
}

/** checks if the aggregation row is only valid locally */
SCIP_Bool SCIPaggrRowIsLocal(
    SCIP_AGGRROW*          aggrrow              /**< aggregation row */
   )
{
   assert(aggrrow != NULL);

   return aggrrow->local;
}

/** gets the right hand side of the aggregation row */
SCIP_Real SCIPaggrRowGetRhs(
    SCIP_AGGRROW*          aggrrow              /**< aggregation row */
   )
{
   assert(aggrrow != NULL);

   return aggrrow->rhs;
}

/* =========================================== c-MIR =========================================== */

#define MAXCMIRSCALE               1e+6 /**< maximal scaling (scale/(1-f0)) allowed in c-MIR calculations */

/** finds the best lower bound of the variable to use for MIR transformation */
static
SCIP_RETCODE findBestLb(
   SCIP*                 scip,
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Bool             usevbds,            /**< should variable bounds be used in bound transformation? */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   SCIP_Real*            bestlb,             /**< pointer to store best bound value */
   int*                  bestlbtype          /**< pointer to store best bound type */
   )
{
   assert(bestlb != NULL);
   assert(bestlbtype != NULL);

   *bestlb = SCIPvarGetLbGlobal(var);
   *bestlbtype = -1;

   if( allowlocal )
   {
      SCIP_Real loclb;

      loclb = SCIPvarGetLbLocal(var);
      if( SCIPisGT(scip, loclb, *bestlb) )
      {
         *bestlb = loclb;
         *bestlbtype = -2;
      }
   }

   if( usevbds && SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS )
   {
      SCIP_Real bestvlb;
      int bestvlbidx;

      SCIP_CALL( SCIPgetVarClosestVlb(scip, var, sol, &bestvlb, &bestvlbidx) );
      if( bestvlbidx >= 0
         && (bestvlb > *bestlb || (*bestlbtype < 0 && SCIPisGE(scip, bestvlb, *bestlb))) )
      {
         SCIP_VAR** vlbvars;

         /* we have to avoid cyclic variable bound usage, so we enforce to use only variable bounds variables of smaller index */
         /**@todo this check is not needed for continuous variables; but allowing all but binary variables
          *       to be replaced by variable bounds seems to be buggy (wrong result on gesa2)
          */
         vlbvars = SCIPvarGetVlbVars(var);
         assert(vlbvars != NULL);
         if( SCIPvarGetProbindex(vlbvars[bestvlbidx]) < SCIPvarGetProbindex(var) )
         {
            *bestlb = bestvlb;
            *bestlbtype = bestvlbidx;
         }
      }
   }

   return SCIP_OKAY;
}

/** finds the best upper bound of the variable to use for MIR transformation */
static
SCIP_RETCODE findBestUb(
   SCIP*                 scip,
   SCIP_VAR*             var,                /**< problem variable */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Bool             usevbds,            /**< should variable bounds be used in bound transformation? */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   SCIP_Real*            bestub,             /**< pointer to store best bound value */
   int*                  bestubtype          /**< pointer to store best bound type */
   )
{
   assert(bestub != NULL);
   assert(bestubtype != NULL);

   *bestub = SCIPvarGetUbGlobal(var);
   *bestubtype = -1;

   if( allowlocal )
   {
      SCIP_Real locub;

      locub = SCIPvarGetUbLocal(var);
      if( SCIPisLT(scip, locub, *bestub) )
      {
         *bestub = locub;
         *bestubtype = -2;
      }
   }

   if( usevbds && SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS )
   {
      SCIP_Real bestvub;
      int bestvubidx;

      SCIP_CALL( SCIPgetVarClosestVub(scip, var, sol, &bestvub, &bestvubidx) );
      if( bestvubidx >= 0
         && (bestvub < *bestub || (*bestubtype < 0 && SCIPisLE(scip, bestvub, *bestub))) )
      {
         SCIP_VAR** vubvars;

         /* we have to avoid cyclic variable bound usage, so we enforce to use only variable bounds variables of smaller index */
         /**@todo this check is not needed for continuous variables; but allowing all but binary variables
          *       to be replaced by variable bounds seems to be buggy (wrong result on gesa2)
          */
         vubvars = SCIPvarGetVubVars(var);
         assert(vubvars != NULL);
         if( SCIPvarGetProbindex(vubvars[bestvubidx]) < SCIPvarGetProbindex(var) )
         {
            *bestub = bestvub;
            *bestubtype = bestvubidx;
         }
      }
   }

   return SCIP_OKAY;
}

static
SCIP_RETCODE determineBestBounds(
   SCIP*                 scip,
   SCIP_VAR*             var,
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Real             boundswitch,        /**< fraction of domain up to which lower bound is used in transformation */
   SCIP_Bool             usevbds,            /**< should variable bounds be used in bound transformation? */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   SCIP_Bool             fixintegralrhs,     /**< should complementation tried to be adjusted such that rhs gets fractional? */
   SCIP_Bool             ignoresol,          /**< should the LP solution be ignored? (eg, apply MIR to dualray) */
   int*                  boundsfortrans,     /**< bounds that should be used for transformed variables: vlb_idx/vub_idx,
                                              *   -1 for global lb/ub, -2 for local lb/ub, or -3 for using closest bound;
                                              *   NULL for using closest bound for all variables */
   SCIP_BOUNDTYPE*       boundtypesfortrans, /**< type of bounds that should be used for transformed variables;
                                              *   NULL for using closest bound for all variables */
   SCIP_Real*            bestlb,
   SCIP_Real*            bestub,
   int*                  bestlbtype,
   int*                  bestubtype,
   SCIP_BOUNDTYPE*       selectedbound,
   SCIP_Bool*            freevariable
   )
{
   int v;

   v = SCIPvarGetProbindex(var);

   /* check if the user specified a bound to be used */
   if( boundsfortrans != NULL && boundsfortrans[v] > -3 )
   {
      assert(SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS || ( boundsfortrans[v] == -2 || boundsfortrans[v] == -1 ));

      /* user has explicitly specified a bound to be used */
      if( boundtypesfortrans[v] == SCIP_BOUNDTYPE_LOWER )
      {
         /* user wants to use lower bound */
         *bestlbtype = boundsfortrans[v];
         if( *bestlbtype == -1 )
            *bestlb = SCIPvarGetLbGlobal(var); /* use global standard lower bound */
         else if( *bestlbtype == -2 )
            *bestlb = SCIPvarGetLbLocal(var);  /* use local standard lower bound */
         else
         {
            SCIP_VAR** vlbvars;
            SCIP_Real* vlbcoefs;
            SCIP_Real* vlbconsts;
            int k;

            assert(!ignoresol);

            /* use the given variable lower bound */
            vlbvars = SCIPvarGetVlbVars(var);
            vlbcoefs = SCIPvarGetVlbCoefs(var);
            vlbconsts = SCIPvarGetVlbConstants(var);
            k = boundsfortrans[v];
            assert(k >= 0 && k < SCIPvarGetNVlbs(var));
            assert(vlbvars != NULL);
            assert(vlbcoefs != NULL);
            assert(vlbconsts != NULL);

            *bestlb = vlbcoefs[k] * (sol == NULL ? SCIPvarGetLPSol(vlbvars[k]) : SCIPgetSolVal(scip, sol, vlbvars[k])) + vlbconsts[k];
         }

         assert(!SCIPisInfinity(scip, - *bestlb));
         *selectedbound = SCIP_BOUNDTYPE_LOWER;

         /* find closest upper bound in standard upper bound (and variable upper bounds for continuous variables) */
         SCIP_CALL( findBestUb(scip, var, sol, usevbds && fixintegralrhs, allowlocal && fixintegralrhs, bestub, bestubtype) );
      }
      else
      {
         assert(boundtypesfortrans[v] == SCIP_BOUNDTYPE_UPPER);

         /* user wants to use upper bound */
         *bestubtype = boundsfortrans[v];
         if( *bestubtype == -1 )
            *bestub = SCIPvarGetUbGlobal(var); /* use global standard upper bound */
         else if( *bestubtype == -2 )
            *bestub = SCIPvarGetUbLocal(var);  /* use local standard upper bound */
         else
         {
            SCIP_VAR** vubvars;
            SCIP_Real* vubcoefs;
            SCIP_Real* vubconsts;
            int k;

            assert(!ignoresol);

            /* use the given variable upper bound */
            vubvars = SCIPvarGetVubVars(var);
            vubcoefs = SCIPvarGetVubCoefs(var);
            vubconsts = SCIPvarGetVubConstants(var);
            k = boundsfortrans[v];
            assert(k >= 0 && k < SCIPvarGetNVubs(var));
            assert(vubvars != NULL);
            assert(vubcoefs != NULL);
            assert(vubconsts != NULL);

            /* we have to avoid cyclic variable bound usage, so we enforce to use only variable bounds variables of smaller index */
            *bestub = vubcoefs[k] * (sol == NULL ? SCIPvarGetLPSol(vubvars[k]) : SCIPgetSolVal(scip, sol, vubvars[k])) + vubconsts[k];
         }

         assert(!SCIPisInfinity(scip, *bestub));
         *selectedbound = SCIP_BOUNDTYPE_UPPER;

         /* find closest lower bound in standard lower bound (and variable lower bounds for continuous variables) */
         SCIP_CALL( findBestLb(scip, var, sol, usevbds && fixintegralrhs, allowlocal && fixintegralrhs, bestlb, bestlbtype) );
      }
   }
   else
   {
      SCIP_Real varsol;

      /* bound selection should be done automatically */

      /* find closest lower bound in standard lower bound (and variable lower bounds for continuous variables) */
      SCIP_CALL( findBestLb(scip, var, sol, usevbds, allowlocal, bestlb, bestlbtype) );

      /* find closest upper bound in standard upper bound (and variable upper bounds for continuous variables) */
      SCIP_CALL( findBestUb(scip, var, sol, usevbds, allowlocal, bestub, bestubtype) );

      /* check, if variable is free variable */
      if( SCIPisInfinity(scip, - *bestlb) && SCIPisInfinity(scip, *bestub) )
      {
         /* we found a free variable in the row with non-zero coefficient
            *  -> MIR row can't be transformed in standard form
            */
         *freevariable = TRUE;
         return SCIP_OKAY;
      }

      if( !ignoresol )
      {
         /* select transformation bound */
         varsol = (sol == NULL ? SCIPvarGetLPSol(var) : SCIPgetSolVal(scip, sol, var));

         if( SCIPisInfinity(scip, *bestub) ) /* if there is no ub, use lb */
            *selectedbound = SCIP_BOUNDTYPE_LOWER;
         else if( SCIPisInfinity(scip, - *bestlb) ) /* if there is no lb, use ub */
            *selectedbound = SCIP_BOUNDTYPE_UPPER;
         else if( SCIPisLT(scip, varsol, (1.0 - boundswitch) * (*bestlb) + boundswitch * (*bestub)) )
            *selectedbound = SCIP_BOUNDTYPE_LOWER;
         else if( SCIPisGT(scip, varsol, (1.0 - boundswitch) * (*bestlb) + boundswitch * (*bestub)) )
            *selectedbound = SCIP_BOUNDTYPE_UPPER;
         else if( *bestlbtype == -1 )  /* prefer global standard bounds */
            *selectedbound = SCIP_BOUNDTYPE_LOWER;
         else if( *bestubtype == -1 )  /* prefer global standard bounds */
            *selectedbound = SCIP_BOUNDTYPE_UPPER;
         else if( *bestlbtype >= 0 )   /* prefer variable bounds over local bounds */
            *selectedbound = SCIP_BOUNDTYPE_LOWER;
         else if( *bestubtype >= 0 )   /* prefer variable bounds over local bounds */
            *selectedbound = SCIP_BOUNDTYPE_UPPER;
         else                         /* no decision yet? just use lower bound */
            *selectedbound = SCIP_BOUNDTYPE_LOWER;
      }
      else
      {
         SCIP_Real glbub = SCIPvarGetUbGlobal(var);
         SCIP_Real glblb = SCIPvarGetLbGlobal(var);
         SCIP_Real distlb = REALABS(glblb - *bestlb);
         SCIP_Real distub = REALABS(glbub - *bestub);

         assert(!SCIPisInfinity(scip, - *bestlb) || !SCIPisInfinity(scip, *bestub));

         if( SCIPisInfinity(scip, - *bestlb) )
            *selectedbound = SCIP_BOUNDTYPE_UPPER;
         else if( !SCIPisNegative(scip, *bestlb) )
         {
            if( SCIPisInfinity(scip, *bestub) )
               *selectedbound = SCIP_BOUNDTYPE_LOWER;
            else if( SCIPisZero(scip, glblb) )
               *selectedbound = SCIP_BOUNDTYPE_LOWER;
            else if( SCIPisLE(scip, distlb, distub) )
               *selectedbound = SCIP_BOUNDTYPE_LOWER;
            else
               *selectedbound = SCIP_BOUNDTYPE_UPPER;
         }
         else
         {
            assert(!SCIPisInfinity(scip, - *bestlb));
            *selectedbound = SCIP_BOUNDTYPE_LOWER;
         }
      }
   }

   return SCIP_OKAY;
}

/** Transform equation \f$ a \cdot x = b; lb \leq x \leq ub \f$ into standard form
 *    \f$ a^\prime \cdot x^\prime = b,\; 0 \leq x^\prime \leq ub' \f$.
 *
 *  Transform variables (lb or ub):
 *  \f[
 *  \begin{array}{llll}
 *    x^\prime_j := x_j - lb_j,&   x_j = x^\prime_j + lb_j,&   a^\prime_j =  a_j,&   \mbox{if lb is used in transformation}\\
 *    x^\prime_j := ub_j - x_j,&   x_j = ub_j - x^\prime_j,&   a^\prime_j = -a_j,&   \mbox{if ub is used in transformation}
 *  \end{array}
 *  \f]
 *  and move the constant terms \f$ a_j\, lb_j \f$ or \f$ a_j\, ub_j \f$ to the rhs.
 *
 *  Transform variables (vlb or vub):
 *  \f[
 *  \begin{array}{llll}
 *    x^\prime_j := x_j - (bl_j\, zl_j + dl_j),&   x_j = x^\prime_j + (bl_j\, zl_j + dl_j),&   a^\prime_j =  a_j,&   \mbox{if vlb is used in transf.} \\
 *    x^\prime_j := (bu_j\, zu_j + du_j) - x_j,&   x_j = (bu_j\, zu_j + du_j) - x^\prime_j,&   a^\prime_j = -a_j,&   \mbox{if vub is used in transf.}
 *  \end{array}
 *  \f]
 *  move the constant terms \f$ a_j\, dl_j \f$ or \f$ a_j\, du_j \f$ to the rhs, and update the coefficient of the VLB variable:
 *  \f[
 *  \begin{array}{ll}
 *    a_{zl_j} := a_{zl_j} + a_j\, bl_j,& \mbox{or} \\
 *    a_{zu_j} := a_{zu_j} + a_j\, bu_j &
 *  \end{array}
 *  \f]
 */
static
SCIP_RETCODE cutsTransformMIR(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Real             boundswitch,        /**< fraction of domain up to which lower bound is used in transformation */
   SCIP_Bool             usevbds,            /**< should variable bounds be used in bound transformation? */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   SCIP_Bool             fixintegralrhs,     /**< should complementation tried to be adjusted such that rhs gets fractional? */
   SCIP_Bool             ignoresol,          /**< should the LP solution be ignored? (eg, apply MIR to dualray) */
   int*                  boundsfortrans,     /**< bounds that should be used for transformed variables: vlb_idx/vub_idx,
                                              *   -1 for global lb/ub, -2 for local lb/ub, or -3 for using closest bound;
                                              *   NULL for using closest bound for all variables */
   SCIP_BOUNDTYPE*       boundtypesfortrans, /**< type of bounds that should be used for transformed variables;
                                              *   NULL for using closest bound for all variables */
   SCIP_Real             minfrac,            /**< minimal fractionality of rhs to produce MIR cut for */
   SCIP_Real             maxfrac,            /**< maximal fractionality of rhs to produce MIR cut for */
   SCIP_Real*            cutcoefs,           /**< array of coefficients of cut */
   SCIP_Real*            cutrhs,             /**< pointer to right hand side of cut */
   int*                  cutinds,            /**< array of variables problem indices for non-zero coefficients in cut */
   int*                  nnz,                /**< number of non-zeros in cut */
   int*                  varsign,            /**< stores the sign of the transformed variable in summation */
   int*                  boundtype,          /**< stores the bound used for transformed variable:
                                              *   vlb/vub_idx, or -1 for global lb/ub, or -2 for local lb/ub */
   SCIP_Bool*            freevariable,       /**< stores whether a free variable was found in MIR row -> invalid summation */
   SCIP_Bool*            localbdsused        /**< pointer to store whether local bounds were used in transformation */
   )
{
   SCIP_Real* bestlbs;
   SCIP_Real* bestubs;
   int* bestlbtypes;
   int* bestubtypes;
   SCIP_BOUNDTYPE* selectedbounds;
   int* varpos;
   int i;
   int aggrrowintstart;
   int nvars;
   int firstcontvar;
   SCIP_VAR** vars;

   assert(varsign != NULL);
   assert(boundtype != NULL);
   assert(freevariable != NULL);
   assert(localbdsused != NULL);

   *freevariable = FALSE;
   *localbdsused = FALSE;

   /* allocate temporary memory to store best bounds and bound types */
   SCIP_CALL( SCIPallocBufferArray(scip, &bestlbs, 2*(*nnz)) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestubs, 2*(*nnz)) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestlbtypes, 2*(*nnz)) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestubtypes, 2*(*nnz)) );
   SCIP_CALL( SCIPallocBufferArray(scip, &selectedbounds, 2*(*nnz)) );

   /* start with continuous variables, because using variable bounds can affect the untransformed integral
    * variables, and these changes have to be incorporated in the transformation of the integral variables
    * (continuous variables have largest problem indices!)
    */
   SCIPsortDownIntReal(cutinds, cutcoefs, *nnz);

   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);
   firstcontvar = nvars - SCIPgetNContVars(scip);

   /* determine the best bounds for the continous variables */
   for( i = 0; i < *nnz && cutinds[i] >= firstcontvar; ++i )
   {
      SCIP_CALL( determineBestBounds(scip, vars[cutinds[i]], sol, boundswitch, usevbds, allowlocal, fixintegralrhs,
                                     ignoresol, boundsfortrans, boundtypesfortrans,
                                     bestlbs + i, bestubs + i, bestlbtypes + i, bestubtypes + i, selectedbounds + i, freevariable) );

      if( *freevariable )
         goto TERMINATE;
   }

   /* remember start of integer variables in the aggrrow */
   aggrrowintstart = i;

   /* remember positions of integral variables */
   SCIP_CALL( SCIPallocCleanBufferArray(scip, &varpos, firstcontvar) );

   for( i = (*nnz) - 1; i >= aggrrowintstart; --i )
      varpos[cutinds[i]] = i + 1;

   /* perform bound substitution for continuous variables */
   for( i = 0; i < aggrrowintstart; ++i )
   {
      SCIP_VAR* var = vars[cutinds[i]];
      if( selectedbounds[i] == SCIP_BOUNDTYPE_LOWER )
      {
         assert(!SCIPisInfinity(scip, -bestlbs[i]));

         /* use lower bound as transformation bound: x'_j := x_j - lb_j */
         boundtype[i] = bestlbtypes[i];
         varsign[i] = +1;

         /* standard (bestlbtype < 0) or variable (bestlbtype >= 0) lower bound? */
         if( bestlbtypes[i] < 0 )
         {
            *cutrhs -= cutcoefs[i] * bestlbs[i];
            *localbdsused = *localbdsused || (bestlbtypes[i] == -2);
         }
         else
         {
            SCIP_VAR** vlbvars;
            SCIP_Real* vlbcoefs;
            SCIP_Real* vlbconsts;
            int zidx;
            int k;

            vlbvars = SCIPvarGetVlbVars(var);
            vlbcoefs = SCIPvarGetVlbCoefs(var);
            vlbconsts = SCIPvarGetVlbConstants(var);
            assert(vlbvars != NULL);
            assert(vlbcoefs != NULL);
            assert(vlbconsts != NULL);

            assert(0 <= bestlbtypes[i] && bestlbtypes[i] < SCIPvarGetNVlbs(var));
            assert(SCIPvarIsActive(vlbvars[bestlbtypes[i]]));
            zidx = SCIPvarGetProbindex(vlbvars[bestlbtypes[i]]);
            assert(0 <= zidx && zidx < firstcontvar);

            *cutrhs -= cutcoefs[i] * vlbconsts[bestlbtypes[i]];

            /* check if integral variable already exists in the row */
            k = varpos[zidx];
            if( k == 0 )
            {
               /* if not add it to the end */
               k = (*nnz)++;
               varpos[zidx] = *nnz;
               cutinds[k] = zidx;
               cutcoefs[k] = cutcoefs[i] * vlbcoefs[bestlbtypes[i]];
            }
            else
            {
               /* if it is update the coefficient */
               cutcoefs[k - 1] += cutcoefs[i] * vlbcoefs[bestlbtypes[i]];
            }
         }
      }
      else
      {
         assert(!SCIPisInfinity(scip, bestubs[i]));

         /* use upper bound as transformation bound: x'_j := ub_j - x_j */
         boundtype[i] = bestubtypes[i];
         varsign[i] = -1;

         /* standard (bestubtype < 0) or variable (bestubtype >= 0) upper bound? */
         if( bestubtypes[i] < 0 )
         {
            *cutrhs -= cutcoefs[i] * bestubs[i];
            *localbdsused = *localbdsused || (bestubtypes[i] == -2);
         }
         else
         {
            SCIP_VAR** vubvars;
            SCIP_Real* vubcoefs;
            SCIP_Real* vubconsts;
            int zidx;
            int k;

            vubvars = SCIPvarGetVubVars(var);
            vubcoefs = SCIPvarGetVubCoefs(var);
            vubconsts = SCIPvarGetVubConstants(var);
            assert(vubvars != NULL);
            assert(vubcoefs != NULL);
            assert(vubconsts != NULL);

            assert(0 <= bestubtypes[i] && bestubtypes[i] < SCIPvarGetNVubs(var));
            assert(SCIPvarIsActive(vubvars[bestubtypes[i]]));
            zidx = SCIPvarGetProbindex(vubvars[bestubtypes[i]]);
            assert(zidx >= 0);

            *cutrhs -= cutcoefs[i] * vubconsts[bestubtypes[i]];

            /* check if integral variable already exists in the row */
            k = varpos[zidx];
            if( k == 0 )
            {
               /* if not add it to the end */
               k = (*nnz)++;
               varpos[zidx] = *nnz;
               cutinds[k] = zidx;
               cutcoefs[k] = cutcoefs[i] * vubcoefs[bestubtypes[i]];
            }
            else
            {
               /* if it is update the coefficient */
               cutcoefs[k - 1] += cutcoefs[i] * vubcoefs[bestubtypes[i]];
            }
         }
      }
   }

   /* remove integral variables that now have a zero coefficient due to variable bound usage of continuous variables
    * and determine the bound to use for the integer variables that are left
    */
   while( i < *nnz )
   {
      assert(cutinds[i] < firstcontvar);
      /* clean the varpos array for each integral variable */
      varpos[cutinds[i]] = 0;

      /* due to variable bound usage for the continous variables cancellation may have occurred */
      if( SCIPisZero(scip, cutcoefs[i]) )
      {
         --(*nnz);
         if( i < *nnz )
         {
            cutcoefs[i] = cutcoefs[*nnz];
            cutinds[i] = cutinds[*nnz];
         }
         /* do not increase i, since last element is copied to the i-th position */
         continue;
      }

      /** determine the best bounds for the integral variable, usevbd can be set to FALSE here as vbds are only used for continous variables */
      SCIP_CALL( determineBestBounds(scip, vars[cutinds[i]], sol, boundswitch, FALSE, allowlocal, fixintegralrhs,
                                     ignoresol, boundsfortrans, boundtypesfortrans,
                                     bestlbs + i, bestubs + i, bestlbtypes + i, bestubtypes + i, selectedbounds + i, freevariable) );


      /* increase i */
      ++i;

      if( *freevariable )
      {
         while( i < *nnz )
         {
            varpos[cutinds[i]] = 0;
            ++i;
         }

         SCIPfreeCleanBufferArray(scip, &varpos);
         goto TERMINATE;
      }
   }

   /* varpos array is not needed any more and has been cleaned in the previous loop */
   SCIPfreeCleanBufferArray(scip, &varpos);

   /* now perform the bound substitution on the remaining integral variables which only uses standard bounds */
   for( i = aggrrowintstart; i < *nnz; ++i )
   {
      /* perform bound substitution */
      if( selectedbounds[i] == SCIP_BOUNDTYPE_LOWER )
      {
         assert(!SCIPisInfinity(scip, - bestlbs[i]));
         assert(bestlbtypes[i] < 0);

         /* use lower bound as transformation bound: x'_j := x_j - lb_j */
         boundtype[i] = bestlbtypes[i];
         varsign[i] = +1;

         /* standard (bestlbtype < 0) or variable (bestlbtype >= 0) lower bound? */
         *cutrhs -= cutcoefs[i] * bestlbs[i];
         *localbdsused = *localbdsused || (bestlbtypes[i] == -2);
      }
      else
      {
         assert(!SCIPisInfinity(scip, bestubs[i]));
         assert(bestubtypes[i] < 0);

         /* use upper bound as transformation bound: x'_j := ub_j - x_j */
         boundtype[i] = bestubtypes[i];
         varsign[i] = -1;

         /* standard (bestubtype < 0) or variable (bestubtype >= 0) upper bound? */
         *cutrhs -= cutcoefs[i] * bestubs[i];
         *localbdsused = *localbdsused || (bestubtypes[i] == -2);
      }
   }

   if( fixintegralrhs )
   {
      SCIP_Real f0;

      /* check if rhs is fractional */
      f0 = EPSFRAC(*cutrhs, SCIPsumepsilon(scip));
      if( f0 < minfrac || f0 > maxfrac )
      {
         SCIP_Real bestviolgain;
         SCIP_Real bestnewf0;
         int besti;

         /* choose complementation of one variable differently such that f0 is in correct range */
         besti = -1;
         bestviolgain = -1e+100;
         bestnewf0 = 1.0;
         for( i = 0; i < *nnz; i++ )
         {
            int v;

            v = cutinds[i];
            assert(0 <= v && v < nvars);
            assert(!SCIPisZero(scip, cutcoefs[i]));

            if( boundtype[i] < 0
               && ((varsign[i] == +1 && !SCIPisInfinity(scip, bestubs[i]) && bestubtypes[i] < 0)
                  || (varsign[i] == -1 && !SCIPisInfinity(scip, -bestlbs[i]) && bestlbtypes[i] < 0)) )
            {
               SCIP_Real fj;
               SCIP_Real newfj;
               SCIP_Real newrhs;
               SCIP_Real newf0;
               SCIP_Real solval;
               SCIP_Real viol;
               SCIP_Real newviol;
               SCIP_Real violgain;

               /* currently:              a'_j =  varsign * a_j  ->  f'_j =  a'_j - floor(a'_j)
                * after complementation: a''_j = -varsign * a_j  -> f''_j = a''_j - floor(a''_j) = 1 - f'_j
                *                        rhs'' = rhs' + varsign * a_j * (lb_j - ub_j)
                * cut violation from f0 and fj:   f'_0 -  f'_j *  x'_j
                * after complementation:         f''_0 - f''_j * x''_j
                *
                * for continuous variables, we just set f'_j = f''_j = |a'_j|
                */
               newrhs = *cutrhs + varsign[i] * cutcoefs[i] * (bestlbs[i] - bestubs[i]);
               newf0 = EPSFRAC(newrhs, SCIPsumepsilon(scip));
               if( newf0 < minfrac || newf0 > maxfrac )
                  continue;
               if( v >= firstcontvar )
               {
                  fj = REALABS(cutcoefs[i]);
                  newfj = fj;
               }
               else
               {
                  fj = SCIPfrac(scip, varsign[i] * cutcoefs[i]);
                  newfj = SCIPfrac(scip, -varsign[i] * cutcoefs[i]);
               }

               if( !ignoresol )
               {
                  solval = (sol == NULL ? SCIPvarGetLPSol(vars[v]) : SCIPgetSolVal(scip, sol, vars[v]));
                  viol = f0 - fj * (varsign[i] == +1 ? solval - bestlbs[i] : bestubs[i] - solval);
                  newviol = newf0 - newfj * (varsign[i] == -1 ? solval - bestlbs[i] : bestubs[i] - solval);
                  violgain = newviol - viol;
               }
               else
               {
                  /* todo: this should be done, this can improve the dualray significantly */
                  SCIPerrorMessage("Cannot handle closest bounds with ignoring the LP solution.\n");
                  return SCIP_INVALIDCALL;
               }

               /* prefer larger violations; for equal violations, prefer smaller f0 values since then the possibility that
                * we f_j > f_0 is larger and we may improve some coefficients in rounding
                */
               if( SCIPisGT(scip, violgain, bestviolgain)
                  || (SCIPisGE(scip, violgain, bestviolgain) && newf0 < bestnewf0) )
               {
                  besti = i;
                  bestviolgain = violgain;
                  bestnewf0 = newf0;
               }
            }
         }

         if( besti >= 0 )
         {
            assert(besti < *nnz);
            assert(boundtype[besti] < 0);
            assert(!SCIPisInfinity(scip, -bestlbs[besti]));
            assert(!SCIPisInfinity(scip, bestubs[besti]));

            /* switch the complementation of this variable */
            *cutrhs += varsign[besti] * cutcoefs[besti] * (bestlbs[besti] - bestubs[besti]);
            if( varsign[besti] == +1 )
            {
               /* switch to upper bound */
               assert(bestubtypes[besti] < 0); /* cannot switch to a variable bound (would lead to further coef updates) */
               boundtype[besti] = bestubtypes[besti];
               varsign[besti] = -1;
            }
            else
            {
               /* switch to lower bound */
               assert(bestlbtypes[besti] < 0); /* cannot switch to a variable bound (would lead to further coef updates) */
               boundtype[besti] = bestlbtypes[besti];
               varsign[besti] = +1;
            }
            *localbdsused = *localbdsused || (boundtype[besti] == -2);
         }
      }
   }

 TERMINATE:

   /*free temporary memory */
   SCIPfreeBufferArray(scip, &selectedbounds);
   SCIPfreeBufferArray(scip, &bestubtypes);
   SCIPfreeBufferArray(scip, &bestlbtypes);
   SCIPfreeBufferArray(scip, &bestubs);
   SCIPfreeBufferArray(scip, &bestlbs);

   return SCIP_OKAY;
}

/** Calculate fractionalities \f$ f_0 := b - down(b), f_j := a^\prime_j - down(a^\prime_j) \f$, and derive MIR cut \f$ \tilde{a} \cdot x' \leq down(b) \f$
 * \f[
 * \begin{array}{rll}
 *  integers :&  \tilde{a}_j = down(a^\prime_j),                        & if \qquad f_j \leq f_0 \\
 *            &  \tilde{a}_j = down(a^\prime_j) + (f_j - f_0)/(1 - f_0),& if \qquad f_j >  f_0 \\
 *  continuous:& \tilde{a}_j = 0,                                       & if \qquad a^\prime_j \geq 0 \\
 *             & \tilde{a}_j = a^\prime_j/(1 - f_0),                    & if \qquad a^\prime_j <  0
 * \end{array}
 * \f]
 *
 *  Transform inequality back to \f$ \hat{a} \cdot x \leq rhs \f$:
 *
 *  (lb or ub):
 * \f[
 * \begin{array}{lllll}
 *    x^\prime_j := x_j - lb_j,&   x_j = x^\prime_j + lb_j,&   a^\prime_j =  a_j,&   \hat{a}_j :=  \tilde{a}_j,&   \mbox{if lb was used in transformation} \\
 *    x^\prime_j := ub_j - x_j,&   x_j = ub_j - x^\prime_j,&   a^\prime_j = -a_j,&   \hat{a}_j := -\tilde{a}_j,&   \mbox{if ub was used in transformation}
 * \end{array}
 * \f]
 *  and move the constant terms
 * \f[
 * \begin{array}{cl}
 *    -\tilde{a}_j \cdot lb_j = -\hat{a}_j \cdot lb_j,& \mbox{or} \\
 *     \tilde{a}_j \cdot ub_j = -\hat{a}_j \cdot ub_j &
 * \end{array}
 * \f]
 *  to the rhs.
 *
 *  (vlb or vub):
 * \f[
 * \begin{array}{lllll}
 *    x^\prime_j := x_j - (bl_j \cdot zl_j + dl_j),&   x_j = x^\prime_j + (bl_j\, zl_j + dl_j),&   a^\prime_j =  a_j,&   \hat{a}_j :=  \tilde{a}_j,&   \mbox{(vlb)} \\
 *    x^\prime_j := (bu_j\, zu_j + du_j) - x_j,&   x_j = (bu_j\, zu_j + du_j) - x^\prime_j,&   a^\prime_j = -a_j,&   \hat{a}_j := -\tilde{a}_j,&   \mbox{(vub)}
 * \end{array}
 * \f]
 *  move the constant terms
 * \f[
 * \begin{array}{cl}
 *    -\tilde{a}_j\, dl_j = -\hat{a}_j\, dl_j,& \mbox{or} \\
 *     \tilde{a}_j\, du_j = -\hat{a}_j\, du_j &
 * \end{array}
 * \f]
 *  to the rhs, and update the VB variable coefficients:
 * \f[
 * \begin{array}{ll}
 *    \hat{a}_{zl_j} := \hat{a}_{zl_j} - \tilde{a}_j\, bl_j = \hat{a}_{zl_j} - \hat{a}_j\, bl_j,& \mbox{or} \\
 *    \hat{a}_{zu_j} := \hat{a}_{zu_j} + \tilde{a}_j\, bu_j = \hat{a}_{zu_j} - \hat{a}_j\, bu_j &
 * \end{array}
 * \f]
 */
static
SCIP_RETCODE cutsRoundMIR(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_Real*RESTRICT    cutcoefs,           /**< array of coefficients of cut */
   SCIP_Real*RESTRICT    cutrhs,             /**< pointer to right hand side of cut */
   int*RESTRICT          cutinds,            /**< array of variables problem indices for non-zero coefficients in cut */
   int*RESTRICT          nnz,                /**< number of non-zeros in cut */
   int*RESTRICT          varsign,            /**< stores the sign of the transformed variable in summation */
   int*RESTRICT          boundtype,          /**< stores the bound used for transformed variable (vlb/vub_idx or -1 for lb/ub) */
   SCIP_Real             f0                  /**< fractional value of rhs */
   )
{
   SCIP_Real onedivoneminusf0;
   int i;
   int firstcontvar;
   SCIP_VAR** vars;
   int*RESTRICT varpos;
   int ndelcontvars;
   int aggrrowlastcontvar;

   assert(cutrhs != NULL);
   assert(cutcoefs != NULL);
   assert(cutinds != NULL);
   assert(nnz != NULL);
   assert(boundtype != NULL);
   assert(varsign != NULL);
   assert(0.0 < f0 && f0 < 1.0);

   onedivoneminusf0 = 1.0 / (1.0 - f0);

   /* Loop backwards to process integral variables first and be able to delete coefficients of integral variables
    * without destroying the ordering of the aggrrow's non-zeros.
    * (due to sorting in cutsTransformMIR the ordering is continuous before integral)
    */

   firstcontvar = SCIPgetNVars(scip) - SCIPgetNContVars(scip);
   vars = SCIPgetVars(scip);
#ifndef NDEBUG
   /*in debug mode check, that all continuous variables of the aggrrow come before the integral variables */
   i = 0;
   while( i < *nnz && cutinds[i] >= firstcontvar )
      ++i;

   while( i < *nnz )
   {
      assert(cutinds[i] < firstcontvar);
      ++i;
   }
#endif

   SCIP_CALL( SCIPallocCleanBufferArray(scip, &varpos, firstcontvar) );

   for( i = *nnz - 1; i >= 0 && cutinds[i] < firstcontvar; --i )
   {
      SCIP_VAR* var;
      SCIP_Real cutaj;
      int v;

      v = cutinds[i];
      assert(0 <= v && v < SCIPgetNVars(scip));

      var = vars[v];
      assert(var != NULL);
      assert(SCIPvarGetProbindex(var) == v);
      assert(varsign[i] == +1 || varsign[i] == -1);

      /* calculate the coefficient in the retransformed cut */
      {
         SCIP_Real aj;
         SCIP_Real downaj;
         SCIP_Real fj;

         aj = varsign[i] * cutcoefs[i]; /* a'_j */
         downaj = SCIPfloor(scip, aj);
         fj = aj - downaj;

         if( SCIPisSumLE(scip, fj, f0) )
            cutaj = varsign[i] * downaj; /* a^_j */
         else
            cutaj = varsign[i] * (downaj + (fj - f0) * onedivoneminusf0); /* a^_j */
      }

      /* remove zero cut coefficients from cut */
      if( SCIPisZero(scip, cutaj) )
      {
         --*nnz;
         if( i < *nnz )
         {
            cutinds[i] = cutinds[*nnz];
            cutcoefs[i] = cutcoefs[*nnz];
            varpos[cutinds[i]] = i + 1;
         }
         continue;
      }

      varpos[v] = i + 1;
      cutcoefs[i] = cutaj;

      /* integral var uses standard bound */
      assert(boundtype[i] < 0);

      /* move the constant term  -a~_j * lb_j == -a^_j * lb_j , or  a~_j * ub_j == -a^_j * ub_j  to the rhs */
      if( varsign[i] == +1 )
      {
         /* lower bound was used */
         if( boundtype[i] == -1 )
         {
            assert(!SCIPisInfinity(scip, -SCIPvarGetLbGlobal(var)));
            *cutrhs += cutaj * SCIPvarGetLbGlobal(var);
         }
         else
         {
            assert(!SCIPisInfinity(scip, -SCIPvarGetLbLocal(var)));
            *cutrhs += cutaj * SCIPvarGetLbLocal(var);
         }
      }
      else
      {
         /* upper bound was used */
         if( boundtype[i] == -1 )
         {
            assert(!SCIPisInfinity(scip, SCIPvarGetUbGlobal(var)));
            *cutrhs += cutaj * SCIPvarGetUbGlobal(var);
         }
         else
         {
            assert(!SCIPisInfinity(scip, SCIPvarGetUbLocal(var)));
            *cutrhs += cutaj * SCIPvarGetUbLocal(var);
         }
      }
   }

   /* now process the continuous variables; postpone deletetion of zeros till all continuous variables have been processed */
   ndelcontvars = 0;
   aggrrowlastcontvar = i;
   while( i >= ndelcontvars )
   {
      SCIP_VAR* var;
      SCIP_Real cutaj;
      int v;

      v = cutinds[i];
      assert(0 <= v && v < SCIPgetNVars(scip));

      var = vars[v];
      assert(var != NULL);
      assert(SCIPvarGetProbindex(var) == v);
      assert(varsign[i] == +1 || varsign[i] == -1);
      assert( v >= firstcontvar );

      /* calculate the coefficient in the retransformed cut */
      {
         SCIP_Real aj;

         aj = varsign[i] * cutcoefs[i]; /* a'_j */
         if( aj >= 0.0 )
            cutaj = 0.0;
         else
            cutaj = varsign[i] * aj * onedivoneminusf0; /* a^_j */
      }

      /* remove zero cut coefficients from cut; move a continuous var from the beginning
       * to the current position, so that all integral variables stay behind the continuous
       * variables
       */
      if( SCIPisZero(scip, cutaj) )
      {
         if( i > ndelcontvars )
         {
            cutinds[i] = cutinds[ndelcontvars];
            cutcoefs[i] = cutcoefs[ndelcontvars];
            varsign[i] = varsign[ndelcontvars];
            boundtype[i] = boundtype[ndelcontvars];
         }
         ++ndelcontvars;
         continue;
      }

      cutcoefs[i] = cutaj;

      /* check for variable bound use */
      if( boundtype[i] < 0 )
      {
         /* standard bound */

         /* move the constant term  -a~_j * lb_j == -a^_j * lb_j , or  a~_j * ub_j == -a^_j * ub_j  to the rhs */
         if( varsign[i] == +1 )
         {
            /* lower bound was used */
            if( boundtype[i] == -1 )
            {
               assert(!SCIPisInfinity(scip, -SCIPvarGetLbGlobal(var)));
               *cutrhs += cutaj * SCIPvarGetLbGlobal(var);
            }
            else
            {
               assert(!SCIPisInfinity(scip, -SCIPvarGetLbLocal(var)));
               *cutrhs += cutaj * SCIPvarGetLbLocal(var);
            }
         }
         else
         {
            /* upper bound was used */
            if( boundtype[i] == -1 )
            {
               assert(!SCIPisInfinity(scip, SCIPvarGetUbGlobal(var)));
               *cutrhs += cutaj * SCIPvarGetUbGlobal(var);
            }
            else
            {
               assert(!SCIPisInfinity(scip, SCIPvarGetUbLocal(var)));
               *cutrhs += cutaj * SCIPvarGetUbLocal(var);
            }
         }
      }
      else
      {
         SCIP_VAR** vbz;
         SCIP_Real* vbb;
         SCIP_Real* vbd;
         int vbidx;
         int zidx;
         int k;

         /* variable bound */
         vbidx = boundtype[i];

         /* change mirrhs and cutaj of integer variable z_j of variable bound */
         if( varsign[i] == +1 )
         {
            /* variable lower bound was used */
            assert(0 <= vbidx && vbidx < SCIPvarGetNVlbs(var));
            vbz = SCIPvarGetVlbVars(var);
            vbb = SCIPvarGetVlbCoefs(var);
            vbd = SCIPvarGetVlbConstants(var);
         }
         else
         {
            /* variable upper bound was used */
            assert(0 <= vbidx && vbidx < SCIPvarGetNVubs(var));
            vbz = SCIPvarGetVubVars(var);
            vbb = SCIPvarGetVubCoefs(var);
            vbd = SCIPvarGetVubConstants(var);
         }
         assert(SCIPvarIsActive(vbz[vbidx]));
         zidx = SCIPvarGetProbindex(vbz[vbidx]);
         assert(0 <= zidx && zidx < firstcontvar);

         *cutrhs += cutaj * vbd[vbidx];

         k = varpos[zidx];

         /* add variable to sparsity pattern */
         if( k == 0 )
         {
            k = (*nnz)++;
            varpos[zidx] = *nnz;
            cutcoefs[k] = - cutaj * vbb[vbidx];
            cutinds[k] = zidx;
         }
         else
            cutcoefs[k - 1] -= cutaj * vbb[vbidx];
      }

      /* advance to next variable */
      --i;
   }

   /* clear the array with the variable positions of the integral variables in the cut */
   for( i = *nnz - 1; i > aggrrowlastcontvar; --i )
      varpos[cutinds[i]] = 0;

   SCIPfreeCleanBufferArray(scip, &varpos);

   /* fill the empty position due to deleted continuous variables */
   if( ndelcontvars > 0 )
   {
      assert(ndelcontvars <= *nnz);
      *nnz -= ndelcontvars;
      if( *nnz < ndelcontvars )
      {
         BMScopyMemoryArray(cutcoefs, cutcoefs + ndelcontvars, *nnz);
         BMScopyMemoryArray(cutinds, cutinds + ndelcontvars, *nnz);
      }
      else
      {
         BMScopyMemoryArray(cutcoefs, cutcoefs + *nnz, ndelcontvars);
         BMScopyMemoryArray(cutinds, cutinds + *nnz, ndelcontvars);
      }
   }

   return SCIP_OKAY;
}

/** substitute aggregated slack variables:
 *
 *  The coefficient of the slack variable s_r is equal to the row's weight times the slack's sign, because the slack
 *  variable only appears in its own row: \f$ a^\prime_r = scale * weight[r] * slacksign[r]. \f$
 *
 *  Depending on the slacks type (integral or continuous), its coefficient in the cut calculates as follows:
 *  \f[
 *  \begin{array}{rll}
 *    integers : & \hat{a}_r = \tilde{a}_r = down(a^\prime_r),                      & \mbox{if}\qquad f_r <= f0 \\
 *               & \hat{a}_r = \tilde{a}_r = down(a^\prime_r) + (f_r - f0)/(1 - f0),& \mbox{if}\qquad f_r >  f0 \\
 *    continuous:& \hat{a}_r = \tilde{a}_r = 0,                                     & \mbox{if}\qquad a^\prime_r >= 0 \\
 *               & \hat{a}_r = \tilde{a}_r = a^\prime_r/(1 - f0),                   & \mbox{if}\qquad a^\prime_r <  0
 *  \end{array}
 *  \f]
 *
 *  Substitute \f$ \hat{a}_r \cdot s_r \f$ by adding \f$ \hat{a}_r \f$ times the slack's definition to the cut.
 */
static
SCIP_RETCODE cutsSubstituteMIR(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_Real*            weights,            /**< row weights in row summation */
   int*                  slacksign,          /**< stores the sign of the row's slack variable in summation */
   int*                  rowinds,            /**< sparsity pattern of used rows */
   int                   nrowinds,           /**< number of used rows */
   SCIP_Real             scale,              /**< additional scaling factor multiplied to all rows */
   SCIP_Real*            cutcoefs,           /**< array of coefficients of cut */
   SCIP_Real*            cutrhs,             /**< pointer to right hand side of cut */
   int*                  cutinds,            /**< array of variables problem indices for non-zero coefficients in cut */
   int*                  nnz,                /**< number of non-zeros in cut */
   SCIP_Real             f0                  /**< fractional value of rhs */
   )
{  /*lint --e{715}*/
   SCIP_ROW** rows;
   SCIP_Real onedivoneminusf0;
   int i;

   assert(scip != NULL);
   assert(weights != NULL);
   assert(slacksign != NULL);
   assert(rowinds != NULL);
   assert(SCIPisPositive(scip, scale));
   assert(cutcoefs != NULL);
   assert(cutrhs != NULL);
   assert(cutinds != NULL);
   assert(nnz != NULL);
   assert(0.0 < f0 && f0 < 1.0);

   onedivoneminusf0 = 1.0 / (1.0 - f0);

   rows = SCIPgetLPRows(scip);
   for( i = 0; i < nrowinds; i++ )
   {
      SCIP_ROW* row;
      SCIP_Real ar;
      SCIP_Real downar;
      SCIP_Real cutar;
      SCIP_Real fr;
      SCIP_Real mul;
      int r;

      r = rowinds[i];
      assert(0 <= r && r < SCIPgetNLPRows(scip));
      assert(slacksign[i] == -1 || slacksign[i] == +1);
      assert(!SCIPisZero(scip, weights[i]));

      row = rows[r];
      assert(row != NULL);
      assert(row->len == 0 || row->cols != NULL);
      assert(row->len == 0 || row->cols_index != NULL);
      assert(row->len == 0 || row->vals != NULL);

      /* get the slack's coefficient a'_r in the aggregated row */
      ar = slacksign[i] * scale * weights[i];

      /* calculate slack variable's coefficient a^_r in the cut */
      if( row->integral
         && ((slacksign[i] == +1 && SCIPisFeasIntegral(scip, row->rhs - row->constant))
            || (slacksign[i] == -1 && SCIPisFeasIntegral(scip, row->lhs - row->constant))) )
      {
         /* slack variable is always integral:
          *    a^_r = a~_r = down(a'_r)                      , if f_r <= f0
          *    a^_r = a~_r = down(a'_r) + (f_r - f0)/(1 - f0), if f_r >  f0
          */
         downar = SCIPfloor(scip, ar);
         fr = ar - downar;
         if( SCIPisLE(scip, fr, f0) )
            cutar = downar;
         else
            cutar = downar + (fr - f0) * onedivoneminusf0;
      }
      else
      {
         /* slack variable is continuous:
          *    a^_r = a~_r = 0                               , if a'_r >= 0
          *    a^_r = a~_r = a'_r/(1 - f0)                   , if a'_r <  0
          */
         if( ar >= 0.0 )
            continue; /* slack can be ignored, because its coefficient is reduced to 0.0 */
         else
            cutar = ar * onedivoneminusf0;
      }

      /* if the coefficient was reduced to zero, ignore the slack variable */
      if( SCIPisZero(scip, cutar) )
         continue;

      /* depending on the slack's sign, we have
       *   a*x + c + s == rhs  =>  s == - a*x - c + rhs,  or  a*x + c - s == lhs  =>  s == a*x + c - lhs
       * substitute a^_r * s_r by adding a^_r times the slack's definition to the cut.
       */
      mul = -slacksign[i] * cutar;

      /* add the slack's definition multiplied with a^_j to the cut */
      SCIP_CALL( varVecAddScaledRowCoefs(scip, &cutinds, &cutcoefs, nnz, NULL, row, mul) );

      /* move slack's constant to the right hand side */
      if( slacksign[i] == +1 )
      {
         SCIP_Real rhs;

         /* a*x + c + s == rhs  =>  s == - a*x - c + rhs: move a^_r * (rhs - c) to the right hand side */
         assert(!SCIPisInfinity(scip, row->rhs));
         rhs = row->rhs - row->constant;
         if( row->integral )
         {
            /* the right hand side was implicitly rounded down in row aggregation */
            rhs = SCIPfeasFloor(scip, rhs);
         }
         *cutrhs -= cutar * rhs;
      }
      else
      {
         SCIP_Real lhs;

         /* a*x + c - s == lhs  =>  s == a*x + c - lhs: move a^_r * (c - lhs) to the right hand side */
         assert(!SCIPisInfinity(scip, -row->lhs));
         lhs = row->lhs - row->constant;
         if( row->integral )
         {
            /* the left hand side was implicitly rounded up in row aggregation */
            lhs = SCIPfeasCeil(scip, lhs);
         }
         *cutrhs += cutar * lhs;
      }
   }

   /* set rhs to zero, if it's very close to */
   if( SCIPisZero(scip, *cutrhs) )
      *cutrhs = 0.0;

   return SCIP_OKAY;
}

/** calculates an MIR cut out of the weighted sum of LP rows; The weights of modifiable rows are set to 0.0, because
 *  these rows cannot participate in an MIR cut.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if @p scip is in one of the following stages:
 *       - \ref SCIP_STAGE_SOLVING
 *
 *  See \ref SCIP_Stage "SCIP_STAGE" for a complete list of all possible solving stages.
 */
SCIP_RETCODE SCIPcalcMIR(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Real             boundswitch,        /**< fraction of domain up to which lower bound is used in transformation */
   SCIP_Bool             usevbds,            /**< should variable bounds be used in bound transformation? */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   SCIP_Bool             fixintegralrhs,     /**< should complementation tried to be adjusted such that rhs gets fractional? */
   int*                  boundsfortrans,     /**< bounds that should be used for transformed variables: vlb_idx/vub_idx,
                                              *   -1 for global lb/ub, -2 for local lb/ub, or -3 for using closest bound;
                                              *   NULL for using closest bound for all variables */
   SCIP_BOUNDTYPE*       boundtypesfortrans, /**< type of bounds that should be used for transformed variables;
                                              *   NULL for using closest bound for all variables */
   SCIP_Real             minfrac,            /**< minimal fractionality of rhs to produce MIR cut for */
   SCIP_Real             maxfrac,            /**< maximal fractionality of rhs to produce MIR cut for */
   SCIP_Real             scale,              /**< additional scaling factor multiplied to the aggrrow; must be positive */
   SCIP_AGGRROW*         aggrrow,            /**< aggrrow to compute MIR cut for */
   SCIP_Real*            cutcoefs,           /**< array to store the non-zero coefficients in the cut */
   SCIP_Real*            cutrhs,             /**< pointer to store the right hand side of the cut */
   int*                  cutinds,            /**< array to store the problem indices of variables with a non-zero coefficient in the cut */
   int*                  cutnnz,             /**< pointer to store the number of non-zeros in the cut */
   SCIP_Real*            cutefficacy,        /**< pointer to store efficacy of cut, or NULL */
   int*                  cutrank,            /**< pointer to return rank of generated cut */
   SCIP_Bool*            cutislocal,         /**< pointer to store whether the generated cut is only valid locally */
   SCIP_Bool*            success             /**< pointer to store whether the returned coefficients are a valid MIR cut */
   )
{
   int i;
   int nvars;
   int* varsign;
   int* boundtype;

   SCIP_Real downrhs;
   SCIP_Real f0;
   SCIP_Bool freevariable;
   SCIP_Bool localbdsused;

   assert(aggrrow != NULL);
   assert(aggrrow->nrows >= 1);
   assert(SCIPisPositive(scip, scale));
   assert(success != NULL);

   SCIPdebugMessage("calculating MIR cut (scale: %g)\n", scale);

   *success = FALSE;

   /* allocate temporary memory */
   nvars = SCIPgetNVars(scip);
   SCIP_CALL( SCIPallocBufferArray(scip, &varsign, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &boundtype, nvars) );

   /* initialize cut with aggregation */
   *cutnnz = aggrrow->nnz;

   BMScopyMemoryArray(cutinds, aggrrow->inds, *cutnnz);
   if( scale != 1.0 )
   {
      *cutrhs = scale * aggrrow->rhs;
      for( i = 0; i < *cutnnz; ++i )
         cutcoefs[i] = aggrrow->vals[i] * scale;
   }
   else
   {
      *cutrhs = aggrrow->rhs;
      BMScopyMemoryArray(cutcoefs, aggrrow->vals, *cutnnz);
   }

   *cutislocal = aggrrow->local;
   /* Transform equation  a*x == b, lb <= x <= ub  into standard form
    *   a'*x' == b, 0 <= x' <= ub'.
    *
    * Transform variables (lb or ub):
    *   x'_j := x_j - lb_j,   x_j == x'_j + lb_j,   a'_j ==  a_j,   if lb is used in transformation
    *   x'_j := ub_j - x_j,   x_j == ub_j - x'_j,   a'_j == -a_j,   if ub is used in transformation
    * and move the constant terms "a_j * lb_j" or "a_j * ub_j" to the rhs.
    *
    * Transform variables (vlb or vub):
    *   x'_j := x_j - (bl_j * zl_j + dl_j),   x_j == x'_j + (bl_j * zl_j + dl_j),   a'_j ==  a_j,   if vlb is used in transf.
    *   x'_j := (bu_j * zu_j + du_j) - x_j,   x_j == (bu_j * zu_j + du_j) - x'_j,   a'_j == -a_j,   if vub is used in transf.
    * move the constant terms "a_j * dl_j" or "a_j * du_j" to the rhs, and update the coefficient of the VLB variable:
    *   a_{zl_j} := a_{zl_j} + a_j * bl_j, or
    *   a_{zu_j} := a_{zu_j} + a_j * bu_j
    */

   cleanupCut(scip, aggrrow->local, cutinds, cutcoefs, cutnnz, cutrhs);

   SCIP_CALL( cutsTransformMIR(scip, sol, boundswitch, usevbds, allowlocal, fixintegralrhs, FALSE,
         boundsfortrans, boundtypesfortrans, minfrac, maxfrac, cutcoefs, cutrhs, cutinds, cutnnz, varsign, boundtype, &freevariable, &localbdsused) );
   assert(allowlocal || !localbdsused);
   *cutislocal = *cutislocal || localbdsused;

   if( freevariable )
      goto TERMINATE;
   SCIPdebug(printCut(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz, FALSE, FALSE));

   /* Calculate fractionalities  f_0 := b - down(b), f_j := a'_j - down(a'_j) , and derive MIR cut
    *   a~*x' <= down(b)
    * integers :  a~_j = down(a'_j)                      , if f_j <= f_0
    *             a~_j = down(a'_j) + (f_j - f0)/(1 - f0), if f_j >  f_0
    * continuous: a~_j = 0                               , if a'_j >= 0
    *             a~_j = a'_j/(1 - f0)                   , if a'_j <  0
    *
    * Transform inequality back to a^*x <= rhs:
    *
    * (lb or ub):
    *   x'_j := x_j - lb_j,   x_j == x'_j + lb_j,   a'_j ==  a_j,   a^_j :=  a~_j,   if lb was used in transformation
    *   x'_j := ub_j - x_j,   x_j == ub_j - x'_j,   a'_j == -a_j,   a^_j := -a~_j,   if ub was used in transformation
    * and move the constant terms
    *   -a~_j * lb_j == -a^_j * lb_j, or
    *    a~_j * ub_j == -a^_j * ub_j
    * to the rhs.
    *
    * (vlb or vub):
    *   x'_j := x_j - (bl_j * zl_j + dl_j),   x_j == x'_j + (bl_j * zl_j + dl_j),   a'_j ==  a_j,   a^_j :=  a~_j,   (vlb)
    *   x'_j := (bu_j * zu_j + du_j) - x_j,   x_j == (bu_j * zu_j + du_j) - x'_j,   a'_j == -a_j,   a^_j := -a~_j,   (vub)
    * move the constant terms
    *   -a~_j * dl_j == -a^_j * dl_j, or
    *    a~_j * du_j == -a^_j * du_j
    * to the rhs, and update the VB variable coefficients:
    *   a^_{zl_j} := a^_{zl_j} - a~_j * bl_j == a^_{zl_j} - a^_j * bl_j, or
    *   a^_{zu_j} := a^_{zu_j} + a~_j * bu_j == a^_{zu_j} - a^_j * bu_j
    */
   downrhs = EPSFLOOR(*cutrhs, SCIPsumepsilon(scip));
   f0 = *cutrhs - downrhs;
   if( f0 < minfrac || f0 > maxfrac )
      goto TERMINATE;

   /* We multiply the coefficients of the base inequality roughly by scale/(1-f0).
    * If this gives a scalar that is very big, we better do not generate this cut.
    */
   if( REALABS(scale)/(1.0 - f0) > MAXCMIRSCALE )
      goto TERMINATE;

   *cutrhs = downrhs;
   SCIP_CALL( cutsRoundMIR(scip, cutcoefs, cutrhs, cutinds, cutnnz, varsign, boundtype, f0) );
   SCIPdebug(printCut(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz, FALSE, FALSE));

   /* substitute aggregated slack variables:
    *
    * The coefficient of the slack variable s_r is equal to the row's weight times the slack's sign, because the slack
    * variable only appears in its own row:
    *    a'_r = scale * weight[r] * slacksign[r].
    *
    * Depending on the slacks type (integral or continuous), its coefficient in the cut calculates as follows:
    *   integers :  a^_r = a~_r = down(a'_r)                      , if f_r <= f0
    *               a^_r = a~_r = down(a'_r) + (f_r - f0)/(1 - f0), if f_r >  f0
    *   continuous: a^_r = a~_r = 0                               , if a'_r >= 0
    *               a^_r = a~_r = a'_r/(1 - f0)                   , if a'_r <  0
    *
    * Substitute a^_r * s_r by adding a^_r times the slack's definition to the cut.
    */
   SCIP_CALL( cutsSubstituteMIR(scip, aggrrow->rowweights, aggrrow->slacksign, aggrrow->rowsinds,
                                aggrrow->nrows, scale, cutcoefs, cutrhs, cutinds, cutnnz, f0) );
   SCIPdebug(printCut(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz, FALSE, FALSE));

   /* remove again all nearly-zero coefficients from MIR row and relax the right hand side correspondingly in order to
    * prevent numerical rounding errors
    */
   cleanupCut(scip, *cutislocal, cutinds, cutcoefs, cutnnz, cutrhs);
   SCIPdebug(printCut(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz, FALSE, FALSE));

   *success = TRUE;

   if( cutefficacy != NULL )
      *cutefficacy = calcEfficacy(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz);

   if( cutrank != NULL )
      *cutrank = aggrrow->rank + 1;

 TERMINATE:
   /* free temporary memory */
   SCIPfreeBufferArray(scip, &boundtype);
   SCIPfreeBufferArray(scip, &varsign);

   return SCIP_OKAY;
}

/** test one value of delta for the given mixed knapsack set obtained from the given aggregation row;
 *  if an efficacious cut better than the current one was found then it will be stored in the given arrays
 *  and the success flag will be set to TRUE
 */
static
SCIP_RETCODE tryDelta(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_AGGRROW*         aggrrow,            /**< the aggregation row that was used to produce the give mixed knapsack set */
   SCIP_Real             minfrac,            /**< minimal fractionality of rhs to produce MIR cut for */
   SCIP_Real             maxfrac,            /**< maximal fractionality of rhs to produce MIR cut for */
   SCIP_Bool             mksetislocal,       /**< is the given mixed knapsack set only valid locally */
   SCIP_Real*            mksetcoefs,         /**< array of coefficients of mixed knapsack set */
   SCIP_Real             mksetrhs,           /**< right hand side of mixed knapsack set */
   int*                  mksetinds,          /**< array of variable problem indices for coefficients in the mixed knapsack set */
   int                   mksetnnz,           /**< number of non-zeros in the mixed knapsack set */
   int*                  boundtype,          /**< stores the bound used for transformed variable:
                                              *   vlb/vub_idx, or -1 for global lb/ub, or -2 for local lb/ub */
   int*                  varsign,            /**< stores the sign of the transformed variable in summation */
   SCIP_Real*            bestcutcoefs,       /**< array of coefficients for currently best cut */
   SCIP_Real*            bestcutrhs,         /**< right hand side of currently best cut */
   int*                  bestcutinds,        /**< indices of currently best cut */
   int*                  bestcutnnz,         /**< number of non-zeros for currently best cut */
   SCIP_Real*            bestcutefficacy,    /**< efficacy of currently best cut */
   SCIP_Real*            bestcutdelta,       /**< delta used for currently best cut */
   SCIP_Real             minefficacy,        /**< minimal efficacy for storing cut in arrays */
   int*                  tmpboundtype,       /**< temporary working array for testing the given delta */
   int*                  tmpvarsign,         /**< temporary working array for testing the given delta */
   SCIP_Real*            tmpcutcoefs,        /**< temporary working array for testing the given delta */
   int*                  tmpcutinds,         /**< temporary working array for testing the given delta */
   SCIP_Real             delta,              /**< delta to try */
   SCIP_Bool*            success             /**< pointer to store whether a cut was stored in the given arrays */
   )
{
   int k;
   int tmpcutnnz;
   SCIP_Real cutefficacy;
   SCIP_Real scale;
   SCIP_Real tmpcutrhs;
   SCIP_Real downrhs;
   SCIP_Real f0;

   /* setup tmpcut with scaled cut */
   scale = 1.0 / delta;
   tmpcutrhs = mksetrhs * scale;

   downrhs = EPSFLOOR(tmpcutrhs, SCIPsumepsilon(scip));
   f0 = tmpcutrhs - downrhs;
   if( f0 < minfrac || f0 > maxfrac )
      return SCIP_OKAY;

   /* We multiply the coefficients of the base inequality roughly by scale/(1-f0).
    * If this gives a scalar that is very big, we better do not generate this cut.
    */
   if( REALABS(scale)/(1.0 - f0) > MAXCMIRSCALE )
      return SCIP_OKAY;

   for( k = 0; k < mksetnnz; ++k )
      tmpcutcoefs[k] = mksetcoefs[k] * scale;
   tmpcutnnz = mksetnnz;
   tmpcutrhs = downrhs;
   BMScopyMemoryArray(tmpcutinds, mksetinds, mksetnnz);
   BMScopyMemoryArray(tmpboundtype, boundtype, mksetnnz);
   BMScopyMemoryArray(tmpvarsign, varsign, mksetnnz);

   SCIP_CALL( cutsRoundMIR(scip, tmpcutcoefs, &tmpcutrhs, tmpcutinds, &tmpcutnnz, tmpvarsign, tmpboundtype, f0) );
   SCIPdebug(printCut(scip, sol, tmpcutcoefs, tmpcutrhs, tmpcutinds, tmpcutnnz, FALSE, FALSE));

   /* substitute aggregated slack variables:
      *
      * The coefficient of the slack variable s_r is equal to the row's weight times the slack's sign, because the slack
      * variable only appears in its own row:
      *    a'_r = scale * weight[r] * slacksign[r].
      *
      * Depending on the slacks type (integral or continuous), its coefficient in the cut calculates as follows:
      *   integers :  a^_r = a~_r = down(a'_r)                      , if f_r <= f0
      *               a^_r = a~_r = down(a'_r) + (f_r - f0)/(1 - f0), if f_r >  f0
      *   continuous: a^_r = a~_r = 0                               , if a'_r >= 0
      *               a^_r = a~_r = a'_r/(1 - f0)                   , if a'_r <  0
      *
      * Substitute a^_r * s_r by adding a^_r times the slack's definition to the cut.
      */
   SCIP_CALL( cutsSubstituteMIR(scip, aggrrow->rowweights, aggrrow->slacksign, aggrrow->rowsinds,
                                aggrrow->nrows, scale, tmpcutcoefs, &tmpcutrhs, tmpcutinds, &tmpcutnnz, f0) );
   SCIPdebug(printCut(scip, sol, tmpcutcoefs, tmpcutrhs, tmpcutinds, tmpcutnnz, FALSE, FALSE));

   /* remove again all nearly-zero coefficients from MIR row and relax the right hand side correspondingly in order to
      * prevent numerical rounding errors
      */
   cleanupCut(scip, mksetislocal, tmpcutinds, tmpcutcoefs, &tmpcutnnz, &tmpcutrhs);
   SCIPdebug(printCut(scip, sol, tmpcutcoefs, tmpcutrhs, tmpcutinds, tmpcutnnz, FALSE, FALSE));

   cutefficacy = calcEfficacy(scip, sol, tmpcutcoefs, tmpcutrhs, tmpcutinds, tmpcutnnz);

   if( cutefficacy > *bestcutefficacy )
   {
      *bestcutefficacy = cutefficacy;
      *bestcutdelta = delta;

      /* only copy cut if it is efficacious */
      if( cutefficacy > minefficacy )
      {
         BMScopyMemoryArray(bestcutinds, tmpcutinds, tmpcutnnz);
         BMScopyMemoryArray(bestcutcoefs, tmpcutcoefs, tmpcutnnz);
         *bestcutnnz = tmpcutnnz;
         *bestcutrhs = tmpcutrhs;
         *success = TRUE;
      }
   }

   return SCIP_OKAY;
}

/** calculates an MIR cut out of the weighted sum of LP rows; The weights of modifiable rows are set to 0.0, because
 *  these rows cannot participate in an MIR cut.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if @p scip is in one of the following stages:
 *       - \ref SCIP_STAGE_SOLVING
 *
 *  See \ref SCIP_Stage "SCIP_STAGE" for a complete list of all possible solving stages.
 */
SCIP_RETCODE SCIPcutGenerationHeuristicCMIR(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Real             boundswitch,        /**< fraction of domain up to which lower bound is used in transformation */
   SCIP_Bool             usevbds,            /**< should variable bounds be used in bound transformation? */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   int*                  boundsfortrans,     /**< bounds that should be used for transformed variables: vlb_idx/vub_idx,
                                              *   -1 for global lb/ub, -2 for local lb/ub, or -3 for using closest bound;
                                              *   NULL for using closest bound for all variables */
   SCIP_BOUNDTYPE*       boundtypesfortrans, /**< type of bounds that should be used for transformed variables;
                                              *   NULL for using closest bound for all variables */
   SCIP_Real             minfrac,            /**< minimal fractionality of rhs to produce MIR cut for */
   SCIP_Real             maxfrac,            /**< maximal fractionality of rhs to produce MIR cut for */
   SCIP_AGGRROW*         aggrrow,            /**< aggrrow to compute MIR cut for */
   SCIP_Real*            cutcoefs,           /**< array to store the non-zero coefficients in the cut */
   SCIP_Real*            cutrhs,             /**< pointer to store the right hand side of the cut */
   int*                  cutinds,            /**< array to store the problem indices of variables with a non-zero coefficient in the cut */
   int*                  cutnnz,             /**< pointer to store the number of non-zeros in the cut */
   SCIP_Real*            cutefficacy,        /**< pointer to store efficacy of best cut; only cuts that are strictly better than the value of
                                              *   this efficacy on input to this function are returned */
   int*                  cutrank,            /**< pointer to return rank of generated cut */
   SCIP_Bool*            cutislocal,         /**< pointer to store whether the generated cut is only valid locally */
   SCIP_Bool*            success             /**< pointer to store whether a valid and efficacious cut was returned */
   )
{
   int i;
   int firstcontvar;
   int nvars;
   int* varsign;
   int* boundtype;
   int* mksetinds;
   SCIP_Real* mksetcoefs;
   SCIP_Real mksetrhs;
   int mksetnnz;
   SCIP_Real* bounddist;
   int* bounddistpos;
   int nbounddist;
   int* tmpvarsign;
   int* tmpboundtype;
   int* tmpcutinds;
   SCIP_Real* tmpcutcoefs;
   SCIP_Real* deltacands;
   int ndeltacands;
   SCIP_Real bestdelta;
   SCIP_Real maxabsmksetcoef;
   SCIP_VAR** vars;
   SCIP_Bool freevariable;
   SCIP_Bool localbdsused;
   SCIP_Real bestmirefficacy;

   assert(aggrrow != NULL);
   assert(aggrrow->nrows >= 1);
   assert(success != NULL);

   *success = FALSE;
   nvars = SCIPgetNVars(scip);
   firstcontvar = nvars - SCIPgetNContVars(scip);
   vars = SCIPgetVars(scip);

   /* allocate temporary memory */

   SCIP_CALL( SCIPallocBufferArray(scip, &varsign, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &boundtype, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &tmpvarsign, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &tmpboundtype, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &mksetcoefs, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &mksetinds, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &tmpcutcoefs, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &tmpcutinds, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &deltacands, nvars) );
   /* each variable is either integral or a variable bound with an integral variable is used
    * so the max number of integral variables that are strictly between it's bounds is
    * aggrrow->nnz
    */
   SCIP_CALL( SCIPallocBufferArray(scip, &bounddist, aggrrow->nnz) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bounddistpos, aggrrow->nnz) );

   /* initialize mkset with aggregation */
   mksetnnz = aggrrow->nnz;
   mksetrhs = aggrrow->rhs;

   BMScopyMemoryArray(mksetinds, aggrrow->inds, mksetnnz);
   BMScopyMemoryArray(mksetcoefs, aggrrow->vals, mksetnnz);

   *cutislocal = aggrrow->local;
   /* Transform equation  a*x == b, lb <= x <= ub  into standard form
    *   a'*x' == b, 0 <= x' <= ub'.
    *
    * Transform variables (lb or ub):
    *   x'_j := x_j - lb_j,   x_j == x'_j + lb_j,   a'_j ==  a_j,   if lb is used in transformation
    *   x'_j := ub_j - x_j,   x_j == ub_j - x'_j,   a'_j == -a_j,   if ub is used in transformation
    * and move the constant terms "a_j * lb_j" or "a_j * ub_j" to the rhs.
    *
    * Transform variables (vlb or vub):
    *   x'_j := x_j - (bl_j * zl_j + dl_j),   x_j == x'_j + (bl_j * zl_j + dl_j),   a'_j ==  a_j,   if vlb is used in transf.
    *   x'_j := (bu_j * zu_j + du_j) - x_j,   x_j == (bu_j * zu_j + du_j) - x'_j,   a'_j == -a_j,   if vub is used in transf.
    * move the constant terms "a_j * dl_j" or "a_j * du_j" to the rhs, and update the coefficient of the VLB variable:
    *   a_{zl_j} := a_{zl_j} + a_j * bl_j, or
    *   a_{zu_j} := a_{zu_j} + a_j * bu_j
    */

   /* will be set to TRUE in tryDelta, if an efficacious cut that is better than the given efficacy was found */
   *success = FALSE;
   cleanupCut(scip, *cutislocal, mksetinds, mksetcoefs, &mksetnnz, &mksetrhs);

   SCIP_CALL( cutsTransformMIR(scip, sol, boundswitch, usevbds, allowlocal, FALSE, FALSE,
         boundsfortrans, boundtypesfortrans, minfrac, maxfrac, mksetcoefs, &mksetrhs, mksetinds, &mksetnnz, varsign, boundtype, &freevariable, &localbdsused) );

   assert(allowlocal || !localbdsused);

   if( freevariable )
      goto TERMINATE;
   SCIPdebug(printCut(scip, sol, mksetcoefs, mksetrhs, mksetinds, mksetnnz, FALSE, FALSE));

   /* found positions of integral variables that are strictly between their bounds */
   maxabsmksetcoef = -1.0;
   nbounddist = 0;
   ndeltacands = 0;

   for( i = mksetnnz - 1; i >= 0 && mksetinds[i] < firstcontvar; --i )
   {
      int k;
      SCIP_Bool newdelta;
      SCIP_VAR* var = vars[mksetinds[i]];
      SCIP_Real primsol = SCIPgetSolVal(scip, sol, var);
      SCIP_Real lb = SCIPvarGetLbLocal(var);
      SCIP_Real ub = SCIPvarGetUbLocal(var);
      SCIP_Real absmksetcoef = REALABS(mksetcoefs[i]);

      maxabsmksetcoef = MAX(absmksetcoef, maxabsmksetcoef);

      if( SCIPisEQ(scip, primsol, lb) || SCIPisEQ(scip, primsol, ub) )
         continue;

      bounddist[nbounddist] = MIN(ub - primsol, primsol - lb);
      bounddistpos[nbounddist] = i;
      ++nbounddist;

      newdelta = TRUE;
      for( k = 0; k < ndeltacands; ++k )
      {
         if( SCIPisEQ(scip, deltacands[k], absmksetcoef) )
         {
            newdelta = FALSE;
            break;
         }
      }
      if( newdelta )
         deltacands[ndeltacands++] = absmksetcoef;
   }

   if( maxabsmksetcoef != -1.0 )
   {
      int k;
      SCIP_Bool newdelta;
      SCIP_Real deltacand;

      deltacand = maxabsmksetcoef + 1.0;

      newdelta = TRUE;
      for( k = 0; k < ndeltacands; ++k )
      {
         if( SCIPisEQ(scip, deltacands[k], deltacand) )
         {
            newdelta = FALSE;
            break;
         }
      }
      if( newdelta )
         deltacands[ndeltacands++] = deltacand;
   }

   /* at least try without scaling if current delta set is empty */
   if( ndeltacands == 0 )
      deltacands[ndeltacands++] = 1.0;

   /* For each delta
    * Calculate fractionalities  f_0 := b - down(b), f_j := a'_j - down(a'_j) , and derive MIR cut
    *   a~*x' <= down(b)
    * integers :  a~_j = down(a'_j)                      , if f_j <= f_0
    *             a~_j = down(a'_j) + (f_j - f0)/(1 - f0), if f_j >  f_0
    * continuous: a~_j = 0                               , if a'_j >= 0
    *             a~_j = a'_j/(1 - f0)                   , if a'_j <  0
    *
    * Transform inequality back to a^*x <= rhs:
    *
    * (lb or ub):
    *   x'_j := x_j - lb_j,   x_j == x'_j + lb_j,   a'_j ==  a_j,   a^_j :=  a~_j,   if lb was used in transformation
    *   x'_j := ub_j - x_j,   x_j == ub_j - x'_j,   a'_j == -a_j,   a^_j := -a~_j,   if ub was used in transformation
    * and move the constant terms
    *   -a~_j * lb_j == -a^_j * lb_j, or
    *    a~_j * ub_j == -a^_j * ub_j
    * to the rhs.
    *
    * (vlb or vub):
    *   x'_j := x_j - (bl_j * zl_j + dl_j),   x_j == x'_j + (bl_j * zl_j + dl_j),   a'_j ==  a_j,   a^_j :=  a~_j,   (vlb)
    *   x'_j := (bu_j * zu_j + du_j) - x_j,   x_j == (bu_j * zu_j + du_j) - x'_j,   a'_j == -a_j,   a^_j := -a~_j,   (vub)
    * move the constant terms
    *   -a~_j * dl_j == -a^_j * dl_j, or
    *    a~_j * du_j == -a^_j * du_j
    * to the rhs, and update the VB variable coefficients:
    *   a^_{zl_j} := a^_{zl_j} - a~_j * bl_j == a^_{zl_j} - a^_j * bl_j, or
    *   a^_{zu_j} := a^_{zu_j} + a~_j * bu_j == a^_{zu_j} - a^_j * bu_j
    */

   bestdelta = SCIP_INVALID;
   bestmirefficacy = -SCIPinfinity(scip);

   /* try all candidates for delta  */
   for( i = 0; i < ndeltacands; ++i )
   {
      SCIP_CALL( tryDelta(scip, sol, aggrrow, minfrac, maxfrac, *cutislocal, mksetcoefs, mksetrhs, mksetinds, mksetnnz,
         boundtype, varsign, cutcoefs, cutrhs, cutinds, cutnnz, &bestmirefficacy, &bestdelta, *cutefficacy,
         tmpboundtype, tmpvarsign, tmpcutcoefs, tmpcutinds, deltacands[i], success) );
   }

   /* no delta was found that yielded any cut */
   if( bestdelta == SCIP_INVALID )
      goto TERMINATE;

   /* try bestdelta divided by 2, 4 and 8 */
   for( i = 2; i <= 8 ; i *= 2 )
   {
      SCIP_CALL( tryDelta(scip, sol, aggrrow, minfrac, maxfrac, *cutislocal, mksetcoefs, mksetrhs, mksetinds, mksetnnz,
         boundtype, varsign, cutcoefs, cutrhs, cutinds, cutnnz, &bestmirefficacy, &bestdelta, *cutefficacy,
         tmpboundtype, tmpvarsign, tmpcutcoefs, tmpcutinds, bestdelta / i, success) );
   }

   /* try to improve efficacy by switching complementation of integral variables that are not at their bounds
    * in order of non-increasing bound distance
    */
   SCIPsortDownRealInt(bounddist, bounddistpos, nbounddist);
   for( i = 0; i < nbounddist; ++i )
   {
      int k;
      SCIP_Real oldbestefficacy;
      SCIP_Real oldmksetrhs;
      int oldboundtype;
      int oldvarsign;
      SCIP_Real bestlb;
      SCIP_Real bestub;
      int bestlbtype;
      int bestubtype;
      SCIP_Bool oldlocalbdsused;

      k = bounddistpos[i];

      findBestLb(scip, vars[mksetinds[k]], sol, FALSE, allowlocal, &bestlb, &bestlbtype);
      findBestUb(scip, vars[mksetinds[k]], sol, FALSE, allowlocal, &bestub, &bestubtype);

      /* store information to restore the changed complementation */
      oldvarsign = varsign[k];
      oldmksetrhs = mksetrhs;
      oldboundtype = boundtype[k];
      oldlocalbdsused = localbdsused;

      /* since we only look at the integral vars, no variable bounds should have been used */
      assert(oldboundtype < 0);

      /* switch the complementation of this variable */
      mksetrhs += oldvarsign * mksetcoefs[k] * (bestlb - bestub);
      if( varsign[k] == +1 )
      {
         /* switch to upper bound */
         assert(bestubtype < 0); /* cannot switch to a variable bound (would lead to further coef updates) */
         boundtype[k] = bestubtype;
         varsign[k] = -1;
      }
      else
      {
         /* switch to lower bound */
         assert(bestlbtype < 0); /* cannot switch to a variable bound (would lead to further coef updates) */
         boundtype[k] = bestlbtype;
         varsign[k] = +1;
      }
      localbdsused = localbdsused || (boundtype[k] == -2);

      oldbestefficacy = bestmirefficacy;

      SCIP_CALL( tryDelta(scip, sol, aggrrow, minfrac, maxfrac, *cutislocal, mksetcoefs, mksetrhs, mksetinds, mksetnnz,
         boundtype, varsign, cutcoefs, cutrhs, cutinds, cutnnz, &bestmirefficacy, &bestdelta, *cutefficacy,
         tmpboundtype, tmpvarsign, tmpcutcoefs, tmpcutinds, bestdelta, success) );

      /* undo the change in complementation if efficacy was not increased */
      if( oldbestefficacy == bestmirefficacy )
      {
         boundtype[k] = oldboundtype;
         varsign[k] = oldvarsign;
         mksetrhs = oldmksetrhs;
         localbdsused = oldlocalbdsused;
      }
   }

   if( *success )
   {
      *cutefficacy = bestmirefficacy;
      *cutislocal = *cutislocal || localbdsused;

      if( cutrank != NULL )
         *cutrank = aggrrow->rank + 1;
   }

 TERMINATE:
   /* free temporary memory */
   SCIPfreeBufferArray(scip, &bounddistpos);
   SCIPfreeBufferArray(scip, &bounddist);
   SCIPfreeBufferArray(scip, &deltacands);
   SCIPfreeBufferArray(scip, &tmpcutinds);
   SCIPfreeBufferArray(scip, &tmpcutcoefs);
   SCIPfreeBufferArray(scip, &mksetinds);
   SCIPfreeBufferArray(scip, &mksetcoefs);
   SCIPfreeBufferArray(scip, &tmpboundtype);
   SCIPfreeBufferArray(scip, &tmpvarsign);
   SCIPfreeBufferArray(scip, &boundtype);
   SCIPfreeBufferArray(scip, &varsign);

   /**@todo pass the sparsity pattern to the calling method in order to speed up the calling method's loops */

   return SCIP_OKAY;
}

/* =========================================== flow cover =========================================== */

#define MAXDNOM                  1000LL
#define MINDELTA                  1e-03
#define MAXDELTA                  1e-09
#define MAXSCALE                 1000.0
#define MAXDYNPROGSPACE         1000000

#define MAXABSVBCOEF               1e+5 /**< maximal absolute coefficient in variable bounds used for snf relaxation */
#define MAXBOUND                  1e+10 /**< maximal value of normal bounds used for snf relaxation */

typedef
struct LiftingData
{
   SCIP_Real*            M;
   SCIP_Real*            m;
   int                   r;
   int                   t;
   SCIP_Real             d1;
   SCIP_Real             d2;
   SCIP_Real             lambda;
   SCIP_Real             mp;
   SCIP_Real             ml;
} LIFTINGDATA;

typedef
struct SNF_Relaxation
{
   int*                  transvarcoefs;      /**< coefficients of all vars in relaxed set */
   SCIP_Real*            transbinvarsolvals; /**< sol val of bin var in vub of all vars in relaxed set */
   SCIP_Real*            transcontvarsolvals;/**< sol val of all real vars in relaxed set */
   SCIP_Real*            transvarvubcoefs;   /**< coefficient in vub of all vars in relaxed set */
   int                   ntransvars;         /**< number of vars in relaxed set */
   SCIP_Real             transrhs;           /**< rhs in relaxed set */
   int*                  origbinvars;        /**< associated original binary var for all vars in relaxed set */
   int*                  origcontvars;       /**< associated original continuous var for all vars in relaxed set */
   SCIP_Real*            aggrcoefsbin;       /**< aggregation coefficient of the original binary var used to define the
                                              *   continuous variable in the relaxed set */
   SCIP_Real*            aggrcoefscont;      /**< aggregation coefficient of the original continous var used to define the
                                              *   continuous variable in the relaxed set */
   SCIP_Real*            aggrconstants;      /**< aggregation constant used to define the continuous variable in the relaxed set */
} SNF_RELAXATION;

/** get LP solution value and index of variable lower bound (with binary variable) which is closest to the current LP
 *  solution value of a given variable; candidates have to meet certain criteria in order to ensure the nonnegativity
 *  of the variable upper bound imposed on the real variable in the 0-1 single node flow relaxation associated with the
 *  given variable
 */
static
SCIP_RETCODE getClosestVlb(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             var,                /**< given active problem variable */
   SCIP_SOL*             sol,
   SCIP_Real*            rowcoefs,           /**< coefficients of row */
   int*                  binvarpos,
   SCIP_Real             bestsub,            /**< closest simple upper bound of given variable */
   SCIP_Real             rowcoef,            /**< coefficient of given variable in current row */
   SCIP_Real*            closestvlb,         /**< pointer to store the LP sol value of the closest variable lower bound */
   int*                  closestvlbidx       /**< pointer to store the index of the closest vlb; -1 if no vlb was found */
   )
{
   int nvlbs;

   assert(scip != NULL);
   assert(var != NULL);
   assert(bestsub == SCIPvarGetUbGlobal(var) || bestsub == SCIPvarGetUbLocal(var)); /*lint !e777*/
   assert(!SCIPisInfinity(scip, bestsub));
   assert(!SCIPisZero(scip, rowcoef));
   assert(rowcoefs != NULL);
   assert(binvarpos != NULL);
   assert(closestvlb != NULL);
   assert(closestvlbidx != NULL);

   nvlbs = SCIPvarGetNVlbs(var);

   *closestvlbidx = -1;
   *closestvlb = -SCIPinfinity(scip);
   if( nvlbs > 0 )
   {
      SCIP_VAR** vlbvars;
      SCIP_Real* vlbcoefs;
      SCIP_Real* vlbconsts;
      int i;

      vlbvars = SCIPvarGetVlbVars(var);
      vlbcoefs = SCIPvarGetVlbCoefs(var);
      vlbconsts = SCIPvarGetVlbConstants(var);

      for( i = 0; i < nvlbs; i++ )
      {
         SCIP_Real rowcoefbinvar;
         SCIP_Real val1;
         SCIP_Real val2;
         SCIP_Real vlbsol;
         SCIP_Bool meetscriteria;
         int probidxbinvar;
         int aggrrowidxbinvar;

         /* use only variable lower bounds l~_i * x_i + d_i with x_i binary which are active */
         if( !SCIPvarIsBinary(vlbvars[i])  || !SCIPvarIsActive(vlbvars[i]) )
            continue;

         /* check if current variable lower bound l~_i * x_i + d_i imposed on y_j meets the following criteria:
          * (let a_j  = coefficient of y_j in current row,
          *      u_j  = closest simple upper bound imposed on y_j,
          *      c_i  = coefficient of x_i in current row)
          *   0. no other non-binary variable y_k has used a variable bound with x_i to get transformed variable y'_k yet
          * if a_j > 0:
          *   1. u_j <= d_i
          *   2. a_j ( u_j - d_i ) + c_i <= 0
          *   3. a_j l~_i + c_i <= 0
          * if a_j < 0:
          *   1. u_j <= d_i
          *   2. a_j ( u_j - d_i ) + c_i >= 0
          *   3. a_j l~_i + c_i >= 0
          */
         probidxbinvar = SCIPvarGetProbindex(vlbvars[i]);
         aggrrowidxbinvar = binvarpos[probidxbinvar];

         /* has already been used in the SNF relaxation */
         if( aggrrowidxbinvar < 0 )
            continue;

         /* get the row coefficient */
         rowcoefbinvar = aggrrowidxbinvar == 0 ? 0.0 : rowcoefs[aggrrowidxbinvar - 1];

         val1 = ( rowcoef * ( bestsub - vlbconsts[i] ) ) + rowcoefbinvar;
         val2 = ( rowcoef * vlbcoefs[i] ) + rowcoefbinvar;

         meetscriteria = FALSE;
         if( SCIPisPositive(scip, rowcoef) )
         {
            if( SCIPisFeasLE(scip, bestsub, vlbconsts[i])
               && SCIPisFeasLE(scip, val1, 0.0) && SCIPisFeasLE(scip, val2, 0.0) )
               meetscriteria = TRUE;
         }
         else
         {
            assert(SCIPisNegative(scip, rowcoef));
            if( SCIPisFeasLE(scip, bestsub, vlbconsts[i])
               && SCIPisFeasGE(scip, val1, 0.0) && SCIPisFeasGE(scip, val2, 0.0) )
               meetscriteria = TRUE;
         }

         /* variable lower bound does not meet criteria */
         if( !meetscriteria )
            continue;

         /* for numerical reasons, ignore variable bounds with large absolute coefficient and
          * those which lead to an infinite variable bound coefficient (val2) in snf relaxation
          */
         if( REALABS(vlbcoefs[i]) > MAXABSVBCOEF || SCIPisInfinity(scip, REALABS(val2)) )
            continue;

         vlbsol = vlbcoefs[i] * SCIPgetSolVal(scip, sol, vlbvars[i]) + vlbconsts[i];
         if( SCIPisGT(scip, vlbsol, *closestvlb) )
         {
            *closestvlb = vlbsol;
            *closestvlbidx = i;
         }
         assert(*closestvlbidx >= 0);

      }
   }

   return SCIP_OKAY;
}

/** get LP solution value and index of variable upper bound (with binary variable) which is closest to the current LP
 *  solution value of a given variable; candidates have to meet certain criteria in order to ensure the nonnegativity
 *  of the variable upper bound imposed on the real variable in the 0-1 single node flow relaxation associated with the
 *  given variable
 */
static
SCIP_RETCODE getClosestVub(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             var,                /**< given active problem variable */
   SCIP_SOL*             sol,
   SCIP_Real*            rowcoefs,           /**< aggrrow to transform */
   int*                  binvarpos,
   SCIP_Real             bestslb,            /**< closest simple lower bound of given variable */
   SCIP_Real             rowcoef,            /**< coefficient of given variable in current row */
   SCIP_Real*            closestvub,         /**< pointer to store the LP sol value of the closest variable upper bound */
   int*                  closestvubidx       /**< pointer to store the index of the closest vub; -1 if no vub was found */
   )
{
   int nvubs;

   assert(scip != NULL);
   assert(var != NULL);
   assert(bestslb == SCIPvarGetLbGlobal(var) || bestslb == SCIPvarGetLbLocal(var)); /*lint !e777*/
   assert(!SCIPisInfinity(scip, - bestslb));
   assert(!SCIPisZero(scip, rowcoef));
   assert(rowcoefs != NULL);
   assert(binvarpos != NULL);
   assert(closestvub != NULL);
   assert(closestvubidx != NULL);

   nvubs = SCIPvarGetNVubs(var);

   *closestvubidx = -1;
   *closestvub = SCIPinfinity(scip);
   if( nvubs > 0 )
   {
      SCIP_VAR** vubvars;
      SCIP_Real* vubcoefs;
      SCIP_Real* vubconsts;
      int i;

      vubvars = SCIPvarGetVubVars(var);
      vubcoefs = SCIPvarGetVubCoefs(var);
      vubconsts = SCIPvarGetVubConstants(var);

      for( i = 0; i < nvubs; i++ )
      {
         SCIP_Real rowcoefbinvar;
         SCIP_Real val1;
         SCIP_Real val2;
         SCIP_Real vubsol;
         SCIP_Bool meetscriteria;
         int probidxbinvar;
         int aggrrowidxbinvar;

         /* use only variable upper bound u~_i * x_i + d_i with x_i binary and which are active */
         if( !SCIPvarIsBinary(vubvars[i]) || !SCIPvarIsActive(vubvars[i]))
            continue;

         /* checks if current variable upper bound u~_i * x_i + d_i meets the following criteria
          * (let a_j  = coefficient of y_j in current row,
          *      l_j  = closest simple lower bound imposed on y_j,
          *      c_i  = coefficient of x_i in current row)
          *   0. no other non-binary variable y_k has used a variable bound with x_i to get transformed variable y'_k
          * if a > 0:
          *   1. l_j >= d_i
          *   2. a_j ( l_i - d_i ) + c_i >= 0
          *   3. a_j u~_i + c_i >= 0
          * if a < 0:
          *   1. l_j >= d_i
          *   2. a_j ( l_j - d_i ) + c_i <= 0
          *   3. a_j u~_i + c_i <= 0
          */
         probidxbinvar = SCIPvarGetProbindex(vubvars[i]);
         aggrrowidxbinvar = binvarpos[probidxbinvar];

         /* has already been used in the SNF relaxation */
         if( aggrrowidxbinvar < 0 )
            continue;

         /* get the row coefficient */
         rowcoefbinvar = aggrrowidxbinvar == 0 ? 0.0 : rowcoefs[aggrrowidxbinvar - 1];

         val1 = ( rowcoef * ( bestslb - vubconsts[i] ) ) + rowcoefbinvar;
         val2 = ( rowcoef * vubcoefs[i] ) + rowcoefbinvar;

         meetscriteria = FALSE;
         if( SCIPisPositive(scip, rowcoef) )
         {
            if( SCIPisFeasGE(scip, bestslb, vubconsts[i])
               && SCIPisFeasGE(scip, val1, 0.0) && SCIPisFeasGE(scip, val2, 0.0) )
               meetscriteria = TRUE;
         }
         else
         {
            assert(SCIPisNegative(scip, rowcoef));
            if( SCIPisFeasGE(scip, bestslb, vubconsts[i])
               && SCIPisFeasLE(scip, val1, 0.0) && SCIPisFeasLE(scip, val2, 0.0) )
               meetscriteria = TRUE;
         }

         /* variable upper bound does not meet criteria */
         if( !meetscriteria )
            continue;

         /* for numerical reasons, ignore variable bounds with large absolute coefficient and
          * those which lead to an infinite variable bound coefficient (val2) in snf relaxation
          */
         if( REALABS(vubcoefs[i]) > MAXABSVBCOEF || SCIPisInfinity(scip, REALABS(val2)) )
            continue;

         vubsol = vubcoefs[i] * SCIPgetSolVal(scip, sol, vubvars[i]) + vubconsts[i];
         if( SCIPisLT(scip, vubsol, *closestvub) )
         {
            *closestvub = vubsol;
            *closestvubidx = i;
         }
         assert(*closestvubidx >= 0);
      }
   }

   return SCIP_OKAY;
}

static
SCIP_RETCODE determineBoundForSNF(
   SCIP*                 scip,
   SCIP_SOL*             sol,
   SCIP_VAR**            vars,
   SCIP_Real*            rowcoefs,           /**< aggrrow to transform */
   int*                  rowinds,            /**< aggrrow to transform */
   int                   varposinrow,
   int*                  binvarpos,
   SCIP_Bool             allowlocal,
   SCIP_Real             boundswitch,
   SCIP_Real*            bestlb,
   SCIP_Real*            bestub,
   SCIP_Real*            bestslb,
   SCIP_Real*            bestsub,
   int*                  bestlbtype,
   int*                  bestubtype,
   int*                  bestslbtype,
   int*                  bestsubtype,
   SCIP_BOUNDTYPE*       selectedbounds,
   SCIP_Bool*            freevariable
   )
{
   SCIP_VAR* var;

   SCIP_Real rowcoef;
   SCIP_Real solval;

   int probidx;

   bestlb[varposinrow] = -SCIPinfinity(scip);
   bestub[varposinrow] = SCIPinfinity(scip);
   bestlbtype[varposinrow] = -3;
   bestubtype[varposinrow] = -3;

   probidx = rowinds[varposinrow];
   var = vars[probidx];
   rowcoef = rowcoefs[varposinrow];

   assert(!SCIPisZero(scip, rowcoef));

   /* get closest simple lower bound and closest simple upper bound */
   SCIP_CALL( findBestLb(scip, var, sol, FALSE, allowlocal, &bestslb[varposinrow], &bestslbtype[varposinrow]) );
   SCIP_CALL( findBestUb(scip, var, sol, FALSE, allowlocal, &bestsub[varposinrow], &bestsubtype[varposinrow]) );

   solval = SCIPgetSolVal(scip, sol, var);

   SCIPdebugMsg(scip, "  %d: %g <%s, idx=%d, lp=%g, [%g(%d),%g(%d)]>:\n", varposinrow, rowcoef, SCIPvarGetName(var), probidx,
      solval, bestslb, bestslbtype, bestsub, bestsubtype);

   /* mixed integer set cannot be relaxed to 0-1 single node flow set because both simple bounds are -infinity
      * and infinity, respectively
      */
   if( SCIPisInfinity(scip, -bestslb[varposinrow]) && SCIPisInfinity(scip, bestsub[varposinrow]) )
   {
      *freevariable = TRUE;
      return SCIP_OKAY;
   }

   /* get closest lower bound that can be used to define the real variable y'_j in the 0-1 single node flow
      * relaxation
      */
   if( !SCIPisInfinity(scip, bestsub[varposinrow]) )
   {
      bestlb[varposinrow] = bestslb[varposinrow];
      bestlbtype[varposinrow] = bestslbtype[varposinrow];

      if( SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS )
      {
         SCIP_Real bestvlb;
         int bestvlbidx;

         SCIP_CALL( getClosestVlb(scip, var, sol, rowcoefs, binvarpos, bestsub[varposinrow], rowcoef, &bestvlb, &bestvlbidx) );
         if( SCIPisGT(scip, bestvlb, bestlb[varposinrow]) )
         {
            bestlb[varposinrow] = bestvlb;
            bestlbtype[varposinrow] = bestvlbidx;
         }
      }
   }
   /* get closest upper bound that can be used to define the real variable y'_j in the 0-1 single node flow
      * relaxation
      */
   if( !SCIPisInfinity(scip, -bestslb[varposinrow]) )
   {
      bestub[varposinrow] = bestsub[varposinrow];
      bestubtype[varposinrow] = bestsubtype[varposinrow];

      if( SCIPvarGetType(var) == SCIP_VARTYPE_CONTINUOUS )
      {
         SCIP_Real bestvub;
         int bestvubidx;

         SCIP_CALL( getClosestVub(scip, var, sol, rowcoefs, binvarpos, bestslb[varposinrow], rowcoef, &bestvub, &bestvubidx) );
         if( SCIPisLT(scip, bestvub, bestub[varposinrow]) )
         {
            bestub[varposinrow] = bestvub;
            bestubtype[varposinrow] = bestvubidx;
         }
      }
   }
   SCIPdebugMsg(scip, "        bestlb=%g(%d), bestub=%g(%d)\n", bestlb[varposinrow], bestlbtype[varposinrow], bestub[varposinrow], bestubtype[varposinrow]);

   /* mixed integer set cannot be relaxed to 0-1 single node flow set because there are no suitable bounds
      * to define the transformed variable y'_j
      */
   if( SCIPisInfinity(scip, -bestlb[varposinrow]) && SCIPisInfinity(scip, bestub[varposinrow]) )
   {
      *freevariable = TRUE;
      return SCIP_OKAY;
   }

   *freevariable = FALSE;

   /* select best upper bound if it is closer to the LP value of y_j and best lower bound otherwise and use this bound
   * to define the real variable y'_j with 0 <= y'_j <= u'_j x_j in the 0-1 single node flow relaxation;
   * prefer variable bounds
   */
   if( SCIPisEQ(scip, solval, (1.0 - boundswitch) * bestlb[varposinrow] + boundswitch * bestub[varposinrow]) && bestlbtype[varposinrow] >= 0 )
   {
      selectedbounds[varposinrow] = SCIP_BOUNDTYPE_LOWER;
   }
   else if( SCIPisEQ(scip, solval, (1.0 - boundswitch) * bestlb[varposinrow] + boundswitch * bestub[varposinrow])
      && bestubtype[varposinrow] >= 0 )
   {
      selectedbounds[varposinrow] = SCIP_BOUNDTYPE_UPPER;
   }
   else if( SCIPisLE(scip, solval, (1.0 - boundswitch) * bestlb[varposinrow] + boundswitch * bestub[varposinrow]) )
   {
      selectedbounds[varposinrow] = SCIP_BOUNDTYPE_LOWER;
   }
   else
   {
      assert(SCIPisGT(scip, solval, (1.0 - boundswitch) * bestlb[varposinrow] + boundswitch * bestub[varposinrow]));
      selectedbounds[varposinrow] = SCIP_BOUNDTYPE_UPPER;
   }

   if( selectedbounds[varposinrow] == SCIP_BOUNDTYPE_LOWER && bestlbtype[varposinrow] >= 0 )
   {
      int vlbvarprobidx;
      SCIP_VAR** vlbvars = SCIPvarGetVlbVars(var);

       /* mark binary variable of vlb so that it is not used for other continuous variables
       * by setting it's position in the aggrrow to a negative value
       */
      vlbvarprobidx = SCIPvarGetProbindex(vlbvars[bestlbtype[varposinrow]]);
      binvarpos[vlbvarprobidx] = binvarpos[vlbvarprobidx] == 0 ? -1 : -binvarpos[vlbvarprobidx];
   }
   else if ( selectedbounds[varposinrow] == SCIP_BOUNDTYPE_UPPER && bestubtype[varposinrow] >= 0 )
   {
      int vubvarprobidx;
      SCIP_VAR** vubvars = SCIPvarGetVubVars(var);

       /* mark binary variable of vub so that it is not used for other continuous variables
       * by setting it's position in the aggrrow to a negative value
       */
      vubvarprobidx = SCIPvarGetProbindex(vubvars[bestubtype[varposinrow]]);
      binvarpos[vubvarprobidx] = binvarpos[vubvarprobidx] == 0 ? -1 : -binvarpos[vubvarprobidx];
   }

   return SCIP_OKAY;
}

/** construct a 0-1 single node flow relaxation (with some additional simple constraints) of a mixed integer set
 *  corresponding to the given aggrrow a * x <= rhs
 */
static
SCIP_RETCODE constructSNFRelaxation(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Real             boundswitch,        /**< fraction of domain up to which lower bound is used in transformation */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   SCIP_Real*            rowcoefs,           /**< array of coefficients of row */
   SCIP_Real             rowrhs,             /**< pointer to right hand side of row */
   int*                  rowinds,            /**< array of variables problem indices for non-zero coefficients in row */
   int                   nnz,                /**< number of non-zeros in row */
   SNF_RELAXATION*       snf,                /**< stores the sign of the transformed variable in summation */
   SCIP_Bool*            success,            /**< stores whether the transformation was valid */
   SCIP_Bool*            localbdsused        /**< pointer to store whether local bounds were used in transformation */
   )
{
   SCIP_VAR** vars;
   int i;
   int nnonbinvarsrow;
   int* binvarpos;
   int nbinvars;
   SCIP_Real DBLDBL(transrhs);

   /* arrays to store the selected bound for each non-binary variable in the row */
   SCIP_Real* bestlb;
   SCIP_Real* bestub;
   SCIP_Real* bestslb;
   SCIP_Real* bestsub;
   int* bestlbtype;
   int* bestubtype;
   int* bestslbtype;
   int* bestsubtype;
   SCIP_BOUNDTYPE* selectedbounds;

   *success = FALSE;

   SCIPdebugMsg(scip, "--------------------- construction of SNF relaxation ------------------------------------\n");


   nbinvars = SCIPgetNBinVars(scip);
   vars = SCIPgetVars(scip);

   SCIP_CALL( SCIPallocBufferArray(scip, &bestlb, nnz) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestub, nnz) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestslb, nnz) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestsub, nnz) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestlbtype, nnz) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestubtype, nnz) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestslbtype, nnz) );
   SCIP_CALL( SCIPallocBufferArray(scip, &bestsubtype, nnz) );
   SCIP_CALL( SCIPallocBufferArray(scip, &selectedbounds, nnz) );

   /* sort descending to have continuous variables first */
   SCIPsortDownIntReal(rowinds, rowcoefs, nnz);

   /* array to store row positions of binary variables and to mark them as used */
   SCIPallocCleanBufferArray(scip, &binvarpos, nbinvars);

   /* store row positions of binary variables */
   for( i = nnz - 1; i >= 0 && rowinds[i] < nbinvars; --i )
      binvarpos[rowinds[i]] = i + 1;

    nnonbinvarsrow = i + 1;

   /* determine the bounds to use for transforming the non-binary variables */
   for( i = 0; i < nnonbinvarsrow; ++i )
   {
      SCIP_Bool freevariable;
      assert(rowinds[i] >= nbinvars);

      determineBoundForSNF(scip, sol, vars, rowcoefs, rowinds, i, binvarpos, allowlocal, boundswitch,
                           bestlb, bestub, bestslb, bestsub, bestlbtype, bestubtype, bestslbtype, bestsubtype, selectedbounds, &freevariable);

      if( freevariable )
      {
         int j;
         /* clear binvarpos at indices of the rows binary variables */
         for( j = nnz - 1; j >= nnonbinvarsrow; --j )
            binvarpos[rowinds[i]] = 0;

         /* clear binvarpos at indices of selected variable bounds */
         for( j = 0; j <= i; ++j )
         {
            if( selectedbounds[j] == SCIP_BOUNDTYPE_LOWER && bestlbtype[j] >= 0 )
            {
               SCIP_VAR** vlbvars = SCIPvarGetVlbVars(vars[rowinds[j]]);
               binvarpos[SCIPvarGetProbindex(vlbvars[bestlbtype[j]])] = 0;
            }
            else if( selectedbounds[j] == SCIP_BOUNDTYPE_UPPER && bestubtype[j] >= 0 )
            {
               SCIP_VAR** vubvars = SCIPvarGetVubVars(vars[rowinds[j]]);
               binvarpos[SCIPvarGetProbindex(vubvars[bestubtype[j]])] = 0;
            }
         }

         /* terminate */
         goto TERMINATE;
      }
   }

   *localbdsused = FALSE;
   DBLDBL_ASSIGN(transrhs, rowrhs);
   snf->ntransvars = 0;

   /* transform non-binary variables */
   for( i = 0; i < nnonbinvarsrow; ++i )
   {
      SCIP_VAR* var;
      SCIP_Real rowcoef;
      SCIP_Real solval;
      int probidx;

      probidx = rowinds[i];
      var = vars[probidx];
      rowcoef = rowcoefs[i];
      solval = SCIPgetSolVal(scip, sol, var);

      if( selectedbounds[i] == SCIP_BOUNDTYPE_LOWER )
      {
         /* use bestlb to define y'_j */

         assert(!SCIPisInfinity(scip, bestsub[i]));
         assert(!SCIPisInfinity(scip, - bestlb[i]));
         assert(bestsubtype[i] == -1 || bestsubtype[i] == -2);
         assert(bestlbtype[i] > -3 && bestlbtype[i] < SCIPvarGetNVlbs(var));

         /* store for y_j that bestlb is the bound used to define y'_j and that y'_j is the associated real variable
          * in the relaxed set
          */
         snf->origcontvars[snf->ntransvars] = probidx;

         if( bestlbtype[i] < 0 )
         {
            SCIP_Real DBLDBL(val);
            SCIP_Real DBLDBL(contsolval);
            SCIP_Real DBLDBL(rowcoeftimesbestsub);

            /* use simple lower bound in bestlb = l_j <= y_j <= u_j = bestsub to define
             *   y'_j = - a_j ( y_j - u_j ) with 0 <= y'_j <=   a_j ( u_j - l_j ) x_j and x_j = 1    if a_j > 0
             *   y'_j =   a_j ( y_j - u_j ) with 0 <= y'_j <= - a_j ( u_j - l_j ) x_j and x_j = 1    if a_j < 0,
             * put j into the set
             *   N2   if a_j > 0
             *   N1   if a_j < 0
             * and update the right hand side of the constraint in the relaxation
             *   rhs = rhs - a_j u_j
             */
            SCIPdbldblSum_DBLDBL(val, bestsub[i], -bestlb[i]);
            SCIPdbldblProd21_DBLDBL(val, val, rowcoef);
            SCIPdbldblSum_DBLDBL(contsolval, solval, -bestsub[i]);
            SCIPdbldblProd21_DBLDBL(contsolval, contsolval, rowcoef);

            if( bestlbtype[i] == -2 || bestsubtype[i] == -2 )
               *localbdsused = TRUE;

            SCIPdbldblProd_DBLDBL(rowcoeftimesbestsub, rowcoef, bestsub[i]);

            /* store aggregation information for y'_j for transforming cuts for the SNF relaxation back to the problem variables later */
            snf->origbinvars[snf->ntransvars] = -1;
            snf->aggrcoefsbin[snf->ntransvars] = 0.0;

            if( SCIPisPositive(scip, rowcoef) )
            {
               snf->transvarcoefs[snf->ntransvars] = - 1;
               snf->transvarvubcoefs[snf->ntransvars] = DBLDBL_ROUND(val);
               snf->transbinvarsolvals[snf->ntransvars] = 1.0;
               snf->transcontvarsolvals[snf->ntransvars] = - DBLDBL_ROUND(contsolval);

               /* aggregation information for y'_j */
               snf->aggrconstants[snf->ntransvars] = DBLDBL_ROUND(rowcoeftimesbestsub);
               snf->aggrcoefscont[snf->ntransvars] = - rowcoef;
            }
            else
            {
               assert(SCIPisNegative(scip, rowcoef));
               snf->transvarcoefs[snf->ntransvars] = 1;
               snf->transvarvubcoefs[snf->ntransvars] = - DBLDBL_ROUND(val);
               snf->transbinvarsolvals[snf->ntransvars] = 1.0;
               snf->transcontvarsolvals[snf->ntransvars] = DBLDBL_ROUND(contsolval);

               /* aggregation information for y'_j */
               snf->aggrconstants[snf->ntransvars] = - DBLDBL_ROUND(rowcoeftimesbestsub);
               snf->aggrcoefscont[snf->ntransvars] = rowcoef;
            }
            SCIPdbldblSum22_DBLDBL(transrhs, transrhs, -rowcoeftimesbestsub);

            SCIPdebugMsg(scip, "    --> bestlb used for trans: ... %s y'_%d + ..., y'_%d <= %g x_%d (=1), rhs=%g-(%g*%g)=%g\n",
                         snf->transvarcoefs[snf->ntransvars] == 1 ? "+" : "-", snf->ntransvars, snf->ntransvars, snf->transvarvubcoefs[snf->ntransvars],
                         snf->ntransvars, DBLDBL_ROUND(transrhs) + DBLDBL_ROUND(rowcoeftimesbestsub), rowcoef, bestsub, DBLDBL_ROUND(transrhs));
         }
         else
         {
            SCIP_Real rowcoefbinary;
            SCIP_Real varsolvalbinary;
            SCIP_Real DBLDBL(val);
            SCIP_Real DBLDBL(contsolval);
            SCIP_Real DBLDBL(rowcoeftimesvlbconst);
            int vlbvarprobidx;

            SCIP_VAR** vlbvars = SCIPvarGetVlbVars(var);
            SCIP_Real* vlbconsts = SCIPvarGetVlbConstants(var);
            SCIP_Real* vlbcoefs = SCIPvarGetVlbCoefs(var);

            /* use variable lower bound in bestlb = l~_j x_j + d_j <= y_j <= u_j = bestsub to define
             *   y'_j = - ( a_j ( y_j - d_j ) + c_j x_j ) with 0 <= y'_j <= - ( a_j l~_j + c_j ) x_j    if a_j > 0
             *   y'_j =     a_j ( y_j - d_j ) + c_j x_j   with 0 <= y'_j <=   ( a_j l~_j + c_j ) x_j    if a_j < 0,
             * where c_j is the coefficient of x_j in the row, put j into the set
             *   N2   if a_j > 0
             *   N1   if a_j < 0
             * and update the right hand side of the constraint in the relaxation
             *   rhs = rhs - a_j d_j
             */
            assert(SCIPvarIsBinary(vlbvars[bestlbtype[i]]));

            vlbvarprobidx = SCIPvarGetProbindex(vlbvars[bestlbtype[i]]);

            /* if the binary variable is not in the row then binvarpos[vlbvarprobidx] will be -1,
             * otherwise binvarpos[vlbvarprobidx] will be -(idx+1) where idx is the binary variables index
             * in the aggrrow
             */
            assert(binvarpos[vlbvarprobidx] < 0);

            rowcoefbinary = binvarpos[vlbvarprobidx] == -1 ? 0.0 : rowcoefs[-binvarpos[vlbvarprobidx] - 1];
            varsolvalbinary = SCIPgetSolVal(scip, sol, vlbvars[bestlbtype[i]]);

            SCIPdbldblProd_DBLDBL(val, rowcoef, vlbcoefs[bestlbtype[i]]);
            SCIPdbldblSum21_DBLDBL(val, val, rowcoefbinary);
            {
               SCIP_Real DBLDBL(tmp);

               SCIPdbldblProd_DBLDBL(tmp, rowcoefbinary, varsolvalbinary);
               SCIPdbldblSum_DBLDBL(contsolval, solval, - vlbconsts[bestlbtype[i]]);
               SCIPdbldblProd21_DBLDBL(contsolval, contsolval, rowcoef);
               SCIPdbldblSum22_DBLDBL(contsolval, contsolval, tmp);
            }

            SCIPdbldblProd_DBLDBL(rowcoeftimesvlbconst, rowcoef, vlbconsts[bestlbtype[i]]);

            /* clear the binvarpos array, since the variable has been processed */
            binvarpos[vlbvarprobidx] = 0;

            /* store aggregation information for y'_j for transforming cuts for the SNF relaxation back to the problem variables later */
            snf->origbinvars[snf->ntransvars] = vlbvarprobidx;

            if( SCIPisPositive(scip, rowcoef) )
            {
               snf->transvarcoefs[snf->ntransvars] = - 1;
               snf->transvarvubcoefs[snf->ntransvars] = - DBLDBL_ROUND(val);
               snf->transbinvarsolvals[snf->ntransvars] = varsolvalbinary;
               snf->transcontvarsolvals[snf->ntransvars] = - DBLDBL_ROUND(contsolval);

               /* aggregation information for y'_j */
               snf->aggrcoefsbin[snf->ntransvars] = - rowcoefbinary;
               snf->aggrcoefscont[snf->ntransvars] = - rowcoef;
               snf->aggrconstants[snf->ntransvars] = DBLDBL_ROUND(rowcoeftimesvlbconst);
            }
            else
            {
               assert(SCIPisNegative(scip, rowcoef));
               snf->transvarcoefs[snf->ntransvars] = 1;
               snf->transvarvubcoefs[snf->ntransvars] = DBLDBL_ROUND(val);
               snf->transbinvarsolvals[snf->ntransvars] = varsolvalbinary;
               snf->transcontvarsolvals[snf->ntransvars] = DBLDBL_ROUND(contsolval);

               /* aggregation information for y'_j */
               snf->aggrcoefsbin[snf->ntransvars] = rowcoefbinary;
               snf->aggrcoefscont[snf->ntransvars] = rowcoef;
               snf->aggrconstants[snf->ntransvars] = - DBLDBL_ROUND(rowcoeftimesvlbconst);
            }
            SCIPdbldblSum22_DBLDBL(transrhs, transrhs, -rowcoeftimesvlbconst);

            SCIPdebugMsg(scip, "    --> bestlb used for trans: ... %s y'_%d + ..., y'_%d <= %g x_%d (=%s), rhs=%g-(%g*%g)=%g\n",
                         snf->transvarcoefs[snf->ntransvars] == 1 ? "+" : "-", snf->ntransvars, snf->ntransvars, snf->transvarvubcoefs[snf->ntransvars],
                         snf->ntransvars, SCIPvarGetName(vlbvars[bestlbtype[i]]), DBLDBL_ROUND(transrhs) + DBLDBL_ROUND(rowcoeftimesvlbconst), rowcoef,
                         vlbconsts[bestlbtype[i]], snf->transrhs );
         }
      }
      else
      {
         /* use bestub to define y'_j */

         assert(!SCIPisInfinity(scip, bestub[i]));
         assert(!SCIPisInfinity(scip, - bestslb[i]));
         assert(bestslbtype[i] == -1 || bestslbtype[i] == -2);
         assert(bestubtype[i] > -3 && bestubtype[i] < SCIPvarGetNVubs(var));

         /* store for y_j that y'_j is the associated real variable
          * in the relaxed set
          */
         snf->origcontvars[snf->ntransvars] = probidx;

         if( bestubtype[i] < 0 )
         {
            SCIP_Real DBLDBL(val);
            SCIP_Real DBLDBL(contsolval);
            SCIP_Real DBLDBL(rowcoeftimesbestslb);

            /* use simple upper bound in bestslb = l_j <= y_j <= u_j = bestub to define
             *   y'_j =   a_j ( y_j - l_j ) with 0 <= y'_j <=   a_j ( u_j - l_j ) x_j and x_j = 1    if a_j > 0
             *   y'_j = - a_j ( y_j - l_j ) with 0 <= y'_j <= - a_j ( u_j - l_j ) x_j and x_j = 1    if a_j < 0,
             * put j into the set
             *   N1   if a_j > 0
             *   N2   if a_j < 0
             * and update the right hand side of the constraint in the relaxation
             *   rhs = rhs - a_j l_j
             */
            SCIPdbldblSum_DBLDBL(val, bestub[i], - bestslb[i]);
            SCIPdbldblProd21_DBLDBL(val, val, rowcoef);
            SCIPdbldblSum_DBLDBL(contsolval, solval, - bestslb[i]);
            SCIPdbldblProd21_DBLDBL(contsolval, contsolval, rowcoef);

            if( bestubtype[i] == -2 || bestslbtype[i] == -2 )
               *localbdsused = TRUE;

            SCIPdbldblProd_DBLDBL(rowcoeftimesbestslb, rowcoef, bestslb[i]);

            /* store aggregation information for y'_j for transforming cuts for the SNF relaxation back to the problem variables later */
            snf->origbinvars[snf->ntransvars] = -1;
            snf->aggrcoefsbin[snf->ntransvars] = 0.0;

            if( SCIPisPositive(scip, rowcoef) )
            {
               snf->transvarcoefs[snf->ntransvars] = 1;
               snf->transvarvubcoefs[snf->ntransvars] = DBLDBL_ROUND(val);
               snf->transbinvarsolvals[snf->ntransvars] = 1.0;
               snf->transcontvarsolvals[snf->ntransvars] = DBLDBL_ROUND(contsolval);

               /* aggregation information for y'_j */
               snf->aggrcoefscont[snf->ntransvars] = rowcoef;
               snf->aggrconstants[snf->ntransvars] = - DBLDBL_ROUND(rowcoeftimesbestslb);
            }
            else
            {
               assert(SCIPisNegative(scip, rowcoef));
               snf->transvarcoefs[snf->ntransvars] = - 1;
               snf->transvarvubcoefs[snf->ntransvars] = - DBLDBL_ROUND(val);
               snf->transbinvarsolvals[snf->ntransvars] = 1.0;
               snf->transcontvarsolvals[snf->ntransvars] = - DBLDBL_ROUND(contsolval);

               /* aggregation information for y'_j */
               snf->aggrcoefscont[snf->ntransvars] = - rowcoef;
               snf->aggrconstants[snf->ntransvars] = DBLDBL_ROUND(rowcoeftimesbestslb);
            }
            SCIPdbldblSum22_DBLDBL(transrhs, transrhs, -rowcoeftimesbestslb);

            SCIPdebugMsg(scip, "    --> bestub used for trans: ... %s y'_%d + ..., Y'_%d <= %g x_%d (=1), rhs=%g-(%g*%g)=%g\n",
                         snf->transvarcoefs[snf->ntransvars] == 1 ? "+" : "-", snf->ntransvars, snf->ntransvars, snf->transvarvubcoefs[snf->ntransvars],
                         snf->ntransvars, DBLDBL_ROUND(transrhs) + DBLDBL_ROUND(rowcoeftimesbestslb), rowcoef, bestslb[i], DBLDBL_ROUND(transrhs));
         }
         else
         {
            SCIP_Real rowcoefbinary;
            SCIP_Real varsolvalbinary;
            SCIP_Real DBLDBL(val);
            SCIP_Real DBLDBL(contsolval);
            SCIP_Real DBLDBL(rowcoeftimesvubconst);
            int vubvarprobidx;

            SCIP_VAR** vubvars = SCIPvarGetVubVars(var);
            SCIP_Real* vubconsts = SCIPvarGetVubConstants(var);
            SCIP_Real* vubcoefs = SCIPvarGetVubCoefs(var);


            /* use variable upper bound in bestslb = l_j <= y_j <= u~_j x_j + d_j = bestub to define
             *   y'_j =     a_j ( y_j - d_j ) + c_j x_j   with 0 <= y'_j <=   ( a_j u~_j + c_j ) x_j    if a_j > 0
             *   y'_j = - ( a_j ( y_j - d_j ) + c_j x_j ) with 0 <= y'_j <= - ( a_j u~_j + c_j ) x_j    if a_j < 0,
             * where c_j is the coefficient of x_j in the row, put j into the set
             *   N1   if a_j > 0
             *   N2   if a_j < 0
             * and update the right hand side of the constraint in the relaxation
             *   rhs = rhs - a_j d_j
             */
            assert(SCIPvarIsBinary(vubvars[bestubtype[i]]));

            vubvarprobidx = SCIPvarGetProbindex(vubvars[bestubtype[i]]);

            /* if the binary variable is not in the row then binvarpos[vlbvarprobidx] will be -1,
             * otherwise binvarpos[vlbvarprobidx] will be -(idx+1) where idx is the binary variables index
             * in the aggrrow
             */
            assert(binvarpos[vubvarprobidx] < 0);

            rowcoefbinary = binvarpos[vubvarprobidx] == -1 ? 0.0 : rowcoefs[-binvarpos[vubvarprobidx] - 1];
            varsolvalbinary = SCIPgetSolVal(scip, sol, vubvars[bestubtype[i]]);

            /* clear the binvarpos array, since the variable has been processed */
            binvarpos[vubvarprobidx] = 0;

            SCIPdbldblProd_DBLDBL(val, rowcoef, vubcoefs[bestubtype[i]]);
            SCIPdbldblSum21_DBLDBL(val, val, rowcoefbinary);
            {
               SCIP_Real DBLDBL(tmp);
               SCIPdbldblProd_DBLDBL(tmp, rowcoefbinary, varsolvalbinary);
               SCIPdbldblSum_DBLDBL(contsolval, solval, - vubconsts[bestubtype[i]]);
               SCIPdbldblProd21_DBLDBL(contsolval, contsolval, rowcoef);
               SCIPdbldblSum22_DBLDBL(contsolval, contsolval, tmp);
            }

            SCIPdbldblProd_DBLDBL(rowcoeftimesvubconst, rowcoef, vubconsts[bestubtype[i]]);
            /* store aggregation information for y'_j for transforming cuts for the SNF relaxation back to the problem variables later */
            snf->origbinvars[snf->ntransvars] = vubvarprobidx;

            if( SCIPisPositive(scip, rowcoef) )
            {
               snf->transvarcoefs[snf->ntransvars] = 1;
               snf->transvarvubcoefs[snf->ntransvars] = DBLDBL_ROUND(val);
               snf->transbinvarsolvals[snf->ntransvars] = varsolvalbinary;
               snf->transcontvarsolvals[snf->ntransvars] = DBLDBL_ROUND(contsolval);

               /* aggregation information for y'_j */
               snf->aggrcoefsbin[snf->ntransvars] = rowcoefbinary;
               snf->aggrcoefscont[snf->ntransvars] = rowcoef;
               snf->aggrconstants[snf->ntransvars] = - DBLDBL_ROUND(rowcoeftimesvubconst);
            }
            else
            {
               assert(SCIPisNegative(scip, rowcoef));
               snf->transvarcoefs[snf->ntransvars] = - 1;
               snf->transvarvubcoefs[snf->ntransvars] = - DBLDBL_ROUND(val);
               snf->transbinvarsolvals[snf->ntransvars] = varsolvalbinary;
               snf->transcontvarsolvals[snf->ntransvars] = - DBLDBL_ROUND(contsolval);

               /* aggregation information for y'_j */
               snf->aggrcoefsbin[snf->ntransvars] = - rowcoefbinary;
               snf->aggrcoefscont[snf->ntransvars] = - rowcoef;
               snf->aggrconstants[snf->ntransvars] = DBLDBL_ROUND(rowcoeftimesvubconst);
            }
            SCIPdbldblSum22_DBLDBL(transrhs, transrhs, -rowcoeftimesvubconst);

            /* store for x_j that y'_j is the associated real variable in the 0-1 single node flow relaxation */

            SCIPdebugMsg(scip, "    --> bestub used for trans: ... %s y'_%d + ..., y'_%d <= %g x_%d (=%s), rhs=%g-(%g*%g)=%g\n",
                         snf->transvarcoefs[snf->ntransvars] == 1 ? "+" : "-", snf->ntransvars, snf->ntransvars, snf->transvarvubcoefs[snf->ntransvars],
                         snf->ntransvars, SCIPvarGetName(vubvars[bestubtype[i]]), DBLDBL_ROUND(transrhs) + DBLDBL_ROUND(rowcoeftimesvubconst), rowcoef,
                         vubconsts[bestubtype[i]], DBLDBL_ROUND(transrhs));
         }
      }

      ++snf->ntransvars;
   }

   snf->transrhs = DBLDBL_ROUND(transrhs);

   /* transform remaining binary variables of row */
   for( i = nnonbinvarsrow; i < nnz; ++i )
   {
      SCIP_VAR* var;
      SCIP_Real rowcoef;
      int probidx;
      SCIP_Real val;
      SCIP_Real contsolval;
      SCIP_Real varsolval;

      probidx = rowinds[i];
      /* variable should be binary */
      assert(probidx < nbinvars);

      /* binary variable was processed together with a non-binary variable */
      if( binvarpos[probidx] == 0 )
         continue;

      /* binary variable was not processed yet, so the position should match the position in the row */
      assert(binvarpos[probidx] == i+1);
      /* set position to zero again */
      binvarpos[probidx] = 0;

      var = vars[probidx];
      rowcoef = rowcoefs[i];

      assert(!SCIPisZero(scip, rowcoef));

      varsolval = SCIPgetSolVal(scip, sol, var);
      SCIPdebugMsg(scip, "  %d: %g <%s, idx=%d, lp=%g, [%g, %g]>:\n", i, rowcoef, SCIPvarGetName(var), probidx, varsolval,
         SCIPvarGetLbGlobal(var), SCIPvarGetUbGlobal(var));

      /* define
       *    y'_j =   c_j x_j with 0 <= y'_j <=   c_j x_j    if c_j > 0
       *    y'_j = - c_j x_j with 0 <= y'_j <= - c_j x_j    if c_j < 0,
       * where c_j is the coefficient of x_j in the row and put j into the set
       *    N1   if c_j > 0
       *    N2   if c_j < 0.
       */
      val = rowcoef;
      contsolval = rowcoef * varsolval;

      /* store aggregation information for y'_j for transforming cuts for the SNF relaxation back to the problem variables later */
      snf->origbinvars[snf->ntransvars] = probidx;
      snf->origcontvars[snf->ntransvars] = -1;
      snf->aggrcoefscont[snf->ntransvars] = 0.0;
      snf->aggrconstants[snf->ntransvars] = 0.0;

      if( SCIPisPositive(scip, rowcoef) )
      {
         snf->transvarcoefs[snf->ntransvars] = 1;
         snf->transvarvubcoefs[snf->ntransvars] = val;
         snf->transbinvarsolvals[snf->ntransvars] = varsolval;
         snf->transcontvarsolvals[snf->ntransvars] = contsolval;

         /* aggregation information for y'_j */
         snf->aggrcoefsbin[snf->ntransvars] = rowcoef;
      }
      else
      {
         assert(SCIPisNegative(scip, rowcoef));
         snf->transvarcoefs[snf->ntransvars] = - 1;
         snf->transvarvubcoefs[snf->ntransvars] = - val;
         snf->transbinvarsolvals[snf->ntransvars] = varsolval;
         snf->transcontvarsolvals[snf->ntransvars] = - contsolval;

         /* aggregation information for y'_j */
         snf->aggrcoefsbin[snf->ntransvars] = - rowcoef;
      }

      assert(snf->transvarcoefs[snf->ntransvars] == 1 || snf->transvarcoefs[snf->ntransvars] == - 1 );
      assert(SCIPisFeasGE(scip, snf->transbinvarsolvals[snf->ntransvars], 0.0)
         && SCIPisFeasLE(scip, snf->transbinvarsolvals[snf->ntransvars], 1.0));
      assert(SCIPisFeasGE(scip, snf->transvarvubcoefs[snf->ntransvars], 0.0)
         && !SCIPisInfinity(scip, snf->transvarvubcoefs[snf->ntransvars]));

      SCIPdebugMsg(scip, "   --> ... %s y'_%d + ..., y'_%d <= %g x_%d (=%s))\n", snf->transvarcoefs[snf->ntransvars] == 1 ? "+" : "-", snf->ntransvars, snf->ntransvars,
         snf->transvarvubcoefs[snf->ntransvars], snf->ntransvars, SCIPvarGetName(var) );

      /* updates number of variables in transformed problem */
      snf->ntransvars++;
   }

   /* construction was successful */
   *success = TRUE;

#ifdef SCIP_DEBUG
   SCIPdebugMsg(scip, "constraint in constructed 0-1 single node flow relaxation: ");
   for( i = 0; i < snf->ntransvars; i++ )
   {
      SCIPdebugMsgPrint(scip, "%s y'_%d ", snf->transvarcoefs[i] == 1 ? "+" : "-", i);
   }
   SCIPdebugMsgPrint(scip, "<= %g\n", snf->transrhs);
#endif

TERMINATE:
   SCIPfreeCleanBufferArray(scip, &binvarpos);

   SCIPfreeBufferArray(scip, &selectedbounds);
   SCIPfreeBufferArray(scip, &bestsubtype);
   SCIPfreeBufferArray(scip, &bestslbtype);
   SCIPfreeBufferArray(scip, &bestubtype);
   SCIPfreeBufferArray(scip, &bestlbtype);
   SCIPfreeBufferArray(scip, &bestsub);
   SCIPfreeBufferArray(scip, &bestslb);
   SCIPfreeBufferArray(scip, &bestub);
   SCIPfreeBufferArray(scip, &bestlb);

   return SCIP_OKAY;
}

static
SCIP_RETCODE allocSNFRelaxation(
   SCIP*                 scip,               /**< SCIP data structure */
   SNF_RELAXATION*       snf,                /**< pointer to snf relaxation to be destroyed */
   int                   nvars               /**< number of active problem variables */
   )
{
   SCIP_CALL( SCIPallocBufferArray(scip, &snf->transvarcoefs, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &snf->transbinvarsolvals, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &snf->transcontvarsolvals, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &snf->transvarvubcoefs, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &snf->origbinvars, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &snf->origcontvars, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &snf->aggrcoefsbin, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &snf->aggrcoefscont, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &snf->aggrconstants, nvars) );

   return SCIP_OKAY;
}

static
void destroySNFRelaxation(
   SCIP*                 scip,               /**< SCIP data structure */
   SNF_RELAXATION*       snf                 /**< pointer to snf relaxation to be destroyed */
   )
{
   SCIPfreeBufferArray(scip, &snf->aggrconstants);
   SCIPfreeBufferArray(scip, &snf->aggrcoefscont);
   SCIPfreeBufferArray(scip, &snf->aggrcoefsbin);
   SCIPfreeBufferArray(scip, &snf->origcontvars);
   SCIPfreeBufferArray(scip, &snf->origbinvars);
   SCIPfreeBufferArray(scip, &snf->transvarvubcoefs);
   SCIPfreeBufferArray(scip, &snf->transcontvarsolvals);
   SCIPfreeBufferArray(scip, &snf->transbinvarsolvals);
   SCIPfreeBufferArray(scip, &snf->transvarcoefs);
}

/** solve knapsack problem in maximization form with "<" constraint approximately by greedy; if needed, one can provide
 *  arrays to store all selected items and all not selected items
 */
static
SCIP_RETCODE SCIPsolveKnapsackApproximatelyLT(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   nitems,             /**< number of available items */
   SCIP_Real*            weights,            /**< item weights */
   SCIP_Real*            profits,            /**< item profits */
   SCIP_Real             capacity,           /**< capacity of knapsack */
   int*                  items,              /**< item numbers */
   int*                  solitems,           /**< array to store items in solution, or NULL */
   int*                  nonsolitems,        /**< array to store items not in solution, or NULL */
   int*                  nsolitems,          /**< pointer to store number of items in solution, or NULL */
   int*                  nnonsolitems,       /**< pointer to store number of items not in solution, or NULL */
   SCIP_Real*            solval              /**< pointer to store optimal solution value, or NULL */
   )
{
   SCIP_Real* tempsort;
   SCIP_Real solitemsweight;
   SCIP_Real mediancapacity;
   int j;
   int i;
   int criticalitem;

   assert(weights != NULL);
   assert(profits != NULL);
   assert(SCIPisFeasGE(scip, capacity, 0.0));
   assert(!SCIPisInfinity(scip, capacity));
   assert(items != NULL);
   assert(nitems >= 0);

   if( solitems != NULL )
   {
      *nsolitems = 0;
      *nnonsolitems = 0;
   }
   if( solval != NULL )
      *solval = 0.0;

   /* allocate memory for temporary array used for sorting; array should contain profits divided by corresponding weights (p_1 / w_1 ... p_n / w_n )*/
   SCIP_CALL( SCIPallocBufferArray(scip, &tempsort, nitems) );

   /* initialize temporary array */
   for( i = nitems - 1; i >= 0; --i )
      tempsort[i] = profits[i] / weights[i];

   /* decrease capacity slightly to make it tighter than the original capacity */
   mediancapacity = capacity * (1 - SCIPfeastol(scip));

   /* rearrange items around  */
   SCIPselectWeightedDownRealRealInt(tempsort, profits, items, weights, mediancapacity, nitems, &criticalitem);

   /* free temporary array */
   SCIPfreeBufferArray(scip, &tempsort);

   /* select items as long as they fit into the knapsack */
   solitemsweight = 0.0;
   for( j = 0; j < nitems && SCIPisFeasLT(scip, solitemsweight + weights[j], capacity); j++ )
   {
      if( solitems != NULL )
      {
         solitems[*nsolitems] = items[j];
         (*nsolitems)++;
      }
      if( solval != NULL )
         (*solval) += profits[j];
      solitemsweight += weights[j];
   }


   /* continue to put items into the knapsack if they entirely fit */
   for( ; j < nitems; j++ )
   {
      if( SCIPisFeasLT(scip, solitemsweight + weights[j], capacity) )
      {
         if( solitems != NULL )
         {
            solitems[*nsolitems] = items[j];
            (*nsolitems)++;
         }
         if( solval != NULL )
            (*solval) += profits[j];
         solitemsweight += weights[j];
      }
      else if( solitems != NULL )
      {
         nonsolitems[*nnonsolitems] = items[j];
         (*nnonsolitems)++;
      }
   }

   return SCIP_OKAY;
}

/** checks, whether the given scalar scales the given value to an integral number with error in the given bounds */
static
SCIP_Bool isIntegralScalar(
   SCIP_Real             val,                /**< value that should be scaled to an integral value */
   SCIP_Real             scalar,             /**< scalar that should be tried */
   SCIP_Real             mindelta,           /**< minimal relative allowed difference of scaled coefficient s*c and integral i */
   SCIP_Real             maxdelta            /**< maximal relative allowed difference of scaled coefficient s*c and integral i */
   )
{
   SCIP_Real sval;
   SCIP_Real downval;
   SCIP_Real upval;

   assert(mindelta <= 0.0);
   assert(maxdelta >= 0.0);

   sval = val * scalar;
   downval = floor(sval);
   upval = ceil(sval);

   return (SCIPrelDiff(sval, downval) <= maxdelta || SCIPrelDiff(sval, upval) >= mindelta);
}

/** get integral number with error in the bounds which corresponds to given value scaled by a given scalar;
 *  should be used in connection with isIntegralScalar()
 */
static
SCIP_Longint getIntegralVal(
   SCIP_Real             val,                /**< value that should be scaled to an integral value */
   SCIP_Real             scalar,             /**< scalar that should be tried */
   SCIP_Real             mindelta,           /**< minimal relative allowed difference of scaled coefficient s*c and integral i */
   SCIP_Real             maxdelta            /**< maximal relative allowed difference of scaled coefficient s*c and integral i */
   )
{
   SCIP_Real sval;
   SCIP_Real upval;
   SCIP_Longint intval;

   assert(mindelta <= 0.0);
   assert(maxdelta >= 0.0);

   sval = val * scalar;
   upval = ceil(sval);

   if( SCIPrelDiff(sval, upval) >= mindelta )
      intval = (SCIP_Longint) upval;
   else
      intval = (SCIP_Longint) (floor(sval));

   return intval;
}

/** build the flow cover which corresponds to the given exact or approximate solution of KP^SNF; given unfinished
 *  flow cover contains variables which have been fixed in advance
 */
static
void buildFlowCover(
   SCIP*                 scip,               /**< SCIP data structure */
   int*                  coefs,              /**< coefficient of all real variables in N1&N2 */
   SCIP_Real*            vubcoefs,           /**< coefficient in vub of all real variables in N1&N2 */
   SCIP_Real             rhs,                /**< right hand side of 0-1 single node flow constraint */
   int*                  solitems,           /**< items in knapsack */
   int*                  nonsolitems,        /**< items not in knapsack */
   int                   nsolitems,          /**< number of items in knapsack */
   int                   nnonsolitems,       /**< number of items not in knapsack */
   int*                  nflowcovervars,     /**< pointer to store number of variables in flow cover */
   int*                  nnonflowcovervars,  /**< pointer to store number of variables not in flow cover */
   int*                  flowcoverstatus,    /**< pointer to store whether variable is in flow cover (+1) or not (-1) */
   DBLDBL(SCIP_Real*     flowcoverweight),   /**< pointer to store weight of flow cover */
   SCIP_Real*            lambda              /**< pointer to store lambda */
   )
{
   int j;
   SCIP_Real DBLDBL(tmp);

   assert(scip != NULL);
   assert(coefs != NULL);
   assert(vubcoefs != NULL);
   assert(solitems != NULL);
   assert(nonsolitems != NULL);
   assert(nsolitems >= 0);
   assert(nnonsolitems >= 0);
   assert(nflowcovervars != NULL && *nflowcovervars >= 0);
   assert(nnonflowcovervars != NULL && *nnonflowcovervars >= 0);
   assert(flowcoverstatus != NULL);
   assert(DBL_HI(flowcoverweight) != NULL);
   assert(DBL_LO(flowcoverweight) != NULL);
   assert(lambda != NULL);

   /* get flowcover status for each item */
   for( j = 0; j < nsolitems; j++ )
   {
      /* j in N1 with z°_j = 1 => j in N1\C1 */
      if( coefs[solitems[j]] == 1 )
      {
         flowcoverstatus[solitems[j]] = -1;
         (*nnonflowcovervars)++;
      }
      /* j in N2 with z_j = 1 => j in C2 */
      else
      {
         assert(coefs[solitems[j]] == -1);
         flowcoverstatus[solitems[j]] = 1;
         (*nflowcovervars)++;
         SCIPdbldblSum21_DBLDBL(*flowcoverweight, *flowcoverweight, -vubcoefs[solitems[j]]);
      }
   }
   for( j = 0; j < nnonsolitems; j++ )
   {
      /* j in N1 with z°_j = 0 => j in C1 */
      if( coefs[nonsolitems[j]] == 1 )
      {
         flowcoverstatus[nonsolitems[j]] = 1;
         (*nflowcovervars)++;
         SCIPdbldblSum21_DBLDBL(*flowcoverweight, *flowcoverweight, vubcoefs[nonsolitems[j]]);
      }
      /* j in N2 with z_j = 0 => j in N2\C2 */
      else
      {
         assert(coefs[nonsolitems[j]] == -1);
         flowcoverstatus[nonsolitems[j]] = -1;
         (*nnonflowcovervars)++;
      }
   }

   /* get lambda = sum_{j in C1} u_j - sum_{j in C2} u_j - rhs */
   SCIPdbldblSum21_DBLDBL(tmp, *flowcoverweight, -rhs);
   *lambda = DBLDBL_ROUND(tmp);
}

/** get a flow cover (C1, C2) for a given 0-1 single node flow set
 *    {(x,y) in {0,1}^n x R^n : sum_{j in N1} y_j - sum_{j in N2} y_j <= b, 0 <= y_j <= u_j x_j},
 *  i.e., get sets C1 subset N1 and C2 subset N2 with sum_{j in C1} u_j - sum_{j in C2} u_j = b + lambda and lambda > 0
 */
static
SCIP_RETCODE getFlowCover(
   SCIP*                 scip,               /**< SCIP data structure */
   SNF_RELAXATION*       snf,
   int*                  nflowcovervars,     /**< pointer to store number of variables in flow cover */
   int*                  nnonflowcovervars,  /**< pointer to store number of variables not in flow cover */
   int*                  flowcoverstatus,    /**< pointer to store whether variable is in flow cover (+1) or not (-1) */
   SCIP_Real*            lambda,             /**< pointer to store lambda */
   SCIP_Bool*            found               /**< pointer to store whether a cover was found */
   )
{
   SCIP_Real* transprofitsint;
   SCIP_Real* transprofitsreal;
   SCIP_Real* transweightsreal;
   SCIP_Longint* transweightsint;
   int* items;
   int* itemsint;
   int* nonsolitems;
   int* solitems;
   SCIP_Real DBLDBL(flowcoverweight);
   SCIP_Real DBLDBL(flowcoverweightafterfix);
   SCIP_Real n1itemsweight;
   SCIP_Real n2itemsminweight;
   SCIP_Real scalar;
   SCIP_Real transcapacityreal;
#if !defined(NDEBUG) || defined(SCIP_DEBUG)
   SCIP_Bool kpexact;
#endif
   SCIP_Bool scalesuccess;
   SCIP_Bool transweightsrealintegral;
   SCIP_Longint transcapacityint;
   int nflowcovervarsafterfix;
   int nitems;
   int nn1items;
   int nnonflowcovervarsafterfix;
   int nnonsolitems;
   int nsolitems;
   int j;

   assert(scip != NULL);
   assert(snf->transvarcoefs != NULL);
   assert(snf->transbinvarsolvals != NULL);
   assert(snf->transvarvubcoefs != NULL);
   assert(snf->ntransvars > 0);
   assert(nflowcovervars != NULL);
   assert(nnonflowcovervars != NULL);
   assert(flowcoverstatus != NULL);
   assert(lambda != NULL);
   assert(found != NULL);

   SCIPdebugMsg(scip, "--------------------- get flow cover ----------------------------------------------------\n");

   /* get data structures */
   SCIP_CALL( SCIPallocBufferArray(scip, &items, snf->ntransvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &itemsint, snf->ntransvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &transprofitsreal, snf->ntransvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &transprofitsint, snf->ntransvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &transweightsreal, snf->ntransvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &transweightsint, snf->ntransvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &solitems, snf->ntransvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &nonsolitems, snf->ntransvars) );

   BMSclearMemoryArray(flowcoverstatus, snf->ntransvars);
   *found = FALSE;
   *nflowcovervars = 0;
   *nnonflowcovervars = 0;

   DBLDBL_ASSIGN(flowcoverweight, 0.0);
   nflowcovervarsafterfix = 0;
   nnonflowcovervarsafterfix = 0;
   DBLDBL_ASSIGN(flowcoverweightafterfix, 0.0);
#if !defined(NDEBUG) || defined(SCIP_DEBUG)
   kpexact = FALSE;
#endif

   /* fix some variables in advance according to the following fixing strategy
    *   put j into N1\C1,          if j in N1 and x*_j = 0,
    *   put j into C1,             if j in N1 and x*_j = 1,
    *   put j into C2,             if j in N2 and x*_j = 1,
    *   put j into N2\C2,          if j in N2 and x*_j = 0
    * and get the set of the remaining variables
    */
   SCIPdebugMsg(scip, "0. Fix some variables in advance:\n");
   nitems = 0;
   nn1items = 0;
   n1itemsweight = 0.0;
   n2itemsminweight = SCIP_REAL_MAX;
   for( j = 0; j < snf->ntransvars; j++ )
   {
      assert(snf->transvarcoefs[j] == 1 || snf->transvarcoefs[j] == -1);
      assert(SCIPisFeasGE(scip, snf->transbinvarsolvals[j], 0.0) && SCIPisFeasLE(scip,  snf->transbinvarsolvals[j], 1.0));
      assert(SCIPisFeasGE(scip, snf->transvarvubcoefs[j], 0.0));

      /* if u_j = 0, put j into N1\C1 and N2\C2, respectively */
      if( SCIPisFeasZero(scip, snf->transvarvubcoefs[j]) )
      {
         flowcoverstatus[j] = -1;
         (*nnonflowcovervars)++;
         continue;
      }

      /* x*_j is fractional */
      if( !SCIPisFeasIntegral(scip,  snf->transbinvarsolvals[j]) )
      {
         items[nitems] = j;
         nitems++;
         if( snf->transvarcoefs[j] == 1 )
         {
            n1itemsweight += snf->transvarvubcoefs[j];
            nn1items++;
         }
         else
            n2itemsminweight = MIN(n2itemsminweight, snf->transvarvubcoefs[j]);
      }
      /* j is in N1 and x*_j = 0 */
      else if( snf->transvarcoefs[j] == 1 &&  snf->transbinvarsolvals[j] < 0.5 )
      {
         flowcoverstatus[j] = -1;
         (*nnonflowcovervars)++;
         SCIPdebugMsg(scip, "     <%d>: in N1-C1\n", j);
      }
      /* j is in N1 and x*_j = 1 */
      else if( snf->transvarcoefs[j] == 1 &&  snf->transbinvarsolvals[j] > 0.5 )
      {
         flowcoverstatus[j] = 1;
         (*nflowcovervars)++;
         SCIPdbldblSum21_DBLDBL(flowcoverweight, flowcoverweight, snf->transvarvubcoefs[j]);
         SCIPdebugMsg(scip, "     <%d>: in C1\n", j);
      }
      /* j is in N2 and x*_j = 1 */
      else if( snf->transvarcoefs[j] == -1 &&  snf->transbinvarsolvals[j] > 0.5 )
      {
         flowcoverstatus[j] = 1;
         (*nflowcovervars)++;
         SCIPdbldblSum21_DBLDBL(flowcoverweight, flowcoverweight, -snf->transvarvubcoefs[j]);
         SCIPdebugMsg(scip, "     <%d>: in C2\n", j);
      }
      /* j is in N2 and x*_j = 0 */
      else
      {
         assert(snf->transvarcoefs[j] == -1 &&  snf->transbinvarsolvals[j] < 0.5);
         flowcoverstatus[j] = -1;
         (*nnonflowcovervars)++;
         SCIPdebugMsg(scip, "     <%d>: in N2-C2\n", j);
      }
   }
   assert((*nflowcovervars) + (*nnonflowcovervars) + nitems == snf->ntransvars);
   assert(nn1items >= 0);

   /* to find a flow cover, transform the following knapsack problem
    *
    * (KP^SNF)      max sum_{j in N1} ( x*_j - 1 ) z_j + sum_{j in N2} x*_j z_j
    *                   sum_{j in N1}          u_j z_j - sum_{j in N2} u_j  z_j > b
    *                                         z_j in {0,1} for all j in N1 & N2
    *
    * 1. to a knapsack problem in maximization form, such that all variables in the knapsack constraint have
    *    positive weights and the constraint is a "<" constraint, by complementing all variables in N1
    *
    *    (KP^SNF_rat)  max sum_{j in N1} ( 1 - x*_j ) z°_j + sum_{j in N2} x*_j z_j
    *                      sum_{j in N1}          u_j z°_j + sum_{j in N2} u_j  z_j < - b + sum_{j in N1} u_j
    *                                                 z°_j in {0,1} for all j in N1
    *                                                  z_j in {0,1} for all j in N2,
    *    and solve it approximately under consideration of the fixing,
    * or
    * 2. to a knapsack problem in maximization form, such that all variables in the knapsack constraint have
    *    positive integer weights and the constraint is a "<=" constraint, by complementing all variables in N1
    *    and multiplying the constraint by a suitable scalar C
    *
    *    (KP^SNF_int)  max sum_{j in N1} ( 1 - x*_j ) z°_j + sum_{j in N2} x*_j z_j
    *                      sum_{j in N1}        C u_j z°_j + sum_{j in N2} C u_j  z_j <= c
    *                                                   z°_j in {0,1} for all j in N1
    *                                                    z_j in {0,1} for all j in N2,
    *    where
    *      c = floor[ C (- b + sum_{j in N1} u_j ) ]      if frac[ C (- b + sum_{j in N1} u_j ) ] > 0
    *      c =        C (- b + sum_{j in N1} u_j )   - 1  if frac[ C (- b + sum_{j in N1} u_j ) ] = 0
    *    and solve it exactly under consideration of the fixing.
    */
   SCIPdebugMsg(scip, "1. Transform KP^SNF to KP^SNF_rat:\n");

   /* get weight and profit of variables in KP^SNF_rat and check, whether all weights are already integral */
   transweightsrealintegral = TRUE;
   for( j = 0; j < nitems; j++ )
   {
      transweightsreal[j] = snf->transvarvubcoefs[items[j]];

      if( !isIntegralScalar(transweightsreal[j], 1.0, -MINDELTA, MAXDELTA) )
         transweightsrealintegral = FALSE;

      if( snf->transvarcoefs[items[j]] == 1 )
      {
         transprofitsreal[j] = 1.0 -  snf->transbinvarsolvals[items[j]];
         SCIPdebugMsg(scip, "     <%d>: j in N1:   w_%d = %g, p_%d = %g %s\n", items[j], items[j], transweightsreal[j],
            items[j], transprofitsreal[j], SCIPisIntegral(scip, transweightsreal[j]) ? "" : "  ----> NOT integral");
      }
      else
      {
         transprofitsreal[j] =  snf->transbinvarsolvals[items[j]];
         SCIPdebugMsg(scip, "     <%d>: j in N2:   w_%d = %g, p_%d = %g %s\n", items[j], items[j], transweightsreal[j],
            items[j], transprofitsreal[j], SCIPisIntegral(scip, transweightsreal[j]) ? "" : "  ----> NOT integral");
      }
   }
   /* get capacity of knapsack constraint in KP^SNF_rat */
   transcapacityreal = - snf->transrhs + DBLDBL_ROUND(flowcoverweight) + n1itemsweight;
   SCIPdebugMsg(scip, "     transcapacity = -rhs(%g) + flowcoverweight(%g) + n1itemsweight(%g) = %g\n",
      snf->transrhs, DBLDBL_ROUND(flowcoverweight), n1itemsweight, transcapacityreal);

   /* there exists no flow cover if the capacity of knapsack constraint in KP^SNF_rat after fixing
    * is less than or equal to zero
    */
   if( SCIPisFeasLE(scip, transcapacityreal/10, 0.0) )
   {
      assert(!(*found));
      goto TERMINATE;
   }

   /* KP^SNF_rat has been solved by fixing some variables in advance */
   assert(nitems >= 0);
   if( nitems == 0)
   {
      /* get lambda = sum_{j in C1} u_j - sum_{j in C2} u_j - rhs */
      SCIPdbldblSum21_DBLDBL(flowcoverweight, flowcoverweight, -snf->transrhs);
      *lambda = DBLDBL_ROUND(flowcoverweight);
      *found = TRUE;
      goto TERMINATE;
   }

   /* Use the following strategy
    *   solve KP^SNF_int exactly,          if a suitable factor C is found and (nitems*capacity) <= MAXDYNPROGSPACE,
    *   solve KP^SNF_rat approximately,    otherwise
    */

   /* find a scaling factor C */
   if( transweightsrealintegral )
   {
      /* weights are already integral */
      scalar = 1.0;
      scalesuccess = TRUE;
   }
   else
   {
      scalesuccess = FALSE;
      SCIP_CALL( SCIPcalcIntegralScalar(transweightsreal, nitems, -MINDELTA, MAXDELTA, MAXDNOM, MAXSCALE, &scalar,
            &scalesuccess) );
   }

   /* initialize number of (non-)solution items, should be changed to a nonnegative number in all possible paths below */
   nsolitems = -1;
   nnonsolitems = -1;

   /* suitable factor C was found*/
   if( scalesuccess )
   {
      SCIP_Real tmp1;
      SCIP_Real tmp2;

      /* transform KP^SNF to KP^SNF_int */
      for( j = 0; j < nitems; ++j )
      {
         transweightsint[j] = getIntegralVal(transweightsreal[j], scalar, -MINDELTA, MAXDELTA);
         transprofitsint[j] = transprofitsreal[j];
         itemsint[j] = items[j];
      }
      if( isIntegralScalar(transcapacityreal, scalar, -MINDELTA, MAXDELTA) )
      {
         transcapacityint = getIntegralVal(transcapacityreal, scalar, -MINDELTA, MAXDELTA);
         transcapacityint -= 1;
      }
      else
         transcapacityint = (SCIP_Longint) (transcapacityreal * scalar);
      nflowcovervarsafterfix = *nflowcovervars;
      nnonflowcovervarsafterfix = *nnonflowcovervars;
      DBLDBL_ASSIGN2(flowcoverweightafterfix, flowcoverweight);

      tmp1 = (SCIP_Real) (nitems + 1);
      tmp2 = (SCIP_Real) ((transcapacityint) + 1);
      if( transcapacityint * nitems <= MAXDYNPROGSPACE && tmp1 * tmp2 <= INT_MAX / 8.0)
      {
         SCIP_Bool success;

         /* solve KP^SNF_int by dynamic programming */
         SCIP_CALL(SCIPsolveKnapsackExactly(scip, nitems, transweightsint, transprofitsint, transcapacityint,
               itemsint, solitems, nonsolitems, &nsolitems, &nnonsolitems, NULL, &success));

         if( !success )
         {
            /* solve KP^SNF_rat approximately */
            SCIP_CALL(SCIPsolveKnapsackApproximatelyLT(scip, nitems, transweightsreal, transprofitsreal,
                  transcapacityreal, items, solitems, nonsolitems, &nsolitems, &nnonsolitems, NULL));
         }
#if !defined(NDEBUG) || defined(SCIP_DEBUG)
         else
            kpexact = TRUE;
#endif
      }
      else
      {
         /* solve KP^SNF_rat approximately */
         SCIP_CALL(SCIPsolveKnapsackApproximatelyLT(scip, nitems, transweightsreal, transprofitsreal, transcapacityreal,
               items, solitems, nonsolitems, &nsolitems, &nnonsolitems, NULL));
         assert(!kpexact);
      }
   }
   else
   {
      /* solve KP^SNF_rat approximately */
      SCIP_CALL(SCIPsolveKnapsackApproximatelyLT(scip, nitems, transweightsreal, transprofitsreal, transcapacityreal,
            items, solitems, nonsolitems, &nsolitems, &nnonsolitems, NULL));
      assert(!kpexact);
   }

   assert(nsolitems != -1);
   assert(nnonsolitems != -1);

   /* build the flow cover from the solution of KP^SNF_rat and KP^SNF_int, respectively and the fixing */
   assert(*nflowcovervars + *nnonflowcovervars + nsolitems + nnonsolitems == snf->ntransvars);
   buildFlowCover(scip, snf->transvarcoefs, snf->transvarvubcoefs, snf->transrhs, solitems, nonsolitems, nsolitems, nnonsolitems, nflowcovervars,
      nnonflowcovervars, flowcoverstatus, DBLDBL(&flowcoverweight), lambda);
   assert(*nflowcovervars + *nnonflowcovervars == snf->ntransvars);

   /* if the found structure is not a flow cover, because of scaling, solve KP^SNF_rat approximately */
   if( SCIPisFeasLE(scip, *lambda, 0.0) )
   {
      assert(kpexact);

      /* solve KP^SNF_rat approximately */
      SCIP_CALL(SCIPsolveKnapsackApproximatelyLT(scip, nitems, transweightsreal, transprofitsreal, transcapacityreal,
            items, solitems, nonsolitems, &nsolitems, &nnonsolitems, NULL));
#ifdef SCIP_DEBUG /* this time only for SCIP_DEBUG, because only then, the variable is used again  */
      kpexact = FALSE;
#endif

      /* build the flow cover from the solution of KP^SNF_rat and the fixing */
      *nflowcovervars = nflowcovervarsafterfix;
      *nnonflowcovervars = nnonflowcovervarsafterfix;
      DBLDBL_ASSIGN2(flowcoverweight, flowcoverweightafterfix);

      assert(*nflowcovervars + *nnonflowcovervars + nsolitems + nnonsolitems == snf->ntransvars);
      buildFlowCover(scip, snf->transvarcoefs, snf->transvarvubcoefs, snf->transrhs, solitems, nonsolitems, nsolitems, nnonsolitems, nflowcovervars,
         nnonflowcovervars, flowcoverstatus, DBLDBL(&flowcoverweight), lambda);
      assert(*nflowcovervars + *nnonflowcovervars == snf->ntransvars);
   }
   *found = TRUE;

 TERMINATE:
   assert((!*found) || SCIPisFeasGT(scip, *lambda, 0.0));
#ifdef SCIP_DEBUG
   if( *found )
   {
      SCIPdebugMsg(scip, "2. %s solution:\n", kpexact ? "exact" : "approximate");
      for( j = 0; j < snf->ntransvars; j++ )
      {
         if( snf->transvarcoefs[j] == 1 && flowcoverstatus[j] == 1 )
         {
            SCIPdebugMsg(scip, "     C1: + y_%d [u_%d = %g]\n", j, j, snf->transvarvubcoefs[j]);
         }
         else if( snf->transvarcoefs[j] == -1 && flowcoverstatus[j] == 1 )
         {
            SCIPdebugMsg(scip, "     C2: - y_%d [u_%d = %g]\n", j, j, snf->transvarvubcoefs[j]);
         }
      }
      SCIPdebugMsg(scip, "     flowcoverweight(%g) = rhs(%g) + lambda(%g)\n", flowcoverweight, snf->transrhs, *lambda);
   }
#endif

   /* free data structures */
   SCIPfreeBufferArray(scip, &nonsolitems);
   SCIPfreeBufferArray(scip, &solitems);
   SCIPfreeBufferArray(scip, &transweightsint);
   SCIPfreeBufferArray(scip, &transweightsreal);
   SCIPfreeBufferArray(scip, &transprofitsint);
   SCIPfreeBufferArray(scip, &transprofitsreal);
   SCIPfreeBufferArray(scip, &itemsint);
   SCIPfreeBufferArray(scip, &items);

   return SCIP_OKAY;
}

static
SCIP_Real evaluateLiftingFunction(
   SCIP*                 scip,
   LIFTINGDATA*          liftingdata,
   SCIP_Real             x
   )
{
   SCIP_Real DBLDBL(tmp);
   SCIP_Real xpluslambda;
   int i;

   xpluslambda = x + liftingdata->lambda;

   i = 0;
   while( i < liftingdata->r && SCIPisGT(scip, xpluslambda, liftingdata->M[i+1]) )
      ++i;

   if( i < liftingdata->t )
   {
      if( SCIPisLE(scip, liftingdata->M[i], x) )
      {
         assert(SCIPisLE(scip, xpluslambda, liftingdata->M[i+1]));
         return i * liftingdata->lambda;
      }

      assert(i > 0 && SCIPisLE(scip, liftingdata->M[i], xpluslambda) && x <= liftingdata->M[i]);

      /* return x - liftingdata->M[i] + i * liftingdata->lambda */
      SCIPdbldblProd_DBLDBL(tmp, i, liftingdata->lambda);
      SCIPdbldblSum21_DBLDBL(tmp, tmp, x);
      SCIPdbldblSum21_DBLDBL(tmp, tmp, -liftingdata->M[i]);
      return DBLDBL_ROUND(tmp);
   }

   if( i < liftingdata->r )
   {
      assert(!SCIPisInfinity(scip, liftingdata->mp));

      /* p = liftingdata->m[i] - (liftingdata->mp - liftingdata->lambda) - liftingdata->ml; */
      SCIPdbldblSum_DBLDBL(tmp, liftingdata->m[i], -liftingdata->mp);
      SCIPdbldblSum21_DBLDBL(tmp, tmp, -liftingdata->ml);
      SCIPdbldblSum21_DBLDBL(tmp, tmp, liftingdata->lambda);

      /* p = MAX(0.0, p); */
      if( DBL_HI(tmp) < 0.0 )
      {
         DBLDBL_ASSIGN(tmp, 0.0);
      }

      SCIPdbldblSum21_DBLDBL(tmp, tmp, liftingdata->M[i]);
      SCIPdbldblSum21_DBLDBL(tmp, tmp, liftingdata->ml);

      if( SCIPisLT(scip, DBLDBL_ROUND(tmp), xpluslambda) )
         return i * liftingdata->lambda;

      assert(SCIPisLE(scip, liftingdata->M[i], xpluslambda) &&
             SCIPisLE(scip, xpluslambda, liftingdata->M[i] + liftingdata->ml + p));

      SCIPdbldblProd_DBLDBL(tmp, i, liftingdata->lambda);
      SCIPdbldblSum21_DBLDBL(tmp, tmp, x);
      SCIPdbldblSum21_DBLDBL(tmp, tmp, - liftingdata->M[i]);
      return DBLDBL_ROUND(tmp);
   }

   assert(i == liftingdata->r && SCIPisLE(scip, liftingdata->M[liftingdata->r], xpluslambda));

   SCIPdbldblProd_DBLDBL(tmp, liftingdata->r, liftingdata->lambda);
   SCIPdbldblSum21_DBLDBL(tmp, tmp, x);
   SCIPdbldblSum21_DBLDBL(tmp, tmp, - liftingdata->M[liftingdata->r]);
   return DBLDBL_ROUND(tmp);
}

static
void getAlphaAndBeta(
   SCIP*                 scip,               /**< SCIP data structure */
   LIFTINGDATA*          liftingdata,        /**< pointer to lifting function struct */
   SCIP_Real             vubcoef,            /**< vub coefficient to get alpha and beta for */
   int*                  alpha,              /**< get alpha coefficient for lifting */
   SCIP_Real*            beta                /**< get beta coefficient for lifting */
   )
{
   SCIP_Real vubcoefpluslambda;
   int i;

   vubcoefpluslambda = vubcoef + liftingdata->lambda;

   i = 0;
   while( i < liftingdata->r && SCIPisGT(scip, vubcoefpluslambda, liftingdata->M[i+1]) )
      ++i;

   if( SCIPisLT(scip, vubcoef, liftingdata->M[i]) )
   {
      SCIP_Real DBLDBL(tmp);
      assert(liftingdata->M[i] < vubcoefpluslambda);
      *alpha = 1;
      SCIPdbldblProd_DBLDBL(tmp, -i, liftingdata->lambda);
      SCIPdbldblSum21_DBLDBL(tmp, tmp, liftingdata->M[i]);
      *beta = DBLDBL_ROUND(tmp);
   }
   else
   {
      assert(SCIPisSumLE(scip, liftingdata->M[i], vubcoef));
      assert(i == liftingdata->r || SCIPisLE(scip, vubcoefpluslambda, liftingdata->M[i+1]));
      *alpha = 0;
      *beta = 0.0;
   }
}

static
SCIP_RETCODE computeLiftingData(
   SCIP*                 scip,               /**< SCIP data structure */
   SNF_RELAXATION*       snf,                /**< pointer to SNF relaxation */
   int*                  transvarflowcoverstatus, /**< pointer to store whether non-binary var is in L2 (2) or not (-1 or 1) */
   SCIP_Real             lambda,             /**< lambda */
   LIFTINGDATA*          liftingdata,        /**< pointer to lifting function struct */
   SCIP_Bool*            valid               /**< is the lifting data valid */
   )
{
   int i;
   SCIP_Real DBLDBL(tmp);
   SCIP_Real DBLDBL(sumN2mC2LE);
   SCIP_Real DBLDBL(sumN2mC2GT);
   SCIP_Real DBLDBL(sumC1LE);
   SCIP_Real DBLDBL(sumC2);

   SCIP_CALL( SCIPallocBufferArray(scip, &liftingdata->m, snf->ntransvars) );

   liftingdata->r = 0;
   DBLDBL_ASSIGN(sumN2mC2LE, 0.0);
   DBLDBL_ASSIGN(sumC1LE, 0.0);
   DBLDBL_ASSIGN(sumN2mC2GT, 0.0);
   DBLDBL_ASSIGN(sumC2, 0.0);

   liftingdata->mp = SCIPinfinity(scip);

   *valid = FALSE;

   for( i = 0; i < snf->ntransvars; ++i )
   {
      int s = (snf->transvarcoefs[i] + 1) + (transvarflowcoverstatus[i] + 1)/2;

      switch(s)
      {
         case 0: /* var is in N2 \ C2 */
            assert(snf->transvarvubcoefs[i] >= 0.0);
            assert(snf->transvarcoefs[i] == -1 && transvarflowcoverstatus[i] == -1);

            if( SCIPisGT(scip, snf->transvarvubcoefs[i], lambda) )
            {
               SCIPdbldblSum21_DBLDBL(sumN2mC2GT, sumN2mC2GT, snf->transvarvubcoefs[i]);
               liftingdata->m[liftingdata->r++] = snf->transvarvubcoefs[i];
            }
            else
            {
               SCIPdbldblSum21_DBLDBL(sumN2mC2LE, sumN2mC2LE, snf->transvarvubcoefs[i]);
            }
            break;
         case 1: /* var is in C2 */
            assert(snf->transvarvubcoefs[i] > 0.0);
            assert(snf->transvarcoefs[i] == -1 && transvarflowcoverstatus[i] == 1);

            SCIPdbldblSum21_DBLDBL(sumC2, sumC2, snf->transvarvubcoefs[i]);
            break;
         case 3: /* var is in C1 */
            assert(snf->transvarcoefs[i] == 1 && transvarflowcoverstatus[i] == 1);
            assert(snf->transvarvubcoefs[i] > 0.0);

            if( SCIPisGT(scip, snf->transvarvubcoefs[i], lambda) )
            {
               liftingdata->m[liftingdata->r++] = snf->transvarvubcoefs[i];
               liftingdata->mp = MIN(liftingdata->mp, snf->transvarvubcoefs[i]);
            }
            else
            {
               SCIPdbldblSum21_DBLDBL(sumC1LE, sumC1LE, snf->transvarvubcoefs[i]);
            }
            break;
      }
   }

   if( SCIPisInfinity(scip, liftingdata->mp) )
   {
      SCIPfreeBufferArray(scip, &liftingdata->m);
      return SCIP_OKAY;
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &liftingdata->M, liftingdata->r + 1) );

   *valid = TRUE;

   SCIPdbldblSum22_DBLDBL(tmp, sumC1LE, sumN2mC2LE);
   liftingdata->ml = MIN(lambda, DBLDBL_ROUND(tmp));
   SCIPdbldblSum21_DBLDBL(tmp, sumC2, snf->transrhs);
   liftingdata->d1 = DBLDBL_ROUND(tmp);
   SCIPdbldblSum22_DBLDBL(tmp, tmp, sumN2mC2GT);
   SCIPdbldblSum22_DBLDBL(tmp, tmp, sumN2mC2LE);
   liftingdata->d2 = DBLDBL_ROUND(tmp);

   SCIPsortDownReal(liftingdata->m, liftingdata->r);

   /* compute M[i] = sum_{i \in [1,r]} m[i] where m[*] is sorted decreasingly and M[0] = 0 */
   DBLDBL_ASSIGN(tmp, 0.0);
   for( i = 0; i < liftingdata->r; ++i)
   {
      liftingdata->M[i] = DBLDBL_ROUND(tmp);
      SCIPdbldblSum21_DBLDBL(tmp, tmp, liftingdata->m[i]);
   }

   liftingdata->M[liftingdata->r] = DBLDBL_ROUND(tmp);

   SCIP_UNUSED( SCIPsortedvecFindDownReal(liftingdata->m, liftingdata->mp, liftingdata->r, &liftingdata->t) );
   assert(liftingdata->m[liftingdata->t] == liftingdata->mp || SCIPisInfinity(scip, liftingdata->mp));

   /* compute t largest index sucht that m_t = mp
    * note that liftingdata->m[t-1] == mp due to zero based indexing of liftingdata->m
    */
   ++liftingdata->t;
   while( liftingdata->t < liftingdata->r && liftingdata->m[liftingdata->t] == liftingdata->mp )
      ++liftingdata->t;

   liftingdata->lambda = lambda;

   return SCIP_OKAY;
}

static
void destroyLiftingData(
   SCIP*                 scip,               /**< SCIP data structure */
   LIFTINGDATA*          liftingdata         /**< pointer to lifting function struct */
   )
{
   SCIPfreeBufferArray(scip, &liftingdata->M);
   SCIPfreeBufferArray(scip, &liftingdata->m);
}

static
SCIP_RETCODE generateLiftedFlowCoverCut(
   SCIP*                 scip,               /**< SCIP data structure */
   SNF_RELAXATION*       snf,                /**< pointer to SNF relaxation */
   SCIP_AGGRROW*         aggrrow,            /**< aggrrow used to construct SNF relaxation */
   int*                  flowcoverstatus,    /**< pointer to store whether variable is in flow cover (+1) or not (-1) */
   SCIP_Real             lambda,             /**< lambda */
   SCIP_Real*            cutcoefs,           /**< array of coefficients of cut */
   SCIP_Real*            cutrhs,             /**< pointer to right hand side of cut */
   int*                  cutinds,            /**< array of variables problem indices for non-zero coefficients in cut */
   int*                  nnz,                /**< number of non-zeros in cut */
   SCIP_Bool*            success             /**< was the cut successfully generated */
   )
{
   SCIP_Real DBLDBL(rhs);
   LIFTINGDATA liftingdata;
   int i;

   SCIP_CALL( computeLiftingData(scip, snf, flowcoverstatus, lambda, &liftingdata, success) );
   if( ! *success )
      return SCIP_OKAY;

   DBLDBL_ASSIGN(rhs, liftingdata.d1);
   *nnz = 0;

   for( i = 0; i < snf->ntransvars; ++i )
   {
      int s = (snf->transvarcoefs[i] + 1) + (flowcoverstatus[i] + 1)/2;

      switch(s)
      {
         case 0: /* var is in N2 \ C2 */
            if( SCIPisGT(scip, snf->transvarvubcoefs[i], lambda) )
            {
               /* var is in L- */
               if( snf->origbinvars[i] != -1 )
               {
                  cutinds[*nnz] = snf->origbinvars[i];
                  cutcoefs[*nnz] = -lambda;
                  ++(*nnz);
               }
               else
               {
                  SCIPdbldblSum21_DBLDBL(rhs, rhs, lambda);
               }
            }
            else
            {
               /* var is in L-- */
               if( snf->origcontvars[i] != -1 )
               {
                  cutinds[*nnz] = snf->origcontvars[i];
                  cutcoefs[*nnz] = -snf->aggrcoefscont[i];
                  ++(*nnz);
               }

               if( snf->origbinvars[i] != -1 )
               {
                  cutinds[*nnz] = snf->origbinvars[i];
                  cutcoefs[*nnz] = -snf->aggrcoefsbin[i];
                  ++(*nnz);
               }

               SCIPdbldblSum21_DBLDBL(rhs, rhs, snf->aggrconstants[i]);
            }
            break;
         case 1: /* var is in C2 */
         {
            assert(snf->transvarvubcoefs[i] > 0.0);
            assert(snf->transvarcoefs[i] == -1 && flowcoverstatus[i] == 1);

            if( snf->origbinvars[i] != -1 )
            {
               SCIP_Real liftedbincoef = evaluateLiftingFunction(scip, &liftingdata, snf->transvarvubcoefs[i]);
               cutinds[*nnz] = snf->origbinvars[i];
               cutcoefs[*nnz] = -liftedbincoef;
               ++(*nnz);
               SCIPdbldblSum21_DBLDBL(rhs, rhs, -liftedbincoef);
            }
            break;
         }
         case 2: /* var is in N1 \ C1 */
         {
            int alpha;
            SCIP_Real beta;

            assert(snf->transvarcoefs[i] == 1 && flowcoverstatus[i] == -1);

            getAlphaAndBeta(scip, &liftingdata, snf->transvarvubcoefs[i], &alpha, &beta);
            assert(alpha == 0 || alpha == 1);

            if( alpha == 1 )
            {
               SCIP_Real DBLDBL(binvarcoef);
               assert(beta > 0.0);

               if( snf->origcontvars[i] != -1 )
               {
                  cutinds[*nnz] = snf->origcontvars[i];
                  cutcoefs[*nnz] = snf->aggrcoefscont[i];
                  ++(*nnz);
               }

               SCIPdbldblSum_DBLDBL(binvarcoef, snf->aggrcoefsbin[i], -beta);
               if( snf->origbinvars[i] != -1 )
               {
                  cutinds[*nnz] = snf->origbinvars[i];
                  cutcoefs[*nnz] = DBLDBL_ROUND(binvarcoef);
                  ++(*nnz);
               }
               else
               {
                  SCIPdbldblSum22_DBLDBL(rhs, rhs, -binvarcoef);
               }

               SCIPdbldblSum21_DBLDBL(rhs, rhs, -snf->aggrconstants[i]);
            }
            break;
         }
         case 3: /* var is in C1 */
         {
            SCIP_Real bincoef = snf->aggrcoefsbin[i];
            SCIP_Real constant = snf->aggrconstants[i];

            if( snf->origbinvars[i] != -1 && SCIPisGT(scip, snf->transvarvubcoefs[i], lambda) )
            {
               /* var is in C++ */
               SCIP_Real DBLDBL(tmp);
               SCIP_Real DBLDBL(tmp2);

               SCIPdbldblSum_DBLDBL(tmp, snf->transvarvubcoefs[i], -lambda);

               SCIPdbldblSum21_DBLDBL(tmp2, tmp, constant);
               constant = DBLDBL_ROUND(tmp2);

               SCIPdbldblSum21_DBLDBL(tmp2, tmp, -bincoef);
               bincoef = -DBLDBL_ROUND(tmp2);
            }

            if( snf->origbinvars[i] != -1 )
            {
               cutinds[*nnz] = snf->origbinvars[i];
               cutcoefs[*nnz] = bincoef;
               ++(*nnz);
            }

            if( snf->origcontvars[i] != -1 )
            {
               cutinds[*nnz] = snf->origcontvars[i];
               cutcoefs[*nnz] = snf->aggrcoefscont[i];
               ++(*nnz);
            }

            SCIPdbldblSum21_DBLDBL(rhs, rhs, -constant);

            break;
         }
      }
   }

   destroyLiftingData(scip, &liftingdata);

   {
      SCIP_ROW** rows = SCIPgetLPRows(scip);
      for( i = 0; i < aggrrow->nrows; ++i )
      {
         SCIP_ROW* row;
         SCIP_Real slackcoef = aggrrow->rowweights[i] * aggrrow->slacksign[i];
         assert(slackcoef != 0.0);

         /* positive slack was implicitly handled in flow cover separation */
         if( slackcoef > 0.0 )
            continue;

         row = rows[aggrrow->rowsinds[i]];

         /* add the slack's definition multiplied with its coefficient to the cut */
         SCIP_CALL( varVecAddScaledRowCoefs(scip, &cutinds, &cutcoefs, nnz, NULL, row, -aggrrow->rowweights[i]) );

         /* move slack's constant to the right hand side */
         if( aggrrow->slacksign[i] == +1 )
         {
            SCIP_Real DBLDBL(rowrhs);

            /* a*x + c + s == rhs  =>  s == - a*x - c + rhs: move a^_r * (rhs - c) to the right hand side */
            assert(!SCIPisInfinity(scip, row->rhs));
            SCIPdbldblSum_DBLDBL(rowrhs, row->rhs, - row->constant);
            if( row->integral )
            {
               /* the right hand side was implicitly rounded down in row aggregation */
               DBLDBL_ASSIGN(rowrhs, SCIPfeasFloor(scip, DBLDBL_ROUND(rowrhs)));
            }
            SCIPdbldblProd21_DBLDBL(rowrhs, rowrhs, -aggrrow->rowweights[i]);
            SCIPdbldblSum22_DBLDBL(rhs, rhs, rowrhs);
         }
         else
         {
            SCIP_Real DBLDBL(rowlhs);

            /* a*x + c - s == lhs  =>  s == a*x + c - lhs: move a^_r * (c - lhs) to the right hand side */
            assert(!SCIPisInfinity(scip, -row->lhs));
            SCIPdbldblSum_DBLDBL(rowlhs, row->lhs, - row->constant);
            if( row->integral )
            {
               /* the left hand side was implicitly rounded up in row aggregation */
               DBLDBL_ASSIGN(rowlhs, SCIPfeasCeil(scip, DBLDBL_ROUND(rowlhs)));
            }
            SCIPdbldblProd21_DBLDBL(rowlhs, rowlhs, -aggrrow->rowweights[i]);
            SCIPdbldblSum22_DBLDBL(rhs, rhs, rowlhs);
         }
      }
   }

   *cutrhs = DBLDBL_ROUND(rhs);
     /* set rhs to zero, if it's very close to */
   if( SCIPisZero(scip, *cutrhs) )
      *cutrhs = 0.0;

   return SCIP_OKAY;
}

/** calculates a lifted simple generalized flow cover cut out of the weighted sum of LP rows given by an aggregation row; the
 *  aggregation row must not contain non-zero weights for modifiable rows, because these rows cannot
 *  participate in an MIR cut.
 *  For further details we refer to:
 *
 *  Gu, Z., Nemhauser, G. L., & Savelsbergh, M. W. (1999). Lifted flow cover inequalities for mixed 0-1 integer programs.
 *  Mathematical Programming, 85(3), 439-467.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if @p scip is in one of the following stages:
 *       - \ref SCIP_STAGE_SOLVING
 *
 *  See \ref SCIP_Stage "SCIP_STAGE" for a complete list of all possible solving stages.
 */
SCIP_RETCODE SCIPcalcFlowCover(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Real             boundswitch,        /**< fraction of domain up to which lower bound is used in transformation */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   SCIP_AGGRROW*         aggrrow,            /**< the aggregation row to compute flow cover cut for */
   SCIP_Real*            cutcoefs,           /**< array to store the non-zero coefficients in the cut */
   SCIP_Real*            cutrhs,             /**< pointer to store the right hand side of the cut */
   int*                  cutinds,            /**< array to store the problem indices of variables with a non-zero coefficient in the cut */
   int*                  cutnnz,             /**< pointer to store the number of non-zeros in the cut */
   SCIP_Real*            cutefficacy,        /**< pointer to store the efficacy of the cut, or NULL */
   int*                  cutrank,            /**< pointer to return rank of generated cut */
   SCIP_Bool*            cutislocal,         /**< pointer to store whether the generated cut is only valid locally */
   SCIP_Bool*            success             /**< pointer to store whether a valid cut was returned */
   )
{
   int nvars;
   SCIP_Bool localbdsused;
   SNF_RELAXATION snf;
   SCIP_Real lambda;
   int *transvarflowcoverstatus;
   int nflowcovervars;
   int nnonflowcovervars;

   nvars = SCIPgetNVars(scip);

   *success = FALSE;

   /* get data structures */
   SCIP_CALL( SCIPallocBufferArray(scip, &transvarflowcoverstatus, nvars) );
   SCIP_CALL( allocSNFRelaxation(scip,  &snf, nvars) );

   *cutrhs = aggrrow->rhs;
   *cutnnz = aggrrow->nnz;
   *cutislocal = aggrrow->local;
   BMScopyMemoryArray(cutinds, aggrrow->inds, *cutnnz);
   BMScopyMemoryArray(cutcoefs, aggrrow->vals, *cutnnz);

   cleanupCut(scip, *cutislocal, cutinds, cutcoefs, cutnnz, cutrhs);

   SCIP_CALL( constructSNFRelaxation(scip, sol, boundswitch, allowlocal, cutcoefs, *cutrhs, cutinds, *cutnnz, &snf, success, &localbdsused) );

   if( ! *success )
   {
      goto TERMINATE;
   }

   *cutislocal = *cutislocal || localbdsused;

   SCIP_CALL( getFlowCover(scip, &snf, &nflowcovervars, &nnonflowcovervars, transvarflowcoverstatus, &lambda, success) );

   if( ! *success )
   {
      goto TERMINATE;
   }

   SCIP_CALL( generateLiftedFlowCoverCut(scip, &snf, aggrrow, transvarflowcoverstatus, lambda, cutcoefs, cutrhs, cutinds, cutnnz, success) );


   if( *success )
   {
      cleanupCut(scip, *cutislocal, cutinds, cutcoefs, cutnnz, cutrhs);

      if( cutefficacy != NULL )
         *cutefficacy = calcEfficacy(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz);

      if( cutrank != NULL )
         *cutrank = aggrrow->rank + 1;
   }

TERMINATE:
   destroySNFRelaxation(scip, &snf);
   SCIPfreeBufferArray(scip, &transvarflowcoverstatus);

   return SCIP_OKAY;
}

/* =========================================== strongcg =========================================== */

/** Transform equation \f$ a \cdot x = b; lb \leq x \leq ub \f$ into standard form
 *    \f$ a^\prime \cdot x^\prime = b,\; 0 \leq x^\prime \leq ub' \f$.
 *
 *  Transform variables (lb or ub):
 *  \f[
 *  \begin{array}{llll}
 *    x^\prime_j := x_j - lb_j,&   x_j = x^\prime_j + lb_j,&   a^\prime_j =  a_j,&   \mbox{if lb is used in transformation}\\
 *    x^\prime_j := ub_j - x_j,&   x_j = ub_j - x^\prime_j,&   a^\prime_j = -a_j,&   \mbox{if ub is used in transformation}
 *  \end{array}
 *  \f]
 *  and move the constant terms \f$ a_j\, lb_j \f$ or \f$ a_j\, ub_j \f$ to the rhs.
 *
 *  Transform variables (vlb or vub):
 *  \f[
 *  \begin{array}{llll}
 *    x^\prime_j := x_j - (bl_j\, zl_j + dl_j),&   x_j = x^\prime_j + (bl_j\, zl_j + dl_j),&   a^\prime_j =  a_j,&   \mbox{if vlb is used in transf.} \\
 *    x^\prime_j := (bu_j\, zu_j + du_j) - x_j,&   x_j = (bu_j\, zu_j + du_j) - x^\prime_j,&   a^\prime_j = -a_j,&   \mbox{if vub is used in transf.}
 *  \end{array}
 *  \f]
 *  move the constant terms \f$ a_j\, dl_j \f$ or \f$ a_j\, du_j \f$ to the rhs, and update the coefficient of the VLB variable:
 *  \f[
 *  \begin{array}{ll}
 *    a_{zl_j} := a_{zl_j} + a_j\, bl_j,& \mbox{or} \\
 *    a_{zu_j} := a_{zu_j} + a_j\, bu_j &
 *  \end{array}
 *  \f]
 */
static
SCIP_RETCODE cutsTransformStrongCG(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Real             boundswitch,        /**< fraction of domain up to which lower bound is used in transformation */
   SCIP_Bool             usevbds,            /**< should variable bounds be used in bound transformation? */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   SCIP_Real*            cutcoefs,           /**< array of coefficients of cut */
   SCIP_Real*            cutrhs,             /**< pointer to right hand side of cut */
   int*                  cutinds,            /**< array of variables problem indices for non-zero coefficients in cut */
   int*                  nnz,                /**< number of non-zeros in cut */
   int*                  varsign,            /**< stores the sign of the transformed variable in summation */
   int*                  boundtype,          /**< stores the bound used for transformed variable:
                                              *   vlb/vub_idx, or -1 for global lb/ub, or -2 for local lb/ub */
   SCIP_Bool*            freevariable,       /**< stores whether a free variable was found in MIR row -> invalid summation */
   SCIP_Bool*            localbdsused        /**< pointer to store whether local bounds were used in transformation */
   )
{
   SCIP_Real* bestbds;
   int* varpos;
   int i;
   int aggrrowintstart;
   int nvars;
   int firstcontvar;
   SCIP_VAR** vars;

   assert(varsign != NULL);
   assert(boundtype != NULL);
   assert(freevariable != NULL);
   assert(localbdsused != NULL);

   *freevariable = FALSE;
   *localbdsused = FALSE;

   /* allocate temporary memory to store best bounds and bound types */
   SCIP_CALL( SCIPallocBufferArray(scip, &bestbds, 2*(*nnz)) );

   /* start with continuous variables, because using variable bounds can affect the untransformed integral
    * variables, and these changes have to be incorporated in the transformation of the integral variables
    * (continuous variables have largest problem indices!)
    */
   SCIPsortDownIntReal(cutinds, cutcoefs, *nnz);

   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);
   firstcontvar = nvars - SCIPgetNContVars(scip);

   /* determine best bounds for the continous variables such that they will have a positive coefficient in the transformation */
   for( i = 0; i < *nnz && cutinds[i] >= firstcontvar; ++i )
   {
      if( cutcoefs[i] > 0.0 )
      {
         /* find closest lower bound in standard lower bound or variable lower bound for continuous variable so that it will have a positive coefficient */
         SCIP_CALL( findBestLb(scip, vars[cutinds[i]], sol, usevbds, allowlocal, bestbds + i, boundtype + i) );

         /* cannot create transformation for strongcg cut */
         if( SCIPisInfinity(scip, -bestbds[i]) )
         {
            *freevariable = TRUE;
            goto TERMINATE;
         }

         varsign[i] = +1;
      }
      else if( cutcoefs[i] < 0.0 )
      {
         /* find closest upper bound in standard upper bound or variable upper bound for continuous variable so that it will have a positive coefficient */
         SCIP_CALL( findBestUb(scip, vars[cutinds[i]], sol, usevbds, allowlocal, bestbds + i, boundtype + i) );

          /* cannot create transformation for strongcg cut */
         if( SCIPisInfinity(scip, bestbds[i]) )
         {
            *freevariable = TRUE;
            goto TERMINATE;
         }

         varsign[i] = -1;
      }
   }

   /* remember start of integer variables in the aggrrow */
   aggrrowintstart = i;

   /* remember positions of integral variables */
   SCIP_CALL( SCIPallocCleanBufferArray(scip, &varpos, firstcontvar) );

   for( i = (*nnz) - 1; i >= aggrrowintstart; --i )
      varpos[cutinds[i]] = i + 1;

   /* perform bound substitution for continuous variables */
   for( i = 0; i < aggrrowintstart; ++i )
   {
      SCIP_VAR* var = vars[cutinds[i]];
      assert(!SCIPisInfinity(scip, -varsign[i] * bestbds[i]));

      /* standard (bestlbtype < 0) or variable (bestlbtype >= 0) lower bound? */
      if( boundtype[i] < 0 )
      {
         *cutrhs -= cutcoefs[i] * bestbds[i];
         *localbdsused = *localbdsused || (boundtype[i] == -2);
      }
      else
      {
         SCIP_VAR** vbdvars;
         SCIP_Real* vbdcoefs;
         SCIP_Real* vbdconsts;
         int zidx;
         int k;

         if( varsign[i] == +1 )
         {
            vbdvars = SCIPvarGetVlbVars(var);
            vbdcoefs = SCIPvarGetVlbCoefs(var);
            vbdconsts = SCIPvarGetVlbConstants(var);
            assert(0 <= boundtype[i] && boundtype[i] < SCIPvarGetNVlbs(var));
         }
         else
         {
            vbdvars = SCIPvarGetVubVars(var);
            vbdcoefs = SCIPvarGetVubCoefs(var);
            vbdconsts = SCIPvarGetVubConstants(var);
            assert(0 <= boundtype[i] && boundtype[i] < SCIPvarGetNVubs(var));
         }

         assert(vbdvars != NULL);
         assert(vbdcoefs != NULL);
         assert(vbdconsts != NULL);
         assert(SCIPvarIsActive(vbdvars[boundtype[i]]));

         zidx = SCIPvarGetProbindex(vbdvars[boundtype[i]]);
         assert(0 <= zidx && zidx < firstcontvar);

         *cutrhs -= cutcoefs[i] * vbdconsts[boundtype[i]];

         /* check if integral variable already exists in the row */
         k = varpos[zidx];
         if( k == 0 )
         {
            /* if not add it to the end */
            k = (*nnz)++;
            varpos[zidx] = *nnz;
            cutinds[k] = zidx;
            cutcoefs[k] = cutcoefs[i] * vbdcoefs[boundtype[i]];
         }
         else
         {
            /* if it is update the coefficient */
            assert(cutinds[k - 1] == zidx);
            cutcoefs[k - 1] += cutcoefs[i] * vbdcoefs[boundtype[i]];
         }
      }
   }

   assert(i == aggrrowintstart);

   /* remove integral variables that now have a zero coefficient due to variable bound usage of continuous variables
    * and perform the bound substitution for the integer variables that are left using simple bounds
    */
   while( i < *nnz )
   {
      SCIP_Real bestlb;
      SCIP_Real bestub;
      int bestlbtype;
      int bestubtype;
      SCIP_BOUNDTYPE selectedbound;

      assert(cutinds[i] < firstcontvar);

      /* clean the varpos array for each integral variable */
      varpos[cutinds[i]] = 0;

      /* due to variable bound usage for the continous variables cancellation may have occurred */
      if( SCIPisZero(scip, cutcoefs[i]) )
      {
         --(*nnz);
         if( i < *nnz )
         {
            cutcoefs[i] = cutcoefs[*nnz];
            cutinds[i] = cutinds[*nnz];
         }
         /* do not increase i, since last element is copied to the i-th position */
         continue;
      }

      /** determine the best bounds for the integral variable, usevbd can be set to FALSE here as vbds are only used for continous variables */
      SCIP_CALL( determineBestBounds(scip, vars[cutinds[i]], sol, boundswitch, FALSE, allowlocal, FALSE, FALSE, NULL, NULL,
                                     &bestlb, &bestub, &bestlbtype, &bestubtype, &selectedbound, freevariable) );

      /* check if we have an unbounded integral variable */
      if( *freevariable )
      {
         /* clean varpos array for remainging variables and terminate */
         while( ++i < *nnz )
            varpos[cutinds[i]] = 0;

         SCIPfreeCleanBufferArray(scip, &varpos);
         goto TERMINATE;
      }

      /* perform bound substitution */
      if( selectedbound == SCIP_BOUNDTYPE_LOWER )
      {
         boundtype[i] = bestlbtype;
         varsign[i] = +1;
         *cutrhs -= cutcoefs[i] * bestlb;
      }
      else
      {
         assert(selectedbound == SCIP_BOUNDTYPE_UPPER);
         boundtype[i] = bestubtype;
         varsign[i] = -1;
         *cutrhs -= cutcoefs[i] * bestub;
      }

      assert(boundtype[i] == -1 || boundtype[i] == -2);
      *localbdsused = *localbdsused || (boundtype[i] == -2);

      /* increase i */
      ++i;
   }

   /* varpos array is not needed any more and has been cleaned in the previous loop */
   SCIPfreeCleanBufferArray(scip, &varpos);

 TERMINATE:

   /*free temporary memory */
   SCIPfreeBufferArray(scip, &bestbds);

   return SCIP_OKAY;
}

/** Calculate fractionalities \f$ f_0 := b - down(b) \f$, \f$ f_j := a^\prime_j - down(a^\prime_j) \f$ and
 *   integer \f$ k >= 1 \f$ with \f$ 1/(k + 1) <= f_0 < 1/k \f$ and \f$ (=> k = up(1/f_0) + 1) \f$
 *   integer \f$ 1 <= p_j <= k \f$ with \f$ f_0 + ((p_j - 1) * (1 - f_0)/k) < f_j <= f_0 + (p_j * (1 - f_0)/k)\f$ \f$ (=> p_j = up( k*(f_j - f_0)/(1 - f_0) )) \f$
 * and derive strong CG cut \f$ \tilde{a}*x^\prime <= down(b) \f$
 * \f[
 * \begin{array}{rll}
 * integers : &  \tilde{a}_j = down(a^\prime_j)                &, if \qquad f_j <= f_0 \\
 *            &  \tilde{a}_j = down(a^\prime_j) + p_j/(k + 1)  &, if \qquad f_j >  f_0 \\
 * continuous:&  \tilde{a}_j = 0                               &, if \qquad a^\prime_j >= 0 \\
 *            &  \mbox{no strong CG cut found}                 &, if \qquad a^\prime_j <  0
 * \end{array}
 * \f]
 *
 * Transform inequality back to \f$ \hat{a}*x <= rhs \f$:
 *
 *  (lb or ub):
 * \f[
 * \begin{array}{lllll}
 *    x^\prime_j := x_j - lb_j,&   x_j == x^\prime_j + lb_j,&   a^\prime_j ==  a_j,&   \hat{a}_j :=  \tilde{a}_j,&   \mbox{if lb was used in transformation} \\
 *    x^\prime_j := ub_j - x_j,&   x_j == ub_j - x^\prime_j,&   a^\prime_j == -a_j,&   \hat{a}_j := -\tilde{a}_j,&   \mbox{if ub was used in transformation}
 * \end{array}
 * \f]
 * \f[
 *  and move the constant terms
 * \begin{array}{rl}
 *    -\tilde{a}_j * lb_j == -\hat{a}_j * lb_j, & \mbox{or} \\
 *     \tilde{a}_j * ub_j == -\hat{a}_j * ub_j &
 * \end{array}
 * \f]
 *  to the rhs.
 *
 *  (vlb or vub):
 * \f[
 * \begin{array}{lllll}
 *    x^\prime_j := x_j - (bl_j * zl_j + dl_j),&   x_j == x^\prime_j + (bl_j * zl_j + dl_j),&   a^\prime_j ==  a_j,&   \hat{a}_j :=  \tilde{a}_j,&   \mbox{(vlb)} \\
 *    x^\prime_j := (bu_j * zu_j + du_j) - x_j,&   x_j == (bu_j * zu_j + du_j) - x^\prime_j,&   a^\prime_j == -a_j,&   \hat{a}_j := -\tilde{a}_j,&   \mbox{(vub)}
 * \end{array}
 * \f]
 *  move the constant terms
 * \f[
 * \begin{array}{rl}
 *    -\tilde{a}_j * dl_j == -\hat{a}_j * dl_j,& \mbox{or} \\
 *     \tilde{a}_j * du_j == -\hat{a}_j * du_j &
 * \end{array}
 * \f]
 *  to the rhs, and update the VB variable coefficients:
 * \f[
 * \begin{array}{ll}
 *    \hat{a}_{zl_j} := \hat{a}_{zl_j} - \tilde{a}_j * bl_j == \hat{a}_{zl_j} - \hat{a}_j * bl_j,& \mbox{or} \\
 *    \hat{a}_{zu_j} := \hat{a}_{zu_j} + \tilde{a}_j * bu_j == \hat{a}_{zu_j} - \hat{a}_j * bu_j &
 * \end{array}
 * \f]
 */
static
SCIP_RETCODE cutsRoundStrongCG(
   SCIP*                 scip,               /**< SCIP datastructure */
   SCIP_Real*            cutcoefs,           /**< array of coefficients of cut */
   SCIP_Real*            cutrhs,             /**< pointer to right hand side of cut */
   int*                  cutinds,            /**< array of variables problem indices for non-zero coefficients in cut */
   int*                  nnz,                /**< number of non-zeros in cut */
   int*                  varsign,            /**< stores the sign of the transformed variable in summation */
   int*                  boundtype,          /**< stores the bound used for transformed variable (vlb/vub_idx or -1 for lb/ub)*/
   SCIP_Real             f0,                 /**< fractional value of rhs */
   SCIP_Real             k                   /**< factor to strengthen strongcg cut */
   )
{
   SCIP_Real onedivoneminusf0;
   int i;
   int firstcontvar;
   SCIP_VAR** vars;
   int aggrrowintstart;

   assert(cutrhs != NULL);
   assert(cutcoefs != NULL);
   assert(cutinds != NULL);
   assert(nnz != NULL);
   assert(boundtype != NULL);
   assert(varsign != NULL);
   assert(0.0 < f0 && f0 < 1.0);

   onedivoneminusf0 = 1.0 / (1.0 - f0);

   /* Loop backwards to process integral variables first and be able to delete coefficients of integral variables
    * without destroying the ordering of the aggrrow's non-zeros.
    * (due to sorting in cutsTransformStrongCG the ordering is continuous before integral)
    */

   firstcontvar = SCIPgetNVars(scip) - SCIPgetNContVars(scip);
   vars = SCIPgetVars(scip);
#ifndef NDEBUG
   /*in debug mode check, that all continuous variables of the aggrrow come before the integral variables */
   i = 0;
   while( i < *nnz && cutinds[i] >= firstcontvar )
      ++i;

   while( i < *nnz )
   {
      assert(cutinds[i] < firstcontvar);
      ++i;
   }
#endif

   /* integer variables */
   for( i = *nnz - 1; i >= 0 && cutinds[i] < firstcontvar; --i )
   {
      SCIP_VAR* var;
      SCIP_Real aj;
      SCIP_Real downaj;
      SCIP_Real cutaj;
      SCIP_Real fj;
      int v;

      v = cutinds[i];
      assert(0 <= v && v < SCIPgetNVars(scip));

      var = vars[v];
      assert(var != NULL);
      assert(SCIPvarGetProbindex(var) == v);
      assert(boundtype[i] == -1 || boundtype[i] == -2);
      assert(varsign[i] == +1 || varsign[i] == -1);

      /* calculate the coefficient in the retransformed cut */
      aj = varsign[i] * cutcoefs[i]; /* a'_j */
      downaj = SCIPfloor(scip, aj);
      fj = aj - downaj;

      if( SCIPisSumLE(scip, fj, f0) )
         cutaj = varsign[i] * downaj; /* a^_j */
      else
      {
         SCIP_Real pj;

         pj = SCIPceil(scip, k * (fj - f0) * onedivoneminusf0);
         assert(pj >= 0); /* should be >= 1, but due to rounding bias can be 0 if fj almost equal to f0 */
         assert(pj <= k);
         cutaj = varsign[i] * (downaj + pj / (k + 1)); /* a^_j */
      }

      /* remove zero cut coefficients from cut */
      if( SCIPisZero(scip, cutaj) )
      {
         --*nnz;
         if( i < *nnz )
         {
            cutinds[i] = cutinds[*nnz];
            cutcoefs[i] = cutcoefs[*nnz];
         }
         continue;
      }

      cutcoefs[i] = cutaj;

       /* integral var uses standard bound */
      assert(boundtype[i] < 0);

      /* move the constant term  -a~_j * lb_j == -a^_j * lb_j , or  a~_j * ub_j == -a^_j * ub_j  to the rhs */
      if( varsign[i] == +1 )
      {
         /* lower bound was used */
         if( boundtype[i] == -1 )
         {
            assert(!SCIPisInfinity(scip, -SCIPvarGetLbGlobal(var)));
            *cutrhs += cutaj * SCIPvarGetLbGlobal(var);
         }
         else
         {
            assert(!SCIPisInfinity(scip, -SCIPvarGetLbLocal(var)));
            *cutrhs += cutaj * SCIPvarGetLbLocal(var);
         }
      }
      else
      {
          /* upper bound was used */
         if( boundtype[i] == -1 )
         {
            assert(!SCIPisInfinity(scip, SCIPvarGetUbGlobal(var)));
            *cutrhs += cutaj * SCIPvarGetUbGlobal(var);
         }
         else
         {
            assert(!SCIPisInfinity(scip, SCIPvarGetUbLocal(var)));
            *cutrhs += cutaj * SCIPvarGetUbLocal(var);
         }
      }
   }

   /* now process the continuous variables; postpone deletetion of zeros till all continuous variables have been processed */
   aggrrowintstart = i + 1;

#ifndef NDEBUG
      /* in a strong CG cut, cut coefficients of continuous variables are always zero; check this in debug mode */
   for( i = 0; i < aggrrowintstart; ++i )
   {
      int v;

      v = cutinds[i];
      assert(firstcontvar <= v && v < SCIPgetNVars(scip));

      {
         SCIP_VAR* var;
         SCIP_Real aj;

         var = vars[v];
         assert(var != NULL);
         assert(!SCIPvarIsIntegral(var));
         assert(SCIPvarGetProbindex(var) == v);
         assert(varsign[i] == +1 || varsign[i] == -1);

         /* calculate the coefficient in the retransformed cut */
         aj = varsign[i] * cutcoefs[i]; /* a'_j */
         assert(aj >= 0.0);
      }
   }
#endif

   /* move integer variables to the empty position of the continuous variables */
   if( aggrrowintstart > 0 )
   {
      assert(aggrrowintstart <= *nnz);
      *nnz -= aggrrowintstart;
      if( *nnz < aggrrowintstart )
      {
         BMScopyMemoryArray(cutcoefs, cutcoefs + aggrrowintstart, *nnz);
         BMScopyMemoryArray(cutinds, cutinds + aggrrowintstart, *nnz);
      }
      else
      {
         BMScopyMemoryArray(cutcoefs, cutcoefs + *nnz, aggrrowintstart);
         BMScopyMemoryArray(cutinds, cutinds + *nnz, aggrrowintstart);
      }
   }

   return SCIP_OKAY;
}

/** substitute aggregated slack variables:
 *
 *  The coefficient of the slack variable s_r is equal to the row's weight times the slack's sign, because the slack
 *  variable only appears in its own row: \f$ a^\prime_r = scale * weight[r] * slacksign[r] \f$.
 *
 *  Depending on the slacks type (integral or continuous), its coefficient in the cut calculates as follows:
 * \f[
 * \begin{array}{rll}
 *    integers:  & \hat{a}_r = \tilde{a}_r = down(a^\prime_r)                  &, if \qquad f_r <= f0 \\
 *               & \hat{a}_r = \tilde{a}_r = down(a^\prime_r) + p_r/(k + 1)    &, if \qquad f_r >  f0 \\
 *    continuous:& \hat{a}_r = \tilde{a}_r = 0                                 &, if \qquad a^\prime_r >= 0 \\
 *               & \mbox{no strong CG cut found}                               &, if \qquad a^\prime_r <  0
 * \end{array}
 * \f]
 *
 *  Substitute \f$ \hat{a}_r * s_r \f$ by adding \f$ \hat{a}_r \f$ times the slack's definition to the cut.
 */
static
SCIP_RETCODE cutsSubstituteStrongCG(
   SCIP*                 scip,
   SCIP_Real*            weights,            /**< row weights in row summation */
   int*                  slacksign,          /**< stores the sign of the row's slack variable in summation */
   int*                  rowinds,            /**< sparsity pattern of used rows */
   int                   nrowinds,           /**< number of used rows */
   SCIP_Real             scale,              /**< additional scaling factor multiplied to all rows */
   SCIP_Real*            cutcoefs,           /**< array of coefficients of cut */
   SCIP_Real*            cutrhs,             /**< pointer to right hand side of cut */
   int*                  cutinds,            /**< array of variables problem indices for non-zero coefficients in cut */
   int*                  nnz,                /**< number of non-zeros in cut */
   SCIP_Real             f0,                 /**< fractional value of rhs */
   SCIP_Real             k                   /**< factor to strengthen strongcg cut */
   )
{  /*lint --e{715}*/
   SCIP_ROW** rows;
   SCIP_Real onedivoneminusf0;
   int i;

   assert(scip != NULL);
   assert(weights != NULL);
   assert(slacksign != NULL);
   assert(rowinds != NULL);
   assert(SCIPisPositive(scip, scale));
   assert(cutcoefs != NULL);
   assert(cutrhs != NULL);
   assert(cutinds != NULL);
   assert(nnz != NULL);
   assert(0.0 < f0 && f0 < 1.0);

   onedivoneminusf0 = 1.0 / (1.0 - f0);

   rows = SCIPgetLPRows(scip);
   for( i = 0; i < nrowinds; i++ )
   {
      SCIP_ROW* row;
      SCIP_Real pr;
      SCIP_Real ar;
      SCIP_Real downar;
      SCIP_Real cutar;
      SCIP_Real fr;
      SCIP_Real mul;
      int r;

      r = rowinds[i];
      assert(0 <= r && r < SCIPgetNLPRows(scip));
      assert(slacksign[i] == -1 || slacksign[i] == +1);
      assert(!SCIPisZero(scip, weights[i]));

      row = rows[r];
      assert(row != NULL);
      assert(row->len == 0 || row->cols != NULL);
      assert(row->len == 0 || row->cols_index != NULL);
      assert(row->len == 0 || row->vals != NULL);

      /* get the slack's coefficient a'_r in the aggregated row */
      ar = slacksign[i] * scale * weights[i];

      /* calculate slack variable's coefficient a^_r in the cut */
      if( row->integral )
      {
         /* slack variable is always integral: */
         downar = SCIPfloor(scip, ar);
         fr = ar - downar;

         if( SCIPisLE(scip, fr, f0) )
            cutar = downar;
         else
         {
            pr = SCIPceil(scip, k * (fr - f0) * onedivoneminusf0);
            assert(pr >= 0); /* should be >= 1, but due to rounding bias can be 0 if fr almost equal to f0 */
            assert(pr <= k);
            cutar = downar + pr / (k + 1);
         }
      }
      else
      {
         /* slack variable is continuous: */
         assert(ar >= 0.0);
         continue; /* slack can be ignored, because its coefficient is reduced to 0.0 */
      }

      /* if the coefficient was reduced to zero, ignore the slack variable */
      if( SCIPisZero(scip, cutar) )
         continue;

      /* depending on the slack's sign, we have
       *   a*x + c + s == rhs  =>  s == - a*x - c + rhs,  or  a*x + c - s == lhs  =>  s == a*x + c - lhs
       * substitute a^_r * s_r by adding a^_r times the slack's definition to the cut.
       */
      mul = -slacksign[i] * cutar;

      /* add the slack's definition multiplied with a^_j to the cut */
      SCIP_CALL( varVecAddScaledRowCoefs(scip, &cutinds, &cutcoefs, nnz, NULL, row, mul) );

      /* move slack's constant to the right hand side */
      if( slacksign[i] == +1 )
      {
         SCIP_Real rhs;

         /* a*x + c + s == rhs  =>  s == - a*x - c + rhs: move a^_r * (rhs - c) to the right hand side */
         assert(!SCIPisInfinity(scip, row->rhs));
         rhs = row->rhs - row->constant;
         if( row->integral )
         {
            /* the right hand side was implicitly rounded down in row aggregation */
            rhs = SCIPfeasFloor(scip, rhs);
         }
         *cutrhs -= cutar * rhs;
      }
      else
      {
         SCIP_Real lhs;

         /* a*x + c - s == lhs  =>  s == a*x + c - lhs: move a^_r * (c - lhs) to the right hand side */
         assert(!SCIPisInfinity(scip, -row->lhs));
         lhs = row->lhs - row->constant;
         if( row->integral )
         {
            /* the left hand side was implicitly rounded up in row aggregation */
            lhs = SCIPfeasCeil(scip, lhs);
         }
         *cutrhs += cutar * lhs;
      }
   }

   /* set rhs to zero, if it's very close to */
   if( SCIPisZero(scip, *cutrhs) )
      *cutrhs = 0.0;

   return SCIP_OKAY;
}


/** calculates a strong CG cut out of the weighted sum of LP rows given by an aggregation row; the
 *  aggregation row must not contain non-zero weights for modifiable rows, because these rows cannot
 *  participate in a strongcg cut
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if @p scip is in one of the following stages:
 *       - \ref SCIP_STAGE_SOLVING
 *
 *  See \ref SCIP_Stage "SCIP_STAGE" for a complete list of all possible solving stages.
 */
SCIP_RETCODE SCIPcalcStrongCG(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SOL*             sol,                /**< the solution that should be separated, or NULL for LP solution */
   SCIP_Real             boundswitch,        /**< fraction of domain up to which lower bound is used in transformation */
   SCIP_Bool             usevbds,            /**< should variable bounds be used in bound transformation? */
   SCIP_Bool             allowlocal,         /**< should local information allowed to be used, resulting in a local cut? */
   SCIP_Real             minfrac,            /**< minimal fractionality of rhs to produce strong CG cut for */
   SCIP_Real             maxfrac,            /**< maximal fractionality of rhs to produce strong CG cut for */
   SCIP_Real             scale,              /**< additional scaling factor multiplied to all rows */
   SCIP_AGGRROW*         aggrrow,            /**< the aggregation row to compute a flow cover cut for */
   SCIP_Real*            cutcoefs,           /**< array to store the non-zero coefficients in the cut */
   SCIP_Real*            cutrhs,             /**< pointer to store the right hand side of the cut */
   int*                  cutinds,            /**< array to store the problem indices of variables with a non-zero coefficient in the cut */
   int*                  cutnnz,             /**< pointer to store the number of non-zeros in the cut */
   SCIP_Real*            cutefficacy,        /**< pointer to store the efficacy of the cut, or NULL */
   int*                  cutrank,            /**< pointer to return rank of generated cut */
   SCIP_Bool*            cutislocal,         /**< pointer to store whether the generated cut is only valid locally */
   SCIP_Bool*            success             /**< pointer to store whether a valid cut was returned */
   )
{
   int i;
   int nvars;
   int* varsign;
   int* boundtype;
   SCIP_Real downrhs;
   SCIP_Real f0;
   SCIP_Real k;
   SCIP_Bool freevariable;
   SCIP_Bool localbdsused;

   assert(scip != NULL);
   assert(aggrrow != NULL);
   assert(SCIPisPositive(scip, scale));
   assert(cutefficacy != NULL);
   assert(cutcoefs != NULL);
   assert(cutrhs != NULL);
   assert(cutinds != NULL);
   assert(success != NULL);
   assert(cutislocal != NULL);

   SCIPdebugMessage("calculating strong CG cut (scale: %g)\n", scale);

   *success = FALSE;
   *cutislocal = FALSE;

   /* allocate temporary memory */
   nvars = SCIPgetNVars(scip);
   SCIP_CALL( SCIPallocBufferArray(scip, &varsign, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &boundtype, nvars) );

   /* initialize cut with aggregation */
   *cutnnz = aggrrow->nnz;

   BMScopyMemoryArray(cutinds, aggrrow->inds, *cutnnz);
   if( scale != 1.0 )
   {
      *cutrhs = scale * aggrrow->rhs;
      for( i = 0; i < *cutnnz; ++i )
         cutcoefs[i] = aggrrow->vals[i] * scale;
   }
   else
   {
      *cutrhs = aggrrow->rhs;
      BMScopyMemoryArray(cutcoefs, aggrrow->vals, *cutnnz);
   }

   *cutislocal = aggrrow->local;

   cleanupCut(scip, aggrrow->local, cutinds, cutcoefs, cutnnz, cutrhs);

   /* Transform equation  a*x == b, lb <= x <= ub  into standard form
    *   a'*x' == b, 0 <= x' <= ub'.
    *
    * Transform variables (lb or ub):
    *   x'_j := x_j - lb_j,   x_j == x'_j + lb_j,   a'_j ==  a_j,   if lb is used in transformation
    *   x'_j := ub_j - x_j,   x_j == ub_j - x'_j,   a'_j == -a_j,   if ub is used in transformation
    * and move the constant terms "a_j * lb_j" or "a_j * ub_j" to the rhs.
    *
    * Transform variables (vlb or vub):
    *   x'_j := x_j - (bl_j * zl_j + dl_j),   x_j == x'_j + (bl_j * zl_j + dl_j),   a'_j ==  a_j,   if vlb is used in transf.
    *   x'_j := (bu_j * zu_j + du_j) - x_j,   x_j == (bu_j * zu_j + du_j) - x'_j,   a'_j == -a_j,   if vub is used in transf.
    * move the constant terms "a_j * dl_j" or "a_j * du_j" to the rhs, and update the coefficient of the VLB variable:
    *   a_{zl_j} := a_{zl_j} + a_j * bl_j, or
    *   a_{zu_j} := a_{zu_j} + a_j * bu_j
    */
   SCIP_CALL( cutsTransformStrongCG(scip, sol, boundswitch, usevbds, allowlocal,
      cutcoefs, cutrhs, cutinds, cutnnz, varsign, boundtype, &freevariable, &localbdsused) );
   assert(allowlocal || !localbdsused);
   *cutislocal = *cutislocal || localbdsused;
   if( freevariable )
      goto TERMINATE;
   SCIPdebug(printMIR(set, stat, prob, NULL, strongcgcoef, rhs, FALSE, FALSE));

   /* Calculate
    *  - fractionalities  f_0 := b - down(b), f_j := a'_j - down(a'_j)
    *  - integer k >= 1 with 1/(k + 1) <= f_0 < 1/k
    *    (=> k = up(1/f_0) + 1)
    *  - integer 1 <= p_j <= k with f_0 + ((p_j - 1) * (1 - f_0)/k) < f_j <= f_0 + (p_j * (1 - f_0)/k)
    *    (=> p_j = up( (f_j - f_0)/((1 - f_0)/k) ))
    * and derive strong CG cut
    *   a~*x' <= (k+1) * down(b)
    * integers :  a~_j = down(a'_j)                , if f_j <= f_0
    *             a~_j = down(a'_j) + p_j/(k + 1)  , if f_j >  f_0
    * continuous: a~_j = 0                         , if a'_j >= 0
    *             no strong CG cut found          , if a'_j <  0
    *
    * Transform inequality back to a^*x <= rhs:
    *
    * (lb or ub):
    *   x'_j := x_j - lb_j,   x_j == x'_j + lb_j,   a'_j ==  a_j,   a^_j :=  a~_j,   if lb was used in transformation
    *   x'_j := ub_j - x_j,   x_j == ub_j - x'_j,   a'_j == -a_j,   a^_j := -a~_j,   if ub was used in transformation
    * and move the constant terms
    *   -a~_j * lb_j == -a^_j * lb_j, or
    *    a~_j * ub_j == -a^_j * ub_j
    * to the rhs.
    *
    * (vlb or vub):
    *   x'_j := x_j - (bl_j * zl_j + dl_j),   x_j == x'_j + (bl_j * zl_j + dl_j),   a'_j ==  a_j,   a^_j :=  a~_j,   (vlb)
    *   x'_j := (bu_j * zu_j + du_j) - x_j,   x_j == (bu_j * zu_j + du_j) - x'_j,   a'_j == -a_j,   a^_j := -a~_j,   (vub)
    * move the constant terms
    *   -a~_j * dl_j == -a^_j * dl_j, or
    *    a~_j * du_j == -a^_j * du_j
    * to the rhs, and update the VB variable coefficients:
    *   a^_{zl_j} := a^_{zl_j} - a~_j * bl_j == a^_{zl_j} - a^_j * bl_j, or
    *   a^_{zu_j} := a^_{zu_j} + a~_j * bu_j == a^_{zu_j} - a^_j * bu_j
    */
   downrhs = SCIPfloor(scip, *cutrhs);
   f0 = *cutrhs - downrhs;
   if( f0 < minfrac || f0 > maxfrac )
      goto TERMINATE;
   k = SCIPceil(scip, 1.0 / f0) - 1;

   *cutrhs = downrhs;
   SCIP_CALL( cutsRoundStrongCG(scip, cutcoefs, cutrhs, cutinds, cutnnz, varsign, boundtype, f0, k) );
   SCIPdebug(printCut(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz, FALSE, FALSE));

   /* substitute aggregated slack variables:
    *
    * The coefficient of the slack variable s_r is equal to the row's weight times the slack's sign, because the slack
    * variable only appears in its own row:
    *    a'_r = scale * weight[r] * slacksign[r].
    *
    * Depending on the slacks type (integral or continuous), its coefficient in the cut calculates as follows:
    *   integers :  a^_r = a~_r = (k + 1) * down(a'_r)        , if f_r <= f0
    *               a^_r = a~_r = (k + 1) * down(a'_r) + p_r  , if f_r >  f0
    *   continuous: a^_r = a~_r = 0                           , if a'_r >= 0
    *               a^_r = a~_r = a'_r/(1 - f0)               , if a'_r <  0
    *
    * Substitute a^_r * s_r by adding a^_r times the slack's definition to the cut.
    */
   SCIP_CALL( cutsSubstituteStrongCG(scip, aggrrow->rowweights, aggrrow->slacksign, aggrrow->rowsinds,
                          aggrrow->nrows, scale, cutcoefs, cutrhs, cutinds, cutnnz, f0, k) );
   SCIPdebug(printCut(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz, FALSE, FALSE));

   /* remove again all nearly-zero coefficients from strong CG row and relax the right hand side correspondingly in order to
    * prevent numerical rounding errors
    */
   cleanupCut(scip, *cutislocal, cutinds, cutcoefs, cutnnz, cutrhs);
   SCIPdebug(printCut(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz, FALSE, FALSE));

   *success = TRUE;

   if( cutefficacy != NULL )
      *cutefficacy = calcEfficacy(scip, sol, cutcoefs, *cutrhs, cutinds, *cutnnz);

   if( cutrank != NULL )
      *cutrank = aggrrow->rank + 1;
   *success = TRUE;

 TERMINATE:
    /* free temporary memory */
   SCIPfreeBufferArray(scip, &boundtype);
   SCIPfreeBufferArray(scip, &varsign);

   return SCIP_OKAY;
}
