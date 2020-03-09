/*
 * VisualOdometry.cpp
 *
 *  Created on: Oct 15, 2019
 *      Author: sujiwo
 */

#include <opencv2/video/tracking.hpp>
#include "vmml/VisualOdometry.h"
#include "vmml/Triangulation.h"
#include "vmml/utilities.h"


namespace Vmml {


typedef LocalLidarMapper::PointType PointType;
typedef LocalLidarMapper::CloudType CloudType;


VisualOdometry::VisualOdometry(Parameters par) :
	param(par),
	featureGrid(par.bucket_width, par.bucket_height),
	points3d(new CloudType)
{
	// XXX: Temporary
	featureDetector=cv::ORB::create(
		6000,
		1.2,
		8,
		32,
		0,
		2,
		cv::ORB::HARRIS_SCORE,
		32,
		10);
}


VisualOdometry::~VisualOdometry()
{
}


//cv::Ptr<cv::BFMatcher> featureBfMatcher = cv::BFMatcher::create(cv::NORM_HAMMING);


bool
VisualOdometry::runMatching (cv::Mat img, const ptime &timestamp, cv::Mat mask)
{
	if (mAnchorImage==nullptr and mCurrentImage==nullptr) {
		mAnchorImage = BaseFrame::create(img, param.camera);
		mAnchorImage->computeFeatures(featureDetector, mask);
		mAnchorImage->setPose(Pose::Identity());
		mVoTrack.push_back(PoseStamped(Pose::Identity(), timestamp));
		return false;
	}

	if (mCurrentImage!=nullptr)
		mAnchorImage = mCurrentImage;

	mCurrentImage = BaseFrame::create(img, param.camera);
	mCurrentImage->computeFeatures(featureDetector, mask);

//	Matcher::matchBruteForce(*mAnchorImage, *mCurrentImage, matcherToAnchor);
	Matcher::matchOpticalFlow(*mAnchorImage, *mCurrentImage, matcherToAnchor);

	return true;
}


bool
VisualOdometry::process(cv::Mat img, const ptime &timestamp, cv::Mat mask)
{
	if (runMatching(img, timestamp, mask)==false)
		return false;

	// Get transformation, and inliers
	TTransform motion = Matcher::calculateMovement(*mAnchorImage, *mCurrentImage, matcherToAnchor, matcherToAnchor);
	if (matcherToAnchor.size()<=10)
		return false;

	Pose pCurrent = mAnchorImage->pose() * motion;
	mCurrentImage->setPose(pCurrent);
	mVoTrack.push_back(PoseStamped(mCurrentImage->pose(), timestamp));

	map<uint, Vector3d> mapPoints;
	float parallax;
	TriangulateCV(*mAnchorImage, *mCurrentImage, matcherToAnchor, mapPoints, &parallax);
	for (auto &pt3dPr: mapPoints) {
		points3d->push_back(PointType(pt3dPr.second.x(), pt3dPr.second.y(), pt3dPr.second.z()));
	}
	cout << "Found " << mapPoints.size() << " points" << endl;

	return true;
}


TTransform
VisualOdometry::estimateMotion()
{

}


} /* namespace Vmml */
