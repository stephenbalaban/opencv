/*
 * Copyright (c) 2011. Philipp Wagner <bytefish[at]gmx[dot]de>.
 * Released to public domain under terms of the BSD Simplified license.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the organization nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 *   See <http://www.opensource.org/licenses/bsd-license>
 */
#include <map>
#include "subspace.hpp"
#include "helper.hpp"
#ifdef HAVE_EIGEN
#include "eigen3/Eigen/Dense"
using namespace Eigen;
#else
#include "decomposition.hpp"
#endif

using namespace cv;

//! computes Y = (X-mean)*W
Mat subspace::project(const Mat& W, const Mat& mean, const Mat& src, bool dataAsRow) {
	// get number of samples and dimension
	int n = dataAsRow ? src.rows : src.cols;
	int d = dataAsRow ? src.cols : src.rows;
	// assert mean has correct shape
	if(d != mean.total())
		CV_Error(CV_StsUnmatchedSizes, "The dimension of the samples in src must equal the dimension of the sample mean!");
	// initalize temporary matrices
	Mat data, X, Y;
	// convert src data to correct type
	src.convertTo(data, mean.type());
	// center the data
	subtract(dataAsRow ? data : transpose(data),
			repeat(mean.reshape(1,1), n, 1),
			X);
	// calculate projection as Y = (X-mean)*W
	gemm(X, W, 1.0, Mat(), 0.0, Y);
	return dataAsRow ? Y : transpose(Y);
}

//! X = Y*W'+mean
Mat subspace::reconstruct(const Mat& W, const Mat& mean, const Mat& src, bool dataAsRow) {
	// get number of samples and dimension
	int n = dataAsRow ? src.rows : src.cols;
	int d = dataAsRow ? src.cols : src.rows;
	// assert mean has correct shape
	if(d != mean.total())
		CV_Error(CV_StsUnmatchedSizes, "The dimension of the samples in src must equal the dimension of the sample mean!");
	// initalize temporary matrices
	Mat data, X;
	// convert src data to correct type
	src.convertTo(data, mean.type());
	// calculate the reconstruction X = Y*W + mean
	gemm(dataAsRow ? data : transpose(data),
			W,
			1.0,
			repeat(mean.reshape(1,1), n, 1),
			1.0,
			X,
			GEMM_2_T);
	return dataAsRow ? X : transpose(X);
}

