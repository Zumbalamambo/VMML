/*
 * ImageBag.cpp
 *
 *  Created on: Oct 17, 2019
 *      Author: sujiwo
 */

#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/image_encodings.h>
#include <cv_bridge/cv_bridge.h>
#include "ImageBag.h"


using namespace std;


namespace Vmml {


ImageBag::ImageBag(const rosbag::Bag &bag, const std::string &imageTopic, float zoom) :
	RandomAccessBag(bag, imageTopic),
	zoomRatio(zoom)
{}


ImageBag::~ImageBag()
{}


cv::Mat
ImageBag::at(unsigned int position)
{
	auto bImageMsg = RandomAccessBag::at<sensor_msgs::Image>(position);
	auto imgPtr = cv_bridge::toCvCopy(bImageMsg, sensor_msgs::image_encodings::BGR8);

	cv::Mat imageRes;
	cv::resize(imgPtr->image, imageRes, cv::Size(), zoomRatio, zoomRatio, cv::INTER_CUBIC);
	return imageRes;
}


bool
ImageBag::save(unsigned int position, const string &filename)
{
	auto image = at(position);
	return cv::imwrite(filename, image);
}


} /* namespace Vmml */