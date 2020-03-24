/*
 * MapBuilder.cpp
 *
 *  Created on: Oct 8, 2019
 *      Author: sujiwo
 */

#include "vmml/SimpleMapBuilder.h"
#include "vmml/BaseFrame.h"
#include "vmml/Matcher.h"
#include "vmml/Triangulation.h"
#include "vmml/Optimizer.h"


using namespace std;
using namespace Eigen;


namespace Vmml {


SimpleMapBuilder::TmpFrame::
	TmpFrame(cv::Mat img, std::shared_ptr<VisionMap> &_parent, const cv::Mat &mask) :
		BaseFrame(img, _parent->getCameraParameter(0), Pose::Identity()),
		parent(_parent)
{
	computeFeatures(parent->getFeatureDetector(), mask);
}


SimpleMapBuilder::TmpFrame::Ptr
SimpleMapBuilder::TmpFrame::create(cv::Mat img, shared_ptr<VisionMap> &_parent, const cv::Mat &mask)
{ return Ptr(new TmpFrame(img, _parent, mask)); }


/*
 * This function test optical flow hypothesis from kf to current frame
 */
bool
SimpleMapBuilder::TmpFrame::initializeMatch(const KeyFrame::Ptr &kf, std::vector<Eigen::Vector3d> &points3)
{
	bool isMoving;
	Matcher::matchOpticalFlow(*kf, *this, matchesToKeyFrame, &isMoving);

	if (isMoving==false)
		return false;

	Matcher::PairList voMatches;
	points3.clear();
	TTransform motion;
	Matcher::calculateMovement2(
		*kf, *this,
		matchesToKeyFrame, voMatches,
		motion, points3);
	if (voMatches.size()<=10)
		return false;

	this->setPose(motion);
	matchesToKeyFrame = voMatches;
	return true;
}


bool
SimpleMapBuilder::TmpFrame::track(const kfid &kf)
{
	parentKeyFrame = parent->keyframe(kf);

	bool isMoving;
	Matcher::matchOpticalFlow(*parentKeyFrame, *this, matchesToKeyFrame, &isMoving);
	if (isMoving==false or matchesToKeyFrame.size()<10)
		return false;

	// Separate old map points (which visible in KF1) from potential new ones (which will be generated by triangulation)
	set<kpid> kp1InMap;
	for (auto &p: parent->allMapPointsAtKeyFrame(kf))
		kp1InMap.insert(p.second);

	for (auto &kpPair: matchesToKeyFrame) {
		if (kp1InMap.find(kpPair.first) != kp1InMap.end()) {
			prevMapPointPairs.push_back(kpPair);
		}
		else {
			candidatesMapPointPairs.push_back(kpPair);
		}
	}

	cout << "Tracking; total: " << matchesToKeyFrame.size() << "; map: " << prevMapPointPairs.size() << "; cand: " << candidatesMapPointPairs.size() << endl;

	// XXX: Find solution for insufficient pairs !
	if (prevMapPointPairs.size() < 4) {
		cerr << "Insufficient points!\n";
		return false;
	}

	// Estimate pose for KF2
	// XXX: Re-check result of this function
	Pose PF2;
	Matcher::solvePose(*parentKeyFrame, *this, prevMapPointPairs, PF2);
	this->setPose(PF2);

	// Debugging
	TTransform movement = parentKeyFrame->pose().inverse() * PF2;
	double dx = movement.translation().norm();

	return true;
}


/*
 * XXX: Check this function
 */
bool
SimpleMapBuilder::TmpFrame::isOkForKeyFrame() const
{
	const float mapPointThresholdRatio = 0.3;

	if (float(candidatesMapPointPairs.size()) / float(matchesToKeyFrame.size()) >= mapPointThresholdRatio) {
		return true;
	}

	else return false;
}


KeyFrame::Ptr
SimpleMapBuilder::TmpFrame::toKeyFrame() const
{
	auto KF = KeyFrame::fromBaseFrame(*this, parent, 0, this->timestamp);
	KF->setPose(this->pose());
	return KF;
}


cv::Mat
SimpleMapBuilder::TmpFrame::visualize() const
{
	return image.clone();
}


SimpleMapBuilder::SimpleMapBuilder(const Parameters &pars) :
	smParameters(pars)
{
	vMap = std::make_shared<VisionMap>();
	vMap->addCameraParameter(smParameters.camera);
}


bool
SimpleMapBuilder::process(const cv::Mat &inputImage, const ptime &timestamp, const cv::Mat &mask)
{
	currentWorkframe = TmpFrame::create(inputImage, vMap, mask);
	currentWorkframe->timestamp = timestamp;
	frameCounter += 1;

	// No frame yet
	if (lastAnchor==0) {
		auto K1 = KeyFrame::fromBaseFrame(*currentWorkframe, vMap);
		lastAnchor = K1->getId();
		vMap->addKeyFrame(K1);
		callFrameFunction();
		return true;
	}
	else {

		if (hasInitialized==false) {
			// Try initialization
			if (initialize()==true) {
				hasInitialized = true;
				cout << "Initialization success; points: " << vMap->numOfMapPoints() << endl;
				return true;
				exit(1);
			}

			else {
				// skip to next frame, maybe better
				cout << "Initialization failed\n";
				return false;
			}
		}

		// Tracking mode
		else {
			track();
		}
	}

	return true;
}


SimpleMapBuilder::~SimpleMapBuilder() {
	// TODO Auto-generated destructor stub
}


bool
SimpleMapBuilder::initialize()
{
	auto anchorKeyframe = vMap->keyframe(lastAnchor);
	vector<Vector3d> trPoints;
	if (currentWorkframe->initializeMatch(anchorKeyframe, trPoints)==false)
		return false;

	return createInitialMap(trPoints);
}


bool
SimpleMapBuilder::track()
{
	if (currentWorkframe->track(lastAnchor)==false)
		return false;

	/*
	 * It's OK, we only continue when there has been enough 'innovation'
	 */
	if (currentWorkframe->isOkForKeyFrame()==false) {
		callFrameFunction();
		return true;
	}

	auto Knew = KeyFrame::fromBaseFrame(*currentWorkframe, vMap);
	vMap->addKeyFrame(Knew);

	// Put point appearances
	for (int i=0; i<currentWorkframe->prevMapPointPairs.size(); i++) {
		mpid ptId = vMap->getMapPointByKeypoint(lastAnchor, currentWorkframe->prevMapPointPairs[i].first);
		vMap->addMapPointVisibility(ptId, Knew->getId(), currentWorkframe->prevMapPointPairs[i].second);
		vMap->updateMapPointDescriptor(ptId);
	}

	// Triangulation for new map points
	map<uint, Vector3d> mapPoints;
	float parallax;
	TriangulateCV(*currentWorkframe->parentKeyFrame, *Knew, currentWorkframe->candidatesMapPointPairs, mapPoints, &parallax);
	cout << "Creating map points: " << currentWorkframe->candidatesMapPointPairs.size() << ", got " << mapPoints.size() << endl;
	for (auto &p: mapPoints) {
		auto ptn = MapPoint::create(p.second);
		vMap->addMapPoint(ptn);
		vMap->addMapPointVisibility(ptn->getId(), lastAnchor, currentWorkframe->candidatesMapPointPairs[p.first].first);
		vMap->addMapPointVisibility(ptn->getId(), Knew->getId(), currentWorkframe->candidatesMapPointPairs[p.first].second);
//		vMap->updateMapPointDescriptor(ptn->getId());
	}

	// Build connections to previous keyframes
	vector<kfid> kfInsToAnchor = vMap->getKeyFramesComeInto(lastAnchor);
	const kfid targetKfId = lastAnchor;
/*
	for (auto &kfx: kfInsToAnchor) {
		trackMapPoints(kfx, lastAnchor);
	}
*/

	// Check whether we need local BA
	kfInsToAnchor = vMap->getKeyFramesComeInto(lastAnchor);
/*
	if (std::find(kfInsToAnchor.begin(), kfInsToAnchor.end(), localBAAnchor)==kfInsToAnchor.end()) {
		cout << "Local BA running\n";
		auto vAnchors = cMap->getKeyFramesComeInto(lastAnchor);
		local_bundle_adjustment(cMap, lastAnchor);
		localBAAnchor = lastAnchor;
	}
*/

	vMap->updateCovisibilityGraph(lastAnchor);
	lastAnchor = Knew->getId();
	callFrameFunction();

	return true;
}


bool
SimpleMapBuilder::createInitialMap(const std::vector<Eigen::Vector3d> &initialTriangulatedPoints)
{
	auto anchorKeyframe = vMap->keyframe(lastAnchor);

	auto K2 = KeyFrame::fromBaseFrame(*currentWorkframe, vMap);
	vMap->addKeyFrame(K2);

	assert(initialTriangulatedPoints.size()==currentWorkframe->matchesToKeyFrame.size());
	for (int i=0; i<initialTriangulatedPoints.size(); ++i) {
		auto fpPair = currentWorkframe->matchesToKeyFrame[i];
		auto vect3 = initialTriangulatedPoints[i];
		auto mp3d = MapPoint::create(vect3);
		vMap->addMapPoint(mp3d);
		vMap->addMapPointVisibility(mp3d->getId(), anchorKeyframe->getId(), fpPair.first);
		vMap->addMapPointVisibility(mp3d->getId(), K2->getId(), fpPair.second);
	}

	vMap->updateCovisibilityGraph(anchorKeyframe->getId());

	// Call bundle adjustment
	Optimizer::BundleAdjustment(*vMap, 50);

	// calculate median of point depths
	double depthMedian = anchorKeyframe->computeSceneMedianDepth();
	double invDepthMedian = 1.0f / depthMedian;
	if (depthMedian < 0 or vMap->getTrackedMapPointsAt(K2->getId(), 1)<100) {
		cout << "Wrong initialization" << endl;
		this->reset();
		return false;
	}

	// Scale initial baseline
	Pose p2=currentWorkframe->pose();
	Vector3d tr = p2.translation();
	tr *= invDepthMedian;
	K2->setPose(Pose::from_Pos_Quat(tr, p2.orientation()));

	// scale points
	for (auto &mpIdx: vMap->allMapPointsAtKeyFrame(lastAnchor)) {
		auto Pt = vMap->mappoint(mpIdx.first);
		Pt->setPosition(Pt->getPosition() * invDepthMedian);
	}

	lastAnchor = K2->getId();
	currentWorkframe->setPose(K2->pose());
	callFrameFunction();
	return true;
}


const int matchCountThreshold = 15;

void
SimpleMapBuilder::trackMapPoints(const kfid ki1, const kfid ki2)
{
	auto KF1 = vMap->keyframe(ki1),
		KF2 = vMap->keyframe(ki2);

	// Track Map Points from KF1 that are visible in KF2
	vector<Matcher::KpPair> pairList12;
	TTransform T12;
	Matcher::matchMapPoints(*KF1, *KF2, pairList12);
	if (pairList12.size()==0)
		return;

	map<kpid, mpid> kf1kp2mp = vMap->getAllMapPointProjectionsAt(ki1);
	// Check the matching with projection
	int pointMatchCounter = 0;
	for (int i=0; i<pairList12.size(); i++) {
		auto &p = pairList12[i];
		const mpid ptId = kf1kp2mp[p.first];

		// Try projection
		Vector3d pt3 = vMap->mappoint(ptId)->getPosition();
		Vector2d kpx1 = KF1->project(pt3);
		Vector2d kpf = KF2->project(pt3);
		double d = ( kpf-KF2->keypointv(p.second) ).norm();
		if (d >= 4.0)
			continue;

		// This particular mappoint is visible in KF2
		pointMatchCounter += 1;
		vMap->addMapPointVisibility(ptId, ki2, p.second);
	}

	vMap->updateCovisibilityGraph(ki1);
	cerr << "Backtrace: Found " << pointMatchCounter << "pts from " << ki1 << endl;

//	abort();
}


bool
SimpleMapBuilder::createNewKeyFrame()
{

}


void
SimpleMapBuilder::reset()
{
	vMap->reset();
	hasInitialized = false;
	lastAnchor = 0;
}


} /* namespace Vmml */


