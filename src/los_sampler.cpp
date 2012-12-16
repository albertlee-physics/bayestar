/*
 * los_sampler.cpp
 * 
 * Samples from posterior distribution of line-of-sight extinction
 * model, given a set of stellar posterior densities in DM, E(B-V).
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

#include "los_sampler.h"


void sample_los_extinction(std::string out_fname, TMCMCOptions &options, TImgStack& img_stack,
                           unsigned int N_regions, double p0, double EBV_max, uint64_t healpix_index) {
	TLOSMCMCParams params(&img_stack, p0, EBV_max);
	
	TNullLogger logger;
	
	unsigned int max_attempts = 3;
	unsigned int N_steps = options.steps;
	unsigned int N_samplers = options.samplers;
	unsigned int N_threads = options.N_threads;
	unsigned int ndim = N_regions + 1;
	
	double *GR = new double[ndim];
	double GR_threshold = 1.2;
	
	typename TAffineSampler<TLOSMCMCParams, TNullLogger>::pdf_t f_pdf = &lnp_los_extinction;
	typename TAffineSampler<TLOSMCMCParams, TNullLogger>::rand_state_t f_rand_state = &gen_rand_los_extinction;
	
	timespec t_start, t_write, t_end;
	
	std::cerr << std::endl;
	
	clock_gettime(CLOCK_MONOTONIC, &t_start);
	
	std::cout << "Line-of-Sight Extinction Profile" << std::endl;
	std::cout << "====================================" << std::endl;
	
	//std::cerr << "# Setting up sampler" << std::endl;
	TParallelAffineSampler<TLOSMCMCParams, TNullLogger> sampler(f_pdf, f_rand_state, ndim, N_samplers*ndim, params, logger, N_threads);
	sampler.set_scale(1.1);
	sampler.set_replacement_bandwidth(0.75);
	
	// Burn-in
	std::cerr << "# Burn-in ..." << std::endl;
	sampler.step(int(N_steps*20./100.), false, 0., 0.5, 0.);
	sampler.step(int(N_steps*5./100), false, 0., 1., 0.);
	sampler.step(int(N_steps*20./100.), false, 0., 0.5, 0.);
	sampler.step(int(N_steps*5./100.), false, 0., 1., 0.);
	sampler.step(int(N_steps*20./100.), false, 0., 0.5, 0.);
	sampler.step(int(N_steps*5./100.), false, 0., 1., 0.);
	sampler.step(int(N_steps*20./100.), false, 0., 0.5, 0.);
	sampler.step(int(N_steps*5./100), false, 0., 1., 0.);
	sampler.clear();
	
	std::cerr << "# Main run ..." << std::endl;
	bool converged = false;
	size_t attempt;
	for(attempt = 0; (attempt < max_attempts) && (!converged); attempt++) {
		sampler.step((1<<attempt)*N_steps, true, 0., 0.1, 0.);
		
		converged = true;
		sampler.get_GR_diagnostic(GR);
		for(size_t i=0; i<ndim; i++) {
			if(GR[i] > GR_threshold) {
				converged = false;
				if(attempt != max_attempts-1) {
					std::cerr << "# Extending run ..." << std::endl;
					sampler.step(int(N_steps*1./5.), false, 0., 1., 0.);
					sampler.clear();
					//logger.clear();
				}
				break;
			}
		}
	}
	
	clock_gettime(CLOCK_MONOTONIC, &t_write);
	
	TChain chain = sampler.get_chain();
	
	std::stringstream group_name;
	group_name << "/pixel " << healpix_index;
	group_name << "/los extinction";
	chain.save(out_fname, group_name.str(), "Delta E(B-V)", 3, 500, 500);
	
	clock_gettime(CLOCK_MONOTONIC, &t_end);
	
	sampler.print_stats();
	std::cout << std::endl;
	
	/*
	for(size_t k=0; k<N_threads; k++) {
		std::cout << std::endl << "Sampler " << k+1 << ":" << std::endl;
		sampler.get_stats(k).print();
		std::cout << std::endl;
	}
	*/
	
	if(!converged) {
		std::cerr << "# Failed to converge." << std::endl;
	}
	std::cerr << "# Number of steps: " << (1<<(attempt-1))*N_steps << std::endl;
	std::cerr << "# Time elapsed: " << std::setprecision(2) << (t_end.tv_sec - t_start.tv_sec) + 1.e-9*(t_end.tv_nsec - t_start.tv_nsec) << " s" << std::endl;
	std::cerr << "# Sample time: " << std::setprecision(2) << (t_write.tv_sec - t_start.tv_sec) + 1.e-9*(t_write.tv_nsec - t_start.tv_nsec) << " s" << std::endl;
	std::cerr << "# Write time: " << std::setprecision(2) << (t_end.tv_sec - t_write.tv_sec) + 1.e-9*(t_end.tv_nsec - t_write.tv_nsec) << " s" << std::endl << std::endl;
	
	delete[] GR;
}


