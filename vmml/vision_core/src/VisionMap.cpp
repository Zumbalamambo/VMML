/*
 * VisionMap.cpp
 *
 *  Created on: Oct 7, 2019
 *      Author: sujiwo
 */

#include <memory>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>

#include "VisionMap.h"

#define MAX_ORB_POINTS_IN_FRAME 9000

using namespace std;
using namespace Eigen;


namespace Vmml {

std::vector<cv::Mat> toDescriptorVector(const cv::Mat &Descriptors)
{
    std::vector<cv::Mat> vDesc;
    vDesc.reserve(Descriptors.rows);
    for (int j=0;j<Descriptors.rows;j++)
        vDesc.push_back(Descriptors.row(j));

    return vDesc;
}


VisionMap::VisionMap() :

	keyframeInvIdx_mtx(new mutex),
	mappointInvIdx_mtx(new mutex)

{
	// Feature detector
	cv::Ptr<cv::ORB> orbf = cv::ORB::create(
		MAX_ORB_POINTS_IN_FRAME,
		1.2,
		8,
		32,
		0,
		2,
		cv::ORB::HARRIS_SCORE,
		32,
		10);
	featureDetector = orbf;

	// Fill scale factors
	int level = orbf->getNLevels();
	mScaleFactors.resize(level);
	mLevelSigma2.resize(level);
	mScaleFactors[0] = 1.0;
	mLevelSigma2[0] = 1.0;
	for (int i=1; i<level; ++i) {
		mScaleFactors[i] = mScaleFactors[i-1] * orbf->getScaleFactor();
		mLevelSigma2[i] = mScaleFactors[i] * mScaleFactors[i];
	}

	// Descriptor matcher
	descriptorMatcher = cv::BFMatcher::create(cv::NORM_HAMMING, false);
}


VisionMap::~VisionMap() {
	// TODO Auto-generated destructor stub
}


void
VisionMap::reset()
{
	keyframeInvIdx.clear();
	mappointInvIdx.clear();
	pointAppearances.clear();
	framePoints.clear();
	framePointsInv.clear();
}


bool
VisionMap::addKeyFrame(KeyFrame::Ptr frame)
{
	auto nId = frame->getId();

	keyframeInvIdx_mtx->lock();
	keyframeInvIdx.insert(make_pair(nId, frame));
	keyframeInvIdx_mtx->unlock();

	/*
	 * image database part
	 */
	auto kfDescriptors = toDescriptorVector(frame->allDescriptors());
	BoWList[nId] = DBoW2::BowVector();
	FeatVecList[nId] = DBoW2::FeatureVector();
	myVoc.transform(kfDescriptors, BoWList[nId], FeatVecList[nId], 4);
	// Build Inverse Index
	for (auto &bowvec: BoWList[nId]) {
		const DBoW2::WordId wrd = bowvec.first;
		invertedKeywordDb[wrd].insert(nId);
	}

	// This keyframe has no map points yet
	framePoints[nId] = map<mpid,kpid>();
	framePointsInv[nId] = map<kpid,mpid>();

	// Graph
	auto vtId = boost::add_vertex(covisibility);
	kfVtxMap[nId] = vtId;
	kfVtxInvMap[vtId] = nId;

	return true;
}


bool
VisionMap::addMapPoint(MapPoint::Ptr mp)
{
	mpid nId = mp->getId();
	mappointInvIdx.insert(make_pair (nId, mp));

	return true;
}


map<mpid,kpid>
VisionMap::allMapPointsAtKeyFrame(const kfid f)
const
{
	if (framePoints.size()==0)
		return map<mpid,kpid>();

	return framePoints.at(f);
}


pcl::PointCloud<pcl::PointXYZ>::Ptr
VisionMap::dumpPointCloudFromMapPoints ()
const
{
	pcl::PointCloud<pcl::PointXYZ>::Ptr mapPtCl
		(new pcl::PointCloud<pcl::PointXYZ>(mappointInvIdx.size(), 1));

	uint64 i = 0;
	for (auto &mpIdx: mappointInvIdx) {
		auto &mp = mpIdx.second;
		mapPtCl->at(i).x = mp->X();
		mapPtCl->at(i).y = mp->Y();
		mapPtCl->at(i).z = mp->Z();
		i++;
	}

	return mapPtCl;
}


vector<kfid>
VisionMap::allKeyFrames () const
{
	vector<kfid> kfIdList;
	for (auto &key: keyframeInvIdx) {
		kfIdList.push_back(key.first);
	}
	return kfIdList;
}


vector<mpid>
VisionMap::allMapPoints () const
{
	vector<mpid> mpIdList;
	for (auto &key: mappointInvIdx) {
		mpIdList.push_back(key.first);
	}
	return mpIdList;
}


bool
VisionMap::removeMapPoint (const mpid &i)
{
	assert(mappointInvIdx.find(i) != mappointInvIdx.end());

	mappointInvIdx.erase(i);
	set<kfid> ptAppears = pointAppearances[i];
	for (auto k: ptAppears) {
		const kpid kp = framePoints[k].at(i);
		framePoints[k].erase(i);
//		framePointsInv[k].erase(kp);
	}

	pointAppearances.erase(i);

	// Modify Graph
	for (auto k: ptAppears) {
		updateCovisibilityGraph(k);
	}

	return true;
}


bool
VisionMap::removeMapPointsBatch (const vector<mpid> &mplist)
{
	set<kfid> kfModified;

	for (auto &pt: mplist) {
		mappointInvIdx.erase(pt);
		set<kfid> kfAppears = pointAppearances[pt];
		for (auto kf: kfAppears) {
			kfModified.insert(kf);
			const kpid kp = framePoints[kf].at(pt);
			framePoints[kf].erase(pt);
		}

		pointAppearances.erase(pt);
	}

	for (auto kf: kfModified) {
		updateCovisibilityGraph(kf);
	}

	return true;
}


void
VisionMap::updateCovisibilityGraph(const kfid k)
{
	map<kfid,int> kfCounter;

	for (auto mp_ptr: framePoints.at(k)) {
		mpid pId = mp_ptr.first;
		for (auto kr: pointAppearances.at(pId)) {
			if (kr==k)
				continue;
			kfCounter[kr]++;
		}
	}

	if (kfCounter.empty())
		return;

//	Should we clear the vertex?
//	boost::clear_vertex(kfVtxMap[k], covisibility);
	for (auto kfctr: kfCounter) {
		covisibilityGraphMtx.lock();
		// XXX: Do NOT put KfID directly to graph; use vertex descriptor instead
		boost::add_edge(kfVtxMap[k], kfVtxMap[kfctr.first], kfctr.second, covisibility);
		covisibilityGraphMtx.unlock();
	}
}


} /* namespace Vmml */