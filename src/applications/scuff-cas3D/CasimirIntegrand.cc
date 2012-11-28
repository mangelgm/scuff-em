/* Copyright (C) 2005-2011 M. T. Homer Reid
 *
 * This file is part of SCUFF-EM.
 *
 * SCUFF-EM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SCUFF-EM is distributed in the hope that it will be useful,
 * but SC3DITHOUT ANY SC3DARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * CasimirIntegrand.cc    -- evaluate the casimir energy, force, and/or torque
 *                        -- integrand at a single Xi point or a single (Xi,kBloch)
 *                        -- point.
 *
 * homer reid  -- 10/2008 -- 6/2010
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>

#include "scuff-cas3D.h"
#include "libscuffInternals.h"

extern "C" {
int dgetrf_(int *m, int *n, double *a, int *lda, int *ipiv, int *info);
}

using namespace scuff;

#define II cdouble(0.0,1.0)

/***************************************************************/
/***************************************************************/
/***************************************************************/
void GCI2(SC3Data *SC3D, double Xi, double *kBloch, double *EFT)
{
  RWGGeometry *G          = SC3D->G;
  HMatrix *M              = SC3D->M;
  HVector *MInfLUDiagonal = SC3D->MInfLUDiagonal;

  /*--------------------------------------------------------------*/ 
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  cdouble Omega = cdouble(0,Xi);
  G->AssembleBEMMatrix(Omega, kBloch, M);

  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  int Offset;
  HMatrix *T;
  for(int ns=0; ns<G->NumSurfaces; ns++)
   { 
     Offset = G->BFIndexOffset[ns];
     T = SC3D->TBlocks[ns];
     SC3D->M->ExtractBlock(Offset, Offset, T);

     T->LUFactorize();
     for(int nr=0; nr<T->NR; nr++)
      MInfLUDiagonal->SetEntry( Offset + nr, T->GetEntry(nr,nr) );

   };

  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  M->LUFactorize();

  double LNDet=0.0;
  for(int n=0; n<G->TotalBFs; n++)
   LNDet += log( fabs( MInfLUDiagonal->GetEntryD(n) / M->GetEntryD(n,n) ) );

  EFT[0] = -LNDet/(2.0*M_PI);

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (SC3D->ByXikBlochFile)
   { 
     int ntnq=0;
     FILE *f=fopen(SC3D->ByXikBlochFile,"a");
     for(int nt=0; nt<SC3D->NumTransformations; nt++)
      { fprintf(f,"%s %e %e %e ",SC3D->GTCList[nt]->Tag,Xi,kBloch[0],kBloch[1]);
        for(int nq=0; nq<SC3D->NumQuantities; nq++, ntnq++) 
         fprintf(f,"%.12e ",EFT[nq]);
        fprintf(f,"\n");
      };
     fclose(f);
   };
  
}

/***************************************************************/
/* stamp T and U blocks into the BEM matrix, then LU-factorize.*/
/***************************************************************/
void Factorize(SC3Data *SC3D)
{ 
  RWGGeometry *G = SC3D->G;
  HMatrix *M = SC3D->M;

  /***************************************************************/
  /* stamp blocks into M matrix                                  */
  /***************************************************************/
  /* T blocks */
  int Offset;
  for(int ns=0; ns<G->NumSurfaces; ns++)
   { 
     Offset=G->BFIndexOffset[ns];
     M->InsertBlock(SC3D->TBlocks[ns], Offset, Offset);
   };

  /* U blocks */
  int nb=0;
  int RowOffset, ColOffset;
  for(int ns=0; ns<G->NumSurfaces; ns++)
   for(int nsp=ns+1; nsp<G->NumSurfaces; nsp++, nb++)
    { 
      RowOffset=G->BFIndexOffset[ns];
      ColOffset=G->BFIndexOffset[nsp];
      M->InsertBlock(SC3D->UBlocks[nb], RowOffset, ColOffset);
      M->InsertBlockTranspose(SC3D->UBlocks[nb], ColOffset, RowOffset);
    };

  /***************************************************************/
  /* LU factorize                                                */
  /***************************************************************/
  M->LUFactorize();

} 

