#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>

#include "json/json.h"

#include "contour-tracing.hpp"
#include "polygons.hpp"

#include "vkllib.hpp"


int main(int argc,char* argv[]){

  //=============== BEGIN:PARSE INPUT =======================
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


  
  

  //=============== BEGIN:GET CRITICAL LINES AND CAUSTICS =======================
  int super_res_x = 10*( static_cast<int>(ceil(width/jiplane["resolution"].asDouble())) );
  int super_res_y = 10*( static_cast<int>(ceil(height/jiplane["resolution"].asDouble())) );
  ImagePlane detA(super_res_x,super_res_y,width,height);

  mycollection->detJacobian(&detA,&detA);
  for(int i=0;i<detA.Nm;i++){
    if( detA.img[i] > 0 ){
      detA.img[i] = 0;
    } else {
      detA.img[i] = 1;
    }
  }
  detA.writeImage(output + "detA.fits");

  // Get detA contours on the image plane
  std::vector<Contour*> contours;
  mooreNeighborTracing(&detA,contours);

  // Add first point as last and close the polygon
  for(int i=0;i<contours.size();i++){
    contours[i]->x.push_back( contours[i]->x[0] );
    contours[i]->y.push_back( contours[i]->y[0] );
  }

  // Create caustic contours, but don't fill them yet
  std::vector<Contour*> caustics(contours.size());
  for(int i=0;i<contours.size();i++){
    Contour* mycontour = new Contour();
    mycontour->x.resize(contours[i]->x.size());
    mycontour->y.resize(contours[i]->y.size());
    caustics[i] = mycontour;
  } 

  // Deflect the contours to create the caustics
  point point_source = {root["point_source"]["x0"].asDouble(),root["point_source"]["y0"].asDouble()};
  double xdefl,ydefl;
  for(int i=0;i<contours.size();i++){
    for(int j=0;j<contours[i]->x.size();j++){
      mycollection->all_defl(contours[i]->x[j],contours[i]->y[j],xdefl,ydefl);
      caustics[i]->x[j] = xdefl;
      caustics[i]->y[j] = ydefl;
    }
  }

  // Create the json output object
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
      cau_x.append(caustics[i]->x[j]);
      cau_y.append(caustics[i]->y[j]);
    }

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
    delete(caustics[i]);
  }
  //================= END:GET CRITICAL LINES AND CAUSTICS =======================
  






  //=============== BEGIN:FIND OVERLAPPING TRIANGLES AND MULTIPLICITY =======================




  //================= END:FIND OVERLAPPING TRIANGLES AND MULTIPLICITY =======================



  
  return 0;
}
