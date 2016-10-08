/***************************************************************************
 *   Copyright (C) 2009 by BUI Quang Minh   *
 *   minh.bui@univie.ac.at   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "phylotree.h"
#include "vectorclass/instrset.h"

#if INSTRSET < 2
#include "phylokernelnew.h"
#define KERNEL_FIX_STATES
#include "phylokernelnew.h"
#include "vectorf64.h"
#endif

//#include "phylokernel.h"
//#include "phylokernelmixture.h"
//#include "phylokernelmixrate.h"
//#include "phylokernelsitemodel.h"

#include "model/modelgtr.h"
#include "model/modelset.h"

/* BQM: to ignore all-gapp subtree at an alignment site */
//#define IGNORE_GAP_LH

//#define USING_SSE

void PhyloTree::setParsimonyKernel(LikelihoodKernel lk) {
    // set parsimony kernel
    if (lk == LK_EIGEN || instruction_set < 2) {
        computeParsimonyBranchPointer = &PhyloTree::computeParsimonyBranchFast;
        computePartialParsimonyPointer = &PhyloTree::computePartialParsimonyFast;
    	return;
    }
    if (instruction_set >= 7) {
        setParsimonyKernelAVX();
        return;
    }
    if (instruction_set >= 2) {
        setParsimonyKernelSSE();
        return;
    }
    assert(0);
}

void PhyloTree::setLikelihoodKernel(LikelihoodKernel lk) {

	sse = lk;
    vector_size = 1;

    //--- parsimony kernel ---
    setParsimonyKernel(lk);

    bool has_fma = (hasFMA3() || hasFMA4()) && (instruction_set >= 7) && (Params::getInstance().lk_no_avx != 2);
    //--- dot-product kernel ---
    if (has_fma) {
		setDotProductFMA();
	} else if (instruction_set >= 7) {
		setDotProductAVX();
    } else if (instruction_set >= 2) {
        setDotProductSSE();
	} else {

#if INSTRSET < 2
#ifdef BOOT_VAL_FLOAT
        // TODO naive dot-product for float
        assert(0 && "Not supported, contact developer");
//		dotProduct = &PhyloTree::dotProductSIMD<float, Vec1f>;
#else
		dotProduct = &PhyloTree::dotProductSIMD<double, Vec1d>;
#endif
        dotProductDouble = &PhyloTree::dotProductSIMD<double, Vec1d>;
#endif
	}

    //--- naive likelihood kernel, no alignment specified yet ---
    if (!aln) {
#if INSTRSET < 2
        computeLikelihoodBranchPointer = &PhyloTree::computeLikelihoodBranchGenericSIMD<Vec1d, SAFE_LH>;
        computeLikelihoodDervPointer = &PhyloTree::computeLikelihoodDervGenericSIMD<Vec1d, SAFE_LH>;
        computePartialLikelihoodPointer = &PhyloTree::computePartialLikelihoodGenericSIMD<Vec1d, SAFE_LH>;
        computeLikelihoodFromBufferPointer = &PhyloTree::computeLikelihoodFromBufferGenericSIMD<Vec1d, SAFE_LH>;
        sse = LK_EIGEN;
#else
        computeLikelihoodBranchPointer = NULL;
        computeLikelihoodDervPointer = NULL;
        computePartialLikelihoodPointer = NULL;
        computeLikelihoodFromBufferPointer = NULL;
        sse = LK_EIGEN;
#endif
        return;
    }

    //--- SIMD kernel ---
    if (sse == LK_EIGEN_SSE && instruction_set >= 2) {
#ifdef INCLUDE_AVX512
    	if (instruction_set >= 9) {
    		setLikelihoodKernelAVX512();
    		return;
    	}
#endif
    	if (has_fma) {
            // CPU supports AVX and FMA
            setLikelihoodKernelFMA();
        } else if (instruction_set >= 7) {
            // CPU supports AVX
            setLikelihoodKernelAVX();
        } else {
            // SSE kernel
            setLikelihoodKernelSSE();
        }
        return;
    }

#if INSTRSET < 2
    //--- naive kernel for site-specific model ---
    if (model_factory && model_factory->model->isSiteSpecificModel()) {
        computeLikelihoodBranchPointer = &PhyloTree::computeLikelihoodBranchGenericSIMD<Vec1d, SAFE_LH, false, true>;
        computeLikelihoodDervPointer = &PhyloTree::computeLikelihoodDervGenericSIMD<Vec1d, SAFE_LH, false, true>;
        computePartialLikelihoodPointer = &PhyloTree::computePartialLikelihoodGenericSIMD<Vec1d, SAFE_LH, false, true>;
        computeLikelihoodFromBufferPointer = &PhyloTree::computeLikelihoodFromBufferGenericSIMD<Vec1d, SAFE_LH, false, true>;
        return;
    }

    //--- naive (no SIMD) kernel ---
    computeLikelihoodBranchPointer = &PhyloTree::computeLikelihoodBranchGenericSIMD<Vec1d, SAFE_LH>;
    computeLikelihoodDervPointer = &PhyloTree::computeLikelihoodDervGenericSIMD<Vec1d, SAFE_LH>;
    computePartialLikelihoodPointer = &PhyloTree::computePartialLikelihoodGenericSIMD<Vec1d, SAFE_LH>;
    computeLikelihoodFromBufferPointer = &PhyloTree::computeLikelihoodFromBufferGenericSIMD<Vec1d, SAFE_LH>;
#else
    computeLikelihoodBranchPointer = NULL;
    computeLikelihoodDervPointer = NULL;
    computePartialLikelihoodPointer = NULL;
    computeLikelihoodFromBufferPointer = NULL;
#endif
}

void PhyloTree::changeLikelihoodKernel(LikelihoodKernel lk) {
	if (sse == lk) return;
    setLikelihoodKernel(lk);
}

/*******************************************************
 *
 * master function: wrapper for other optimized functions
 *
 ******************************************************/

void PhyloTree::computePartialLikelihood(PhyloNeighbor *dad_branch, PhyloNode *dad) {
	(this->*computePartialLikelihoodPointer)(dad_branch, dad);
}

double PhyloTree::computeLikelihoodBranch(PhyloNeighbor *dad_branch, PhyloNode *dad) {
	return (this->*computeLikelihoodBranchPointer)(dad_branch, dad);

}

void PhyloTree::computeLikelihoodDerv(PhyloNeighbor *dad_branch, PhyloNode *dad, double &df, double &ddf) {
	(this->*computeLikelihoodDervPointer)(dad_branch, dad, df, ddf);
}


double PhyloTree::computeLikelihoodFromBuffer() {
	assert(current_it && current_it_back);

	if (computeLikelihoodFromBufferPointer && optimize_by_newton)
		return (this->*computeLikelihoodFromBufferPointer)();
	else
		return (this->*computeLikelihoodBranchPointer)(current_it, (PhyloNode*)current_it_back->node);

}

double PhyloTree::dotProductDoubleCall(double *x, double *y, int size) {
    return (this->*dotProductDouble)(x, y, size);
}



