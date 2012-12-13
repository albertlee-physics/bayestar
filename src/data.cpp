/*
 * data.cpp
 * 
 * Defines class for stellar data.
 * 
 * This file is part of bayestar.
 * Copyright 2012 Gregory Green
 * 
 * Bayestar is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */

#include "data.h"


TStellarData::TStellarData(std::string infile, uint32_t _healpix_index, double err_floor) {
	std::stringstream ss;
	ss << "/pixel " << healpix_index;
	load(infile, ss.str(), err_floor);
}



TStellarData::TStellarData(uint64_t _healpix_index, uint32_t _nside, bool _nested, double _l, double _b) {
	healpix_index = _healpix_index;
	nside = _nside;
	nested = _nested;
	l = _l;
	b = _b;
}


bool TStellarData::save(std::string fname, std::string group_name, int compression) {
	if((compression < 0) || (compression > 9)) {
		std::cerr << "! Invalid gzip compression level: " << compression << std::endl;
		return false;
	}
	
	hsize_t nstars = star.size();
	if(nstars == 0) {
		std::cerr << "! No stars to write." << std::endl;
		return false;
	}
	
	H5::Exception::dontPrint();
	
	H5::H5File *file = H5Utils::openFile(fname);
	if(file == NULL) { return false; }
	
	H5::Group *group = H5Utils::openGroup(file, group_name);
	if(group == NULL) {
		delete file;
		return false;
	}
	
	/*
	 *  Photometry
	 */
	
	// Datatype
	hsize_t nbands = NBANDS;
	H5::ArrayType mtype(H5::PredType::NATIVE_FLOAT, 1, &nbands);
	H5::CompType dtype(sizeof(TFileData));
	dtype.insertMember("obj_id", HOFFSET(TFileData, obj_id), H5::PredType::NATIVE_UINT64);
	dtype.insertMember("l", HOFFSET(TFileData, l), H5::PredType::NATIVE_FLOAT);
	dtype.insertMember("b", HOFFSET(TFileData, b), H5::PredType::NATIVE_FLOAT);
	dtype.insertMember("mag", HOFFSET(TFileData, mag), mtype);
	dtype.insertMember("err", HOFFSET(TFileData, err), mtype);
	
	// Dataspace
	hsize_t dim = nstars;
	H5::DataSpace dspace(1, &dim);
	
	// Property List
	H5::DSetCreatPropList plist;
	plist.setChunk(1, &nstars);
	plist.setDeflate(compression);
	
	// Dataset
	H5::DataSet dset = group->createDataSet("photometry", dtype, dspace, plist);
	
	// Write dataset
	TFileData* data = new TFileData[nstars];
	for(size_t i=0; i<nstars; i++) {
		data[i].obj_id = star[i].obj_id;
		data[i].l = star[i].l;
		data[i].b = star[i].b;
		for(size_t k=0; k<NBANDS; k++) {
			data[i].mag[k] = star[i].m[k];
			data[i].err[k] = star[i].err[k];
		}
	}
	dset.write(data, dtype);
	
	/*
	 *  Attributes
	 */
	
	dim = 1;
	H5::DataSpace att_dspace(1, &dim);
	
	H5::PredType att_dtype = H5::PredType::NATIVE_UINT64;
	H5::Attribute att_healpix_index = group->createAttribute("healpix_index", att_dtype, att_dspace);
	att_healpix_index.write(att_dtype, &healpix_index);
	
	att_dtype = H5::PredType::NATIVE_UINT32;
	H5::Attribute att_nside = group->createAttribute("nside", att_dtype, att_dspace);
	att_nside.write(H5::PredType::NATIVE_UINT32, &nside);
	
	att_dtype = H5::PredType::NATIVE_HBOOL;
	H5::Attribute att_nested = group->createAttribute("nested", att_dtype, att_dspace);
	att_nested.write(H5::PredType::NATIVE_HBOOL, &nested);
	
	att_dtype = H5::PredType::NATIVE_FLOAT;
	H5::Attribute att_l = group->createAttribute("l", att_dtype, att_dspace);
	att_l.write(H5::PredType::NATIVE_FLOAT, &l);
	
	att_dtype = H5::PredType::NATIVE_FLOAT;
	H5::Attribute att_b = group->createAttribute("b", att_dtype, att_dspace);
	att_l.write(H5::PredType::NATIVE_FLOAT, &b);
	
	file->close();
	
	delete data;
	delete group;
	delete file;
	
	return true;
}