void los_integral(TImgStack &img_stack, double *ret, const double *EBV, unsigned int N_regions) {
	assert(img_stack.rect->N_bins[0] % N_regions == 0);
	
	unsigned int N_samples = img_stack.rect->N_bins[0] / N_regions;
	int y_max = img_stack.rect->N_bins[1];
	
	double y = (EBV[0] - img_stack.rect->min[1]) / img_stack.rect->dx[1];
	double y_ceil, y_floor, dy;
	int x = 0;
	
	for(size_t i=0; i<img_stack.N_images; i++) { ret[i] = 0.; }
	
	for(int i=0; i<N_regions; i++) {
		dy = (double)(EBV[i+1] - EBV[i]) / (double)(N_samples) / img_stack.rect->dx[1];
		//std::cout << "(" << x << ", " << y << ", " << tmp << ") ";
		for(int j=0; j<N_samples; j++, x++, y+=dy) {
			y_floor = floor(y);
			y_ceil = y_floor + 1.;
			if((int)y_ceil >= y_max) { break; }
			if((int)y_floor < 0) { break; }
			for(int k=0; k<img_stack.N_images; k++) {
				ret[k] += (y_ceil - y) * img_stack.img[k]->at<double>(x, (int)y_floor)
				          + (y - y_floor) * img_stack.img[k]->at<double>(x, (int)y_ceil);
			}
		}
		
		if((int)y_ceil >= y_max) { break; }
		if((int)y_floor < 0) { break; }
	}
}

double lnp_los_extinction(const double* EBV, unsigned int N, TLOSMCMCParams& params) {
	#define neginf -std::numeric_limits<double>::infinity()
	
	// Extinction must not exceed maximum value
	if(EBV[N-1] >= params.img_stack->rect->max[1]) { return neginf; }
	
	double lnp = 0.;
	
	// Extinction must increase monotonically
	if(EBV[0] < 0.) { return neginf; }
	double Delta_EBV;
	for(size_t i=1; i<N; i++) {
		Delta_EBV = EBV[i] - EBV[i-1];
		if(Delta_EBV < 0.) {return neginf; }
		
		// Favor lower differential reddening
		//lnp -= Delta_EBV * Delta_EBV / (2. * 0.5 * 0.5);
	}
	
	// Compute line integrals through probability surfaces
	double *line_int = new double[params.img_stack->N_images];
	los_integral(*(params.img_stack), line_int, EBV, N-1);
	
	// Soften and multiply line integrals
	for(size_t i=0; i<params.img_stack->N_images; i++) {
		if(line_int[i] < 1.e5*params.p0) {
			line_int[i] += params.p0 * exp(-line_int[i]/params.p0);
		}
		lnp += log( line_int[i] );
		//std::cerr << line_int[i] << std::endl;
	}
	
	// Reddening prior
	if(params.EBV_max > 0.) {
		//double EBV = 0.;
		//for(size_t i=0; i<N; i++) { EBV += Delta_EBV[i]; }
		if(EBV[N-1] > params.EBV_max) {
			lnp -= 0.5 * (EBV[N-1] - params.EBV_max) * (EBV[N-1] - params.EBV_max) / (params.EBV_max * params.EBV_max);
		}
	}
	
	delete[] line_int;
	
	return lnp;
	
	#undef neginf
}

