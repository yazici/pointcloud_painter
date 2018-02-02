

#include "pointcloud_painter/pointcloud_painter.h"

PointcloudPainter::PointcloudPainter()
{
	std::string service_name;
	nh_.param<std::string>("/pointcloud_painter/service_name", service_name, "/pointcloud_painter/paint");
	ROS_INFO_STREAM("[PointcloudPainter] Initializing service with name " << service_name << ".");

	ros::ServiceServer painter = nh_.advertiseService(service_name, &PointcloudPainter::paint_pointcloud, this);

	ros::spin();
}

/* Paint_Pointcloud - colors a pointcloud using RGB data from a spherical image
 	Inputs
 	 - Input Cloud (generated by laser scan)
 	 - Input Image (spherical, full 360 view)
 	 - Image Frame - assumes INTO image is Z, Horizontal is X, Vertical is Y

*/
bool PointcloudPainter::paint_pointcloud(pointcloud_painter::pointcloud_painter_srv::Request &req, pointcloud_painter::pointcloud_painter_srv::Response &res)
{
	ROS_INFO_STREAM("[PointcloudPainter] Received call to paint pointcloud!");
	ROS_INFO_STREAM("[PointcloudPainter]   Input cloud size: " << req.input_cloud.height*req.input_cloud.width);
	ROS_INFO_STREAM("[PointcloudPainter]   Front image size: " << req.image_front.height << " by " << req.image_front.width);
	ROS_INFO_STREAM("[PointcloudPainter]   Rear image size: " << req.image_rear.height << " by " << req.image_rear.width);

	// Image Angular Resolution
	float hor_res; 	// horizontal
	float ver_res;  // vertical

	//tf2::Transform transform_to_image;

	std::string image_frame = req.image_front.header.frame_id;

	// ------ Transform input_cloud to camera_frame ------
	std::string cloud_frame = req.input_cloud.header.frame_id;
	tf::TransformListener listener;
	sensor_msgs::PointCloud2 transformed_depth_cloud;
	if(listener.waitForTransform(cloud_frame, image_frame, ros::Time::now(), ros::Duration(0.5)))  
	{
		pcl_ros::transformPointCloud (image_frame, req.input_cloud, transformed_depth_cloud, listener);  	// transforms input_pc2 into process_message
	}
	else 
	{  													// if Transform request times out... Continues WITHOUT TRANSFORM
		ROS_WARN_THROTTLE(60, "[PointcloudPainter] listen for transformation from %s to %s timed out. Returning paint_pointcloud service unsuccessfully...", cloud_frame.c_str(), image_frame.c_str());
		// un-comment this later...
		//return false;
	}

	// ------ Create PCL Pointclouds ------
	pcl::PointCloud<pcl::PointXYZ> input_depth_pcl = pcl::PointCloud<pcl::PointXYZ>();
	pcl::PointCloud<pcl::PointXYZRGB> output_pcl = pcl::PointCloud<pcl::PointXYZRGB>();
	pcl::fromROSMsg(transformed_depth_cloud, input_depth_pcl); 	// Initialize input cloud 

	// ------ Create PCL Pointclouds for Second Method ------
	//   These only matter for the K Nearest Neighbors approach (not for interpolation)
	// Input Cloud - projected onto a sphere of fixed radius
	pcl::PointCloud<pcl::PointXYZRGB> input_pcl_projected = pcl::PointCloud<pcl::PointXYZRGB>(); 
	// Actually perform projection: 
	for(int i=0; i<input_depth_pcl.points.size(); i++)
	{
		float distance = sqrt( pow(input_depth_pcl.points[i].x,2) + pow(input_depth_pcl.points[i].y,2) + pow(input_depth_pcl.points[i].z,2) );
		input_pcl_projected.points[i].x = input_depth_pcl.points[i].x / distance;
		input_pcl_projected.points[i].y = input_depth_pcl.points[i].y / distance;
		input_pcl_projected.points[i].z = input_depth_pcl.points[i].z / distance;
	}

	// Build cloud from Input Image - XYZRGB cloud fixed on a flat raster plane
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr flat_image_pcl = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>); 
	// Build cloud from Input Image - XYZRGB cloud projected back onto life-like sphere of radius given by PROJECTION_RADIUS above 
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr spherical_image_pcl = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>);

	cv_bridge::CvImagePtr cv_ptr_front; 
	try
	{
		cv_ptr_front = cv_bridge::toCvCopy(req.image_front, sensor_msgs::image_encodings::BGR8);
	}
	catch(cv_bridge::Exception& e)
	{
		ROS_ERROR_STREAM("[PointcloudPainter] cv_bridge exception: " << e.what());
		return false; 
	}
	cv_bridge::CvImagePtr cv_ptr_rear; 
	try
	{
		cv_ptr_rear = cv_bridge::toCvCopy(req.image_rear, sensor_msgs::image_encodings::BGR8);
	}
	catch(cv_bridge::Exception& e)
	{
		ROS_ERROR_STREAM("[PointcloudPainter] cv_bridge exception: " << e.what());
		return false; 
	}

	build_image_clouds(flat_image_pcl, spherical_image_pcl, cv_ptr_front, req.projection, req.max_angle, req.image_front.height, req.image_rear.width, false);
	build_image_clouds(flat_image_pcl, spherical_image_pcl, cv_ptr_rear, req.projection, req.max_angle, req.image_front.height, req.image_rear.height, true);

	ROS_ERROR_STREAM("size: " << spherical_image_pcl->points.size() << " example: " << spherical_image_pcl->points[0].x << " " << spherical_image_pcl->points[0].y << " " << spherical_image_pcl->points[0].z);
	pcl::VoxelGrid<pcl::PointXYZRGB> vg;
	vg.setInputCloud(flat_image_pcl);
	vg.setLeafSize(req.flat_voxel_size, req.flat_voxel_size, req.flat_voxel_size);
	// Apply Filter and return Voxelized Data
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr temp_pcp = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(new pcl::PointCloud<pcl::PointXYZRGB>());
	vg.filter(*temp_pcp);
	*flat_image_pcl = *temp_pcp;

	vg.setInputCloud(spherical_image_pcl);
	vg.setLeafSize(req.spherical_voxel_size, req.spherical_voxel_size, req.spherical_voxel_size);
	// Apply Filter and return Voxelized Data
	temp_pcp->points.clear();
	vg.filter(*temp_pcp);
	*spherical_image_pcl = *temp_pcp;
	
	ROS_ERROR_STREAM("size: " << spherical_image_pcl->points.size() << " example: " << spherical_image_pcl->points[0].x << " " << spherical_image_pcl->points[0].y << " " << spherical_image_pcl->points[0].z);

	sensor_msgs::PointCloud2 image_flat_out;
	pcl::toROSMsg(*flat_image_pcl, image_flat_out);
	image_flat_out.header.frame_id = "map";
	ros::Publisher pub_flat = nh_.advertise<sensor_msgs::PointCloud2>("image_out_flat", 1, this);
	pub_flat.publish(image_flat_out);

	sensor_msgs::PointCloud2 image_sphere_out;
	pcl::toROSMsg(*spherical_image_pcl, image_sphere_out);
	image_sphere_out.header.frame_id = "map";
	ros::Publisher pub_sphere = nh_.advertise<sensor_msgs::PointCloud2>("image_out_sphere", 1, this);
	pub_sphere.publish(image_sphere_out);


	pcl::toROSMsg(output_pcl, res.output_cloud);

	//transform output_cloud to cloud_frame
	if(listener.waitForTransform(image_frame, cloud_frame, ros::Time::now(), ros::Duration(0.5)))  
	{
		sensor_msgs::PointCloud2 transformed_cloud;
		pcl_ros::transformPointCloud (cloud_frame, res.output_cloud, transformed_cloud, listener);  	// transforms input_pc2 into process_message
	}
	else {  													// if Transform request times out... Continues WITHOUT TRANSFORM
		ROS_WARN_THROTTLE(60, "[PointcloudPainter] listen for transformation from %s to %s timed out. Returning paint_pointcloud service unsuccessfully...", image_frame.c_str(), cloud_frame.c_str());
		return false;
	}

	return true;
}