/***************************************************************/
/* evaluate the casimir energy, force, and/or torque integrand */
/* at a single Xi point, or a single (Xi,kBloch) point for PBC */
/* geometries, possibly under multiple spatial transformations.*/
/*                                                             */
/* the output vector 'EFT' stands for 'energy, force, and      */
/* torque.' the output quantities are packed into this vector  */
/* in the following order:                                     */
/*                                                             */
/*  energy  integrand (spatial transformation 1)               */
/*  xforce  integrand (spatial transformation 1)               */
/*  yforce  integrand (spatial transformation 1)               */
/*  zforce  integrand (spatial transformation 1)               */
/*  torque1 integrand (spatial transformation 1)               */
/*  torque2 integrand (spatial transformation 1)               */
/*  torque3 integrand (spatial transformation 1)               */
/*  energy  integrand (spatial transformation 2)               */
/* ...                                                         */
/*                                                             */
/* where only requested quantities are present, i.e. if the    */
/* user requested only energy and yforce then we would have    */
/*                                                             */
/*  EFT[0] = energy integrand (spatial transformation 1)       */
/*  EFT[1] = yforce integrand (spatial transformation 1)       */
/*  EFT[2] = energy integrand (spatial transformation 2)       */
/*  EFT[3] = yforce integrand (spatial transformation 2)       */
/*  ...                                                        */
/*  EFT[2*N-1] = yforce integrand (spatial transformation N)   */
/***************************************************************/
void GetCasimirIntegrand(SC3Data *SC3D, double Xi, double *kBloch, double *EFT)
{ 
  if (kBloch)
   { GCI2(SC3D, Xi, kBloch, EFT);
     return;
   };
  
#if 0
  RWGGeometry *G=SC3D->G;

  /***************************************************************/
  /* record the value of Xi in the internal storage slot within SC3D*/
  /***************************************************************/
  SC3D->Xi=Xi;

  /***************************************************************/
  /* SurfaceNeverMoved[ns] is initialized to 1 and remains at 1  */
  /* as long as surface #ns is not displaced or rotated by any   */
  /* geometrical transformation, but switches to 0 as soon as    */
  /* that surface is moved, and then remains at 0 for the rest   */
  /* of the calculations done at this frequency                  */
  /***************************************************************/
  static int *SurfaceNeverMoved=0;
  if (SurfaceNeverMoved==0)
   SurfaceNeverMoved=(int *)mallocSE(G->NumSurfaces*sizeof(int));
  for(ns=0; ns<G->NumSurfaces; ns++)
   SurfaceNeverMoved[ns]=1;

  /***************************************************************/
  /* preallocate an argument structure for AssembleBEMMatrixBlock*/
  /***************************************************************/
  ABMBArgs MyArgs, *Args=&MyArgs;
  InitABMBArgs(Args);
  Args->G=G;
  Args->Omega = II*Xi;

  /***************************************************************/
  /* assemble T matrices                                         */
  /***************************************************************/
  int ns, nsp;
  for(ns=0; ns<G->NumSurfaces; ns++)
   { 
     /* skip if this surface is identical to a previous surface */
     if ( (nsp=G->Mate[ns]) !=-1 )
      { Log("Surface %i is identical to object %i (skipping)",ns,nsp);
        continue;
      };

     Log("Assembling T%i at Xi=%e...",ns+1,Xi);
     Args->Sa = Args->Sb = G->Surfaces[ns];
     Args->B = SC3D->TBlocks[ns];
     Args->Symmetric = 1;
     
     AssembleBEMMatrixBlock(&Args);

   }; // for(ns=0; ns<G->NumSurfaces; ns++)

  /***************************************************************/
  /* if an energy calculation was requested, compute and save    */
  /* the diagonals of the LU factorization of the T blocks       */
  /* (which collectively constitute the diagonal of the LU       */
  /* factorization of the M_{\infinity} matrix).                 */
  /***************************************************************/
  if ( SC3D->WhichQuantities & QUANTITY_ENERGY )
   {
     HMatrix *M=SC3D->M;
     HMatrix *V=SC3D->MInfLUDiagonal;
     for(ns=0; ns<G->NumSurfaces; ns++)
      { 
        Offset=G->BFIndexOffset[ns];
        NBF=G->Surfaces[ns]->NumBFs;

        if ( (nsp=G->Mate[ns]) != -1 )
         { 
           /* if this object has a mate, just copy the diagonals of the */
           /* mate's LU factor                                          */
           MateOffset=G->BFIndexOffset[nsp];
           memcpy(V->DV+Offset,V->DV+MateOffset,NBF*sizeof(double));
         }
        else
         { 
           /* LU factorize the matrix (non-PEC) case or cholesky-factorize */
           /* the negative of the matrix (all-PEC case).                   */
           /* KIND OF HACKY: we use the data buffer inside M as temporary  */
           /* storage for the content of T; this means that we have to     */
           /* call the lapack routines directly instead of using the nice  */
           /* wrappers provided by libhmat                                 */
           for(nbf=0; nbf<NBF; nbf++)
            for(nbfp=0; nbfp<NBF; nbfp++)
             M->DM[nbf + nbfp*NBF] = SC3D->TBlocks[ns]->GetEntryD(nbf,nbfp);

           Log("LU-factorizing T%i at Xi=%g...",ns+1,Xi);
           dgetrf_(&NBF, &NBF, M->DM, &NBF, SC3D->ipiv, &info);
           if (info!=0)
            Log("...FAILED with info=%i (N=%i)",info,NBF);

           /* copy the LU diagonals into DRMInf */
           for(nbf=0; nbf<NBF; nbf++)
            V->SetEntry(Offset+nbf, M->DM[nbf+nbf*NBF]);
         };
      };
   };
     
  /***************************************************************/
  /* for each line in the TransFile, apply the specified         */
  /* transformation, then calculate all quantities requested.    */
  /***************************************************************/
  ByXiFile=fopen(SC3D->ByXiFileName,"a");
  setlinebuf(ByXiFile);
  for(ntnq=nt=0; nt<SC3D->NumTransforms; nt++)
   { 
     Tag=SC3D->CurrentTag=SC3D->Tags[nt];

     /******************************************************************/
     /* skip if all quantities are already converged at this transform */
     /******************************************************************/
     AllConverged=1;
     for(nq=0; AllConverged==1 && nq<SC3D->NumQuantities; nq++)
      if ( !SC3D->Converged[ ntnq + nq ] )
       AllConverged=0;
     if (AllConverged)
      { Log("All quantities already converged at Tag %s",Tag);

        for(nq=0; nq<SC3D->NumQuantities; nq++)
         EFT[ntnq++]=0.0;

        fprintf(ByXiFile,"%s %.15e ",Tag,Xi);
        for(nq=0; nq<SC3D->NumQuantities; nq++)
         fprintf(ByXiFile,"%.15e ",0.0);
        fprintf(ByXiFile,"\n");

        continue;
      };

     /******************************************************************/
     /* apply the geometrical transform                                */
     /******************************************************************/
     Log("Applying transform %s...",Tag);
     G->Transform( GTCList[nt] );
     for(ns=0; ns<G->NumSurfaces; ns++)
      if (G->SurfaceMoved[ns]) SurfaceNeverMoved[ns]=0;

     /***************************************************************/
     /* assemble U_{a,b} blocks and dUdXYZT_{0,b} blocks            */
     /***************************************************************/
     Args->NumTorqueAxes = SC3D->NumTorqueAxes;
     if (Args->NumTorqueAxes>0)
      memcpy(Args->GammaMatrix, SC3D->GammaMatrix, NumTorqueAxes*9*sizeof(double));
     Args->Symmetric=0;
     for(ns=0; ns<G->NumSurfaces; ns++)
      for(nsp=ns+1; nsp<G->NumSurfaces; nsp++)
       { 
         /* if we already computed the interaction between objects ns  */
         /* and nsp once at this frequency, and if neither object has  */
         /* moved, then we do not need to recompute the interaction    */
         if ( nt>0 && SurfaceNeverMoved[ns] && SurfaceNeverMoved[nsp] )
          continue;

         Log(" Assembling U(%i,%i)",ns,nsp);
         Args->Sa    = G->Surfaces[ns];
         Args->Sb    = G->Surfaces[nsp];
         Args->B     = SC3D->Uab[ns][nsp],
         Args->GradB = SC3D->Uab[ns][nsp],

         if (ns==0)
          G->AssembleU(ns, nsp, Xi, IMAG_FREQ,
                       SC3D->NumTorqueAxes, SC3D->GammaMatrix, SC3D->nThread,
                       SC3D->dU0bdX[nsp], SC3D->dU0bdY[nsp], SC3D->dU0bdZ[nsp], 
                       SC3D->dU0bdTheta1[nsp], SC3D->dU0bdTheta2[nsp], SC3D->dU0bdTheta3[nsp]);
         else
          G->AssembleU(ns, nsp, Xi, IMAG_FREQ, 0, 0, SC3D->nThread, 
                       SC3D->Uab[ns][nsp], 0, 0, 0, 0, 0, 0);

         if (ns==0 && SC3D->pCC)
          { if (SC3D->dU0bdX[nsp]) 
             SC3D->dU0bdX[nsp]->ExportToMATLAB(SC3D->pCC,"dU0%idX%s",nsp,SC3D->CurrentTag);
            if (SC3D->dU0bdY[nsp]) 
             SC3D->dU0bdY[nsp]->ExportToMATLAB(SC3D->pCC,"dU0%idY%s",nsp,SC3D->CurrentTag);
            if (SC3D->dU0bdZ[nsp]) 
             SC3D->dU0bdZ[nsp]->ExportToMATLAB(SC3D->pCC,"dU0%idZ%s",nsp,SC3D->CurrentTag);
            if (SC3D->dU0bdTheta1[nsp]) 
             SC3D->dU0bdTheta1[nsp]->ExportToMATLAB(SC3D->pCC,"dU0%idTheta1%s",nsp,SC3D->CurrentTag);
            if (SC3D->dU0bdTheta2[nsp]) 
             SC3D->dU0bdTheta2[nsp]->ExportToMATLAB(SC3D->pCC,"dU0%idTheta2%s",nsp,SC3D->CurrentTag);
            if (SC3D->dU0bdTheta3[nsp]) 
             SC3D->dU0bdTheta3[nsp]->ExportToMATLAB(SC3D->pCC,"dU0%idTheta3%s",nsp,SC3D->CurrentTag);
          };
       };

     /* factorize the M matrix, and, if successful, compute all quantities */
     if ( Factorize(SC3D) ) 
      {
        if ( SC3D->SIMethod==SIMETHOD_SPECTRAL )
         { 
           /* compute all requested quantities using logdet/trace methods */
           if ( SC3D->WhichQuantities & QUANTITY_ENERGY )
            EFT[ntnq++]=GetLNDetMInvMInf(SC3D);
           if ( SC3D->WhichQuantities & QUANTITY_XFORCE )
            EFT[ntnq++]=GetTraceMInvdM(SC3D,'X');
           if ( SC3D->WhichQuantities & QUANTITY_YFORCE )
            EFT[ntnq++]=GetTraceMInvdM(SC3D,'Y');
           if ( SC3D->WhichQuantities & QUANTITY_ZFORCE )
            EFT[ntnq++]=GetTraceMInvdM(SC3D,'Z');
           if ( SC3D->WhichQuantities & QUANTITY_TORQUE1 )
            EFT[ntnq++]=GetTraceMInvdM(SC3D,'1');
           if ( SC3D->WhichQuantities & QUANTITY_TORQUE2 )
            EFT[ntnq++]=GetTraceMInvdM(SC3D,'2');
           if ( SC3D->WhichQuantities & QUANTITY_TORQUE3 )
            EFT[ntnq++]=GetTraceMInvdM(SC3D,'3');
         }
        else
         { 
           SpatialIntegral(SC3D,SC3D->TransLines[nt],EFT+ntnq, SC3D->Error + ntnq);
           ntnq+=SC3D->NumQuantities;
         };
      }
     else 
      { 
        /* matrix factorization failed */
        memset(EFT+ntnq,0,SC3D->NumQuantities*sizeof(double));
        ntnq+=SC3D->NumQuantities;
      };

     /******************************************************************/
     /* write results to .byXi file                                    */
     /******************************************************************/
     fprintf(ByXiFile,"%s %.15e ",Tag,Xi);
     for(nq=SC3D->NumQuantities; nq>0; nq--)
      fprintf(ByXiFile,"%.15e ",EFT[ntnq-nq]);
     if (SC3D->SIMethod!=SIMETHOD_SPECTRAL)
      fprintf(ByXiFile,"%i ",SC3D->SIPoints);
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
     if(SC3D->Error)
      { for(nq=SC3D->NumQuantities; nq>0; nq--)
         fprintf(ByXiFile,"%.15e ",SC3D->Error[ntnq-nq]);
      };
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
     fprintf(ByXiFile,"\n");

     /******************************************************************/
     /* undo the geometrical transform                                 */
     /******************************************************************/
     G->UnTransform();

   }; // for(ntnq=nt=0; nt<SC3D->NumTransforms; nt++)
  fclose(ByXiFile);
#endif
}