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
#ifndef IQPTREE_H
#define IQPTREE_H

#include "phylotree.h"
#include "phylonode.h"
#include <set>
#include <map>
#include "stoprule.h"


/**
	TODO
*/
const double MIN_BRANCH_LEN = 0.000001;
const double MAX_BRANCH_LEN = 9.0;
const double TOL_BRANCH_LEN = 0.00001;
const double TOL_LIKELIHOOD = 0.001;
const double SCALING_THRESHOLD = 1e-150;
const double LOG_SCALING_THRESHOLD = log(SCALING_THRESHOLD);


/**
	TODO
*/
typedef std::map< string, double > MapBranchLength;

/**
	nodeheightcmp, for building k-representative leaf set
*/
struct nodeheightcmp
{
  bool operator()(const Node* s1, const Node* s2) const
  {
    return (s1->height) < (s2->height);
  }
};

/**
	NNISwap, define a NNI Swap or Move
*/
struct NNIMove
{
	PhyloNode *node1;
	Neighbor *node1Nei;
	PhyloNode *node2;
	Neighbor *node2Nei;
	double score;

	bool operator<(const NNIMove& rhs) const
	{
		return score > rhs.score;
	}

};

/**
	Representative Leaf Set, stored as a multiset template of STL,
	sorted in ascending order of leaf's height
*/
typedef multiset<Node*, nodeheightcmp> RepresentLeafSet;

/**
	A Branch in the tree
*/
struct Branch
{
	PhyloNode *node1;
	PhyloNode *node2;
	double length;
};

/**
Important Quartet Puzzling

	@author BUI Quang Minh <minh.bui@univie.ac.at>
*/
class IQPTree : public PhyloTree
{
public:
	/**
		constructor
	*/
    IQPTree();

	/**
		destructor
	*/
    virtual ~IQPTree();

	/**
		set k-representative parameter
		@param k_rep k-representative
	*/
    void setRepresentNum(int k_rep);

	/**
		set the probability of deleteing sequences for IQP algorithm
		@param p_del probability of deleting sequences
	*/
    void setProbDelete(double p_del);

	/**
		set the number of iterations for the IQPNNI algorithm
		@param stop_condition stop condition (SC_FIXED_ITERATION, SC_STOP_PREDICT)
		@param min_iterations the min number of iterations
		@param max_iterations the maximum number of iterations
	*/
	void setIQPIterations(STOP_CONDITION stop_condition, double stop_confidence, int min_iterations, int max_iterations);

	/**
		find the k-representative leaves under the node
		@param node the node at which the subtree is rooted
		@param dad the dad node of the considered subtree, to direct the search
		@param leaves (OUT) the k-representative leaf set
	*/
	void findRepresentLeaves(RepresentLeafSet &leaves, PhyloNode *node = NULL, PhyloNode *dad = NULL);

	/**
		perform one IQPNNI iteration
		@return current likelihood
	*/
	double doIQP();

	/**
		perform all IQPNNI iterations
		@return best likelihood found
		@param tree_file_name name of the tree file to write the best tree found
	*/
	double doIQPNNI(string tree_file_name);

/****************************************************************************
	Fast Nearest Neighbor Interchange by maximum likelihood
****************************************************************************/

	/**
		This implement the fastNNI algorithm proposed in PHYML paper
		TUNG: this is a virtual function, so it will be called automatically by optimizeNNIBranches()
		@return best likelihood found
	*/
	virtual double optimizeNNI();


	/**
		search all positive NNI move on the current tree and save them on the possilbleNNIMoves list
		//TODO
	*/
	void genNNIMoves( PhyloNode *node = NULL, PhyloNode *dad = NULL );


	/**
		search the best swap for a branch
		@return NNIMove The best Move/Swap
		@param cur_score the current score of the tree before the swaps
		@param node1 1 of the 2 nodes on the branch
		@param node2 1 of the 2 nodes on the branch
	*/
	NNIMove getBestNNIMoveForBranch( PhyloNode *node1, PhyloNode *node2 );

	/**
		distance matrix, used for IQP algorithm
	*/
	double *dist_matrix;

	/**
		add a NNI move to the list of possible NNI moves;
	*/
	void addPossibleNNIMove(NNIMove myMove);

	/**
		Described in PhyML paper: apply changes to all branches that do not correspond to a swap with the following formula  l = l + lamda(la - l)
		TODO
	*/
	void applyAllBranchLengthChanges(PhyloNode *node, PhyloNode *dad = NULL);


	/**
		Described in PhyML paper: apply change to branch that does not correspond to a swap with the following formula l = l + lamda(la - l)
		@param node1 the first node of the branch
		@param node2 the second node of the branch
	*/
	double applyBranchLengthChange( PhyloNode *node1, PhyloNode *node2, bool nonNNIBranch );