bool PointcloudPainter::build_image_clouds(pcl::PointCloud<pcl::PointXYZRGB>::Ptr &pcl_flat, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &pcl_spherical, cv_bridge::CvImagePtr cv_image, int projection, float max_angle, int image_hgt, int image_wdt, bool flip_cloud)
{
	// Determine real image dimensions (to project properly to a R=1m sphere)
	float plane_width, X_max_dist, Z_max_dist;
	switch(projection)
	{
		case 1:
			// X position on the sphere where the highest angle allowed by the lens penetrates it from the origin:
			X_max_dist = cos( (max_angle-180)/2 *3.14159/180 ); 	
			Z_max_dist = sin( (max_angle-180)/2 *3.14159/180 );
			// maximum width of planar projection such that it will wrap properly to a R=1m sphere (since lenses do not have a 360 FOV, and blank areas are omitted from file)
			plane_width = X_max_dist/(1 - Z_max_dist);
		case 2:
			// X position on the sphere where the highest angle allowed by the lens penetrates it from the origin:
			X_max_dist = cos( (max_angle-180)/2 *3.14159/180 );
			Z_max_dist = sin( (max_angle-180)/2 *3.14159/180 );
			// maximum width of planar projection such that it will wrap properly to a R=1m sphere (since lenses do not have a 360 FOV, and blank areas are omitted from file)
			plane_width = X_max_dist/(1 - Z_max_dist) *2;
		case 3:
			// X and Z position on the sphere where the highest angle allowed by the lens penetrates it from the origin:
			X_max_dist = cos( (-90 + (max_angle-180)) *3.14159/180);
			Z_max_dist = 1 - sin( (-90 + (max_angle-180)) *3.14159/180);
			// maximum width of planar projection such that it will wrap properly to a R=1m sphere (since lenses do not have a 360 FOV, and blank areas are omitted from file)
			plane_width = sqrt(2/(1-Z_max_dist)) * X_max_dist;
	}
	// ------------------ Process Front Cloud ------------------
	for(int i=0; i<image_hgt; i++)
	{
		for(int j=0; j<image_wdt; j++)
		{
			// ------------------ Create point for flat RGB image cloud ------------------
			// ----- Create point and set XYZ -----
			// Results in an image that is 1x1m, centered at origin, normal in Z
			pcl::PointXYZRGB point_flat;
			point_flat.x = float(i-image_hgt/2) / image_hgt;
			point_flat.y = float(j-image_wdt/2) / image_wdt;
			point_flat.z = 0;
			// ----- Set RGB -----
			// cv_bridge::CVImagePtr->image returns a cv::Mat, which allows pixelwise access
			//   https://answers.ros.org/question/187649/pointer-image-multi-channel-access/
			//   NOTE - data is saved in BGR format (not RGB)
			point_flat.b = cv_image->image.at<cv::Vec3b>(i,j)[0];
			point_flat.g = cv_image->image.at<cv::Vec3b>(i,j)[1];
			point_flat.r = cv_image->image.at<cv::Vec3b>(i,j)[2];

			// ------------------ Create point for spherical RGB image cloud ------------------
			pcl::PointXYZRGB point_sphere;
			// ----- Set RGB -----
			point_sphere.r = point_flat.r;
			point_sphere.g = point_flat.g;
			point_sphere.b = point_flat.b;
			// ----- Set XYZ -----
			switch(projection)
			{
				float xs, ys;
				case 1:
					// Account for FOV lens angle being less than 360 degrees
					xs = (float(i)/image_hgt - 0.5) * plane_width * 2;
					ys = (float(j)/image_wdt - 0.5) * plane_width * 2;
					// Perform projection
					point_sphere.x = ( 2*xs/(1 + pow(xs,2) + pow(ys,2)) );
					point_sphere.y = ( 2*ys/(1 + pow(xs,2) + pow(ys,2)) );
					point_sphere.z = ( (-1 + pow(xs,2) + pow(ys,2))/(1 + pow(xs,2) + pow(ys,2)) );
				case 2:
					// Account for FOV lens angle being less than 360 degrees
					xs = (float(i)/image_hgt - 0.5) * plane_width * 4;
					ys = (float(j)/image_wdt - 0.5) * plane_width * 4;
					// Perform projection
					point_sphere.x = ( 2*xs/(1 + pow(xs,2) + pow(ys,2)) );
					point_sphere.y = ( 2*ys/(1 + pow(xs,2) + pow(ys,2)) );
					point_sphere.z = ( (-1 + pow(xs,2) + pow(ys,2))/(1 + pow(xs,2) + pow(ys,2)) );
				case 3:
					// Account for FOV lens angle being less than 360 degrees
					xs = (float(i)/image_hgt - 0.5) * plane_width * 2;
					ys = (float(j)/image_wdt - 0.5) * plane_width * 2;
					// Perform projection
					point_sphere.x = sqrt( 1 - (pow(xs,2) + pow(ys,2))/4 ) * xs;
					point_sphere.y = sqrt( 1 - (pow(xs,2) + pow(ys,2))/4 ) * ys;
					point_sphere.z = ( -1 + (pow(xs,2) + pow(ys,2))/2 );
			}

			if(flip_cloud)
			{
				point_flat.x += image_hgt;
				point_sphere.z *= -1;
				point_sphere.y *= -1;
			}

			// ------------------ Add to cloud ------------------
			pcl_flat->points.push_back(point_flat);
			pcl_spherical->points.push_back(point_sphere);
		}
	}
}

