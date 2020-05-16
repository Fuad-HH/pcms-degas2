#include "BoundaryDescr3D.h"
#include "couplingTypes.h"
#include "importpart3mesh.h"
#include "dataprocess.h"
#include "commpart1.h"
#include "testutilities.h"
#include "sendrecv_impl.h"
#include <mpi.h>

namespace coupler {

BoundaryDescr3D::BoundaryDescr3D(
    const Part3Mesh3D& p3m3d,
    const Part1ParalPar3D &p1pp3d,
    const DatasProc3D& dp3d,
    const TestCase tcase,
    bool pproc)
{
  preproc=pproc;
  if(preproc==true){
    test_case=tcase;
    nzb=p1pp3d.nzb;
    updenz=new double**[p1pp3d.li0];
    lowdenz=new double**[p1pp3d.li0];
    for(LO i=0;i<p1pp3d.li0;i++){
      updenz[i]=new double*[p3m3d.lj0];
      lowdenz[i]=new double*[p3m3d.lj0];
      for(LO j=0;j<p3m3d.lj0;j++){
	updenz[i][j]=new double[nzb];
	lowdenz[i][j]=new double[nzb];
      }
    } 
    uppotentz=new double**[p3m3d.xboxinds[p1pp3d.mype_x][0]];
    lowpotentz=new double**[p3m3d.xboxinds[p1pp3d.mype_x][0]];
    upzpart3=new double*[p3m3d.xboxinds[p1pp3d.mype_x][0]];
    lowzpart3=new double*[p3m3d.xboxinds[p1pp3d.mype_x][0]];
    for(LO i=0;i<p3m3d.xboxinds[p1pp3d.mype_x][0];i++){
      uppotentz[i]=new double*[p3m3d.lj0];
      lowpotentz[i]=new double*[p3m3d.lj0]; 
      upzpart3[i]=new double[nzb];
      lowzpart3[i]=new double[nzb];
      for(LO j=0;j<p3m3d.lj0;j++){
	uppotentz[i][j]=new double[nzb];
	lowpotentz[i][j]=new double[nzb];
      }
    }
  }
}

void BoundaryDescr3D::zPotentBoundaryBufAssign(
    const DatasProc3D& dp3d, 
    const Part3Mesh3D& p3m3d,
    const Part1ParalPar3D &p1pp3d)
{
  if(lowpotentz==NULL||uppotentz==NULL){
    std::cout<<"ERROR:the boundary buffer of the potential must be allocated beforing invoking this routine.";
    std::exit(EXIT_FAILURE);
  }
  LO li0,lj0,lk0;
  li0=p3m3d.xboxinds[p1pp3d.mype_x][0];
  lj0=p3m3d.lj0;
  if(p1pp3d.npz>1){
    if(p1pp3d.periods[2]==1){  
      for(LO i=0;i<li0;i++){
        lk0=p3m3d.mylk0[i];
        if(lk0<nzb){
          std::cout<<"ERROR: the interpolation order is larger than the box count along z dimension.";
          std::exit(EXIT_FAILURE);
        } 
        mpisendrecv_aux1D(p1pp3d.comm_z,nzb,li0,lj0,lk0,lowzpart3[i],upzpart3[i],
          p3m3d.pzcoords[i]);   

       if(test_case==TestCase::t0){
         if(p1pp3d.mype_z==0){
             std::cout<<"lowzpart3="<<lowzpart3[i][0]<<" "<<lowzpart3[i][1]<<'\n';
             std::cout<<"lowpzcoords="<<p3m3d.pzcoords[i][0]<<" "<<p3m3d.pzcoords[i][1]<<'\n';
           
        } else if(p1pp3d.mype_z==p1pp3d.npz-1){
            std::cout<<"upzpart3="<<upzpart3[i][0]<<" "<<upzpart3[i][1]<<'\n';
            std::cout<<"upzcoords="<<p3m3d.pzcoords[i][lk0-2]<<" "<<p3m3d.pzcoords[i][lk0-1]<<'\n'; 
          }
        }

        for(LO j=0;j<lj0;j++){
          mpisendrecv_aux1D(p1pp3d.comm_z,nzb,li0,lj0,lk0,lowpotentz[i][j],uppotentz[i][j],
              dp3d.potentin[i][j]); 
        }

     }
      if(p1pp3d.mype_z==0){
        for(LO h=0;h<li0;h++){
          for(LO k=0;k<nzb;k++){  
            lowzpart3[h][k]=lowzpart3[h][k]-2.0*cplPI;
          }
        }
      }else if(p1pp3d.mype_z==p1pp3d.npz-1){
         for(LO h=0;h<li0;h++){
           for(LO k=0;k<nzb;k++){
              upzpart3[h][k]=upzpart3[h][k]+2.0*cplPI;
           }
         }          
       }
       if(test_case==TestCase::t0){
         if(p1pp3d.mype_z==0){
           for(LO k=0;k<li0;k++){
             std::cout<<"lowzpart3["<<k<<"][1]="<<lowzpart3[k][1]<<'\n';
           }
         }else if(p1pp3d.mype_z==p1pp3d.npz-1){
            for(LO k=0;k<li0;k++){ 
              std::cout<<"upzpart3["<<k<<"][1]="<<upzpart3[k][1]<<'\n'; 
            }  
          }
       } 
    } else if(p1pp3d.periods[2]==0){
         std::cout<<"The parallelization of 'z' domain is not down with unperiodic boundary condiiton"
                  <<" and npz>1"<<'\n';
         std::exit(EXIT_FAILURE);
     }
  } else {
      if(p1pp3d.periods[2]==1){ 
	for(LO i=0;i<li0;i++){
	   lk0=p3m3d.mylk0[i];
	   if(lk0<nzb){
	     std::cout<<"ERROR: the interpolation order is larger than the box count along z dimension.";
	     std::exit(EXIT_FAILURE);
	   }  
	   for(LO k=0;k<nzb;k++){
	     lowzpart3[i][k]=p3m3d.pzcoords[i][lk0-nzb+k];
	     upzpart3[i][k]=p3m3d.pzcoords[i][k];
	   }
	   for(LO j=0;j<lj0;j++){
	     for(LO k=0;k<nzb;k++){
	       lowpotentz[i][j][k]=dp3d.potentin[i][j][k];
	       lowpotentz[i][j][k]=dp3d.potentin[i][j][lk0-nzb+k];
	     }  
	   }     
	}
     } else if(p1pp3d.periods[2]==0) {
         std::cout<<"The parallelization of 'z' domain is not down with unperiodic boundary condiiton"
                  <<" and npz=1"<<'\n';
         std::exit(EXIT_FAILURE);
      }
   }
}

BoundaryDescr3D::~BoundaryDescr3D()
{
  if(upzpart3!=NULL) delete[] upzpart3;
  if(lowzpart3!=NULL) delete[] lowzpart3;
  if(updenz!=NULL) delete[] updenz;
  if(lowdenz!=NULL) delete[] lowdenz;
  if(uppotentz!=NULL) delete[] uppotentz;
  if(lowpotentz!=NULL) delete[] lowpotentz;
}

}
 