	/**
		TODO
	*/
	void applyChildBranchChanges(PhyloNode *node, PhyloNode *dad);

	/**
		Do an NNI
	*/
	double swapNNIBranch(NNIMove move);


	/**
		TODO
	*/
	double calculateOptBranchLen( PhyloNode *node1, PhyloNode *node2 );

protected:

	/**
		stopping rule
	*/
	StopRule stop_rule;

	/**
		The lamda number for NNI process (described in PhyML Paper)
	*/
	double lamda;

	/**
		TODO
	*/
	int nbNNIToApply;

  	/**
		The list of possible NNI moves for the current tree;
	*/
	vector<NNIMove> possibleNNIMoves;


	/**
		List contains non-conflicting NNI moves for the current tree;
	*/
	vector<NNIMove> nonConflictMoves;


	/**
		Data structure (Map)storing all the optimal length of each branch
	*/
	MapBranchLength mapOptBranLens;

	/**
		k-representative parameter
	*/
	int k_represent;

	/**
		probability to delete a leaf
	*/
	double p_delete;

	/**
		number of IQPNNI iterations
	*/
	//int iqpnni_iterations;

	/**
		bonus values of all branches, used for IQP algorithm
	*/
	//double *bonus_values;

	/**
		delete a leaf from the tree, assume tree is birfucating
		@param leaf the leaf node to remove
	*/
	void deleteLeaf(Node *leaf);

	/**
		delete a set of leaves from tree (with the probability p_delete), assume tree is birfucating
		@param del_leaves (OUT) the list of deleted leaves
		@param adjacent_nodes (OUT) the corresponding list of nodes adjacent to the deleted leaves
	*/
	void deleteLeaves(PhyloNodeVector &del_leaves, PhyloNodeVector &adjacent_nodes);

	/**
		reinsert one leaf back into the tree
		@param leaf the leaf to reinsert
		@param adjacent_node the node adjacent to the leaf, returned by deleteLeaves() funcrion
		@param node one end node of the reinsertion branch in the existing tree
		@param dad the other node of the reinsertion branch in the existing tree
	*/
	void reinsertLeaf(Node *leaf, Node *adjacent_node, Node *node, Node *dad);

	/**
		reinsert the whole list of leaves back into the tree
		@param del_leaves the list of deleted leaves, returned by deleteLeaves() function
		@param adjacent_nodes the corresponding list of nodes adjacent to the deleted leaves, returned by deleteLeaves() function
	*/
	void reinsertLeaves(PhyloNodeVector &del_leaves, PhyloNodeVector &adjacent_nodes);

	/**
		assess a quartet with four taxa. Current implementation uses the four-point condition
		based on distance matrix for quick evaluation.
		@param leaf0 one of the leaf in the existing sub-tree
		@param leaf1 one of the leaf in the existing sub-tree
		@param leaf2 one of the leaf in the existing sub-tree
		@param del_leaf a leaf that was deleted (not in the existing sub-tree)
	*/
	int assessQuartet(Node *leaf0, Node *leaf1, Node *leaf2, Node *del_leaf);

	/**
		assess the important quartets around a virtual root of the tree.
		This function will assign bonus points to branches by updating the variable 'bonus_values'
		@param cur_root the current virtual root
		@param del_leaf a leaf that was deleted (not in the existing sub-tree)
	*/
	void assessQuartets(PhyloNode *cur_root, PhyloNode *del_leaf);

	/**
		initialize the bonus points to ZERO
		@param node the root of the sub-tree
		@param dad dad of 'node', used to direct the recursion
	*/
	void initializeBonus(PhyloNode *node = NULL, PhyloNode *dad = NULL);

	/**
		raise the bonus points for all branches in the subtree rooted at a node
		@param node the root of the sub-tree
		@param dad dad of 'node', used to direct the recursion
	*/
	void raiseBonus(Node *node, Node *dad);

	/**
		find the best bonus point
		@param node the root of the sub-tree
		@param dad dad of 'node', used to direct the recursion
	*/
	double findBestBonus(Node *node = NULL, Node *dad = NULL);

	/**
		determine the list of branches with the same best bonus point
		@param best_bonus the best bonus determined by findBestBonus()
		@param best_nodes (OUT) vector of one ends of the branches with highest bonus point
		@param best_dads (OUT) vector of the other ends of the branches with highest bonus point
		@param node the root of the sub-tree
		@param dad dad of 'node', used to direct the recursion
	*/
	void findBestBranch(double best_bonus, NodeVector &best_nodes, NodeVector &best_dads, Node *node = NULL, Node *dad = NULL);

};

#endif