void TStellarData::TMagnitudes::set(TStellarData::TFileData dat, double err_floor) {
	obj_id = dat.obj_id;
	l = dat.l;
	b = dat.b;
	lnL_norm = NBANDS * 0.9189385332;
	for(unsigned int i=0; i<NBANDS; i++) {
		m[i] = dat.mag[i];
		err[i] = sqrt(dat.err[i]*dat.err[i] + err_floor*err_floor);
		lnL_norm += log(err[i]);
		N_det[i] = dat.N_det[i];
	}
}


bool TStellarData::load(std::string fname, std::string group_name, double err_floor) {
	//H5::Exception::dontPrint();
	
	H5::H5File *file = H5Utils::openFile(fname);
	if(file == NULL) { return false; }
	
	H5::Group *group = H5Utils::openGroup(file, group_name);
	if(group == NULL) {
		delete file;
		return false;
	}
	
	H5::DataSet dataset = file->openDataSet("photometry");
	
	/*
	 *  Photometry
	 */
	
	// Datatype
	hsize_t nbands = NBANDS;
	H5::ArrayType floatarrtype(H5::PredType::NATIVE_FLOAT, 1, &nbands);
	H5::ArrayType uint32arrtype(H5::PredType::NATIVE_UINT32, 1, &nbands);
	H5::CompType dtype(sizeof(TFileData));
	dtype.insertMember("obj_id", HOFFSET(TFileData, obj_id), H5::PredType::NATIVE_UINT64);
	dtype.insertMember("l", HOFFSET(TFileData, l), H5::PredType::NATIVE_DOUBLE);
	dtype.insertMember("b", HOFFSET(TFileData, b), H5::PredType::NATIVE_DOUBLE);
	dtype.insertMember("mag", HOFFSET(TFileData, mag), floatarrtype);
	dtype.insertMember("err", HOFFSET(TFileData, err), floatarrtype);
	dtype.insertMember("N_det", HOFFSET(TFileData, N_det), uint32arrtype);
	
	// Dataspace
	hsize_t length;
	H5::DataSpace dataspace = dataset.getSpace();
	dataspace.getSimpleExtentDims(&length);
	
	// Read in dataset
	TFileData* data_buf = new TFileData[length];
	dataset.read(data_buf, dtype);
	std::cerr << "# Read in dimensions." << std::endl;
	
	TMagnitudes mag_tmp;
	for(size_t i=0; i<length; i++) {
		mag_tmp.set(data_buf[i], err_floor);
		star.push_back(mag_tmp);
	}
	
	/*
	 *  Attributes
	 */
	
	//hsize_t dim = 1;
	//H5::DataSpace att_dspace(1, &dim);
	
	H5::Attribute att = dataset.openAttribute("healpix_index");
	H5::DataType att_dtype = H5::PredType::NATIVE_UINT64;
	att.read(att_dtype, reinterpret_cast<void*>(&healpix_index));
	
	att = dataset.openAttribute("nested");
	att_dtype = H5::PredType::NATIVE_HBOOL;
	att.read(att_dtype, reinterpret_cast<void*>(&nested));
	
	att = dataset.openAttribute("nside");
	att_dtype = H5::PredType::NATIVE_UINT32;
	att.read(att_dtype, reinterpret_cast<void*>(&nside));
	
	att = dataset.openAttribute("l");
	att_dtype = H5::PredType::NATIVE_DOUBLE;
	att.read(att_dtype, reinterpret_cast<void*>(&l));
	
	att = dataset.openAttribute("b");
	att_dtype = H5::PredType::NATIVE_DOUBLE;
	att.read(att_dtype, reinterpret_cast<void*>(&b));
	
	delete data_buf;
	delete group;
	delete file;
	
	return true;
}