void PhyloTree::computeTipPartialLikelihood() {
	if (tip_partial_lh_computed)
		return;
	tip_partial_lh_computed = true;
    
    
	//-------------------------------------------------------
	// initialize ptn_freq and ptn_invar
	//-------------------------------------------------------

	computePtnFreq();
	// for +I model
	computePtnInvar();

    if (getModel()->isSiteSpecificModel()) {
//        ModelSet *models = (ModelSet*)model;
        size_t nptn = aln->getNPattern(), max_nptn = ((nptn+vector_size-1)/vector_size)*vector_size, tip_block_size = max_nptn * aln->num_states;
        int nstates = aln->num_states;
        int nseq = aln->getNSeq();
        assert(vector_size > 0);
#ifdef _OPENMP
        #pragma omp parallel for schedule(static)
#endif
        for (int nodeid = 0; nodeid < nseq; nodeid++) {
            int i, x, v;
            double *partial_lh = tip_partial_lh + tip_block_size*nodeid;
            size_t ptn;
            for (ptn = 0; ptn < nptn; ptn+=vector_size, partial_lh += nstates*vector_size) {
//                int state[vector_size];
//                for (v = 0; v < vector_size; v++) {
//                    if (ptn+v < nptn)
//                        state[v] = aln->at(ptn+v)[nodeid];
//                    else
//                        state[v] = aln->STATE_UNKNOWN;
//                }

                double *inv_evec = &model->getInverseEigenvectors()[ptn*nstates*nstates];
                for (v = 0; v < vector_size; v++) {
                    int state = aln->STATE_UNKNOWN;
                    if (ptn+v < nptn)
                        state = aln->at(ptn+v)[nodeid];
    //                double *partial_lh = node_partial_lh + ptn*nstates;
//                    double *inv_evec = models->at(ptn)->getInverseEigenvectors();

                    if (state < nstates) {
                        for (i = 0; i < nstates; i++)
                            partial_lh[i*vector_size+v] = inv_evec[(i*nstates+state)*vector_size+v];
                    } else if (state == aln->STATE_UNKNOWN) {
                        // special treatment for unknown char
                        for (i = 0; i < nstates; i++) {
                            double lh_unknown = 0.0;
//                            double *this_inv_evec = inv_evec + i*nstates;
                            for (x = 0; x < nstates; x++)
                                lh_unknown += inv_evec[(i*nstates+x)*vector_size+v];
                            partial_lh[i*vector_size+v] = lh_unknown;
                        }
                    } else {
                        double lh_ambiguous;
                        // ambiguous characters
                        int ambi_aa[] = {
                            4+8, // B = N or D
                            32+64, // Z = Q or E
                            512+1024 // U = I or L
                            };
                        switch (aln->seq_type) {
                        case SEQ_DNA:
                            {
                                int cstate = state-nstates+1;
                                for (i = 0; i < nstates; i++) {
                                    lh_ambiguous = 0.0;
                                    for (x = 0; x < nstates; x++)
                                        if ((cstate) & (1 << x))
                                            lh_ambiguous += inv_evec[(i*nstates+x)*vector_size+v];
                                    partial_lh[i*vector_size+v] = lh_ambiguous;
                                }
                            }
                            break;
                        case SEQ_PROTEIN:
                            //map[(unsigned char)'B'] = 4+8+19; // N or D
                            //map[(unsigned char)'Z'] = 32+64+19; // Q or E
                            {
                                int cstate = state-nstates;
                                for (i = 0; i < nstates; i++) {
                                    lh_ambiguous = 0.0;
                                    for (x = 0; x < 11; x++)
                                        if (ambi_aa[cstate] & (1 << x))
                                            lh_ambiguous += inv_evec[(i*nstates+x)*vector_size+v];
                                    partial_lh[i*vector_size+v] = lh_ambiguous;
                                }
                            }
                            break;
                        default:
                            assert(0);
                            break;
                        }
                    }
                    // sanity check
    //                bool all_zero = true;
    //                for (i = 0; i < nstates; i++)
    //                    if (partial_lh[i] != 0) {
    //                        all_zero = false;
    //                        break;
    //                    }
    //                assert(!all_zero && "some tip_partial_lh are all zeros");
                    
                } // FOR v
            } // FOR ptn
            // NO Need to copy dummy anymore
            // dummy values
//            for (ptn = nptn; ptn < max_nptn; ptn++, partial_lh += nstates)
//                memcpy(partial_lh, partial_lh-nstates, nstates*sizeof(double));
        } // FOR nodeid
        return;
    }
    
	int m, i, x, state, nstates = aln->num_states, nmixtures = model->getNMixtures();
	double *all_inv_evec = model->getInverseEigenvectors();
	assert(all_inv_evec);
	assert(tip_partial_lh);

	for (state = 0; state < nstates; state++) {
		double *this_tip_partial_lh = &tip_partial_lh[state*nstates*nmixtures];
		for (m = 0; m < nmixtures; m++) {
			double *inv_evec = &all_inv_evec[m*nstates*nstates];
			for (i = 0; i < nstates; i++)
				this_tip_partial_lh[m*nstates + i] = inv_evec[i*nstates+state];
		}
	}
	// special treatment for unknown char
	for (i = 0; i < nstates; i++) {
		double *this_tip_partial_lh = &tip_partial_lh[aln->STATE_UNKNOWN*nstates*nmixtures];
		for (m = 0; m < nmixtures; m++) {
			double *inv_evec = &all_inv_evec[m*nstates*nstates];
			double lh_unknown = 0.0;
			for (x = 0; x < nstates; x++)
				lh_unknown += inv_evec[i*nstates+x];
			this_tip_partial_lh[m*nstates + i] = lh_unknown;
		}
	}

	double lh_ambiguous;
	// ambiguous characters
	int ambi_aa[] = {
        4+8, // B = N or D
        32+64, // Z = Q or E
        512+1024 // U = I or L
        };
	switch (aln->seq_type) {
	case SEQ_DNA:
		for (state = 4; state < 18; state++) {
			int cstate = state-nstates+1;
			double *this_tip_partial_lh = &tip_partial_lh[state*nstates*nmixtures];
			for (m = 0; m < nmixtures; m++) {
				double *inv_evec = &all_inv_evec[m*nstates*nstates];
				for (i = 0; i < nstates; i++) {
					lh_ambiguous = 0.0;
					for (x = 0; x < nstates; x++)
						if ((cstate) & (1 << x))
							lh_ambiguous += inv_evec[i*nstates+x];
					this_tip_partial_lh[m*nstates+i] = lh_ambiguous;
				}
			}
		}
		break;
	case SEQ_PROTEIN:
		//map[(unsigned char)'B'] = 4+8+19; // N or D
		//map[(unsigned char)'Z'] = 32+64+19; // Q or E
		for (state = 0; state < sizeof(ambi_aa)/sizeof(int); state++) {
			double *this_tip_partial_lh = &tip_partial_lh[(state+20)*nstates*nmixtures];
			for (m = 0; m < nmixtures; m++) {
				double *inv_evec = &all_inv_evec[m*nstates*nstates];
				for (i = 0; i < nstates; i++) {
					lh_ambiguous = 0.0;
					for (x = 0; x < 11; x++)
						if (ambi_aa[state] & (1 << x))
							lh_ambiguous += inv_evec[i*nstates+x];
					this_tip_partial_lh[m*nstates+i] = lh_ambiguous;
				}
			}
		}
		break;
	default:
		break;
	}

}

void PhyloTree::computePtnFreq() {
	if (ptn_freq_computed) return;
	ptn_freq_computed = true;
	size_t nptn = aln->getNPattern();
	size_t maxptn = get_safe_upper_limit(nptn)+get_safe_upper_limit(model_factory->unobserved_ptns.size());
	int ptn;
	for (ptn = 0; ptn < nptn; ptn++)
		ptn_freq[ptn] = (*aln)[ptn].frequency;
	for (ptn = nptn; ptn < maxptn; ptn++)
		ptn_freq[ptn] = 0.0;
}

void PhyloTree::computePtnInvar() {
	size_t nptn = aln->getNPattern(), ptn;
	size_t maxptn = get_safe_upper_limit(nptn)+get_safe_upper_limit(model_factory->unobserved_ptns.size());
	int nstates = aln->num_states;

    double *state_freq = aligned_alloc<double>(nstates);
    model->getStateFrequency(state_freq);
	memset(ptn_invar, 0, maxptn*sizeof(double));
	double p_invar = site_rate->getPInvar();
	if (p_invar != 0.0) {
		for (ptn = 0; ptn < nptn; ptn++) {
			if ((*aln)[ptn].const_char == nstates)
				ptn_invar[ptn] = p_invar;
			else if ((*aln)[ptn].const_char < nstates) {
				ptn_invar[ptn] = p_invar * state_freq[(int) (*aln)[ptn].const_char];
			}
		}
//		// ascertmain bias correction
//		for (ptn = 0; ptn < model_factory->unobserved_ptns.size(); ptn++)
//			ptn_invar[nptn+ptn] = p_invar * state_freq[(int)model_factory->unobserved_ptns[ptn]];
//
		// dummy values
		for (ptn = nptn; ptn < maxptn; ptn++)
			ptn_invar[ptn] = p_invar;
	}
	aligned_free(state_freq);
}

