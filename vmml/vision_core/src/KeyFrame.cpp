/*
 * KeyFrame.cpp
 *
 *  Created on: Oct 7, 2019
 *      Author: sujiwo
 */

#include "vmml/utilities.h"
#include "vmml/KeyFrame.h"
#include "vmml/VisionMap.h"

using namespace std;
using namespace Eigen;


namespace Vmml {

/*
 * Ensure that KeyFrame ID is always positive and non-zero
 */
kfid KeyFrame::nextId = 1;


KeyFrame::KeyFrame(const std::shared_ptr<VisionMap> _parent) :
	mParent(_parent)
{}


KeyFrame::KeyFrame(cv::Mat img, const std::shared_ptr<VisionMap> _parent, int cameraNo, const ptime &ts, const Pose &p, bool doComputeFeatures, const cv::Mat &mask) :
	BaseFrame(img, _parent->getCameraParameter(cameraNo), p),
	mParent(_parent),
	cameraId(cameraNo),
	frCreationTime(ts)
{
	id = KeyFrame::nextId;
	if (doComputeFeatures==true) {
		computeFeatures(mParent->getFeatureDetector(), mask);
	}
	nextId++;
}


KeyFrame::KeyFrame
(const BaseFrame &bsFrame, const std::shared_ptr<VisionMap> _parent, int cameraNo, const ptime &ts) :
	BaseFrame::BaseFrame(bsFrame),
	mParent(_parent),
	cameraId(cameraNo),
	frCreationTime(ts)
{
	id = KeyFrame::nextId;
	nextId++;
}


KeyFrame::~KeyFrame() {
	// TODO Auto-generated destructor stub
}


KeyFrame::Ptr
KeyFrame::create(cv::Mat image, const std::shared_ptr<VisionMap>& mParent, int cameraNumber, const ptime &ts)
{
	Ptr newKf(new KeyFrame(image, mParent, cameraNumber, ts));
	newKf->frCreationTime = ts;
	return newKf;
}


KeyFrame::Ptr
KeyFrame::fromBaseFrame(const BaseFrame &frameSource, const std::shared_ptr<VisionMap>& mParent, int cameraNumber, const ptime &ts)
{
	Ptr kfrm (new KeyFrame(frameSource, mParent, cameraNumber));
	kfrm->frCreationTime = ts;
	return kfrm;
}


int
KeyFrame::numberOfMappoints() const
{
	return mParent->framePoints.at(id).size();
}


double
KeyFrame::computeSceneMedianDepth() const
{
	auto allMps = mParent->allMapPointsAtKeyFrame(id);
	VectorXx<float> depths=VectorXx<float>::Zero(allMps.size());
	Matrix4d Cw = externalParamMatrix4();
	Vector3d R2 = Cw.row(2).head(3);
	double zcw = Cw(2,3);

	int i = 0;
	for (auto &ptPair: allMps) {
		auto mapPoint = mParent->mappoint(ptPair.first);
		Vector3d mPos = mapPoint->getPosition();
		double x=mPos.x(), y=mPos.y(), z=mPos.z();
		if (abs(x)<1e-10 or abs(y)<1e-10 or abs(z)<1e-10) {
			int m=1;
		}
		double d = R2.dot(mPos) + zcw;
		depths[i] = d;
		i++;
	}

	return median(depths);
}


std::vector<mpid>
KeyFrame::getMapPointsInArea (const float x, const float y, const float windowSize, const int minLevel, const int maxLevel)
const
{
	vector<mpid> mapPts;

	auto keyPoints = getKeyPointsInArea(x, y, windowSize, minLevel, maxLevel);
	for (auto &kp: keyPoints) {
		try {
			mpid mp = mParent->getMapPointByKeypoint(id, kp);
			mapPts.push_back(mp);
		} catch (...) { continue; }
	}

	return mapPts;
}


std::vector<MapPoint::Ptr>
KeyFrame::getVisibleMapPoints() const
{
	vector<MapPoint::Ptr> mpList;

	for (auto &mp: mParent->framePoints.at(id)) {
		auto mpId = mp.first;
		mpList.push_back(mParent->mappoint(mpId));
	}

	return mpList;
}

} /* namespace Vmml */