TDraw1D::TDraw1D(func_ptr_t func, double _x_min, double _x_max, void* _params, unsigned int samples, bool is_log)
	: x_of_P(NULL), r(NULL), params(_params)
{
	assert(samples > 1);
	
	x_min = _x_min;
	x_max = _x_max;
	double dx = (x_max - x_min) / (double)(samples - 1);
	
	// Construct an interpolator for P(x)
	double fill = -1.;
	TMultiLinearInterp<double> P_of_x(&x_min, &x_max, &samples, 1, fill);
	double x;
	double P = 0.;
	for(unsigned int i=0; i<samples; i++) {
		x = x_min + (double)i * dx;
		P_of_x.set(&x, P);
		if(i < samples - 1) {
			if(is_log) { P += dx * exp(func(x, params)); } else { P += dx * func(x, params); }
		}
	}
	double P_norm = P;
	
	// Invert the interpolator for get x(P)
	double P_min = 0.;
	double P_max = 1.;
	double dP = 1. / (double)(samples - 1);
	x_of_P = new TMultiLinearInterp<double>(&P_min, &P_max, &samples, 1, fill);
	unsigned int k_last = 0;
	double P_tmp, dPdx;
	for(unsigned int i=0; i<samples; i++) {
		P = (double)i * dP;
		for(unsigned int k=k_last+1; k<samples; k++) {
			x = x_min + (double)k * dx;
			P_tmp = P_of_x(&x) / P_norm;
			if(P_tmp >= P) {
				dPdx = (P_tmp - (double)(i-1)*dP) / dx;
				x = x_min + (double)(k-1) * dx + dP / dPdx;
				k_last = k - 1;
				break;
			}
		}
		
		x_of_P->set(&P, x);
	}
	P_tmp = 1.;
	x_of_P->set(&P_tmp, x_max);
	
	seed_gsl_rng(&r);
}

TDraw1D::~TDraw1D() {
	delete x_of_P;
	gsl_rng_free(r);
}


double TDraw1D::operator()() {
	double P = gsl_rng_uniform(r);
	return (*x_of_P)(&P);
}



double log_dNdmu_draw(double DM, void* params) {
	TGalacticLOSModel *gal_model = static_cast<TGalacticLOSModel*>(params);
	return gal_model->log_dNdmu(DM);
}

double disk_IMF_draw(double logMass, void* params) {
	TGalacticLOSModel *gal_model = static_cast<TGalacticLOSModel*>(params);
	return gal_model->IMF(logMass, 0);
}

double halo_IMF_draw(double logMass, void* params) {
	TGalacticLOSModel *gal_model = static_cast<TGalacticLOSModel*>(params);
	return gal_model->IMF(logMass, 1);
}

double disk_SFR_draw(double tau, void* params) {
	TGalacticLOSModel *gal_model = static_cast<TGalacticLOSModel*>(params);
	return gal_model->SFR(tau, 0);
}

double halo_SFR_draw(double tau, void* params) {
	TGalacticLOSModel *gal_model = static_cast<TGalacticLOSModel*>(params);
	return gal_model->SFR(tau, 1);
}

double disk_FeH_draw(double FeH, void* params) {
	TGalacticLOSModel *gal_model = static_cast<TGalacticLOSModel*>(params);
	return gal_model->p_FeH_fast(5., FeH, 0);
}

double halo_FeH_draw(double FeH, void* params) {
	TGalacticLOSModel *gal_model = static_cast<TGalacticLOSModel*>(params);
	return gal_model->p_FeH_fast(23., FeH, 1);
}

double Mr_draw(double Mr, void* params) {
	TStellarModel *stellar_model = static_cast<TStellarModel*>(params);
	return stellar_model->get_log_lf(Mr);
}

