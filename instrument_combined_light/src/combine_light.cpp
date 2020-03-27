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

  Json::Value images;
  Json::Value intrinsic_lc;
  Json::Value extrinsic_lc;
  if( root.isMember("point_source") ){
    // Read the image parameters
    fin.open(output+"multiple_images.json",std::ifstream::in);
    fin >> images;
    fin.close();
    
    // Read intrinsic light curve
    if( root["point_source"]["variability"]["intrinsic"]["type"].asString() == "custom" ){
      fin.open(input_path+"intrinsic_light_curves.json",std::ifstream::in);
    } else {
      fin.open(output+"intrinsic_light_curves.json",std::ifstream::in);
    }
    fin >> intrinsic_lc;
    fin.close();

    // Read extrinsic light curve
    if( root["point_source"]["variability"]["extrinsic"]["type"].asString() == "custom" ){
      fin.open(input_path+"extrinsic_light_curves.json",std::ifstream::in);
    } else {
      fin.open(output+"extrinsic_light_curves.json",std::ifstream::in);
    }
    fin >> extrinsic_lc;
    fin.close();
  }
  //================= END:PARSE INPUT =======================



  // Loop over the bands
  const Json::Value bands = root["instrument"]["bands"];
  for(int b=0;b<bands.size();b++){
    std::string band_name = bands[b]["name"].asString();
    
    // Set output image plane in super-resolution
    double width  = bands[b]["field-of-view_x"].asDouble();
    double height = bands[b]["field-of-view_y"].asDouble();
    int res_x = static_cast<int>(ceil(width/bands[b]["resolution"].asDouble()));
    int res_y = static_cast<int>(ceil(height/bands[b]["resolution"].asDouble()));
    int super_res_x = 10*res_x;
    int super_res_y = 10*res_y;
    ImagePlane mysim(super_res_x,super_res_y,width,height);

    
    // Get the psf in super-resolution, crop it, and create convolution kernel
    std::string psf_path = input_path + "psf_" + band_name + ".fits";
    double psf_width  = bands[b]["psf"]["width"].asDouble();
    double psf_height = bands[b]["psf"]["height"].asDouble();
    int psf_Nx = bands[b]["psf"]["pix_x"].asInt();
    int psf_Ny = bands[b]["psf"]["pix_y"].asInt();
    PSF mypsf(psf_path,psf_Nx,psf_Ny,psf_width,psf_height,&mysim);
    mypsf.cropPSF(0.99);
    mypsf.createKernel(mysim.Ni,mysim.Nj);

    
    //=============== BEGIN:CREATE THE FIXED EXTENDED LENSED LIGHT ====================
    ImagePlane* extended = new ImagePlane(output+"lensed_image_super.fits",super_res_x,super_res_y,width,height);
    mypsf.convolve(extended);
    //extended->writeImage(output+"psf_lensed_image_super.fits");
    //================= END:CREATE THE FIXED EXTENDED LENSED LIGHT ====================

    //=============== BEGIN:CREATE THE FIXED LENS GALAXY LIGHT ====================
    ImagePlane* lens_light = new ImagePlane(output+"lens_light_super.fits",super_res_x,super_res_y,width,height);
    mypsf.convolve(lens_light);
    //lens_light->writeImage(output+"psf_lens_light_super.fits");
    //================= END:CREATE THE FIXED LENS GALAXY LIGHT ====================


    //=============== BEGIN:CREATE FIXED MASKS ====================
    //createMask(extended,0.3,0.08,output+"mask_lensed_image_super.fits");
    //================= END:CREATE FIXED MASKS ====================


    // Create combined extended lensed features and lens light
    ImagePlane base(super_res_x,super_res_y,width,height); 
    for(int i=0;i<base.Nm;i++){
      base.img[i] = lens_light->img[i] + extended->img[i];
    }
    delete(extended);
    delete(lens_light);


    
    if( root.isMember("point_source") ){
	
      //=============== BEGIN:CREATE THE TIME VARYING LIGHT ====================
      
      // Read observed time vector
      std::vector<double> obs_time;
      for(int t=0;t<bands[b]["time"].size();t++){
	obs_time.push_back(bands[b]["time"][t].asDouble());
      }
	
      // Get maximum image time delay
      double td_max = 0.0;
      for(int q=0;q<images.size();q++){
	double td = images[q]["dt"].asDouble();
	if( td > td_max ){
	  td_max = td;
	}
      }

      // Read the intrinsic light curves
      LightCurve LC_intrinsic(intrinsic_lc[band_name]);
      for(int i=0;i<LC_intrinsic.signal.size();i++){
	LC_intrinsic.signal[i] = pow(10.0,-0.4*LC_intrinsic.signal[i]); // convert from magnitudes to intensities
      }


      // Check time limitations
      double tmax_intrinsic = LC_intrinsic.time.back();
      double tmax_obs = obs_time.back();
      if( td_max > tmax_obs ){
	printf("Observing period (%f days) shorter than the maximum time delay (%f days).\n",tmax_obs,td_max);
	printf("Increase the observing time period!!!\n");
	return 1;
      }
      if( (td_max+tmax_obs) > tmax_intrinsic ){
	printf("Intrinsic light curve duration (%f days) shorter than the maximum time delay plus the observing period (%f + %f days).\n",tmax_intrinsic,td_max,tmax_obs);
	int i=0;
	while( obs_time[i] < (tmax_intrinsic-td_max) ){
	  i++;
	}
	obs_time.resize(i);
	printf("Observing period is truncated to %f days!!!\n",obs_time.back());
      }

      

      // Read the extrinsic light curves
      std::vector<LightCurve> LC_extrinsic;
      for(int q=0;q<images.size();q++){
	LC_extrinsic.push_back(extrinsic_lc[q][band_name]);
      }

      // Create interpolated intrinsic + extrinsic signal for each image
      double* intrinsic_signal = (double*) malloc(obs_time.size()*sizeof(double));
      double** img_signal = (double**) malloc(images.size()*sizeof(double*));
      for(int q=0;q<images.size();q++){
	LC_intrinsic.interpolate(obs_time,td_max - images[q]["dt"].asDouble(),intrinsic_signal);
	img_signal[q] = (double*) malloc(obs_time.size()*sizeof(double));
	if( LC_extrinsic[q].time.size() > 0 ){
	  LC_extrinsic[q].interpolate(obs_time,0.0,img_signal[q]);
	  for(int t=0;t<obs_time.size();t++){
	    // Do nothing in this loop in order to keep the microlensing signal only
	    //img_signal[q][t] = intrinsic_signal[t]; // this line includes only the intrinsic signal and excludes microlensing
	    img_signal[q][t] *= intrinsic_signal[t]; // this line includes both intrinsic and microlensing signals
	  }
	} else {
	  for(int t=0;t<obs_time.size();t++){
	    img_signal[q][t] = intrinsic_signal[t]; // this line includes only the intrinsic signal and excludes microlensing
	  }
	}
      }
      free(intrinsic_signal);

      Json::Value total_lcs;
      for(int q=0;q<images.size();q++){
	double macro_mag = abs(images[q]["mag"].asDouble());
	Json::Value total_lc,signal;
	for(int t=0;t<obs_time.size();t++){
	  signal.append(-2.5*log10(macro_mag*img_signal[q][t]));
	}
	total_lc[band_name]["signal"] = signal;
	total_lcs.append(total_lc);
      }
      std::ofstream file_total_lcs(output+"observed_light_curves.json");
      file_total_lcs << total_lcs;
      file_total_lcs.close();
      
      
      /*
      for(int t=0;t<obs_time.size();t++){
	printf("%3d: ",t);
	for(int q=0;q<images.size();q++){
	  printf("%f ",img_signal[q][t]);
	}
	printf("\n");
      }
      */




      
      
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
	psf_partial_sum[q] = 1.0;
      }
	
      
      // Loop over time starts here
      for(int t=0;t<obs_time.size();t++){
	ImagePlane pp_light(super_res_x,super_res_y,width,height);

	/*
	for(int q=0;q<images.size();q++){
	  // Calcuate the PSF sum and its contribution to each image pixel, and keep only the non-zeros
	  double value,xout,yout;
	  double sum = 0.0;
	  std::vector<int> indices;
	  std::vector<double> values;
	  for(int i=0;i<pp_light.Nm;i++){
	    transPSF[q]->applyTransform(pp_light.x[i],pp_light.y[i],xout,yout);
	    value = transPSF[q]->interpolateValue(xout,yout,&mypsf);
	    if( value > 0.0 ){
	      indices.push_back(i);
	      values.push_back(value);
	      sum += value;
	    }
	  }

	  // Assign the corrected by the PSF sum value to each image pixel
	  double macro_mag = abs(images[q]["mag"].asDouble());
	  double factor = macro_mag*img_signal[q][t]/sum;
	  for(int j=0;j<indices.size();j++){
	    pp_light.img[indices[j]] += factor*values[j];
	  }
	}
	*/
	
        for(int q=0;q<images.size();q++){
	  double macro_mag = abs(images[q]["mag"].asDouble());
	  //double macro_mag = 10.0;
	  double factor = macro_mag*(img_signal[q][t]/psf_partial_sum[q]);
	  for(int i=0;i<PSFoffsets[q].ni;i++){
	    for(int j=0;j<PSFoffsets[q].nj;j++){
	      int index_img = pp_light.Nj*i + j;
	      int index_psf = i*PSF_list[q]->cropped_psf->Nj + j;
	      //pp_light.img[PSFoffsets[q].offset_image + pp_light.Nj*i + j] += 1.0;
	      pp_light.img[PSFoffsets[q].offset_image + index_img] += factor*PSF_list[q]->cropped_psf->img[PSFoffsets[q].offset_cropped + index_psf];
	    }
	  }
	}

	



	// Add time-dependent image to base
	for(int i=0;i<pp_light.Nm;i++){
	  //pp_light.img[i] = pp_light.img[i] + base.img[i];
	  pp_light.img[i] = -2.5*log10(pp_light.img[i] + base.img[i]);
	}

	// Bin image from 'super' to observed resolution
	ImagePlane obs_img(res_x,res_y,width,height);
	int* counts = (int*) calloc(obs_img.Nm,sizeof(int));
	double inf_dx = width/super_res_x;
	double inf_dy = height/super_res_y;
	double obs_dx = width/res_x;
	double obs_dy = height/res_y;
	for(int i=0;i<pp_light.Ni;i++){
	  int ii = (int) floor(i*inf_dy/obs_dy);
	  for(int j=0;j<pp_light.Nj;j++){
	    int jj = (int) floor(j*inf_dx/obs_dx);
	    obs_img.img[ii*obs_img.Nj + jj] += pp_light.img[i*pp_light.Nj + j];
	    counts[ii*obs_img.Nj + jj] += 1;
	  }
	}
	for(int i=0;i<obs_img.Nm;i++){
	  obs_img.img[i] = obs_img.img[i]/counts[i];
	}
	free(counts);



	// Adding noise here

	
	char buf[4];
	sprintf(buf,"%03d",t);
	std::string timestep = buf;
	obs_img.writeImage(output + "OBS_" + band_name + "_" + timestep + ".fits");
      }

      
      for(int q=0;q<images.size();q++){
	free(img_signal[q]);
	//free(transPSF[q]);
      }
      free(img_signal);
      //================= END:CREATE THE TIME VARYING LIGHT ====================

    } else {

      // Bin image from 'super' to observed resolution
      ImagePlane obs_img(res_x,res_y,width,height);
      int* counts = (int*) calloc(obs_img.Nm,sizeof(int));
      double inf_dx = width/super_res_x;
      double inf_dy = height/super_res_y;
      double obs_dx = width/res_x;
      double obs_dy = height/res_y;
      for(int i=0;i<base.Ni;i++){
	int ii = (int) floor(i*inf_dy/obs_dy);
	for(int j=0;j<base.Nj;j++){
	  int jj = (int) floor(j*inf_dx/obs_dx);
	  obs_img.img[ii*obs_img.Nj + jj] += base.img[i*base.Nj + j];
	  counts[ii*obs_img.Nj + jj] += 1;
	}
      }
      for(int i=0;i<obs_img.Nm;i++){
	obs_img.img[i] = obs_img.img[i]/counts[i];
      }
      free(counts);
      obs_img.writeImage(output + "OBS_" + band_name + ".fits");
      
    }
    
  }
  
  return 0;
}