// ------------------ FIRST METHOD ------------------
// Assumes clean indexing for square interpolation - might not always be possible (but straightforward for any raster RGB image format)
// Have to still write the bit to get the bounding four pixels for each LIDAR ray position within the raster grid  
bool interpolate_colors(pcl::PointCloud<pcl::PointXYZRGB>::Ptr &output_cloud, pcl::PointCloud<pcl::PointXYZ> &depth_cloud, pcl::PointCloud<pcl::PointXYZRGB> &rgb_cloud, float ver_res, float hor_res)
{
	output_cloud->points.clear();

	// ------ Populate Output Cloud ------
	for(int i=0; i<depth_cloud.points.size(); i++)
	{
		// ------ Populate XYZ Values ------
		pcl::PointXYZRGB point;  // Output point 
		point.x = depth_cloud.points[i].x;
		point.y = depth_cloud.points[i].y;
		point.z = depth_cloud.points[i].z;

		// ------------------ FIRST METHOD ------------------
		// Assumes clean indexing for square interpolation - probably not as reasonable to implement
		// Expect it to be tricky to find the four relevant pixels in RGB image for each target position 
		// ------ Find Pixels ------
		// Point Angular Location
		float ver_pos = atan2(point.y, point.z); 	// Vertical Angle to Target Point from Z Axis (rotation about X Axis)
		float hor_pos = atan2(point.x, point.z); 	// Horizontal Angle to Target Point from Z Axis (rotation about -Y Axis)
		// Found Pixels - Colors
		float r_ll, g_ll, b_ll; 	// RGB (left, lower)
		float r_rl, g_rl, b_rl; 	// RGB (right, lower)
		float r_lu, g_lu, b_lu; 	// RGB (left, upper)
		float r_ru, g_ru, b_ru; 	// RGB (right, upper)

		// ------ Find Color ------
		// Interpolation
		//   Left Virtual Pixel   	(Vertical Interpolation)
		float r_l = r_ll + (r_lu - r_ll)*ver_res/ver_pos;
		float g_l = g_ll + (g_lu - g_ll)*ver_res/ver_pos;
		float b_l = b_ll + (b_lu - b_ll)*ver_res/ver_pos;
		//   Right Virtual Pixel   	(Vertical Interpolation)
		float r_r = r_rl + (r_ru - r_ll)*ver_res/ver_pos;
		float g_r = g_rl + (g_ru - g_ll)*ver_res/ver_pos;
		float b_r = b_rl + (b_ru - b_ll)*ver_res/ver_pos;
		//   Final Pixel   			(Horizontal Interpolation)
		point.r = r_l + (r_r - r_l)*hor_res/hor_pos;
		point.g = g_l + (g_r - g_l)*hor_res/hor_pos;
		point.b = b_l + (b_r - b_l)*hor_res/hor_pos;
		
		output_cloud->points.push_back(point);
	}
}

