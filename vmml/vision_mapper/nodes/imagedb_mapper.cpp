/*
 * imagedb_mapper.cpp
 *
 *  Created on: Nov 18, 2019
 *      Author: sujiwo
 */

#include <sstream>
#include <iostream>
#include <pcl/io/pcd_io.h>
#include "Pose.h"
#include "ImageBag.h"
#include "LidarScanBag.h"
#include "ImageDatabaseBuilder.h"


using namespace std;
using namespace Vmml;


CameraPinholeParams camera0 (
	1150.96938467,	// fx
	1150.96938467,	// fy
	988.511326762,	// cx
	692.803953253,	// cy
	1920, 1440);	// width, height
const float enlarge = 0.333333333333;

// XXX: Supply your own values!
const TTransform tLidarToCamera = TTransform::from_XYZ_RPY(
	Eigen::Vector3d(0.9, 0.3, -0.6),
	-1.520777, -0.015, -1.5488);


int main(int argc, char *argv[])
{
	// Find vocabulary, mask & velodyne calibration file
	auto vocabPath = boost::filesystem::path(ros::package::getPath("vision_core")) / "ORBvoc.txt";
	auto maskPath = boost::filesystem::path(ros::package::getPath("vision_core")) / "meidai_mask.png";
	auto calibPath = boost::filesystem::path(ros::package::getPath("vision_mapper")) / "meidai-64e-S2.yaml";
	camera0.mask = cv::imread(maskPath.string(), cv::IMREAD_GRAYSCALE);
	camera0 = camera0 * enlarge;

	ImageDatabaseBuilder::Param idbParams;
	ImageDatabaseBuilder imageDbMapper(idbParams, camera0, vocabPath.string());
	imageDbMapper.setTranformationFromLidarToCamera(tLidarToCamera);

	rosbag::Bag mybag(argv[1]);
	Vmml::ImageBag imageBag(mybag, "/camera1/image_raw", enlarge);
	Vmml::LidarScanBag lidarBag(mybag, "/velodyne_packets", calibPath.string());

	int limit;
	if (argc>2)
		limit = stoi(argv[2]);
	else limit = lidarBag.size();

	for (int li=0; li<limit; li++) {

		ptime lidarTimestamp, imageTimestamp;
		cv::Mat nearImage;
		auto lidarScan = lidarBag.getUnfiltered<ImageDatabaseBuilder::PointT>(li, &lidarTimestamp);

		try {
			uint imageN = imageBag.getPositionAtTime(ros::Time::fromBoost(lidarTimestamp));
			imageTimestamp = imageBag.timeAt(imageN).toBoost();
			nearImage = imageBag.at(imageN);
		} catch (out_of_range &e) {
			continue;
		}

		auto isKey = imageDbMapper.feed(lidarScan, lidarTimestamp, nearImage, imageTimestamp);
		cout << (isKey ? "*" : "") << li  << " / " << limit << endl;
	}

	imageDbMapper.getMap()->save("/tmp/maptest.vmap");

	auto track=imageDbMapper.getTrajectory();
	track.dump("/tmp/imagedb_simple.txt");

	auto pointCloud = imageDbMapper.getMap()->dumpPointCloudFromMapPoints();
	pcl::io::savePCDFileBinary("/tmp/pcd_vision.pcd", *pointCloud);

	return 0;
}