void subspace::LinearDiscriminantAnalysis::compute(const Mat& src, const vector<int>& labels) {
	if(src.channels() != 1)
		CV_Error(CV_StsBadArg, "Only single channel matrices allowed.");
	Mat data = _dataAsRow ? src.clone() : transpose(src);
	// since we are dealing with very small numbers, use double values
	data.convertTo(data, CV_64FC1);
	// maps the labels, so they have an ascending identifier from 0:C
	vector<int> mapped_labels(labels.size());
	vector<int> num2label = cv::remove_dups(labels);
	map<int,int> label2num;
	for(int i=0;i<num2label.size();i++)
		label2num[num2label[i]] = i;
	for(int i=0;i<labels.size();i++)
		mapped_labels[i] = label2num[labels[i]];
	// get sample size, dimension
	int N = data.rows;
	int D = data.cols;
	// get number of classes (number of unique labels)
	int C = num2label.size();
	// assert, that: len(src) = len(labels)
	if(labels.size() != N)
		CV_Error(CV_StsBadArg, "Error: The number of samples must equal the number of labels.");
	// warn if within-classes scatter matrix becomes singular
	if(N < D)
		cout << "Warning: Less observations than feature dimension given! Computation will probably fail." << endl;
	// clip number of components to be a valid number
	if((_num_components <= 0) || (_num_components > (C-1)))
		_num_components = (C-1);
	// holds the mean over all classes
	Mat meanTotal = Mat::zeros(1, D, data.type());
	// holds the mean for each class
	Mat meanClass[C];
	int numClass[C];
	// initialize
	for (int i = 0; i < C; i++) {
		numClass[i] = 0;
		meanClass[i] = Mat::zeros(1, D, data.type()); //! Dx1 image vector
	}
	// calculate sums
	for (int i = 0; i < N; i++) {
		Mat instance = data.row(i);
		int classIdx = mapped_labels[i];
		add(meanTotal, instance, meanTotal);
		add(meanClass[classIdx], instance, meanClass[classIdx]);
		numClass[classIdx]++;
	}
	// calculate means
	meanTotal.convertTo(meanTotal, meanTotal.type(), 1.0/static_cast<double>(N));
	for (int i = 0; i < C; i++)
		meanClass[i].convertTo(meanClass[i], meanClass[i].type(), 1.0/static_cast<double>(numClass[i]));
	// subtract class means
	for (int i = 0; i < N; i++) {
		int classIdx = mapped_labels[i];
		Mat instance = data.row(i);
		subtract(instance, meanClass[classIdx], instance);
	}
	// calculate within-classes scatter
	Mat Sw = Mat::zeros(D, D, data.type());
	mulTransposed(data, Sw, true);
	// calculate between-classes scatter
	Mat Sb = Mat::zeros(D, D, data.type());
	for (int i = 0; i < C; i++) {
		Mat tmp;
		subtract(meanClass[i], meanTotal, tmp);
		mulTransposed(tmp, tmp, true);
		add(Sb, tmp, Sb);
	}
	// invert Sw
	Mat Swi = Sw.inv();
	// M = inv(Sw)*Sb
	Mat M;
	gemm(Swi, Sb, 1.0, Mat(), 0.0, M);
#ifdef HAVE_EIGEN
	// now switch to eigen (cv2eigen defined in helper.hpp)
	MatrixXd Me;
	cv2eigen(M, Me);
	// solve eigenvalue problem for the general matrix $M = Sw^{-1} Sb$
	Eigen::EigenSolver<MatrixXd> es(Me);
	// copy real values over to opencv
	eigen2cv(MatrixXd(es.eigenvectors().real()), _eigenvectors);
	eigen2cv(MatrixXd(es.eigenvalues().real()), _eigenvalues);
#else
	EigenvalueDecomposition es(M);
	_eigenvalues = es.eigenvalues();
	_eigenvectors = es.eigenvectors();
#endif
	// reshape eigenvalues, so they are stored by column
	_eigenvalues = _eigenvalues.reshape(1,1);
	// get sorted indices descending by their eigenvalue
	vector<int> sorted_indices = argsort(_eigenvalues, false);
	// now sort eigenvalues and eigenvectors accordingly
	_eigenvalues = sortMatrixByColumn(_eigenvalues, sorted_indices);
	_eigenvectors = sortMatrixByColumn(_eigenvectors, sorted_indices);
	// and now take only the num_components and we're out!
	_eigenvalues = Mat(_eigenvalues, Range::all(), Range(0,_num_components));
	_eigenvectors = Mat(_eigenvectors, Range::all(), Range(0, _num_components));
}

void subspace::LinearDiscriminantAnalysis::compute(const vector<Mat>& src, const vector<int>& labels) {
	compute(_dataAsRow? asRowMatrix(src) : asColumnMatrix(src), labels);
}

Mat subspace::LinearDiscriminantAnalysis::project(const Mat& src) {
	Mat dst;
	gemm(_dataAsRow ? src : transpose(src), _eigenvectors, 1.0, Mat(), 0, dst);
	return _dataAsRow ? dst : transpose(dst);
}

Mat subspace::LinearDiscriminantAnalysis::reconstruct(const Mat& src) {
	Mat dst;
	gemm(_dataAsRow ? src : transpose(src), src, 1.0, Mat(), 0, dst, GEMM_2_T);
	return _dataAsRow ? dst : transpose(dst);
}
