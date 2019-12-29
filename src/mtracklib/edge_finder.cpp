/******************************************************************************

   REBVO: RealTime Edge Based Visual Odometry For a Monocular Camera.
   Copyright (C) 2016  Juan José Tarrio
   
   Jose Tarrio, J., & Pedre, S. (2015). Realtime Edge-Based Visual Odometry
   for a Monocular Camera. In Proceedings of the IEEE International Conference
   on Computer Vision (pp. 702-710).
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

 *******************************************************************************/


#include "mtracklib/edge_finder.h"
#include <iostream>
#include <TooN/TooN.h>

using namespace TooN;
namespace  rebvo {
edge_finder::edge_finder(cam_model &cam, float max_i_value, int kl_num_max)		//maximum keypoint value
    :cam_mod(cam),fsz(cam_mod.sz),max_img_value(max_i_value),
     img_mask_kl(fsz),kl_size(kl_num_max),kn(0)
{
    if(kl_num_max>0)
        kl=new KeyLine[kl_num_max];
    else
        kl=nullptr;
    img_mask_kl.Reset(-1);
}

edge_finder::edge_finder(const edge_finder &ef)
    :cam_mod(ef.cam_mod),fsz(ef.cam_mod.sz),max_img_value(ef.max_img_value),
     img_mask_kl(fsz),kl_size(ef.KNum()),kl(new KeyLine[kl_size]),kn(kl_size)
{
    img_mask_kl=ef.img_mask_kl;
//    for(int i=0;i<kn;i++)
  //      kl[i]=ef.kl[i];
    memcpy(kl,ef.kl,kn*sizeof(KeyLine));


}

edge_finder::~edge_finder()		//maximum keypoint value
{

    if(kl)
        delete[] kl;

}


//********************************************************************
// build_mask(): detect KeyLines using DoG and Gradient
//********************************************************************

void edge_finder::build_mask(sspace *ss,        //State space containing image data
                             int kl_max,        //Maximun number of KeyLines to search for
                             int win_s,         //Windopw size for plane fitting
                             float per_hist,    //Fast threshold on the maximun
                             float grad_thesh,  //Fast threshold on the gradioent
                             float dog_thesh    //Threshold on the DoG
                             )
{

    //Matrix used for plane fitting
    Matrix <Dynamic,Dynamic> Y((win_s*2+1)*(win_s*2+1),1);
    Vector <3> theta;

    //Pseudo inverse for plane fitting can be catched
    static bool PhiReCalc=true;
    static Matrix <Dynamic,Dynamic> PInv(3,(win_s*2+1)*(win_s*2+1));


    if(PhiReCalc){
        Matrix <Dynamic,Dynamic,double> Phi((win_s*2+1)*(win_s*2+1),3);
        for(int i=-win_s,k=0;i<=win_s;i++) {
            for(int j=-win_s;j<=win_s;j++,k++){
                Phi(k,0)=j;
                Phi(k,1)=i;
                Phi(k,2)=1;
            }
        }
       

        PInv=util::Matrix3x3Inv(Phi.T()*Phi)*Phi.T();
        PhiReCalc=false;
    }



    if(kl_max>kl_size)
        kl_max=kl_size;

    kn=0;

    //Start searching...
     //std::cout <<"win_s" << win_s;
     //std::cout <<"height" << fsz.h;
    for(int y=win_s+98;y<fsz.h-98-win_s;y++){
        for(int x=win_s;x<fsz.w-win_s;x++){

            int img_inx=img_mask_kl.GetIndex(x,y);  //Catch the image index

            img_mask_kl[img_inx]=-1;                //Default to no Keyline

            float n2gI=util::norm2(ss->ImgDx()[img_inx],ss->ImgDy()[img_inx]);

            if(n2gI<util::square(grad_thesh*max_img_value))        //Fisrt test on the 2-norm squared of the gradient
                continue;                                          //The theshhold must be proporcional to the
                                                                   //intensity scaling of the image
            int pn=0;


            for(int i=-win_s,k=0;i<=win_s;i++){
                for(int j=-win_s;j<=win_s;j++,k++){             //For each pixel in a (2*win_sz+1)*(2*win_sz+1) window
                    Y(k,0)=ss->ImgDOG()[(y+i)*fsz.w+x+j];   
                    //printf("ffff %d",Y(k,0));   //Store DoG value on Y
                    if(ss->ImgDOG()[(y+i)*fsz.w+x+j]>0)         //
                        pn++;                                   //Increment pn if the DoG is positive
                    else
                        pn--;                                   //Decrement pn if the DoG is negative
                }
            }



            if(fabs(pn)>((float)((2.0*win_s+1.0)*(2.0*win_s+1.0)))*per_hist)        //If the point is a local root of the DoG
                continue;                                                           //Then the number of neighbour Positive values
                                                                                    //must be similar to the Negative values.
                                                                                    //So, apply a threshold on the difference
                                                                                    //(for non-max suppesion)


            theta=(PInv*Y).T()[0];                                                  //Fit a plane to the DoG

            float xs=-theta[0]*theta[2]/(theta[0]*theta[0]+theta[1]*theta[1]);      //(xs,ys) is the point belonging to the line of zero
            float ys=-theta[1]*theta[2]/(theta[0]*theta[0]+theta[1]*theta[1]);      //crossing of the plane, that is closest to (0,0)


            if(fabs(xs)>0.5 || fabs(ys)>0.5)                             //if the zero crossing is outside pixel area, then is not an edge
               continue;


            Point2DF mn={(float)theta[0],(float)theta[1]};              //doG gradient


            float n2_m=util::norm2(mn.x,mn.y);                          //sqare norm of the gradient

            if(n2_m<util::square(grad_thesh*max_img_value*dog_thesh))       //Apply a treshold on the DoG gradient
                continue;                                                   //This threshold is proporcional to the pure gradient one



            //Al test passed, anotate keyline...

            kl[kn].p_inx=img_inx;

            kl[kn].m_m=mn;                          //KeyLine Gradient
            kl[kn].n_m=sqrt(n2_m);
            kl[kn].u_m.x=kl[kn].m_m.x/kl[kn].n_m;
            kl[kn].u_m.y=kl[kn].m_m.y/kl[kn].n_m;

            kl[kn].c_p={x+xs,y+ys};                 //Subpixel position of the edge
            // std::cout <<"x value with precesion" << x+xs;
            //std::cout <<"x values without precesion"<< x <<"\n";
        
            kl[kn].p_m=cam_mod.Img2Hom(kl[kn].c_p); //Homogeneous position
            kl[kn].p_m_0=kl[kn].p_m;

            kl[kn].rho=RhoInit;                     //Rho & srho init point
            kl[kn].s_rho=RHO_MAX;
            kl[kn].rho0=RhoInit;                     //Rho & srho init point
            kl[kn].s_rho0=RHO_MAX;
            kl[kn].rho_nr=RhoInit;                     //Rho & srho init point
            kl[kn].s_rho_nr=RHO_MAX;


            kl[kn].m_num=0;
            kl[kn].n_id=-1;
            kl[kn].p_id=-1;
            kl[kn].net_id=-1;

            kl[kn].m_id=-1;
            kl[kn].m_id_f=-1;
            kl[kn].m_id_kf=-1;

            kl[kn].stereo_m_id=-1;
            kl[kn].stereo_rho=RhoInit;
            kl[kn].stereo_s_rho=RHO_MAX;
            kl[kn].checked = 0;

            img_mask_kl[img_inx]=kn;    //Set the index of the keyline in the mask


            if(++kn>=kl_max){
                for(++img_inx;img_inx<fsz.w*fsz.h;img_inx++){
                    img_mask_kl[img_inx]=-1;
                }
                return;
            }


        }
    }
}


//********************************************************************
// NextPoint(): Look for a consecutive KeyLine neighbour
//********************************************************************

inline int NextPoint(const int &x, const int &y,    //Keyline coordinate
                     const Point2DF &m,             //KeyLine grad
                     Image<int>&img_mask)            //Keyline Image Mask
{
    const auto tx=-m.y;
    const auto ty=m.x;  //KL Tangent Direction

    //Look for the 3 posible points in cuadrant of the tangeng direction

    int kl_inx;

    if(ty>0){
        if(tx>0){

            if((kl_inx=img_mask(x+1,y+0))>=0){
                return kl_inx;                  //If presense, return KL index
            } 

            if((kl_inx=img_mask(x+2,y+0))>=0){
                return kl_inx;
            }

            if((kl_inx=img_mask(x+0,y+1))>=0){
                return kl_inx;
            }

            if((kl_inx=img_mask(x+0,y+2))>=0){
                return kl_inx;
            }


            if((kl_inx=img_mask(x+1,y+1))>=0){
                return kl_inx;
            }

            if((kl_inx=img_mask(x+2,y+2))>=0){
                return kl_inx;
            }

        }else{
            if((kl_inx=img_mask(x-1,y+0))>=0){
                return kl_inx;
            }
            if((kl_inx=img_mask(x-2,y+0))>=0){
                return kl_inx;
            }

            if((kl_inx=img_mask(x+0,y+1))>=0){
                return kl_inx;
            }
            if((kl_inx=img_mask(x+0,y+2))>=0){
                return kl_inx;
            }

            if((kl_inx=img_mask(x-1,y+1))>=0){
                return kl_inx;
            }
            
            if((kl_inx=img_mask(x-2,y+2))>=0){
                return kl_inx;
            }

        }
    }else{

        if(tx<0){

            if((kl_inx=img_mask(x-1,y+0))>=0){
                return kl_inx;
            }

            if((kl_inx=img_mask(x+0,y-1))>=0){
                return kl_inx;
            }

            if((kl_inx=img_mask(x-1,y-1))>=0){
                return kl_inx;
            }

        }else{

            if((kl_inx=img_mask(x+1,y+0))>=0){
                return kl_inx;
            }

            if((kl_inx=img_mask(x+0,y-1))>=0){
                return kl_inx;
            }

            if((kl_inx=img_mask(x+1,y-1))>=0){
                return kl_inx;
            }

        }
    }
    return -1;  //-1 if no match

}


//********************************************************************
// join_edges(): Join consecutive edge points
//********************************************************************

void edge_finder::join_edges(){
    
    for(int ikl=0;ikl<kn;ikl++) {

        int x=util::round2int_positive(kl[ikl].c_p.x);
        int y=util::round2int_positive(kl[ikl].c_p.y);  //KeyLine image coordinate

        int ikl2;
        if((ikl2=NextPoint(x,y,kl[ikl].m_m,img_mask_kl))<0)    //Look for an edge
            continue;

        kl[ikl2].p_id=ikl;      //asign previous and next index
        kl[ikl].n_id=ikl2;

    }
    
    for(int fil=0;fil<kn;fil++){
        //printf("inside for\n");
        int maximumLength= 10;
        if(kl[fil].checked == 0){
            //printf("if checked == 0\n");
            int length=0;
            int nextKl = fil;
            int prevKl = fil;
            while(length <= maximumLength && (kl[prevKl].p_id != -1 || kl[nextKl].n_id != -1)){
                //printf("first while \n");
                if(kl[nextKl].n_id != -1) {
                    kl[nextKl].checked = 1;
                    length++;
                    nextKl = kl[nextKl].n_id; 
                }

                if(kl[prevKl].p_id != -1) {
                    kl[prevKl].checked = 1;
                    length++;
                    prevKl = kl[prevKl].p_id;
                }
            }

            if(length <= maximumLength){
                int nextKl_r = fil;
                int prevKl_r = fil;
                int p,q,r,s,imageIndex1,imageIndex2;
                while((kl[prevKl_r].p_id != -1 || kl[nextKl_r].n_id != -1)){
                if(kl[nextKl_r].n_id != -1) {
                    kl[nextKl_r].checked = -1;
                    p = util::round2int_positive(kl[nextKl_r].c_p.x);
                    q = util::round2int_positive(kl[nextKl_r].c_p.y);
                    imageIndex1 =  img_mask_kl.GetIndex(p,q);
                    img_mask_kl[imageIndex1] =-1;
                    ///kl[nextKl_r].c_p.x = 0;
                    //kl[nextKl_r].c_p.y =0;
                    nextKl_r = kl[nextKl_r].n_id; 
                    
                }
                //printf("inside if , while \n");

                if(kl[prevKl_r].p_id != -1) {
                    kl[prevKl_r].checked = -1;
                    r = util::round2int_positive(kl[prevKl_r].c_p.x);
                    s = util::round2int_positive(kl[prevKl_r].c_p.y);
                    imageIndex2 =  img_mask_kl.GetIndex(r,s);
                    img_mask_kl[imageIndex2] =-1;
                    ///kl[prevKl_r].c_p.x = -1;
                   // kl[prevKl_r].c_p.y = -1;
                    prevKl_r = kl[prevKl_r].p_id;
                }
            }

        }

    }
    }
}





//********************************************************************
// UpdateThresh(): perform auto-threshold with a Proporcional-Law
//********************************************************************

inline void UpdateThresh(double &tresh,const int& l_kl_num, const int& kl_ref,const double& gain,const double& thresh_max,const double& thresh_min){

    tresh-=gain*(double)(kl_ref-l_kl_num);
    tresh=util::Constrain(tresh,thresh_min,thresh_max);

}


//********************************************************************
// detect(): Full edge detect
//********************************************************************

void edge_finder::detect(sspace *ss,
                         int plane_fit_size,    //Size of window for plane fitting
                         double pos_neg_thresh, //DoG Pos-Neg max difference
                         double dog_thresh,     //DoG gradient thresh (proporcional to &tresh)
                         int kl_max,            //Maximun number of KeyLines
                         double &tresh,         //Current threshold
                         int &l_kl_num,         //Number of KLs on the last edgemap
                         int kl_ref,            //Requested number of KLs (for auto-thresh)
                         double gain,           //Auto-thresh gain
                         double thresh_max,     //Maximun allowed thresh
                         double thresh_min      //Minimun...
                         )
{

    if(gain>0)
        UpdateThresh(tresh,l_kl_num,kl_ref,gain,thresh_max,thresh_min);     //Autogain threshold

    build_mask(ss,kl_max,plane_fit_size,pos_neg_thresh,tresh,dog_thresh);   //Detect & Anotate KeyLines

    join_edges();   //Join consecutive KeyLines

    l_kl_num=kn;    //Save the number of effective KL detected
    //printf("\n effective keylines %d",l_kl_num);
}


//********************************************************************************
// EstimateQuantile() uses histograms to estimate an uncertainty threshold
// for a certain quatile
//********************************************************************************

int edge_finder::reEstimateThresh(int  knum,int n){               //Number of bins on the histo


    float max_dog=kl[0].n_m;
    float min_dog=kl[0].n_m;

    for(int ikl=1;ikl<kn;ikl++){
        util::keep_max(max_dog,kl[ikl].n_m);
        util::keep_min(min_dog,kl[ikl].n_m);
    }

    int histo[n];


    for(int i=0;i<n;i++){
        histo[i]=0;
    }

    for(int ikl=0;ikl<kn;ikl++){
        int i=n*(max_dog-kl[ikl].n_m)/(max_dog-min_dog);    //histogram position

        i=i>n-1?n-1:i;
        i=i<0?0:i;

        histo[i]++;     //count
    }

    int i=0;
    for(int a=0;i<n && a<knum;i++,a+=histo[i]);

    return reTunedThresh=max_dog-(float)i*(max_dog-min_dog)/(float)n;

}


//************************************************************************
// dumpToBinaryFile(): Print keyline list to file
//************************************************************************
void edge_finder::dumpToBinaryFile(std::ofstream &file)
{

    file.write((const char *)&kn,sizeof(kn));
    file.write((const char *)kl,sizeof(KeyLine)*kn);

    std::cout <<"\nDumpped "<<kn<<" Keylines, "<<sizeof(kn)+sizeof(KeyLine)*kn<<" bytes";

}

//************************************************************************
// dumpToBinaryFile(): Read keyline list from file
//************************************************************************
void edge_finder::readFromBinaryFile(std::ifstream &file)
{

    file.read((char *)&kn,sizeof(kn));

    if(kl!=nullptr)
        delete [] kl;
    kl=new KeyLine[kn];
    kl_size=kn;

    file.read((char *)kl,sizeof(KeyLine)*kn);

}
}