/*******************************************************
 *
 * non-vectorized likelihood functions.
 * this version uses Alexis' technique that stores the
 * dot product of partial likelihoods and eigenvectors at node
 * for faster branch length optimization
 *
 ******************************************************/

/*
void PhyloTree::computePartialLikelihoodEigen(PhyloNeighbor *dad_branch, PhyloNode *dad) {

    // don't recompute the likelihood
	assert(dad);
    if (dad_branch->partial_lh_computed & 1)
        return;
    dad_branch->partial_lh_computed |= 1;
    PhyloNode *node = (PhyloNode*)(dad_branch->node);


    size_t nstates = aln->num_states;
    size_t nptn = aln->size()+model_factory->unobserved_ptns.size();

    if (!tip_partial_lh_computed)
        computeTipPartialLikelihood();

	if (node->isLeaf()) {
	    dad_branch->lh_scale_factor = 0.0;
		return;
	}
    
    size_t ptn, c;
    size_t orig_ntn = aln->size();
    size_t ncat = site_rate->getNRate();
    size_t ncat_mix = (model_factory->fused_mix_rate) ? ncat : ncat*model->getNMixtures();
    size_t mix_addr_nstates[ncat_mix], mix_addr[ncat_mix];
    size_t denom = (model_factory->fused_mix_rate) ? 1 : ncat;
    for (c = 0; c < ncat_mix; c++) {
        size_t m = c/denom;
        mix_addr_nstates[c] = m*nstates;
        mix_addr[c] = mix_addr_nstates[c]*nstates;
    }
    size_t i, x;
    size_t block = nstates * ncat_mix;
    size_t tip_block = nstates * model->getNMixtures();
    size_t scale_size = nptn * ncat_mix;
    
	double *evec = model->getEigenvectors();
	double *inv_evec = model->getInverseEigenvectors();
	assert(inv_evec && evec);
	double *eval = model->getEigenvalues();

    dad_branch->lh_scale_factor = 0.0;

	// internal node
	PhyloNeighbor *left = NULL, *right = NULL; // left & right are two neighbors leading to 2 subtrees
    int num_leaves = 0;
	FOR_NEIGHBOR_IT(node, dad, it) {
        PhyloNeighbor *nei = (PhyloNeighbor*)*it;
		if (!left) left = (PhyloNeighbor*)(*it); else right = (PhyloNeighbor*)(*it);
        if ((nei->partial_lh_computed & 1) == 0)
            computePartialLikelihood(nei, node);
        dad_branch->lh_scale_factor += nei->lh_scale_factor;
        if (nei->node->isLeaf())
            num_leaves ++;
	}

    if (params->lh_mem_save == LM_PER_NODE && !dad_branch->partial_lh) {
        // re-orient partial_lh
        bool done = false;
        FOR_NEIGHBOR_IT(node, dad, it2) {
            PhyloNeighbor *backnei = ((PhyloNeighbor*)(*it2)->node->findNeighbor(node));
            if (backnei->partial_lh) {
                dad_branch->partial_lh = backnei->partial_lh;
                dad_branch->scale_num = backnei->scale_num;
                backnei->partial_lh = NULL;
                backnei->scale_num = NULL;
                backnei->partial_lh_computed &= ~1; // clear bit
                done = true;
                break;
            }
        }
        if (!done) {
            printTree(cout, WT_BR_LEN + WT_NEWLINE);
        }
        assert(done && "partial_lh is not re-oriented");
    }

    // precompute buffer to save times
    double *echildren = new double[block*nstates*(node->degree()-1)];
    double *partial_lh_leaves = NULL;
    if (num_leaves > 0)
        partial_lh_leaves = new double[(aln->STATE_UNKNOWN+1)*block*num_leaves];
    double *echild = echildren;
    double *partial_lh_leaf = partial_lh_leaves;

    FOR_NEIGHBOR_IT(node, dad, it) {
        double expchild[nstates];
        PhyloNeighbor *child = (PhyloNeighbor*)*it;
        // precompute information buffer
        double *echild_ptr = echild;
        for (c = 0; c < ncat_mix; c++) {
            double len_child = site_rate->getRate(c%ncat) * child->length;
            double *eval_ptr = eval + mix_addr_nstates[c];
            double *evec_ptr = evec + mix_addr[c];
            for (i = 0; i < nstates; i++) {
                expchild[i] = exp(eval_ptr[i]*len_child);
            }
            for (x = 0; x < nstates; x++) {
                for (i = 0; i < nstates; i++) {
                    echild_ptr[i] = evec_ptr[x*nstates+i] * expchild[i];
                }
                echild_ptr += nstates;
            }
        }

        // pre compute information for tip
        if (child->node->isLeaf()) {
            vector<int>::iterator it;
            for (it = aln->seq_states[child->node->id].begin(); it != aln->seq_states[child->node->id].end(); it++) {
                int state = (*it);
                double *this_partial_lh_leaf = partial_lh_leaf + state*block;
                double *echild_ptr = echild;
                for (c = 0; c < ncat_mix; c++) {
                    double *this_tip_partial_lh = tip_partial_lh + state*tip_block + mix_addr_nstates[c];
                    for (x = 0; x < nstates; x++) {
                        double vchild = 0.0;
                        for (i = 0; i < nstates; i++) {
                            vchild += echild_ptr[i] * this_tip_partial_lh[i];
                        }
                        this_partial_lh_leaf[x] = vchild;
                        echild_ptr += nstates;
                    }
                    this_partial_lh_leaf += nstates;
                }
            }
            size_t addr = aln->STATE_UNKNOWN * block;
            for (x = 0; x < block; x++) {
                partial_lh_leaf[addr+x] = 1.0;
            }
            partial_lh_leaf += (aln->STATE_UNKNOWN+1)*block;
        }
        echild += block*nstates;
    }
    
    
    double *eleft = echildren, *eright = echildren + block*nstates;
    
	if (!left->node->isLeaf() && right->node->isLeaf()) {
		PhyloNeighbor *tmp = left;
		left = right;
		right = tmp;
        double *etmp = eleft;
        eleft = eright;
        eright = etmp;
	}
    
    if (node->degree() > 3) {

        //--------------------- multifurcating node ------------------//
    
        // now for-loop computing partial_lh over all site-patterns
#ifdef _OPENMP
#pragma omp parallel for private(ptn, c, x, i) schedule(static)
#endif
        for (ptn = 0; ptn < nptn; ptn++) {
            double partial_lh_all[block];
            for (i = 0; i < block; i++)
                partial_lh_all[i] = 1.0;
            UBYTE *scale_dad = dad_branch->scale_num + ptn*ncat_mix;
            memset(scale_dad, 0, sizeof(UBYTE)*ncat_mix);
                
            double *partial_lh_leaf = partial_lh_leaves;
            double *echild = echildren;

            FOR_NEIGHBOR_IT(node, dad, it) {
                PhyloNeighbor *child = (PhyloNeighbor*)*it;
                UBYTE *scale_child = child->scale_num + ptn*ncat_mix;
                if (child->node->isLeaf()) {
                    // external node
                    int state_child = (ptn < orig_ntn) ? (aln->at(ptn))[child->node->id] : model_factory->unobserved_ptns[ptn-orig_ntn];
                    double *child_lh = partial_lh_leaf + state_child*block;
                    for (c = 0; c < block; c++) {
                        // compute real partial likelihood vector
                        partial_lh_all[c] *= child_lh[c];
                    }
                    partial_lh_leaf += (aln->STATE_UNKNOWN+1)*block;
                } else {
                    // internal node
                    double *partial_lh = partial_lh_all;
                    double *partial_lh_child = child->partial_lh + ptn*block;

                    double *echild_ptr = echild;
                    for (c = 0; c < ncat_mix; c++) {
                        scale_dad[c] += scale_child[c];
                        // compute real partial likelihood vector
                        for (x = 0; x < nstates; x++) {
                            double vchild = 0.0;
//                            double *echild_ptr = echild + (c*nstatesqr+x*nstates);
                            for (i = 0; i < nstates; i++) {
                                vchild += echild_ptr[i] * partial_lh_child[i];
                            }
                            echild_ptr += nstates;
                            partial_lh[x] *= vchild;
                        }
                        partial_lh += nstates;
                        partial_lh_child += nstates;
                    }
                } // if
                echild += block*nstates;
            } // FOR_NEIGHBOR
            
        
            // compute dot-product with inv_eigenvector
            double *partial_lh_tmp = partial_lh_all;
            double *partial_lh = dad_branch->partial_lh + ptn*block;
            for (c = 0; c < ncat_mix; c++) {
                double lh_max = 0.0;
                double *inv_evec_ptr = inv_evec + mix_addr[c];
                for (i = 0; i < nstates; i++) {
                    double res = 0.0;
                    for (x = 0; x < nstates; x++) {
                        res += partial_lh_tmp[x]*inv_evec_ptr[x];
                    }
                    inv_evec_ptr += nstates;
                    partial_lh[i] = res;
                    lh_max = max(lh_max, fabs(res));
                }
                // check if one should scale partial likelihoods
                if (lh_max < SCALING_THRESHOLD && lh_max != 0.0) {
                    //assert(lh_max != 0.0 && "Numerical underflow for multifurcation node");
                    if (ptn_invar[ptn] == 0.0) {
                        // now do the likelihood scaling
                        for (i = 0; i < nstates; i++)
                            partial_lh[i] *= SCALING_THRESHOLD_INVER;
                        scale_dad[c] += 1;
                    }
                }
                partial_lh += nstates;
                partial_lh_tmp += nstates;
            }

        } // for ptn
//        dad_branch->lh_scale_factor += sum_scale;               
                
        // end multifurcating treatment
    } else if (left->node->isLeaf() && right->node->isLeaf()) {

        //--------------------- TIP-TIP (cherry) case ------------------//

        double *partial_lh_left = partial_lh_leaves;
        double *partial_lh_right = partial_lh_leaves + (aln->STATE_UNKNOWN+1)*block;

		// scale number must be ZERO
	    memset(dad_branch->scale_num, 0, scale_size * sizeof(UBYTE));
#ifdef _OPENMP
#pragma omp parallel for private(ptn, c, x, i) schedule(static)
#endif
		for (ptn = 0; ptn < nptn; ptn++) {
			double partial_lh_tmp[nstates];
			double *partial_lh = dad_branch->partial_lh + ptn*block;
			double *vleft = partial_lh_left + block*((ptn < orig_ntn) ? (aln->at(ptn))[left->node->id] : model_factory->unobserved_ptns[ptn-orig_ntn]);
			double *vright = partial_lh_right + block*((ptn < orig_ntn) ? (aln->at(ptn))[right->node->id] : model_factory->unobserved_ptns[ptn-orig_ntn]);
			for (c = 0; c < ncat_mix; c++) {
                double *inv_evec_ptr = inv_evec + mix_addr[c];
				// compute real partial likelihood vector
				for (x = 0; x < nstates; x++) {
					partial_lh_tmp[x] = vleft[x] * vright[x];
				}

				// compute dot-product with inv_eigenvector
				for (i = 0; i < nstates; i++) {
					double res = 0.0;
					for (x = 0; x < nstates; x++) {
						res += partial_lh_tmp[x]*inv_evec_ptr[x];
					}
                    inv_evec_ptr += nstates;
					partial_lh[i] = res;
				}
                vleft += nstates;
                vright += nstates;
                partial_lh += nstates;
			}
		}
	} else if (left->node->isLeaf() && !right->node->isLeaf()) {

        //--------------------- TIP-INTERNAL NODE case ------------------//

		// only take scale_num from the right subtree
		memcpy(dad_branch->scale_num, right->scale_num, scale_size * sizeof(UBYTE));


        double *partial_lh_left = partial_lh_leaves;

#ifdef _OPENMP
#pragma omp parallel for private(ptn, c, x, i) schedule(static)
#endif
		for (ptn = 0; ptn < nptn; ptn++) {
			double partial_lh_tmp[nstates];
			double *partial_lh = dad_branch->partial_lh + ptn*block;
			double *partial_lh_right = right->partial_lh + ptn*block;
			double *vleft = partial_lh_left + block*((ptn < orig_ntn) ? (aln->at(ptn))[left->node->id] : model_factory->unobserved_ptns[ptn-orig_ntn]);

            double *eright_ptr = eright;
			for (c = 0; c < ncat_mix; c++) {
                double lh_max = 0.0;
                double *inv_evec_ptr = inv_evec + mix_addr[c];
				// compute real partial likelihood vector
				for (x = 0; x < nstates; x++) {
					double vright = 0.0;
//					size_t addr = c*nstatesqr+x*nstates;
//					vleft = partial_lh_left[state_left*block+c*nstates+x];
					for (i = 0; i < nstates; i++) {
						vright += eright_ptr[i] * partial_lh_right[i];
					}
                    eright_ptr += nstates;
					partial_lh_tmp[x] = vleft[x] * (vright);
				}
                
				// compute dot-product with inv_eigenvector
				for (i = 0; i < nstates; i++) {
					double res = 0.0;
					for (x = 0; x < nstates; x++) {
						res += partial_lh_tmp[x]*inv_evec_ptr[x];
					}
                    inv_evec_ptr += nstates;
					partial_lh[i] = res;
                    lh_max = max(fabs(res), lh_max);
				}
                // check if one should scale partial likelihoods
                if (lh_max < SCALING_THRESHOLD && lh_max != 0.0) {
                    //assert(lh_max != 0.0 && "Numerical underflow for tip-inner node");
                    if (ptn_invar[ptn] == 0.0) {
                        // now do the likelihood scaling
                        for (i = 0; i < nstates; i++)
                            partial_lh[i] *= SCALING_THRESHOLD_INVER;
                        dad_branch->scale_num[ptn*ncat_mix+c] += 1;
                    }
                }
                vleft += nstates;
                partial_lh_right += nstates;
                partial_lh += nstates;
			}

		}
//		dad_branch->lh_scale_factor += sum_scale;
//		delete [] partial_lh_left;

	} else {

        //--------------------- INTERNAL-INTERNAL NODE case ------------------//

#ifdef _OPENMP
#pragma omp parallel for private(ptn, c, x, i) schedule(static)
#endif
		for (ptn = 0; ptn < nptn; ptn++) {
			double partial_lh_tmp[nstates];
			double *partial_lh = dad_branch->partial_lh + ptn*block;
			double *partial_lh_left = left->partial_lh + ptn*block;
			double *partial_lh_right = right->partial_lh + ptn*block;
            UBYTE *scale_dad = dad_branch->scale_num + ptn*ncat_mix;
            UBYTE *scale_left = left->scale_num + ptn*ncat_mix;
            UBYTE *scale_right = right->scale_num + ptn*ncat_mix; 

            double *eleft_ptr = eleft;
            double *eright_ptr = eright;

			for (c = 0; c < ncat_mix; c++) {
                scale_dad[c] = scale_left[c] + scale_right[c];
                double lh_max = 0.0;
                double *inv_evec_ptr = inv_evec + mix_addr[c];
				// compute real partial likelihood vector
				for (x = 0; x < nstates; x++) {
					double vleft = 0.0, vright = 0.0;
//					size_t addr = c*nstatesqr+x*nstates;
					for (i = 0; i < nstates; i++) {
						vleft += eleft_ptr[i] * partial_lh_left[i];
						vright += eright_ptr[i] * partial_lh_right[i];
					}
                    eleft_ptr += nstates;
                    eright_ptr += nstates;
					partial_lh_tmp[x] = vleft*vright;
//                    assert(partial_lh_tmp[x] != 0.0);
				}
                
				// compute dot-product with inv_eigenvector
				for (i = 0; i < nstates; i++) {
					double res = 0.0;
					for (x = 0; x < nstates; x++) {
						res += partial_lh_tmp[x]*inv_evec_ptr[x];
					}
                    inv_evec_ptr += nstates;
					partial_lh[i] = res;
                    lh_max = max(lh_max, fabs(res));
				}
                // check if one should scale partial likelihoods
                if (lh_max < SCALING_THRESHOLD && lh_max != 0.0) {
                    //assert(lh_max != 0.0 && "Numerical underflow for inner-inner node");
                    if (ptn_invar[ptn] == 0.0) {
                        // BQM 2016-05-03: only scale for non-constant sites
                        // now do the likelihood scaling
                        for (i = 0; i < nstates; i++)
                            partial_lh[i] *= SCALING_THRESHOLD_INVER;
                        scale_dad[c] += 1;
                    }
                }
                partial_lh_left += nstates;
                partial_lh_right += nstates;
                partial_lh += nstates;
			}

		}
//		dad_branch->lh_scale_factor += sum_scale;

	}

    if (partial_lh_leaves)
        delete [] partial_lh_leaves;
    delete [] echildren;
}

void PhyloTree::computeLikelihoodDervEigen(PhyloNeighbor *dad_branch, PhyloNode *dad, double &df, double &ddf) {
    PhyloNode *node = (PhyloNode*) dad_branch->node;
    PhyloNeighbor *node_branch = (PhyloNeighbor*) node->findNeighbor(dad);
    if (!central_partial_lh)
        initializeAllPartialLh();
    if (node->isLeaf()) {
    	PhyloNode *tmp_node = dad;
    	dad = node;
    	node = tmp_node;
    	PhyloNeighbor *tmp_nei = dad_branch;
    	dad_branch = node_branch;
    	node_branch = tmp_nei;
    }
    if ((dad_branch->partial_lh_computed & 1) == 0)
        computePartialLikelihoodEigen(dad_branch, dad);
    if ((node_branch->partial_lh_computed & 1) == 0)
        computePartialLikelihoodEigen(node_branch, node);
        
    size_t nstates = aln->num_states;
    size_t ncat = site_rate->getNRate();
    size_t ncat_mix = (model_factory->fused_mix_rate) ? ncat : ncat*model->getNMixtures();

    size_t block = ncat_mix * nstates;
    size_t tip_block = nstates * model->getNMixtures();
    size_t ptn; // for big data size > 4GB memory required
    size_t c, i;
    size_t orig_nptn = aln->size();
    size_t nptn = aln->size()+model_factory->unobserved_ptns.size();

    size_t mix_addr_nstates[ncat_mix], mix_addr[ncat_mix];
    size_t denom = (model_factory->fused_mix_rate) ? 1 : ncat;
    for (c = 0; c < ncat_mix; c++) {
        size_t m = c/denom;
        mix_addr_nstates[c] = m*nstates;
        mix_addr[c] = mix_addr_nstates[c]*nstates;
    }

    double *eval = model->getEigenvalues();
    assert(eval);

	assert(theta_all);
	if (!theta_computed) {
		// precompute theta for fast branch length optimization

	    if (dad->isLeaf()) {
	    	// special treatment for TIP-INTERNAL NODE case
#ifdef _OPENMP
#pragma omp parallel for private(ptn, i, c) schedule(static)
#endif
	    	for (ptn = 0; ptn < nptn; ptn++) {
				double *partial_lh_dad = dad_branch->partial_lh + ptn*block;
                UBYTE *scale_dad = dad_branch->scale_num+ptn*ncat_mix;
				double *theta = theta_all + ptn*block;
                double *this_tip_partial_lh = tip_partial_lh + tip_block*((ptn < orig_nptn) ? (aln->at(ptn))[dad->id] :  model_factory->unobserved_ptns[ptn-orig_nptn]);
                UBYTE min_scale = scale_dad[0];
                for (c = 1; c < ncat_mix; c++)
                    min_scale = min(min_scale, scale_dad[c]);
                for (c = 0; c < ncat_mix; c++) {
                    double *lh_tip = this_tip_partial_lh + mix_addr_nstates[c];
                    if (scale_dad[c] == min_scale) {
                        for (i = 0; i < nstates; i++) {
                            theta[i] = lh_tip[i] * partial_lh_dad[i];
                        }
                    } else if (scale_dad[c] == min_scale+1) {
                        for (i = 0; i < nstates; i++) {
                            theta[i] = lh_tip[i] * partial_lh_dad[i] * SCALING_THRESHOLD;
                        }
                    } else {
                        memset(theta, 0, sizeof(double)*nstates);
                    }
                    partial_lh_dad += nstates;
                    theta += nstates;
                }

			}
			// ascertainment bias correction
	    } else {
	    	// both dad and node are internal nodes

//	    	size_t all_entries = nptn*block;
#ifdef _OPENMP
#pragma omp parallel for private(ptn, i, c) schedule(static)
#endif
	    	for (ptn = 0; ptn < nptn; ptn++) {
				double *theta = theta_all + ptn*block;
			    double *partial_lh_node = node_branch->partial_lh + ptn*block;
			    double *partial_lh_dad = dad_branch->partial_lh + ptn*block;

                size_t ptn_ncat = ptn*ncat_mix; 
                UBYTE *scale_dad = dad_branch->scale_num + ptn_ncat;
                UBYTE *scale_node = node_branch->scale_num + ptn_ncat;
                UBYTE sum_scale[ncat_mix];
                UBYTE min_scale = sum_scale[0] = scale_dad[0] + scale_node[0];
                for (c = 1; c < ncat_mix; c++) {
                    sum_scale[c] = scale_dad[c] + scale_node[c];
                    min_scale = min(min_scale, sum_scale[c]);
                }
                for (c = 0; c < ncat_mix; c++) {
                    if (sum_scale[c] == min_scale) {
                        for (i = 0; i < nstates; i++) {
                            theta[i] = partial_lh_node[i] * partial_lh_dad[i];
                        }
                    } else if (sum_scale[c] == min_scale+1) {
                        for (i = 0; i < nstates; i++) {
                            theta[i] = partial_lh_node[i] * partial_lh_dad[i] * SCALING_THRESHOLD;
                        }
                    } else {
                        memset(theta, 0, sizeof(double)*nstates);
                    }
                    theta += nstates;
                    partial_lh_dad += nstates;
                    partial_lh_node += nstates;
                }
			}
	    }
		theta_computed = true;
	}

    double *val0 = new double[block];
    double *val1 = new double[block];
    double *val2 = new double[block];
	for (c = 0; c < ncat_mix; c++) {
        size_t m = c/denom;
        double *eval_ptr = eval + mix_addr_nstates[c];
        size_t mycat = c%ncat;
        double prop = site_rate->getProp(mycat) * model->getMixtureWeight(m);
        size_t addr = c*nstates;
		for (i = 0; i < nstates; i++) {
			double cof = eval_ptr[i]*site_rate->getRate(mycat);
			double val = exp(cof*dad_branch->length) * prop;
			double val1_ = cof*val;
			val0[addr+i] = val;
			val1[addr+i] = val1_;
			val2[addr+i] = cof*val1_;
		}
	}


    double my_df = 0.0, my_ddf = 0.0, prob_const = 0.0, df_const = 0.0, ddf_const = 0.0;
//    double tree_lh = node_branch->lh_scale_factor + dad_branch->lh_scale_factor;

#ifdef _OPENMP
#pragma omp parallel for reduction(+: my_df, my_ddf, prob_const, df_const, ddf_const) private(ptn, i) schedule(static)
#endif
    for (ptn = 0; ptn < nptn; ptn++) {
		double lh_ptn = ptn_invar[ptn], df_ptn = 0.0, ddf_ptn = 0.0;
		double *theta = theta_all + ptn*block;
		for (i = 0; i < block; i++) {
			lh_ptn += val0[i] * theta[i];
			df_ptn += val1[i] * theta[i];
			ddf_ptn += val2[i] * theta[i];
		}

//        assert(lh_ptn > 0.0);
        lh_ptn = fabs(lh_ptn);
        
        if (ptn < orig_nptn) {
			double df_frac = df_ptn / lh_ptn;
			double ddf_frac = ddf_ptn / lh_ptn;
			double freq = ptn_freq[ptn];
			double tmp1 = df_frac * freq;
			double tmp2 = ddf_frac * freq;
			my_df += tmp1;
			my_ddf += tmp2 - tmp1 * df_frac;
		} else {
			// ascertainment bias correction
			prob_const += lh_ptn;
			df_const += df_ptn;
			ddf_const += ddf_ptn;
		}
    }
	df = my_df;
	ddf = my_ddf;
    
    assert(!isnan(df) && !isinf(df) && "Numerical underflow for lh-derivative");

    if (isnan(df) || isinf(df)) {
        df = 0.0;
        ddf = 0.0;
//        outWarning("Numerical instability (some site-likelihood = 0)");
    }

	if (orig_nptn < nptn) {
    	// ascertainment bias correction
    	prob_const = 1.0 - prob_const;
    	double df_frac = df_const / prob_const;
    	double ddf_frac = ddf_const / prob_const;
    	int nsites = aln->getNSite();
    	df += nsites * df_frac;
    	ddf += nsites *(ddf_frac + df_frac*df_frac);
    }


    delete [] val2;
    delete [] val1;
    delete [] val0;
}

//template <const int nstates>
double PhyloTree::computeLikelihoodBranchEigen(PhyloNeighbor *dad_branch, PhyloNode *dad) {
    PhyloNode *node = (PhyloNode*) dad_branch->node;
    PhyloNeighbor *node_branch = (PhyloNeighbor*) node->findNeighbor(dad);
    if (!central_partial_lh)
        initializeAllPartialLh();
    if (node->isLeaf()) {
    	PhyloNode *tmp_node = dad;
    	dad = node;
    	node = tmp_node;
    	PhyloNeighbor *tmp_nei = dad_branch;
    	dad_branch = node_branch;
    	node_branch = tmp_nei;
    }
    if ((dad_branch->partial_lh_computed & 1) == 0)
//        computePartialLikelihoodEigen(dad_branch, dad);
        computePartialLikelihood(dad_branch, dad);
    if ((node_branch->partial_lh_computed & 1) == 0)
//        computePartialLikelihoodEigen(node_branch, node);
        computePartialLikelihood(node_branch, node);
    double tree_lh = node_branch->lh_scale_factor + dad_branch->lh_scale_factor;
    size_t nstates = aln->num_states;
    size_t ncat = site_rate->getNRate();
    size_t ncat_mix = (model_factory->fused_mix_rate) ? ncat : ncat*model->getNMixtures();

    size_t block = ncat_mix * nstates;
    size_t tip_block = nstates * model->getNMixtures();
    size_t ptn; // for big data size > 4GB memory required
    size_t c, i;
    size_t orig_nptn = aln->size();
    size_t nptn = aln->size()+model_factory->unobserved_ptns.size();

    size_t mix_addr_nstates[ncat_mix], mix_addr[ncat_mix];
    size_t denom = (model_factory->fused_mix_rate) ? 1 : ncat;

    double *eval = model->getEigenvalues();
    assert(eval);

    double *val = new double[block];
	for (c = 0; c < ncat_mix; c++) {
        size_t mycat = c%ncat;
        size_t m = c/denom;
        mix_addr_nstates[c] = m*nstates;
        mix_addr[c] = mix_addr_nstates[c]*nstates;
        double *eval_ptr = eval + mix_addr_nstates[c];
		double len = site_rate->getRate(mycat)*dad_branch->length;
		double prop = site_rate->getProp(mycat) * model->getMixtureWeight(m);
        double *this_val = val + c*nstates;
		for (i = 0; i < nstates; i++)
			this_val[i] = exp(eval_ptr[i]*len) * prop;
	}

	double prob_const = 0.0;
	memset(_pattern_lh_cat, 0, sizeof(double)*nptn*ncat_mix);

    if (dad->isLeaf()) {
    	// special treatment for TIP-INTERNAL NODE case
    	double *partial_lh_node = new double[(aln->STATE_UNKNOWN+1)*block];
    	IntVector states_dad = aln->seq_states[dad->id];
    	states_dad.push_back(aln->STATE_UNKNOWN);
    	// precompute information from one tip
    	for (IntVector::iterator it = states_dad.begin(); it != states_dad.end(); it++) {
    		double *lh_node = partial_lh_node +(*it)*block;
    		double *val_tmp = val;
            double *this_tip_partial_lh = tip_partial_lh + (*it)*tip_block;
			for (c = 0; c < ncat_mix; c++) {
                double *lh_tip = this_tip_partial_lh + mix_addr_nstates[c];
				for (i = 0; i < nstates; i++) {
					  lh_node[i] = val_tmp[i] * lh_tip[i];
				}
				lh_node += nstates;
				val_tmp += nstates;
			}
    	}

    	// now do the real computation
#ifdef _OPENMP
#pragma omp parallel for reduction(+: tree_lh, prob_const) private(ptn, i, c) schedule(static)
#endif
    	for (ptn = 0; ptn < nptn; ptn++) {
			double lh_ptn = ptn_invar[ptn];
            double *lh_cat = _pattern_lh_cat + ptn*ncat_mix;
            double *partial_lh_dad = dad_branch->partial_lh + ptn*block;
            UBYTE *scale_dad = dad_branch->scale_num + ptn*ncat_mix;
            double *lh_node = partial_lh_node + block*((ptn < orig_nptn) ? (aln->at(ptn))[dad->id] : model_factory->unobserved_ptns[ptn-orig_nptn]);
            // determine the min scaling
            UBYTE min_scale = scale_dad[0];
            for (c = 1; c < ncat_mix; c++) 
                min_scale = min(min_scale, scale_dad[c]);

            for (c = 0; c < ncat_mix; c++) {
                if (scale_dad[c] <= min_scale+1) {
                    // only compute for least scale category
                    for (i = 0; i < nstates; i++) {
                        *lh_cat += (lh_node[i] * partial_lh_dad[i]);
                    }
                    if (scale_dad[c] != min_scale)
                        *lh_cat *= SCALING_THRESHOLD;
                    lh_ptn += *lh_cat;
                }
                lh_node += nstates;
                partial_lh_dad += nstates;
                lh_cat++;
            }
//			assert(lh_ptn > -1e-10);
			if (ptn < orig_nptn) {
				lh_ptn = log(fabs(lh_ptn)) + LOG_SCALING_THRESHOLD*min_scale;
				_pattern_lh[ptn] = lh_ptn;
				tree_lh += lh_ptn * ptn_freq[ptn];
			} else {
                // bugfix 2016-01-21, prob_const can be rescaled
                if (min_scale >= 1)
                    lh_ptn *= SCALING_THRESHOLD;
//				_pattern_lh[ptn] = lh_ptn;
				prob_const += lh_ptn;
			}
		}
		delete [] partial_lh_node;
    } else {
    	// both dad and node are internal nodes
#ifdef _OPENMP
#pragma omp parallel for reduction(+: tree_lh, prob_const) private(ptn, i, c) schedule(static)
#endif
    	for (ptn = 0; ptn < nptn; ptn++) {
			double lh_ptn = ptn_invar[ptn];
            double *lh_cat = _pattern_lh_cat + ptn*ncat_mix;
            double *partial_lh_dad = dad_branch->partial_lh + ptn*block;
            double *partial_lh_node = node_branch->partial_lh + ptn*block;
            double *val_tmp = val;
            UBYTE *scale_dad = dad_branch->scale_num + ptn*ncat_mix;
            UBYTE *scale_node = node_branch->scale_num + ptn*ncat_mix;
            UBYTE sum_scale[ncat_mix];
            UBYTE min_scale = sum_scale[0] = scale_dad[0]+scale_node[0];
            for (c = 1; c < ncat_mix; c++) {
                sum_scale[c] = scale_dad[c] + scale_node[c];
                min_scale = min(min_scale, sum_scale[c]);
            }
            for (c = 0; c < ncat_mix; c++) {
                if (sum_scale[c] <= min_scale+1) {
                    // only compute for least scale category
                    for (i = 0; i < nstates; i++) {
                        *lh_cat +=  (val_tmp[i] * partial_lh_node[i] * partial_lh_dad[i]);
                    }
                    if (sum_scale[c] != min_scale)
                        *lh_cat *= SCALING_THRESHOLD;
                    lh_ptn += *lh_cat;
                }
                partial_lh_node += nstates;
                partial_lh_dad += nstates;
                val_tmp += nstates;
                lh_cat++;
            }

//			assert(lh_ptn > 0.0);
            if (ptn < orig_nptn) {
				lh_ptn = log(fabs(lh_ptn)) + LOG_SCALING_THRESHOLD*min_scale;
				_pattern_lh[ptn] = lh_ptn;
				tree_lh += lh_ptn * ptn_freq[ptn];
			} else {
                // bugfix 2016-01-21, prob_const can be rescaled
                if (min_scale >= 1)
                    lh_ptn *= SCALING_THRESHOLD;
//				_pattern_lh[ptn] = lh_ptn;
				prob_const += lh_ptn;
			}
		}
    }

    assert(!isnan(tree_lh) && !isinf(tree_lh) && "Numerical underflow for lh-branch");

    if (orig_nptn < nptn) {
    	// ascertainment bias correction
        if (prob_const >= 1.0 || prob_const < 0.0) {
            printTree(cout, WT_TAXON_ID + WT_BR_LEN + WT_NEWLINE);
            model->writeInfo(cout);
        }
        assert(prob_const < 1.0 && prob_const >= 0.0);

        // BQM 2015-10-11: fix this those functions using _pattern_lh_cat
//        double inv_const = 1.0 / (1.0-prob_const);
//        size_t nptn_cat = orig_nptn*ncat;
//    	for (ptn = 0; ptn < nptn_cat; ptn++)
//            _pattern_lh_cat[ptn] *= inv_const;
        
    	prob_const = log(1.0 - prob_const);
    	for (ptn = 0; ptn < orig_nptn; ptn++)
    		_pattern_lh[ptn] -= prob_const;
    	tree_lh -= aln->getNSite()*prob_const;
		assert(!isnan(tree_lh) && !isinf(tree_lh));
    }

	assert(!isnan(tree_lh) && !isinf(tree_lh));

    delete [] val;
    return tree_lh;
}
*/