void draw_from_synth_model(size_t nstars, double RV, TGalacticLOSModel& gal_model, TSyntheticStellarModel& stellar_model,
                     TStellarData& stellar_data, TExtinctionModel& ext_model, double (&mag_limit)[5]) {
	unsigned int samples = 1000;
	void* gal_model_ptr = static_cast<void*>(&gal_model);
	
	double DM_min = 0.;
	double DM_max = 25.;
	TDraw1D draw_DM(&log_dNdmu_draw, DM_min, DM_max, gal_model_ptr, samples, true);
	
	double logMass_min = -0.9;
	double logMass_max = 1.1;
	TDraw1D draw_logMass_disk(&disk_IMF_draw, logMass_min, logMass_max, gal_model_ptr, samples, false);
	TDraw1D draw_logMass_halo(&halo_IMF_draw, logMass_min, logMass_max, gal_model_ptr, samples, false);
	
	double tau_min = 1.e6;
	double tau_max = 13.e9;
	TDraw1D draw_tau_disk(&disk_SFR_draw, tau_min, tau_max, gal_model_ptr, samples, false);
	TDraw1D draw_tau_halo(&halo_SFR_draw, tau_min, tau_max, gal_model_ptr, samples, false);
	
	double FeH_min = -2.5;
	double FeH_max = 1.;
	TDraw1D draw_FeH_disk(&disk_FeH_draw, FeH_min, FeH_max, gal_model_ptr, samples, false);
	TDraw1D draw_FeH_halo(&halo_FeH_draw, FeH_min, FeH_max, gal_model_ptr, samples, false);
	
	stellar_data.clear();
	gal_model.get_lb(stellar_data.l, stellar_data.b);
	
	gsl_rng *r;
	seed_gsl_rng(&r);
	double EBV, DM, logtau, logMass, FeH;
	double f_halo;
	bool halo, in_lib, observed;
	TSED sed;
	double mag[NBANDS];
	double err[NBANDS];
	std::cout << "Component E(B-V)    DM        log(Mass) log(tau)  [Fe/H]    g         r         i         z         y        " << std::endl;
	std::cout << "=============================================================================================================" << std::endl;
	std::cout.flags(std::ios::left);
	std::cout.precision(3);
	for(size_t i=0; i<nstars; i++) {
		observed = false;
		while(!observed) {
			// Draw E(B-V)
			EBV = gsl_ran_chisq(r, 1.);
			
			// Draw DM
			DM = draw_DM();
			
			// Draw stellar type
			f_halo = gal_model.f_halo(DM);
			halo = (gsl_rng_uniform(r) < f_halo);
			in_lib = false;
			while(!in_lib) {
				if(halo) {
					logMass = draw_logMass_halo();
					logtau = log10(draw_tau_halo());
					FeH = draw_FeH_halo();
				} else {
					logMass = draw_logMass_disk();
					logtau = log10(draw_tau_disk());
					FeH = draw_FeH_disk();
				}
				in_lib = stellar_model.get_sed(logMass, logtau, FeH, sed);
			}
			
			// Generate magnitudes
			observed = true;
			unsigned int N_nonobs = 0;
			for(size_t k=0; k<NBANDS; k++) {
				mag[k] = sed.absmag[k] + DM + EBV * ext_model.get_A(RV, k);
				err[k] = 0.02 + 0.1*exp(mag[i]-mag_limit[i]-1.5);
				mag[k] += gsl_ran_gaussian_ziggurat(r, err[k]);
				
				// Require detection in g band and 3 other bands
				if(mag[k] > mag_limit[k]) {
					N_nonobs++;
					if((k == 0) || N_nonobs > 1) {
						observed = false;
						break;
					}
				}
			}
		}
		
		std::cout << (halo ? "halo" : "disk") << "      ";
		std::cout << std::setw(9) << EBV << " ";
		std::cout << std::setw(9) << DM << " ";
		std::cout << std::setw(9) << logMass << " ";
		std::cout << std::setw(9) << logtau << " ";
		std::cout << std::setw(9) << FeH << " ";
		for(size_t k=0; k<NBANDS; k++) {
			std::cout << std::setw(9) << mag[k] << " ";
		}
		std::cout << std::endl;
		
		TStellarData::TMagnitudes mag_tmp(mag, err);
		mag_tmp.obj_id = i;
		mag_tmp.l = stellar_data.l;
		mag_tmp.b = stellar_data.b;
		stellar_data.star.push_back(mag_tmp);
		
	}
	std::cout << std::endl;
	
	gsl_rng_free(r);
	
	/*std::vector<bool> filled;
	DM_of_P.get_filled(filled);
	for(std::vector<bool>::iterator it = filled.begin(); it != filled.end(); ++it) {
		std::cout << *it << std::endl;
	}
	*/
	
}