void gen_rand_los_extinction(double *const EBV, unsigned int N, gsl_rng *r, TLOSMCMCParams &params) {
	double EBV_ceil = params.img_stack->rect->max[1];
	double mu = EBV_ceil / (double)N;
	for(size_t i=0; i<N; i++) {
		EBV[i] = 0.5 * mu * gsl_rng_uniform(r);//gsl_ran_chisq(r, 2.);
		if(i > 0) { EBV[i] += EBV[i-1]; }
	}
	
	// Ensure that reddening is not more than allowed
	if(EBV[N-1] >= 0.95 * EBV_ceil) {
		double factor = 0.9 * EBV_ceil / EBV[N-1];
		for(size_t i=0; i<N; i++) {
			EBV[i] *= factor;
		}
	}
}



/****************************************************************************************************************************
 * 
 * TLOSMCMCParams
 * 
 ****************************************************************************************************************************/

TLOSMCMCParams::TLOSMCMCParams(TImgStack* _img_stack, double _p0, double _EBV_max)
	: img_stack(_img_stack)
{
	p0 = _p0;
	lnp0 = log(p0);
	EBV_max = _EBV_max;
}

TLOSMCMCParams::~TLOSMCMCParams() { }

void TLOSMCMCParams::set_p0(double _p0) {
	p0 = _p0;
	lnp0 = log(p0);
}




/****************************************************************************************************************************
 * 
 * TImgStack
 * 
 ****************************************************************************************************************************/

TImgStack::TImgStack(size_t _N_images) {
	N_images = _N_images;
	img = new cv::Mat*[N_images];
	for(size_t i=0; i<N_images; i++) {
		img[i] = new cv::Mat;
	}
	rect = NULL;
}

TImgStack::TImgStack(size_t _N_images, TRect& _rect) {
	N_images = _N_images;
	img = new cv::Mat*[N_images];
	for(size_t i=0; i<N_images; i++) { img[i] = NULL; }
	rect = new TRect(_rect);
}

TImgStack::~TImgStack() {
	if(img != NULL) {
		for(size_t i=0; i<N_images; i++) {
			if(img[i] != NULL) {
				delete img[i];
			}
		}
		delete[] img;
	}
	if(rect != NULL) { delete rect; }
}

void TImgStack::resize(size_t _N_images) {
	if(img != NULL) {
		for(size_t i=0; i<N_images; i++) {
			if(img[i] != NULL) {
				delete img[i];
			}
		}
		delete[] img;
	}
	if(rect != NULL) { delete rect; }
	
	N_images = _N_images;
	img = new cv::Mat*[N_images];
	for(size_t i=0; i<N_images; i++) {
		img[i] = new cv::Mat;
	}
}

void TImgStack::cull(const std::vector<bool> &keep) {
	assert(keep.size() == N_images);
	
	size_t N_tmp = 0;
	for(std::vector<bool>::const_iterator it = keep.begin(); it != keep.end(); ++it) {
		if(*it) { N_tmp++; }
	}
	
	cv::Mat **img_tmp = new cv::Mat*[N_tmp];
	size_t i = 0;
	size_t k = 0;
	for(std::vector<bool>::const_iterator it = keep.begin(); it != keep.end(); ++it, ++i) {
		if(*it) {
			img_tmp[k] = img[i];
			k++;
		} else {
			delete img[i];
		}
	}
	
	delete[] img;
	img = img_tmp;
	N_images = N_tmp;
}

void TImgStack::set_rect(TRect& _rect) {
	if(rect == NULL) {
		rect = new TRect(_rect);
	} else {
		*rect = _rect;
	}
}