/*******************************************************
 *
 * ancestral sequence reconstruction
 *
 ******************************************************/


void PhyloTree::computeMarginalAncestralProbability(PhyloNeighbor *dad_branch, PhyloNode *dad, double *ptn_ancestral_prob) {
    PhyloNode *node = (PhyloNode*) dad_branch->node;
    PhyloNeighbor *node_branch = (PhyloNeighbor*) node->findNeighbor(dad);
    if (!central_partial_lh)
        initializeAllPartialLh();
    assert(!node->isLeaf());

    if ((dad_branch->partial_lh_computed & 1) == 0)
        computePartialLikelihood(dad_branch, dad);
    if ((node_branch->partial_lh_computed & 1) == 0)
        computePartialLikelihood(node_branch, node);
    size_t nstates = aln->num_states;
    const size_t nstatesqr=nstates*nstates;
    size_t ncat = site_rate->getNRate();
    size_t statecat = nstates * ncat;
    size_t nmixture = model->getNMixtures();

    size_t block = ncat * nstates * nmixture;
    size_t ptn; // for big data size > 4GB memory required
    size_t c, i, m, x;
    size_t nptn = aln->size();
    double *eval = model->getEigenvalues();
    double *evec = model->getEigenvectors();
    double *inv_evec = model->getInverseEigenvectors();
    assert(eval);

    double echild[block*nstates];

    for (c = 0; c < ncat; c++) {
        double expchild[nstates];
        double len_child = site_rate->getRate(c) * dad_branch->length;
        for (m = 0; m < nmixture; m++) {
            for (i = 0; i < nstates; i++) {
                expchild[i] = exp(eval[m*nstates+i]*len_child);
            }
            for (x = 0; x < nstates; x++)
                for (i = 0; i < nstates; i++) {
                    echild[(m*ncat+c)*nstatesqr+x*nstates+i] = evec[m*nstatesqr+x*nstates+i] * expchild[i];
                }
        }
    }


	memset(ptn_ancestral_prob, 0, sizeof(double)*nptn*nstates);

    if (dad->isLeaf()) {
    	// special treatment for TIP-INTERNAL NODE case
        double partial_lh_leaf[(aln->STATE_UNKNOWN+1)*block];

        for (IntVector::iterator it = aln->seq_states[dad->id].begin(); it != aln->seq_states[dad->id].end(); it++) {
            int state = (*it);
            for (m = 0; m < nmixture; m++) {
                double *this_echild = &echild[m*nstatesqr*ncat];
                double *this_tip_partial_lh = &tip_partial_lh[state*nstates*nmixture + m*nstates];
                double *this_partial_lh_leaf = &partial_lh_leaf[state*block+m*statecat];
                for (x = 0; x < statecat; x++) {
                    double vchild = 0.0;
                    for (i = 0; i < nstates; i++) {
                        vchild += this_echild[x*nstates+i] * this_tip_partial_lh[i];
                    }
                    this_partial_lh_leaf[x] = vchild;
                }
            }
        }
        size_t addr = aln->STATE_UNKNOWN * block;
        for (x = 0; x < block; x++) {
            partial_lh_leaf[addr+x] = 1.0;
        }


    	// now do the real computation
#ifdef _OPENMP
#pragma omp parallel for private(ptn, i, c, m, x)
#endif
    	for (ptn = 0; ptn < nptn; ptn++) {
            double *lh_state = ptn_ancestral_prob + ptn*nstates;
			double *partial_lh_dad = dad_branch->partial_lh + ptn*block;
			int state_dad = (aln->at(ptn))[dad->id];
			double *lh_leaf = partial_lh_leaf + state_dad*block;
            for (m = 0; m < nmixture; m++) {
                double *this_inv_evec = inv_evec + (m*nstatesqr); 
				for (c = 0; c < ncat; c++) {
					// compute real partial likelihood vector
					for (x = 0; x < nstates; x++) {
						double vnode = 0.0;
						for (i = 0; i < nstates; i++) {
							vnode += this_inv_evec[i*nstates+x] * partial_lh_dad[i];
						}
						lh_state[x] += lh_leaf[x] * vnode;
					}
                    lh_leaf += nstates;
                    partial_lh_dad += nstates;
                }
            }
            
            double lh_sum = lh_state[0];
            for (x = 1; x < nstates; x++)
                lh_sum += lh_state[x];
            lh_sum = 1.0/lh_sum;
            for (x = 0; x < nstates; x++)
                lh_state[x] *= lh_sum;
		}
    } else {
    	// both dad and node are internal nodes
#ifdef _OPENMP
#pragma omp parallel for private(ptn, i, c, m, x)
#endif
    	for (ptn = 0; ptn < nptn; ptn++) {
            double *lh_state = ptn_ancestral_prob + ptn*nstates;
			double *partial_lh_dad = dad_branch->partial_lh + ptn*block;
			double *partial_lh_node = node_branch->partial_lh + ptn*block;

			for (m = 0; m < nmixture; m++) {
                double *this_inv_evec = inv_evec + (m*nstatesqr); 
				for (c = 0; c < ncat; c++) {
					// compute real partial likelihood vector
					for (x = 0; x < nstates; x++) {
						double vdad = 0.0, vnode = 0.0;
						size_t addr = (m*ncat+c)*nstatesqr+x*nstates;
						for (i = 0; i < nstates; i++) {
							vdad += echild[addr+i] * partial_lh_node[m*statecat+c*nstates+i];
                            vnode += this_inv_evec[i*nstates+x] * partial_lh_dad[m*statecat+c*nstates+i];
						}
						lh_state[x] += vnode*vdad;
					}
				}
			}

            double lh_sum = lh_state[0];
            for (x = 1; x < nstates; x++)
                lh_sum += lh_state[x];
            lh_sum = 1.0/lh_sum;
            for (x = 0; x < nstates; x++)
                lh_state[x] *= lh_sum;

		}
    }
}

