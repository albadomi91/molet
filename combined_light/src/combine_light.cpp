#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>

#include "json/json.h"

#include "vkllib.hpp"

#include "auxiliary_functions.hpp"
#include "mask_functions.hpp"

int main(int argc,char* argv[]){

  //=============== BEGIN:PARSE INPUT =======================
  std::ifstream fin;
  Json::Value::Members jmembers;

  // Read the main projection parameters
  Json::Value root;
  fin.open(argv[1],std::ifstream::in);
  fin >> root;
  fin.close();

  std::string path = argv[2];

  std::string output     = path + "output/";
  std::string input_path = path + "input_files/";




  // Loop over the bands
  // ===================================================================================================================
  // ===================================================================================================================
  for(int b=0;b<root["instrument"]["bands"].size();b++){
    const Json::Value band = root["instrument"]["bands"][b];
    std::string band_name = band["name"].asString();
    
    // Set output image plane in super-resolution
    double width  = band["field-of-view_x"].asDouble();
    double height = band["field-of-view_y"].asDouble();
    int res_x = static_cast<int>(ceil(width/band["resolution"].asDouble()));
    int res_y = static_cast<int>(ceil(height/band["resolution"].asDouble()));
    int super_res_x = 10*res_x;
    int super_res_y = 10*res_y;
    ImagePlane mysim(super_res_x,super_res_y,width,height);
    
    
    // Get the psf in super-resolution, crop it, and create convolution kernel
    std::string psf_path = input_path + "psf_" + band_name + ".fits";
    double psf_width  = band["psf"]["width"].asDouble();
    double psf_height = band["psf"]["height"].asDouble();
    int psf_Nx = band["psf"]["pix_x"].asInt();
    int psf_Ny = band["psf"]["pix_y"].asInt();
    PSF mypsf(psf_path,psf_Nx,psf_Ny,psf_width,psf_height,&mysim);
    mypsf.cropPSF(0.99);
    mypsf.createKernel(mysim.Ni,mysim.Nj);
    
    
    // Create the fixed extended lensed light
    ImagePlane* extended = new ImagePlane(output+"lensed_image_super.fits",super_res_x,super_res_y,width,height);
    mypsf.convolve(extended);
    //extended->writeImage(output+"psf_lensed_image_super.fits");
    
    // Create the fixed lens galaxy light
    ImagePlane* lens_light = new ImagePlane(output+"lens_light_super.fits",super_res_x,super_res_y,width,height);
    mypsf.convolve(lens_light);
    //lens_light->writeImage(output+"psf_lens_light_super.fits");
    
    // Combined light of the observed base image (binned from 'super' to observed resolution)
    ImagePlane* base = new ImagePlane(super_res_x,super_res_y,width,height); 
    for(int i=0;i<base->Nm;i++){
      base->img[i] = lens_light->img[i] + extended->img[i];
    }
    delete(extended);
    delete(lens_light);
    ImagePlane obs_base(res_x,res_y,width,height); // obs_base.img array is initialized to zero
    base->lowerResRebinIntegrate(&obs_base);
    delete(base);



    // All the static light components have been created.
    // The resulting observed image (not super-resolved) is "obs_base".
    // If there is no time dimension required, the code just outputs the obs_base image and stops.
    // But further work is done when it comes to time varying images in three nested loops:
    // - one over all the intrinsic variability light curves,
    // - one over all the extrinsic variability light curves,
    // - and one over the observed time.

    

    if( !root.isMember("point_source") ){
      //=============== CREATE A SINGLE STATIC IMAGE ====================

      // Adding noise here

      // Output the observed base image
      for(int i=0;i<obs_base.Nm;i++){
	obs_base.img[i] = -2.5*log10(obs_base.img[i]);
      }
      obs_base.writeImage(output + "OBS_" + band_name + ".fits");
      
    } else {
      //=============== CREATE THE TIME VARYING LIGHT ====================
      
      // Read the multiple images' parameters from JSON
      Json::Value images;
      fin.open(output+"multiple_images.json",std::ifstream::in);
      fin >> images;
      fin.close();

      // Get maximum image time delay
      double td_max = 0.0;
      for(int q=0;q<images.size();q++){
	double td = images[q]["dt"].asDouble();
	if( td > td_max ){
	  td_max = td;
	}
      }

      


      // Get observed time vector
      std::vector<double> tobs;
      for(int t=0;t<band["time"].size();t++){
	tobs.push_back(band["time"][t].asDouble());
      }
      double tobs_t0   = tobs[0];
      double tobs_tmax = tobs.back();
      double tobs_Dt   = tobs_tmax - tobs_t0;

      // Create a 'continuous' time vector: daily cadence
      int Ndays = (int) ceil(tobs_Dt);
      std::vector<double> tcont(Ndays);
      for(int t=0;t<Ndays;t++){
	tcont[t] = tobs_t0 + t;
      }

      // Quick check on time delay and observing time compatibility
      if( td_max > tobs_tmax ){
	printf("Observing period (%f days) shorter than the maximum time delay (%f days).\n",tobs_tmax,td_max);
	printf("Increase the observing time period!!!\n");
	return 1;
      }



      
      // Read intrinsic light curve(s) from JSON
      Json::Value intrinsic_lc;
      if( root["point_source"]["variability"]["intrinsic"]["type"].asString() == "custom" ){
	fin.open(input_path+"intrinsic_light_curves.json",std::ifstream::in);
      } else {
	fin.open(output+"intrinsic_light_curves.json",std::ifstream::in);
      }
      fin >> intrinsic_lc;
      fin.close();
      int N_in = intrinsic_lc[band_name].size();

      // Process the intrinsic light curves
      std::vector<LightCurve*> LC_intrinsic(N_in);
      for(int lc_in=0;lc_in<N_in;lc_in++){
	LC_intrinsic[lc_in] = new LightCurve(intrinsic_lc[band_name][lc_in]);

	// Convert from magnitudes to intensities
	for(int i=0;i<LC_intrinsic[lc_in]->signal.size();i++){
	  LC_intrinsic[lc_in]->signal[i] = pow(10.0,-0.4*LC_intrinsic[lc_in]->signal[i]);
	}
	
	// Check time limitations
	double tmax_intrinsic = LC_intrinsic[lc_in]->time.back();
	if( (td_max+tobs_tmax) > tmax_intrinsic ){
	  printf("Intrinsic light curve %i duration (%f days) is shorter than the maximum time delay plus the observing period (%f + %f days).\n",lc_in,tmax_intrinsic,td_max,tobs_tmax);
	  int i=0;
	  while( tobs[i] < (tmax_intrinsic-td_max) ){
	    i++;
	  }
	  tobs.resize(i);
	  printf("Observing period is truncated to %f days!!!\n",tobs_tmax);
	}
      }
      
      

      
      // Read extrinsic light curve(s) from JSON
      Json::Value extrinsic_lc;
      if( root["point_source"]["variability"]["extrinsic"]["type"].asString() == "custom" ){
	fin.open(input_path+"extrinsic_light_curves.json",std::ifstream::in);
      } else {
	fin.open(output+"extrinsic_light_curves.json",std::ifstream::in);
      }
      fin >> extrinsic_lc;
      fin.close();
      int N_ex;
      for(int q=0;q<extrinsic_lc.size();q++){
	if( extrinsic_lc[q][band_name].size() > 0 ){
	  N_ex = extrinsic_lc[q][band_name].size();
	  break;
	}
      }
      
      // Process the extrinsic light curves
      std::vector< std::vector<LightCurve*> > LC_extrinsic(images.size());
      for(int q=0;q<images.size();q++){
	LC_extrinsic[q].resize(N_ex);
      }
      for(int q=0;q<images.size();q++){
	for(int lc_ex=0;lc_ex<N_ex;lc_ex++){
	  if( extrinsic_lc[q][band_name].size() > 0 ){
	    LC_extrinsic[q][lc_ex] = new LightCurve(extrinsic_lc[q][band_name][lc_ex]);
	    // Ex. light curves' time begins at 0, so I need to add t0 (of both tobs and tcont) so that the time vectors match
	    for(int t=0;t<LC_extrinsic[q][lc_ex]->time.size();t++){
	      LC_extrinsic[q][lc_ex]->time[t] += tobs_t0;
	    }
	  } else {
	    LC_extrinsic[q][lc_ex] = new LightCurve();
	  }
	}
      }
      
      
      
      


      
      // Configure the PSF for the point source
      // Perturb the PSF at each image location
      std::vector<PSF*> PSF_list(images.size());
      for(int q=0;q<images.size();q++){
	//TransformPSF* dum = new TransformPSF(images[q]["x"].asDouble(),images[q]["y"].asDouble(),0.0,false,false);
	//transPSF[q] = dum;
	PSF_list[q] = &mypsf; // just copy the same psf per image
      }
      // Set the PSF related offsets for each image
      std::vector<offsetPSF> PSFoffsets(images.size());
      for(int q=0;q<images.size();q++){
	PSFoffsets[q] = PSF_list[q]->offsetPSFtoPosition(images[q]["x"].asDouble(),images[q]["y"].asDouble(),&mysim);
      }
      // Calculate the appropriate PSF sums
      std::vector<double> psf_partial_sum(images.size());
      for(int q=0;q<images.size();q++){
	double sum = 0.0;
	for(int i=0;i<PSFoffsets[q].ni;i++){
	  for(int j=0;j<PSFoffsets[q].nj;j++){
	    int index_psf = i*PSF_list[q]->cropped_psf->Nj + j;
	    sum += PSF_list[q]->cropped_psf->img[index_psf];
	  }
	}
	psf_partial_sum[q] = sum;
      }
      
      
      
      
      
      
      // Loop over intrinsic light curves
      //0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=
      for(int lc_in=0;lc_in<N_in;lc_in++){
	// Loop over extrinsic light curves
	//0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=
	for(int lc_ex=0;lc_ex<N_ex;lc_ex++){ // use the first multiple image to get the number of extrinsic light curves

	  // Output directory
	  char buffer[15];
	  sprintf(buffer,"mock_%04d_%04d",lc_in,lc_ex);
	  std::string mock = buffer;
	  //std::cout << mock << std::endl;


	  // *********************** Product: Observed continuous light curves ***********************
	  LightCurve* cont_LC_intrinsic = new LightCurve(tcont);
	  std::vector<LightCurve*> cont_LC(images.size());
	  for(int q=0;q<images.size();q++){
	    cont_LC[q] = new LightCurve(tcont);
	  }

	  // Calculate the combined light curve for each image
	  for(int q=0;q<images.size();q++){
	    double macro_mag = abs(images[q]["mag"].asDouble());
	    LC_intrinsic[lc_in]->interpolate(cont_LC_intrinsic,td_max - images[q]["dt"].asDouble());

	    // Check if multiple image does not have a corresponding extrinsic light curve (i.e. a maximum image without a magnification map)
	    if( LC_extrinsic[q][lc_ex]->time.size() > 0 ){
	      LC_extrinsic[q][lc_ex]->interpolate(cont_LC[q],0.0);
	      for(int t=0;t<tcont.size();t++){
		cont_LC[q]->signal[t] = cont_LC[q]->signal[t] * macro_mag * cont_LC_intrinsic->signal[t]; // this line includes both intrinsic and microlensing signals
		//cont_LC[q]->signal[t] = macro_mag * cont_LC_intrinsic->signal[t];
	      }
	    } else {
	      for(int t=0;t<tcont.size();t++){
		cont_LC[q]->signal[t] = macro_mag * cont_LC_intrinsic->signal[t]; // this line includes only the intrinsic signal and excludes microlensing
	      }
	    }
	  }

	  // Write json light curves
	  outputLightCurvesJson(cont_LC,path+mock+"/LCcont_"+band_name+".json");

	  // Clean up
	  delete(cont_LC_intrinsic);
	  for(int q=0;q<images.size();q++){
	    delete(cont_LC[q]);
	  }
	  // *********************** End of product **************************************************



	  // *********************** Product: Observed sampled light curves **************************
	  LightCurve* samp_LC_intrinsic = new LightCurve(tobs);
	  std::vector<LightCurve*> samp_LC(images.size());
	  for(int q=0;q<images.size();q++){
	    samp_LC[q] = new LightCurve(tobs);
	  }

	  // Calculate the combined light curve for each image
	  for(int q=0;q<images.size();q++){
	    double macro_mag = abs(images[q]["mag"].asDouble());
	    LC_intrinsic[lc_in]->interpolate(samp_LC_intrinsic,td_max - images[q]["dt"].asDouble());

	    // Check if multiple image does not have a corresponding extrinsic light curve (i.e. a maximum image without a magnification map)
	    if( LC_extrinsic[q][lc_ex]->time.size() > 0 ){
	      LC_extrinsic[q][lc_ex]->interpolate(samp_LC[q],0.0);
	      for(int t=0;t<tobs.size();t++){
		samp_LC[q]->signal[t] = samp_LC[q]->signal[t] * macro_mag * samp_LC_intrinsic->signal[t]; // this line includes both intrinsic and microlensing signals
		//samp_LC[q]->signal[t] = macro_mag * samp_LC_intrinsic->signal[t];
	      }
	    } else {
	      for(int t=0;t<tobs.size();t++){
		samp_LC[q]->signal[t] = macro_mag * samp_LC_intrinsic->signal[t]; // this line includes only the intrinsic signal and excludes microlensing
	      }
	    }
	  }

	  // Write json light curves
	  outputLightCurvesJson(samp_LC,path+mock+"/LCsamp_"+band_name+".json");
	  // *********************** End of product **************************************************





	  
	  
	  // *********************** Product: Observed sampled cut-outs (images) *****************************
	  if( root["point_source"]["output_cutouts"].asBool() ){
	    for(int t=0;t<tobs.size();t++){
	      
	      // Loop over the truncated PSF (through PSF_offsets) for each image, and add their light to the pp_light image that contains all the point source light.
	      ImagePlane pp_light(super_res_x,super_res_y,width,height); // this has to be in intensity units in order to be able to add the different light components
	      for(int q=0;q<images.size();q++){
		for(int i=0;i<PSFoffsets[q].ni;i++){
		  for(int j=0;j<PSFoffsets[q].nj;j++){
		    int index_img = pp_light.Nj*i + j;
		    int index_psf = i*PSF_list[q]->cropped_psf->Nj + j;
		    //pp_light.img[PSFoffsets[q].offset_image + pp_light.Nj*i + j] += 1.0;
		    double psf_pix = PSF_list[q]->cropped_psf->img[PSFoffsets[q].offset_cropped + index_psf];
		    pp_light.img[PSFoffsets[q].offset_image + index_img] += samp_LC[q]->signal[t]*psf_pix/psf_partial_sum[q];
		  }
		}
	      }
	      
	      
	      // Check the expected brightness of the multiple images vs the image

		//double sum = 0.0;
		//for(int i=0;i<pp_light.Nm;i++){
		//sum += pp_light.img[i];
		//}
		//double fac = inf_dx*inf_dy;
		//double true_sum = 0.0;
		//for(int q=0;q<images.size();q++){
		//true_sum += img_signal[q][t];
		//}
		//printf("True: %15.10f (%15.10f)  Numerical: %15.10f (%15.10f)\n",true_sum,-2.5*log10(true_sum),sum,-2.5*log10(sum));
	      
	      // Bin image from 'super' to observed resolution
	      ImagePlane obs_img = obs_base;
	      pp_light.lowerResRebinAdditive(&obs_img);
    
	      
	      
	      // Adding time-dependent noise here
	      
	      
	      
	      // Finalize output (e.g convert to magnitudes) and write
	      for(int i=0;i<obs_img.Nm;i++){
		obs_img.img[i] = -2.5*log10(obs_img.img[i]);
	      }
	      char buf[4];
	      sprintf(buf,"%03d",t);
	      std::string timestep = buf;
	      obs_img.writeImage(path+mock+"/OBS_"+band_name+"_"+timestep+".fits");
	    }
	  }
	  // *********************** End of product **************************************************	    

	  // Do some cleanup
	  delete(samp_LC_intrinsic);
	  for(int q=0;q<images.size();q++){
	    delete(samp_LC[q]);
	  }
	  
	  //std::cout << "done" << std::endl;
	}
	// Loop over extrinsic light curves ends here
	//0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=
      }
      // Loop over intrinsic light curves ends here
      //0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=0=

      for(int lc_in=0;lc_in<N_in;lc_in++){
	delete(LC_intrinsic[lc_in]);
      }
      for(int q=0;q<images.size();q++){
	for(int lc_ex=0;lc_ex<N_ex;lc_ex++){
	  delete(LC_extrinsic[q][lc_ex]);
	}
      }
      
    }
    //================= END:CREATE THE TIME VARYING LIGHT ====================
      


  }
  // Loop over the bands ends here
  // ===================================================================================================================
  // ===================================================================================================================

  
  return 0;
}