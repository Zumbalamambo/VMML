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

const cv::Size optFlowWindowSize(15, 15);
const int maxLevel = 2;
const cv::TermCriteria optFlowStopCriteria(cv::TermCriteria::EPS|cv::TermCriteria::COUNT, 10, 0.03);


VisualOdometry::VisualOdometry(Parameters par) :
	param(par),
	featureGrid(par.bucket_width, par.bucket_height),
	points3d(new CloudType)
{
	// XXX: Temporary
	featureDetector=cv::ORB::create(
		maxNumberFeatures,
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
VisualOdometry::runMatching2 (cv::Mat img, const ptime &timestamp, cv::Mat mask)
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

	auto bfMatch = cv::BFMatcher::create(cv::NORM_HAMMING, true);
	Matcher::matchOpticalFlow(*mAnchorImage, *mCurrentImage, flowMatcherToAnchor);
	return true;
}


bool
VisualOdometry::runMatching (cv::Mat img, const ptime &timestamp, cv::Mat mask)
{
	mCurrentImage = BaseFrame::create(img, param.camera);
	if (frameCounter==0)
		mCurrentImage->setPose(Pose::Identity());

	if (voFeatureTracker.size()>0) {
		uint nextFrameCounter = frameCounter+1, prevFrameCounter = frameCounter-1;
		auto trackedFeatsPoints = voFeatureTracker.trackedFeaturesAtFrame(prevFrameCounter);
		auto &_featureTrackIds = voFeatureTracker.getFeatureTrackIdsAtFrame(prevFrameCounter);
		assert(trackedFeatsPoints.rows==_featureTrackIds.size());
		vector<FeatureTrackList::TrackId> featureTrackIds(_featureTrackIds.begin(), _featureTrackIds.end());
		cv::Mat p1, p0r, status, errOf;

		cv::calcOpticalFlowPyrLK(mAnchorImage->getImage(), mCurrentImage->getImage(), trackedFeatsPoints, p1, status, errOf, optFlowWindowSize, maxLevel, optFlowStopCriteria);
		cv::calcOpticalFlowPyrLK(mCurrentImage->getImage(), mAnchorImage->getImage(), p1, p0r, status, errOf, optFlowWindowSize, maxLevel, optFlowStopCriteria);

		cv::Mat absDiff(cv::abs(trackedFeatsPoints-p0r));

		for (int r=0; r<trackedFeatsPoints.rows; ++r) {
			cv::Point2f pt(p1.row(r));

			// Do track/feature filtering here
			if (absDiff.at<float>(r,0)>=1 or absDiff.at<float>(r,1)>=1)
				continue;
			if (pt.x<0 or pt.x>=mCurrentImage->width() or pt.y<0 or pt.y>=mCurrentImage->height())
				continue;

			// Add tracked point
			auto trackId = featureTrackIds[r];
			voFeatureTracker.addTrackedPoint(frameCounter, trackId, pt);
		}
	}

	if (frameCounter % featureDetectionInterval==0) {
		cv::Mat fMask;
		int prevFrameNumber = frameCounter-1;
		int numOfFeaturesToDetect = maxNumberFeatures;

		// Get feature tracks from previous frame
		if (prevFrameNumber==-1)
			fMask = mask;
		else {
			fMask = voFeatureTracker.createFeatureMask(mask, prevFrameNumber);
			numOfFeaturesToDetect -= voFeatureTracker.getNumberOfFeatureTracksAt(prevFrameNumber);
		}
		featureDetector->setMaxFeatures(numOfFeaturesToDetect);

		mCurrentImage->computeFeatures(featureDetector, fMask);
		// Add all features as new track
		for (int i=0; i<mCurrentImage->numOfKeyPoints(); ++i) {
			FeatureTrack ftNew(frameCounter, mCurrentImage->keypoint(i).pt);
			voFeatureTracker.add(ftNew);
		}
	}

	drawFlow(img);
	return true;
}


bool
VisualOdometry::process(cv::Mat img, const ptime &timestamp, cv::Mat mask, bool matchOnly)
{
	bool bMatch = runMatching2(img, timestamp, mask);
	if (bMatch==false)
		return false;

	if (matchOnly==false) {
		// Epipolar geometry constraints tend to reduce number of feature matches,
		// to the point that when camera is stationary there is no matching detected

		// Get transformation, and inliers
		TTransform motion = Matcher::calculateMovement(*mAnchorImage, *mCurrentImage, flowMatcherToAnchor, voMatcherToAnchor);
		if (voMatcherToAnchor.size()<=10)
			return false;

		Pose pCurrent = mAnchorImage->pose() * motion;
		mCurrentImage->setPose(pCurrent);
		mVoTrack.push_back(PoseStamped(mCurrentImage->pose(), timestamp));

		map<uint, Vector3d> mapPoints;
		float parallax;
		TriangulateCV(*mAnchorImage, *mCurrentImage, voMatcherToAnchor, mapPoints, &parallax);
		for (auto &pt3dPr: mapPoints) {
			points3d->push_back(PointType(pt3dPr.second.x(), pt3dPr.second.y(), pt3dPr.second.z()));
		}
		cout << "Found " << mapPoints.size() << " points" << endl;
	}

	else {
		voMatcherToAnchor = flowMatcherToAnchor;
	}

	frameCounter+=1;

	return true;
}


void
VisualOdometry::drawFlow(cv::Mat canvas)
{
	_flowCanvas = canvas.clone();

	for (auto tr: voFeatureTracker.getFeatureTracksAt(frameCounter)) {
		cv::Mat ptMat = tr->getPointsAsMat(10);
		cv::circle(_flowCanvas, cv::Point2i(ptMat.row(ptMat.rows-1)), 1, cv::Scalar(0,0,255), -1);
		cv::polylines(_flowCanvas, ptMat, false, cv::Scalar(0,255,0));
	}
}


TTransform
VisualOdometry::estimateMotion()
{

}


} /* namespace Vmml */