// ------------------ SECOND METHOD ------------------
// K Nearest Neighbor search for color determination 
// might be easiest to operate on image if I convert it to openCV format
// BUT probably a lot more useful if I can avoid that because openCV is a pain to compile etc...
bool neighbor_color_search(pcl::PointCloud<pcl::PointXYZRGB>::Ptr &output_cloud, pcl::PointCloud<pcl::PointXYZ>::Ptr &depth_cloud, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &rgb_cloud, float ver_res, float hor_res, int k)
{
	pcl::KdTreeFLANN<pcl::PointXYZRGB> kdtree;
	kdtree.setInputCloud(rgb_cloud);

	for(int i=0; i<depth_cloud->points.size(); i++)
	{
		// Create a new point, assign the XYZ positions from depth_cloud
		pcl::PointXYZRGB point;
		point.x = depth_cloud->points[i].x;
		point.y = depth_cloud->points[i].y;
		point.z = depth_cloud->points[i].z;

		std::vector<int> nearest_indices(k); 			// Indices (within RGB cloud) of neighbors to target point
		std::vector<float> nearest_dist_squareds(k);	// Distances (within RGB cloud) of neighbors to target point

		if ( kdtree.nearestKSearch (point, k, nearest_indices, nearest_dist_squareds) > 0 )
		{
			// Currently, just assign colors as inverse-distance weighted average of neighbor colors
			float dist_total = 0;
			// Iterate over each neighbor
			for(int j=0; j<nearest_indices.size(); j++)
			{
				// For each neighbor, add its weighted color to the total for the target point
				float dist = pow(nearest_dist_squareds[j],0.5);
				point.r += rgb_cloud->points[nearest_indices[j]].r / dist;
				point.g += rgb_cloud->points[nearest_indices[j]].g / dist;
				point.b += rgb_cloud->points[nearest_indices[j]].b / dist;
				// Increment the total distance by the distance to this neighbor
				dist_total += dist;
			}
			// Correct for distance weights!
			point.r *= dist_total;
			point.g *= dist_total;
			point.b *= dist_total;
			// Add new point to output cloud
			output_cloud->points.push_back(point);
		}
		else 
			ROS_ERROR_STREAM_THROTTLE(0.1, "[PointcloudPainter] KdTree Nearest Neighbor search failed! Unable to find neighbors for point " << i << "with XYZ values " << depth_cloud->points[i].x << " " << depth_cloud->points[i].y << " " << depth_cloud->points[i].z << ". This message is throttled...");
	}
			
}


int main(int argc, char** argv)
{
	ros::init(argc, argv, "pointcloud_processing_server");

	pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS);

	PointcloudPainter painter;

}