void PhyloTree::computeJointAncestralSequences(int *ancestral_seqs) {

    // step 1-3 of the dynamic programming algorithm of Pupko et al. 2000, MBE 17:890-896
    assert(root->isLeaf());
    int *C = new int[(size_t)getAlnNPattern()*model->num_states*leafNum];
    computeAncestralLikelihood((PhyloNeighbor*)root->neighbors[0], NULL, C);
    
    // step 4-5 of the dynamic programming algorithm of Pupko et al. 2000, MBE 17:890-896
    computeAncestralState((PhyloNeighbor*)root->neighbors[0], NULL, C, ancestral_seqs);
    
    clearAllPartialLH();
    
    delete[] C;
}

void PhyloTree::computeAncestralLikelihood(PhyloNeighbor *dad_branch, PhyloNode *dad, int *C) {
    PhyloNode *node = (PhyloNode*)dad_branch->node;
    if (node->isLeaf())
        return;
    
    int num_leaves = 0;
    
    // recursive into subtree
    FOR_NEIGHBOR_DECLARE(node, dad, it) {
        if ((*it)->node->isLeaf()) {
            num_leaves++;
        } else {
            computeAncestralLikelihood((PhyloNeighbor*)(*it), node, C);
        }
    }
    
    if (params->lh_mem_save == LM_PER_NODE && !dad_branch->partial_lh) {
        // re-orient partial_lh
        bool done = false;
        FOR_NEIGHBOR_IT(node, dad, it2) {
            PhyloNeighbor *backnei = ((PhyloNeighbor*)(*it2)->node->findNeighbor(node));
            if (backnei->partial_lh) {
                dad_branch->partial_lh = backnei->partial_lh;
                dad_branch->scale_num = backnei->scale_num;
                backnei->partial_lh = NULL;
                backnei->scale_num = NULL;
                backnei->partial_lh_computed &= ~1; // clear bit
                done = true;
                break;
            }
        }
        assert(done && "partial_lh is not re-oriented");
    }
    
    size_t nptn = aln->getNPattern();
    size_t ptn;
    size_t nstates = model->num_states;
    size_t nstatesqr = nstates*nstates;
    size_t parent, child;
    double *trans_mat = new double[nstatesqr];
    double *lh_leaves = NULL;
    if (num_leaves > 0) {
        lh_leaves = new double[(aln->STATE_UNKNOWN+1)*nstates*num_leaves];
    }
    if (dad) {
        model->computeTransMatrix(dad_branch->length, trans_mat);
        for (parent = 0; parent < nstatesqr; parent++)
            trans_mat[parent] = log(trans_mat[parent]);
    } else {
        model->getStateFrequency(trans_mat);
        for (parent = 0; parent < nstates; parent++)
            trans_mat[parent] = log(trans_mat[parent]);
        for (parent = 1; parent < nstates; parent++)
            memcpy(trans_mat+parent*nstates, trans_mat, sizeof(double)*nstates);
    }
    
    // compute information buffer for leaves
	int ambi_aa[] = {
        4+8, // B = N or D
        32+64, // Z = Q or E
        512+1024 // U = I or L
    };
    int leafid = 0; 
    FOR_NEIGHBOR(node, dad, it) {
        if ((*it)->node->isLeaf()) {
            double trans_leaf[nstatesqr];
            model->computeTransMatrix((*it)->length, trans_leaf);
            double *lh_leaf = lh_leaves+leafid*nstates*(aln->STATE_UNKNOWN+1);
            
            // assign lh_leaf for normal states
            for (parent = 0; parent < nstates; parent++)
                for (child = 0; child < nstates; child++)
                    lh_leaf[child*nstates+parent] = log(trans_leaf[parent*nstates+child]);
            
            // for unknown state
            double *this_lh_leaf = lh_leaf + (aln->STATE_UNKNOWN*nstates);
            for (parent = 0; parent < nstates; parent++)
                this_lh_leaf[parent] = 0.0;
            
            // special treatment for ambiguous characters
            switch (aln->seq_type) {
            case SEQ_DNA:
                for (int state = 4; state < 18; state++) {
                    this_lh_leaf = lh_leaf + (state*nstates);
                    int cstate = state-nstates+1;
                    for (parent = 0; parent < nstates; parent++) {
                        double sumlh = 0.0;
                        for (child = 0; child < nstates; child++) {
                            if ((cstate) & (1 << child))
                                sumlh += trans_leaf[parent*nstates+child];
                        }
                        this_lh_leaf[parent] = log(sumlh);
                    }
                }
                break;
            case SEQ_PROTEIN:
                for (int state = 0; state < sizeof(ambi_aa)/sizeof(int); state++) {
                    this_lh_leaf = lh_leaf + ((state+20)*nstates);
                    for (parent = 0; parent < nstates; parent++) {
                        double sumlh = 0.0;                
                        for (child = 0; child < nstates; child++) {
                            if (ambi_aa[state] & (1 << child))
                                sumlh += trans_leaf[parent*nstates+child];
                        }
                        this_lh_leaf[parent] = log(sumlh);
                    }
                }
                break;
            default:
                break;
            }
            leafid++;
        }
    }

    // initialize L_y(i) and C_y(i)
//    memset(dad_branch->partial_lh, 0, nptn*nstates*sizeof(double));

    int *C_node = C + (node->id-leafNum)*nptn*nstates;

    for (ptn = 0; ptn < nptn; ptn++) {
        double *lh_dad = dad_branch->partial_lh+ptn*nstates;
        int *this_C_node = C_node + (ptn*nstates);
        leafid = 0;
        double sumlh[nstates];
        memset(sumlh, 0, sizeof(double)*nstates);
        FOR_NEIGHBOR(node, dad, it) {
            PhyloNeighbor *childnei = (PhyloNeighbor*)(*it);
            if ((*it)->node->isLeaf()) {
                double *lh_leaf = lh_leaves+leafid*nstates*(aln->STATE_UNKNOWN+1); 
                // external node
                int state_child;
                state_child = (aln->at(ptn))[(*it)->node->id];
                double *child_lh = lh_leaf + state_child*nstates;
                for (child = 0; child < nstates; child++)
                    sumlh[child] += child_lh[child];
                leafid++;
            } else {
                double *child_lh = childnei->partial_lh + ptn*nstates;
                for (child = 0; child < nstates; child++)
                    sumlh[child] += child_lh[child];
            }
        }
        
        
        if (dad) {
            // internal node
            for (parent = 0; parent < nstates; parent++) {
                lh_dad[parent] = trans_mat[parent*nstates] + sumlh[0];
                this_C_node[parent] = 0;
                for (child = 1; child < nstates; child++) {
                    double lh = trans_mat[parent*nstates+child] + sumlh[child];
                    if (lh > lh_dad[parent]) {
                        lh_dad[parent] = lh;
                        this_C_node[parent] = child;
                    }
                }
            }
        } else {
            // at the root
            lh_dad[0] = trans_mat[0] + sumlh[0];
            this_C_node[0] = 0;
            for (parent = 1; parent < nstates; parent++) {
                double lh = trans_mat[parent] + sumlh[parent];
                if (lh > lh_dad[0]) {
                    lh_dad[0] = lh;
                    this_C_node[0] = parent;
                }
            }
        }
    }
    

    if (lh_leaves)
        delete[] lh_leaves;
    delete[] trans_mat;
}


void PhyloTree::computeAncestralState(PhyloNeighbor *dad_branch, PhyloNode *dad, int *C, int *ancestral_seqs) {
    PhyloNode *node = (PhyloNode*)dad_branch->node;
    if (node->isLeaf())
        return;

    size_t nptn = aln->getNPattern();
    size_t ptn;
    size_t nstates = model->num_states;

    int *C_node = C + (node->id-leafNum)*nptn*nstates;
    int *ancestral_seqs_node = ancestral_seqs + (node->id-leafNum)*nptn; 
    if (dad) {
        // at an internal node
        int *ancestral_seqs_dad = ancestral_seqs + (dad->id-leafNum)*nptn;
        for (ptn = 0; ptn < nptn; ptn++)
            ancestral_seqs_node[ptn] = C_node[ptn*nstates+ancestral_seqs_dad[ptn]];
        
    } else {
        // at the root
        for (ptn = 0; ptn < nptn; ptn++)
            ancestral_seqs_node[ptn] = C_node[ptn*nstates];
    }
    FOR_NEIGHBOR_IT(node, dad, it)
        computeAncestralState((PhyloNeighbor*)(*it), node, C, ancestral_seqs);
}



