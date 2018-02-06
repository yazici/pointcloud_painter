
#include <ros/ros.h>
#include "pointcloud_painter/pointcloud_painter.h"

int main(int argc, char** argv)
{
	// ---------------------------------------------------------------------------
	// ----------------------------- Basic ROS Stuff -----------------------------
	// ---------------------------------------------------------------------------

	ros::init(argc, argv, "painter_client");

	ros::NodeHandle nh;

	std::string service_name;
	nh.param<std::string>("/pointcloud_painter/service_name", service_name, "/pointcloud_painter/paint");
	ros::ServiceClient painter_srv = nh.serviceClient<pointcloud_painter::pointcloud_painter_srv>(service_name);

	// Maximum angle viewable through the lens 
	float max_lens_angle;
	nh.param<float>("/pointcloud_painter/max_lens_angle", max_lens_angle, 235);
	// Type of projection (see pointcloud_painter/pointcloud_painter_srv)
	int projection_type;
	nh.param<int>("/pointcloud_painter/projection_type", projection_type, 2);
	// Should this process loop? 
	bool should_loop;
	nh.param<bool>("/pointcloud_painter/should_loop", should_loop);
	// Voxel sizes for image postprocessing
	float flat_voxel_size, spherical_voxel_size;
	nh.param<float>("/pointcloud_painter/flat_voxel_size", flat_voxel_size, 0.005);
	nh.param<float>("/pointcloud_painter/spherical_voxel_size", spherical_voxel_size, 0.005);

	std::string camera_frame_front, camera_frame_rear, target_frame;
	nh.param<std::string>("/pointcloud_painter/camera_frame_front", camera_frame_front, "camera_front");
	nh.param<std::string>("/pointcloud_painter/camera_frame_rear", camera_frame_rear, "camera_rear");
	nh.param<std::string>("/pointcloud_painter/target_frame", target_frame, "target_frame");

	// ---------------------------------------------------------------------------
	// ------------------------ Extract Data from ROSBAGs ------------------------
	// ---------------------------------------------------------------------------

	// ------------- Bag Names and Topics -------------
	std::string cloud_bag_name = 	"/home/conor/catkin-ws/src/pointcloud_painter/data/local_dense_cloud.bag";
	std::string front_bag_name = 	"/home/conor/catkin-ws/src/pointcloud_painter/data/front_view_image_360.bag";
	std::string rear_bag_name = 	"/home/conor/catkin-ws/src/pointcloud_painter/data/rear_view_image_360.bag";
	std::string cloud_bag_topic = 	"/laser_stitcher/local_dense_cloud";
	std::string front_bag_topic = 	"front_camera/image_raw";
	std::string rear_bag_topic = 	"rear_camera/image_raw";
	ROS_INFO_STREAM("[PointcloudPainter] Loading data from bag files.");

	// ------------- First Bag - CLOUD -------------
	sensor_msgs::PointCloud2 pointcloud;
	rosbag::Bag cloud_bag; 
	cloud_bag.open(cloud_bag_name, rosbag::bagmode::Read);

	std::vector<std::string> topics;
	topics.push_back(cloud_bag_topic);
	rosbag::View view_cloud(cloud_bag, rosbag::TopicQuery(topics));

	BOOST_FOREACH(rosbag::MessageInstance const m, view_cloud)
    {
        sensor_msgs::PointCloud2::ConstPtr cloud_ptr = m.instantiate<sensor_msgs::PointCloud2>();
        if (cloud_ptr != NULL)
            pointcloud = *cloud_ptr;
        else
        	ROS_ERROR_STREAM("[PointcloudPainter] Cloud retrieved from bag is null...");
    }
    cloud_bag.close(); 

    // ------------- Second Bag - FRONT IMAGE -------------
    sensor_msgs::Image front_image;
	rosbag::Bag front_bag; 
	front_bag.open(front_bag_name, rosbag::bagmode::Read);

	std::vector<std::string> front_topics;
	front_topics.push_back(front_bag_topic);
	rosbag::View view_front(front_bag, rosbag::TopicQuery(front_topics));

	BOOST_FOREACH(rosbag::MessageInstance const m, view_front)
    {
        sensor_msgs::Image::ConstPtr front_ptr = m.instantiate<sensor_msgs::Image>();
        if (front_ptr != NULL)
            front_image = *front_ptr;
        else
        	ROS_ERROR_STREAM("[PointcloudPainter] Front image retrieved from bag is null...");
    }
    front_bag.close();

    // ------------- Third Bag - REAR IMAGE -------------
    sensor_msgs::Image rear_image;
	rosbag::Bag rear_bag; 
	rear_bag.open(rear_bag_name, rosbag::bagmode::Read);

	std::vector<std::string> rear_topics;
	rear_topics.push_back(rear_bag_topic);
	rosbag::View view_rear(rear_bag, rosbag::TopicQuery(rear_topics));

	BOOST_FOREACH(rosbag::MessageInstance const m, view_rear)
    {
    	ROS_INFO_STREAM("opening this bit..." );
        sensor_msgs::Image::ConstPtr rear_ptr = m.instantiate<sensor_msgs::Image>();
        if (rear_ptr != NULL)
            rear_image = *rear_ptr;
        else
        	ROS_ERROR_STREAM("[PointcloudPainter] Front image retrieved from bag is null...");
    }
    rear_bag.close();

    ROS_INFO_STREAM("fkjljwe " << rear_image.height << " " << rear_image.width);

	// Set up service object
	pointcloud_painter::pointcloud_painter_srv srv;
	// Data
	srv.request.input_cloud = pointcloud;
	srv.request.image_front = front_image;
	srv.request.image_rear = rear_image;
	// Projection Stuff
	srv.request.projection = projection_type;
	srv.request.max_angle = max_lens_angle;
	// Voxelization
	srv.request.flat_voxel_size = flat_voxel_size;
	srv.request.spherical_voxel_size = spherical_voxel_size;
	// Frames
	srv.request.camera_frame_front = camera_frame_front;
	srv.request.camera_frame_rear = camera_frame_rear;
	srv.request.target_frame = target_frame;

	ROS_ERROR_STREAM("first: " << front_image.header.frame_id << " " << rear_image.header.frame_id);

	// Run Service
	while(ros::ok())
	{

		// Wait a moment to ensure that the service is up...
		ros::Duration(1.0).sleep();

		// Call service
		if( ! painter_srv.call(srv) )
			ROS_WARN_STREAM("[PointcloudPainter] Painting service call failed - prob not up yet");
		else
		{	
			ROS_INFO_STREAM("[PointcloudPainter] Successfully called painting service.");
			ROS_INFO_STREAM("[PointcloudPainter]   Cloud Size: " << srv.response.output_cloud.height*srv.response.output_cloud.width);
			ros::Duration(0.5).sleep();
		}

		// If we shouldn't loop, break the loop
		if(!should_loop)
			break;
	}	
	
}	