/*
 * RVizConnector.h
 *
 *  Created on: Oct 21, 2019
 *      Author: sujiwo
 */

#ifndef VMML_MAPPER_RVIZCONNECTOR_H_
#define VMML_MAPPER_RVIZCONNECTOR_H_

#include <string>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/PointCloud2.h>
#include <geometry_msgs/Pose.h>
#include <image_transport/image_transport.h>
#include "BaseFrame.h"
#include "VisionMap.h"


namespace Vmml {
namespace Mapper {

class RVizConnector {
public:
	RVizConnector(int argc, char *argv[], const std::string &nodeName);
	virtual ~RVizConnector();

	void setMap(const VisionMap::Ptr &vmap);

	void publishFrame(const BaseFrame &fr);

	void publishPointcloud();

protected:
	std::shared_ptr<ros::NodeHandle> hdl;
	VisionMap::Ptr mMap;

	std::shared_ptr<image_transport::ImageTransport> imagePubTr;
	image_transport::Publisher imagePub;
};

} /* namespace Mapper */
} /* namespace Vmml */

#endif /* VMML_MAPPER_RVIZCONNECTOR_H_ */