#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>
#include <ctime>

#include "json/json.h"

#include "polygons.hpp"
#include "polyclip.hpp"
#include "initFuncs.hpp"
#include "sourceProfile.hpp"
#include "contour-tracing.hpp"

#include "nonLinearPars.hpp"
#include "massModels.hpp"
#include "imagePlane.hpp"


using std::cout;
using std::endl;




double myRandomNumber(double min,double max){
  double r = (double) rand() / (double)RAND_MAX;
  return min + r * (max - min);
}


int main(int argc,char* argv[]){

  //=============== BEGIN:PARSE INPUT =======================
  //Read in the json parameters
  std::ifstream fin;
  Json::Value::Members jmembers;

  // Read the main projection parameters
  Json::Value root;
  fin.open(argv[1],std::ifstream::in);
  fin >> root;
  fin.close();
  
  // Read the cosmological parameters
  Json::Value cosmo;
  fin.open(argv[2],std::ifstream::in);
  fin >> cosmo;
  fin.close();

  std::string output = argv[3];
  
  // Initialize image plane
  const Json::Value jiplane = root["instrument"]["bands"][0];
  double width  = jiplane["field-of-view_x"].asDouble();
  double height = jiplane["field-of-view_y"].asDouble();
  int super_res_x = 10*( static_cast<int>(ceil(width/jiplane["resolution"].asDouble())) );
  int super_res_y = 10*( static_cast<int>(ceil(height/jiplane["resolution"].asDouble())) );
  ImagePlane mysim(super_res_x,super_res_y,width,height);
  //================= END:PARSE INPUT =======================




  //=============== BEGIN:CREATE THE LENSES ====================
  const Json::Value jlens = root["lenses"][0];

  // Initialize mass model physical parameters
  jmembers = jlens["external_shear"].getMemberNames();
  
  std::vector<Nlpar*> ext_pars;
  for(int i=0;i<jmembers.size();i++){
    double value = 0.0;
    if( jmembers[i] == "phi" ){
      value = jlens["external_shear"][jmembers[i]].asDouble() + 90.0;
    } else {
      value = jlens["external_shear"][jmembers[i]].asDouble();
    }
    ext_pars.push_back( new Nlpar(jmembers[i],0,0,value,0,0,0) );
  }
  CollectionMassModels* mycollection = new CollectionMassModels(ext_pars);
  for(int i=0;i<ext_pars.size();i++){ delete(ext_pars[i]); }
  ext_pars.clear();

  // Initialize main mass model
  mycollection->models.resize(jlens["mass_model"].size());
  for(int k=0;k<jlens["mass_model"].size();k++){
    std::string mmodel = jlens["mass_model"][k]["type"].asString();

    if( mmodel == "custom" ){

      jmembers = jlens["mass_model"][k]["pars"].getMemberNames();
      std::map<std::string,std::string> pars;
      for(int i=0;i<jmembers.size();i++){
	pars[jmembers[i]] = jlens["mass_model"][k]["pars"][jmembers[i]].asString();
      }
      mycollection->models[k] = FactoryMassModel::getInstance()->createMassModel(mmodel,pars);

    } else if ( mmodel == "eagle" ){
      
    } else {
      
      jmembers = jlens["mass_model"][k]["pars"].getMemberNames();
      std::vector<Nlpar*> pars;
      for(int i=0;i<jmembers.size();i++){
	pars.push_back( new Nlpar(jmembers[i],0,0,jlens["mass_model"][k]["pars"][jmembers[i]].asDouble(),0,0,0) ); // only nam and val have meaning in this call
      }
      mycollection->models[k] = FactoryMassModel::getInstance()->createMassModel(mmodel,pars,cosmo[0]["Dls"].asDouble(),cosmo[0]["Ds"].asDouble());
    }
  }

  //  for(int i=0;i<mycollection->models.size();i++){
  //    mycollection->models[i]->printMassPars();
  //  }
  //  mycollection->printPhysPars();
  //================= END:CREATE THE LENSES ====================





  //=============== BEGIN:CREATE THE SOURCES =======================
  const Json::Value jsource = root["source"]["light_profile"];
  BaseProfile* mysource = NULL;

  std::string smodel = jsource["type"].asString();
  if( smodel == "analytic" ){

    std::vector<std::string> names;
    std::vector<std::map<std::string,double> > all_pars;
    for(int i=0;i<jsource["pars"].size();i++){
      std::string name = jsource["pars"][i]["type"].asString();
      names.push_back(name);

      std::map<std::string,double> pars;
      const Json::Value::Members jpars = jsource["pars"][i].getMemberNames();
      for(int j=0;j<jpars.size();j++){
	if( jpars[j] != "type" ){
	  pars[jpars[j]] = jsource["pars"][i][jpars[j]].asDouble();
	}
      }
      all_pars.push_back(pars);
    }
    mysource = new Analytic(names,all_pars);

  } else if( smodel == "delaunay" ){

    std::string filename = jsource["pars"]["filename"].asString();
    mysource = new myDelaunay(filename);

  } else if( smodel == "custom" ){

    std::string filename = jsource["pars"]["filename"].asString();
    int Ni               = jsource["pars"]["Ni"].asInt();
    int Nj               = jsource["pars"]["Nj"].asInt();
    double height        = jsource["pars"]["height"].asDouble();
    double width         = jsource["pars"]["width"].asDouble();
    double x0            = jsource["pars"]["x0"].asDouble();
    double y0            = jsource["pars"]["y0"].asDouble();
    mysource = new fromFITS(filename,Ni,Nj,height,width,x0,y0);

  } else {

    std::cout << "Unknown source profile type" << std::endl;
    return 1;

  }
  //================= END:CREATE THE SOURCES =======================





  
  //=============== BEGIN:PRODUCE IMAGE USING RAY-SHOOTING =======================
  double xdefl,ydefl;
  for(int i=0;i<mysim.Nm;i++){
    mycollection->all_defl(mysim.x[i],mysim.y[i],xdefl,ydefl);
    mysim.img[i] = mysource->value(xdefl,ydefl);
  }
  //================= END:PRODUCE IMAGE USING RAY-SHOOTING =======================



  
  //=============== BEGIN:GET CRITICAL LINES AND CAUSTICS =======================
  ImagePlane detA = mysim;
  mycollection->detJacobian(&mysim,&detA);
  for(int i=0;i<detA.Nm;i++){
    if( detA.img[i] > 0 ){
      detA.img[i] = 0;
    } else {
      detA.img[i] = 1;
    }
  }
  detA.writeImage(output + "detA.fits");
  std::vector<Contour*> contours;
  mooreNeighborTracing(&detA,contours);

  
  // create the main object
  Json::Value json_caustics;
  Json::Value json_criticals;
  for(int i=0;i<contours.size();i++){
    Json::Value crit_x;
    Json::Value crit_y;
    Json::Value cau_x;
    Json::Value cau_y;
    for(int j=0;j<contours[i]->x.size();j++){
      crit_x.append(contours[i]->x[j]);
      crit_y.append(contours[i]->y[j]);
      mycollection->all_defl(contours[i]->x[j],contours[i]->y[j],xdefl,ydefl);
      cau_x.append(xdefl);
      cau_y.append(ydefl);
    }
    cau_x.append(cau_x[0]);
    cau_y.append(cau_y[0]);
    crit_x.append(crit_x[0]);
    crit_y.append(crit_y[0]);

    Json::Value caustic;
    caustic["x"] = cau_x;
    caustic["y"] = cau_y;
    json_caustics.append(caustic);
    Json::Value critical;
    critical["x"] = crit_x;
    critical["y"] = crit_y;
    json_criticals.append(critical);
  }
  
  std::ofstream file_caustics(output+"caustics.json");
  file_caustics << json_caustics;
  file_caustics.close();
  std::ofstream file_criticals(output+"criticals.json");
  file_criticals << json_criticals;
  file_criticals.close();
  
  for(int i=0;i<contours.size();i++){
    delete(contours[i]);
  }
  //================= END:GET CRITICAL LINES AND CAUSTICS =======================
  
  
  
  
  //=============== BEGIN:OUTPUT =======================
  // Super-resolved lensed image
  mysim.writeImage(output + "lensed_image_super.fits");
  // Super-resolved source image
  mysource->outputProfile(output + "source_super.fits");
  //================= END:OUTPUT =======================





  //=============== BEGIN:CLEAN UP =======================
  delete(mycollection);
  delete(mysource);
  //================= END:CLEAN UP =======================


  return 0;
}