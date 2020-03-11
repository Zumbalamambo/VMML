/*
 * test_vo.cpp
 *
 *  Created on: Nov 8, 2019
 *      Author: sujiwo
 */

#include <iostream>
#include <string>
#include <pcl/io/pcd_io.h>
#include "vmml/VisualOdometry.h"
#include "vmml/utilities.h"
#include "ProgramOptions.h"
#include "RVizConnector.h"


using namespace Vmml;
using namespace Vmml::Mapper;
using namespace std;


cv::Mat drawOpticalFlow(const BaseFrame::Ptr &anchor, const BaseFrame::Ptr &current, const Matcher::PairList &matches)
{
	if (anchor==nullptr or current==nullptr)
		return cv::Mat();

	cv::Mat myFrame = current->getImage().clone();
	for (auto &pair: matches) {
		auto &kpCurrent = current->keypoint(pair.second);
		auto &kpAnchor = anchor->keypoint(pair.first);
		cv::circle(myFrame, kpCurrent.pt, 2, cv::Scalar(0,255,0));
		cv::circle(myFrame, kpAnchor.pt, 2, cv::Scalar(0,0,255));
		cv::line(myFrame, kpCurrent.pt, kpAnchor.pt, cv::Scalar(255,0,0));
	}

	return myFrame;
}


bool hasBreak = false;
void breakHandler(int sign)
{
	if (sign==SIGINT)
		hasBreak = true;
	cout << "Break is pressed\n";
}


int main(int argc, char *argv[])
{
	ProgramOptions voProg;
	voProg.parseCommandLineArgs(argc, argv);

	VisualOdometry::Parameters voPars;
	voPars.camera = voProg.getWorkingCameraParameter();
	auto imagePipe = voProg.getImagePipeline();
	imagePipe.setRetinex();

	signal(SIGINT, breakHandler);

	VisualOdometry VoRunner(voPars);
	auto imageBag = voProg.getImageBag();

	assert(imagePipe.getOutputSize()==voPars.camera.getImageSize());

	RVizConnector rosConn(argc, argv, "test_vo");

	vector<uint64> targetFrameId;
	imageBag->desample(10.0, targetFrameId);

	for (int n=0; n<targetFrameId.size(); ++n) {

		auto currentImage = imageBag->at(targetFrameId[n]);
		ptime timestamp = imageBag->timeAt(n).toBoost();

		cv::Mat mask;
		imagePipe.run(currentImage, currentImage, mask);

		VoRunner.runMatching(currentImage, timestamp, mask);

		// Visualization
		auto anchor = VoRunner.getAnchorFrame();
		auto current = VoRunner.getCurrentFrame();
		auto matches = VoRunner.getLastMatch();

//		auto drawFrame = drawOpticalFlow(anchor, current, matches);
		rosConn.publishImage(VoRunner._flowCanvas, ros::Time::fromBoost(timestamp));

		cout << n << ": " << VoRunner.getInlier() << endl;

		if (hasBreak==true)
			break;
	}

	cout << "Done\n";

	const auto voTrack = VoRunner.getTrajectory();
	const auto cloudBuild = VoRunner.getPoints();
	voTrack.dump("/tmp/x.csv");
	pcl::io::savePCDFileBinary("/tmp/mapVoTest.pcd", *cloudBuild);

	return 0;
}