void draw_from_emp_model(size_t nstars, double RV, TGalacticLOSModel& gal_model, TStellarModel& stellar_model,
                     TStellarData& stellar_data, TExtinctionModel& ext_model, double (&mag_limit)[5]) {
	unsigned int samples = 1000;
	void* gal_model_ptr = static_cast<void*>(&gal_model);
	void* stellar_model_ptr = static_cast<void*>(&stellar_model);
	
	double DM_min = 0.;
	double DM_max = 25.;
	TDraw1D draw_DM(&log_dNdmu_draw, DM_min, DM_max, gal_model_ptr, samples, true);
	
	double FeH_min = -2.5;
	double FeH_max = 1.;
	TDraw1D draw_FeH_disk(&disk_FeH_draw, FeH_min, FeH_max, gal_model_ptr, samples, false);
	TDraw1D draw_FeH_halo(&halo_FeH_draw, FeH_min, FeH_max, gal_model_ptr, samples, false);
	
	double Mr_min = -1.;
	double Mr_max = mag_limit[1];
	TDraw1D draw_Mr(&Mr_draw, Mr_min, Mr_max, stellar_model_ptr, samples, true);
	
	stellar_data.clear();
	gal_model.get_lb(stellar_data.l, stellar_data.b);
	
	gsl_rng *r;
	seed_gsl_rng(&r);
	double EBV, DM, Mr, FeH;
	double f_halo;
	bool halo, in_lib, observed;
	TSED sed;
	double mag[NBANDS];
	double err[NBANDS];
	std::cout << "#         Component E(B-V)    DM        Mr        [Fe/H]    g         r         i         z         y        " << std::endl;
	std::cout << "=============================================================================================================" << std::endl;
	std::cout.flags(std::ios::left);
	std::cout.precision(3);
	for(size_t i=0; i<nstars; i++) {
		observed = false;
		while(!observed) {
			// Draw DM
			DM = draw_DM();
			
			// Draw E(B-V)
			//EBV = gsl_ran_chisq(r, 1.);
			
			EBV = 0.;
			if(DM > 5.) { EBV += 0.5; }
			if(DM > 10.) { EBV += 3.5; }
			
			// Draw stellar type
			f_halo = gal_model.f_halo(DM);
			halo = (gsl_rng_uniform(r) < f_halo);
			in_lib = false;
			while(!in_lib) {
				if(halo) {
					FeH = draw_FeH_halo();
				} else {
					FeH = draw_FeH_disk();
				}
				Mr = draw_Mr();
				in_lib = stellar_model.get_sed(Mr, FeH, sed);
			}
			
			// Generate magnitudes
			observed = true;
			unsigned int N_nonobs = 0;
			for(size_t k=0; k<NBANDS; k++) {
				mag[k] = sed.absmag[k] + DM + EBV * ext_model.get_A(RV, k);
				err[k] = 0.02 + 0.1*exp(mag[i]-mag_limit[i]-1.5);
				if(err[k] > 1.5) { err[k] = 1.5; }
				mag[k] += gsl_ran_gaussian_ziggurat(r, err[k]);
				
				// Require detection in g band and 3 other bands
				if(mag[k] > mag_limit[k]) {
					N_nonobs++;
					if((k == 0) || N_nonobs > 1) {
						observed = false;
						break;
					}
				}
			}
		}
		
		std::cout << std::setw(9) << i+1 << " ";
		std::cout << (halo ? "halo" : "disk") << "      ";
		std::cout << std::setw(9) << EBV << " ";
		std::cout << std::setw(9) << DM << " ";
		std::cout << std::setw(9) << Mr << " ";
		std::cout << std::setw(9) << FeH << " ";
		for(size_t k=0; k<NBANDS; k++) {
			std::cout << std::setw(9) << mag[k] << " ";
		}
		std::cout << std::endl;
		
		TStellarData::TMagnitudes mag_tmp(mag, err);
		mag_tmp.obj_id = i;
		mag_tmp.l = stellar_data.l;
		mag_tmp.b = stellar_data.b;
		stellar_data.star.push_back(mag_tmp);
		
	}
	std::cout << std::endl;
	
	gsl_rng_free(r);
	
	/*std::vector<bool> filled;
	DM_of_P.get_filled(filled);
	for(std::vector<bool>::iterator it = filled.begin(); it != filled.end(); ++it) {
		std::cout << *it << std::endl;
	}
	*/
	
}


/*************************************************************************
 * 
 *   Auxiliary Functions
 * 
 *************************************************************************/

#ifndef __SEED_GSL_RNG_
#define __SEED_GSL_RNG_
// Seed a gsl_rng with the Unix time in nanoseconds
inline void seed_gsl_rng(gsl_rng **r) {
	timespec t_seed;
	clock_gettime(CLOCK_REALTIME, &t_seed);
	long unsigned int seed = 1e9*(long unsigned int)t_seed.tv_sec;
	seed += t_seed.tv_nsec;
	*r = gsl_rng_alloc(gsl_rng_taus);
	gsl_rng_set(*r, seed);
}
#endif