#include <stdio.h>
#include <string>
#include <opencv2/opencv.hpp>
#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/distortion_models.h>
#include <image_transport/image_transport.h>
#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <camera_info_manager/camera_info_manager.h>

#define WIDTH_ID 3
#define HEIGHT_ID 4
#define FPS_ID 5

namespace arti {


class StereoCamera
{

public:

	/**
	 * @brief      { stereo camera driver }
	 *
	 * @param[in]  resolution  The resolution
	 * @param[in]  frame_rate  The frame rate
	 */
	StereoCamera(int resolution, double frame_rate): frame_rate_(30.0) {

		camera_ = new cv::VideoCapture(0);

		cv::Mat raw;
		cv::Mat left_image;
		cv::Mat right_image;

		setResolution(resolution);
		// // this function doesn't work very well in current Opencv 2.4, so, just use ROS to control frame rate.
		// setFrameRate(frame_rate);

		std::cout << "Stereo Camera Set Resolution: " << camera_->get(WIDTH_ID) << "x" << camera_->get(HEIGHT_ID) << std::endl;
		// std::cout << "Stereo Camera Set Frame Rate: " << camera_->get(FPS_ID) << std::endl;
	}

	~StereoCamera() {
		// std::cout << "Destroy the pointer" << std::endl;
		delete camera_;
	}

	/**
	 * @brief      Sets the resolution.
	 *
	 * @param[in]  type  The type
	 */
	void setResolution(int type) {


		if (type == 0) { width_ = 4416; height_ = 1242;} // 2K
		if (type == 1) { width_ = 3840; height_ = 1080;} // FHD
		if (type == 2) { width_ = 2560; height_ = 720;}  // HD
		if (type == 3) { width_ = 1344; height_ = 376;}  // VGA


		camera_->set(WIDTH_ID, width_);
		camera_->set(HEIGHT_ID, height_);

		// make sure that the number set are right from the hardware
		width_ = camera_->get(WIDTH_ID);
		height_ = camera_->get(HEIGHT_ID);


	}

	/**
	 * @brief      Sets the frame rate.
	 *
	 * @param[in]  frame_rate  The frame rate
	 */
	void setFrameRate(double frame_rate) {
		camera_->set(FPS_ID, frame_rate);
		frame_rate_ = camera_->get(FPS_ID);
	}

	/**
	 * @brief      Gets the images.
	 *
	 * @param      left_image   The left image
	 * @param      right_image  The right image
	 *
	 * @return     The images.
	 */
	bool getImages(cv::Mat& left_image, cv::Mat& right_image) {
		cv::Mat raw;
		if (camera_->grab()) {
			camera_->retrieve(raw);
			cv::Rect left_rect(0, 0, width_ / 2, height_);
			cv::Rect right_rect(width_ / 2, 0, width_ / 2, height_);
			left_image = raw(left_rect);
			right_image = raw(right_rect);
			cv::waitKey(10);
			return true;
		} else {
			return false;
		}
	}
	int width_;
	int height_;

private:

	cv::VideoCapture* camera_;

	double frame_rate_;
	bool cv_three_;
};

/**
 * @brief       the camera ros warpper class
 */
class ZedCameraROS {
public:

	/**
	 * @brief      { function_description }
	 *
	 * @param[in]  resolution  The resolution
	 * @param[in]  frame_rate  The frame rate
	 */


	ZedCameraROS(){
		ros::NodeHandle nh;
		ros::NodeHandle private_nh("~");
		// get ros param
		private_nh.param("resolution", resolution_, 1);
		private_nh.param("frame_rate", frame_rate_, 30.0);
		private_nh.param("config_file_location", config_file_location_, std::string("~/SN1267.conf"));
		//private_nh.param("undistort_config_file_location", undistort_config_file_location_, std::string("~/SN1267.conf"));
		private_nh.param("left_frame_id", left_frame_id_, std::string("left_camera"));
		private_nh.param("right_frame_id", right_frame_id_, std::string("right_camera"));
		private_nh.param("show_image", show_image_, false);
		private_nh.param("rectify_image", rectify_image_, true);
		private_nh.param("load_zed_config", load_zed_config_, true);

		ROS_INFO("Try to initialize the camera");
		StereoCamera zed(resolution_, frame_rate_);
		ROS_INFO("Initialized the camera");


		WIDTH=zed.width_;
		HEIGHT=zed.height_;


		// setup publisher stuff
		image_transport::ImageTransport it(nh);
		image_transport::Publisher left_image_pub = it.advertise("left/image_raw", 1);
		image_transport::Publisher right_image_pub = it.advertise("right/image_raw", 1);

		image_transport::Publisher rec_left_image_pub = it.advertise("left/image_rectified", 1);
		image_transport::Publisher rec_right_image_pub = it.advertise("right/image_rectified", 1);

		image_transport::Publisher rec_whole_image_pub = it.advertise("wholeRecImage", 1);
		image_transport::Publisher raw_whole_image_pub = it.advertise("wholeRawImage", 1);

		ros::Publisher left_cam_info_pub = nh.advertise<sensor_msgs::CameraInfo>("left/camera_info", 1);
		ros::Publisher right_cam_info_pub = nh.advertise<sensor_msgs::CameraInfo>("right/camera_info", 1);

		sensor_msgs::CameraInfo left_info, right_info;


		ROS_INFO("Try load camera calibration files");
		if (load_zed_config_) {
			ROS_INFO("Loading from zed calibration files");
			// get camera info from zed
			try {
				getZedCameraInfo(config_file_location_, resolution_, left_info, right_info);
				getUndistortedMaps();
			}
			catch (std::runtime_error& e) {
				ROS_INFO("Can't load camera info");
				ROS_ERROR("%s", e.what());
				throw e;
			}
		} else {
			ROS_INFO("Loading from ROS calibration files");
			// get config from the left, right.yaml in config
			camera_info_manager::CameraInfoManager info_manager(nh);
			info_manager.setCameraName("zed/left");
			info_manager.loadCameraInfo( "package://zed_cpu_ros/config/left.yaml");
			left_info = info_manager.getCameraInfo();

			info_manager.setCameraName("zed/right");
			info_manager.loadCameraInfo( "package://zed_cpu_ros/config/right.yaml");
			right_info = info_manager.getCameraInfo();

			left_info.header.frame_id = left_frame_id_;
			right_info.header.frame_id = right_frame_id_;
		}

		// std::cout << left_info << std::endl;
		// std::cout << right_info << std::endl;

		ROS_INFO("Got camera calibration files");
		// loop to publish images;
		cv::Mat left_image, right_image, wholeRawImage;
		ros::Rate r(frame_rate_);

		while (nh.ok()) {
			ros::Time now = ros::Time::now();
			if (!zed.getImages(left_image, right_image)) {
				ROS_INFO_ONCE("Can't find camera");
			} else {
				ROS_INFO_ONCE("Success, found camera");
			}
			if (show_image_) {
				cv::imshow("left", left_image);
				cv::imshow("right", right_image);
			}

			hconcat(left_image,right_image,wholeRawImage);

			if(rectify_image_){
				cv::Mat imLeftRec, imRightRec, wholeRecImage;
        		cv::remap(left_image,imLeftRec,M1l,M2l,cv::INTER_LINEAR);
        		cv::remap(right_image,imRightRec,M1r,M2r,cv::INTER_LINEAR);
        		hconcat(imLeftRec,imRightRec,wholeRecImage);
        		//cv::imshow("left_rectified", imLeftRec);
				if (rec_left_image_pub.getNumSubscribers() > 0) {
					publishImage(imLeftRec, rec_left_image_pub, "rec_left_frame", now);
				}
				if (rec_right_image_pub.getNumSubscribers() > 0) {
					publishImage(imRightRec, rec_right_image_pub, "rec_right_frame", now);
				}
				if (rec_whole_image_pub.getNumSubscribers() > 0) {
					publishImage(wholeRecImage, rec_whole_image_pub, "rec_whole_frame", now);
				}
			}

			if (raw_whole_image_pub.getNumSubscribers() > 0) {
				publishImage(wholeRawImage, raw_whole_image_pub, "raw_whole_frame", now);
			}

			if (left_image_pub.getNumSubscribers() > 0) {
				publishImage(left_image, left_image_pub, "left_frame", now);
			}
			if (right_image_pub.getNumSubscribers() > 0) {
				publishImage(right_image, right_image_pub, "right_frame", now);
			}
			if (left_cam_info_pub.getNumSubscribers() > 0) {
				publishCamInfo(left_cam_info_pub, left_info, now);
			}
			if (right_cam_info_pub.getNumSubscribers() > 0) {
				publishCamInfo(right_cam_info_pub, right_info, now);
			}
			r.sleep();
			// since the frame rate was set inside the camera, no need to do a ros sleep
		}
	}

	/**
	 * @brief      Gets the camera information From Zed config.
	 *
	 * @param[in]  config_file         The configuration file
	 * @param[in]  resolution          The resolution
	 * @param[in]  left_cam_info_msg   The left camera information message
	 * @param[in]  right_cam_info_msg  The right camera information message
	 */
	void getZedCameraInfo(std::string config_file, int resolution, sensor_msgs::CameraInfo& left_info, sensor_msgs::CameraInfo& right_info) {
		boost::property_tree::ptree pt;
		boost::property_tree::ini_parser::read_ini(config_file, pt);
		std::string left_str = "LEFT_CAM_";
		std::string right_str = "RIGHT_CAM_";
		std::string reso_str = "";

		switch (resolution) {
			case 0: reso_str = "2K"; break;
			case 1: reso_str = "FHD"; break;
			case 2: reso_str = "HD"; break;
			case 3: reso_str = "VGA"; break;
		}
		// left value
		double l_cx = pt.get<double>(left_str + reso_str + ".cx");
		double l_cy = pt.get<double>(left_str + reso_str + ".cy");
		double l_fx = pt.get<double>(left_str + reso_str + ".fx");
		double l_fy = pt.get<double>(left_str + reso_str + ".fy");
		double l_k1 = pt.get<double>(left_str + reso_str + ".k1");
		double l_k2 = pt.get<double>(left_str + reso_str + ".k2");
		// right value
		double r_cx = pt.get<double>(right_str + reso_str + ".cx");
		double r_cy = pt.get<double>(right_str + reso_str + ".cy");
		double r_fx = pt.get<double>(right_str + reso_str + ".fx");
		double r_fy = pt.get<double>(right_str + reso_str + ".fy");
		double r_k1 = pt.get<double>(right_str + reso_str + ".k1");
		double r_k2 = pt.get<double>(right_str + reso_str + ".k2");

		// get baseline and convert mm to m
		boost::optional<double> baselineCheck;
		double baseline = 0.0;
		// some config files have "Baseline" instead of "BaseLine", check accordingly...
		if (baselineCheck = pt.get_optional<double>("STEREO.BaseLine")) {
			baseline = pt.get<double>("STEREO.BaseLine") * 0.001;
		}
		else if (baselineCheck = pt.get_optional<double>("STEREO.Baseline")) {
			baseline = pt.get<double>("STEREO.Baseline") * 0.001;
		}
		else
			throw std::runtime_error("baseline parameter not found");

		// get Rx and Rz
		double rx = pt.get<double>("STEREO.RX_"+reso_str);
		double rz = pt.get<double>("STEREO.RZ_"+reso_str);
		double ry = pt.get<double>("STEREO.CV_"+reso_str);

		// assume zeros, maybe not right
		double p1 = 0, p2 = 0, k3 = 0;

		left_info.height=HEIGHT;
		left_info.width=WIDTH;

		right_info.height=HEIGHT;
		right_info.width=WIDTH;

		left_info.distortion_model = sensor_msgs::distortion_models::PLUMB_BOB;
		right_info.distortion_model = sensor_msgs::distortion_models::PLUMB_BOB;

		// distortion parameters
		// For "plumb_bob", the 5 parameters are: (k1, k2, t1, t2, k3).
		left_info.D.resize(5);
		left_info.D[0] = l_k1;
		left_info.D[1] = l_k2;
		left_info.D[2] = k3;
		left_info.D[3] = p1;
		left_info.D[4] = p2;

		D_l=(cv::Mat1d(1,5) << l_k1, l_k2, k3, p1, p2);
		D_r=(cv::Mat1d(1,5) << r_k1, r_k2, k3, p1, p2);

		right_info.D.resize(5);
		right_info.D[0] = r_k1;
		right_info.D[1] = r_k2;
		right_info.D[2] = k3;
		right_info.D[3] = p1;
		right_info.D[4] = p2;

		// Intrinsic camera matrix
		// 	[fx  0 cx]
		// K =  [ 0 fy cy]
		//	[ 0  0  1]
		left_info.K.fill(0.0);
		left_info.K[0] = l_fx;
		left_info.K[2] = l_cx;
		left_info.K[4] = l_fy;
		left_info.K[5] = l_cy;
		left_info.K[8] = 1.0;

		right_info.K.fill(0.0);
		right_info.K[0] = r_fx;
		right_info.K[2] = r_cx;
		right_info.K[4] = r_fy;
		right_info.K[5] = r_cy;
		right_info.K[8] = 1.0;

		K_l = (cv::Mat1d(3, 3) << l_fx, 0, l_cx, 0, l_fy, l_cy, 0, 0, 1);
		K_r = (cv::Mat1d(3, 3) << r_fx, 0, r_cx, 0, r_fy, r_cy, 0, 0, 1);

		//std::cout<<"left focal length: "<<K_l.at<double>(0,0)<<'\n';


		// rectification matrix
		// Rl = R_rect, R_r = R * R_rect
		// since R is identity, Rl = Rr;
		left_info.R.fill(0.0);
		right_info.R.fill(0.0);
		cv::Mat rvec = (cv::Mat_<double>(3, 1) << rx, ry, rz);
		cv::Mat rmat(3, 3, CV_64F);
		cv::Rodrigues(rvec, rmat);
		int id = 0;
		cv::MatIterator_<double> it, end;
		for (it = rmat.begin<double>(); it != rmat.end<double>(); ++it, id++){
			left_info.R[id] = *it;
			right_info.R[id] = *it;

		}
		R_r=rmat;
		R_l=rmat;
		//std::cout<<"left focal length: "<<R_l.at<double>(0,0)<<'\n';



		// Projection/camera matrix
		//     [fx'  0  cx' Tx]
		// P = [ 0  fy' cy' Ty]
		//     [ 0   0   1   0]
		left_info.P.fill(0.0);
		left_info.P[0] = l_fx;
		left_info.P[2] = l_cx;
		left_info.P[5] = l_fy;
		left_info.P[6] = l_cy;
		left_info.P[10] = 1.0;

		right_info.P.fill(0.0);
		right_info.P[0] = r_fx;
		right_info.P[2] = r_cx;
		right_info.P[3] = (-1 * l_fx * baseline);
		right_info.P[5] = r_fy;
		right_info.P[6] = r_cy;
		right_info.P[10] = 1.0;

		P_l=(cv::Mat1d(3, 4) << l_fx, 0, l_cx, 0, 0, l_fy, l_cy, 0, 0, 0, 1, 0);
		P_r=(cv::Mat1d(3, 4) << r_fx, 0, r_cx, -1 * l_fx * baseline, 0, r_fy, r_cy, 0, 0, 0, 1, 0);

		left_info.width = right_info.width = width_;
		left_info.height = right_info.height = height_;

		left_info.header.frame_id = left_frame_id_;
		right_info.header.frame_id = right_frame_id_;



	}


	void getUndistortedMaps(){

       
        if(K_l.empty() || K_r.empty() || P_l.empty() || P_r.empty() || R_l.empty() || R_r.empty() || D_l.empty() || D_r.empty() ||
                WIDTH==0 || HEIGHT==0)
        {
            std::cerr << "ERROR: Calibration parameters to rectify stereo are missing!" << "/n";
            return ;
        }

        cv::initUndistortRectifyMap(K_l,D_l,R_l,P_l.rowRange(0,3).colRange(0,3),cv::Size(WIDTH/2,HEIGHT),CV_32F,M1l,M2l);
        cv::initUndistortRectifyMap(K_r,D_r,R_r,P_r.rowRange(0,3).colRange(0,3),cv::Size(WIDTH/2,HEIGHT),CV_32F,M1r,M2r);

	}


	/**
	 * @brief      { publish camera info }
	 *
	 * @param[in]  pub_cam_info  The pub camera information
	 * @param[in]  cam_info_msg  The camera information message
	 * @param[in]  now           The now
	 */
	void publishCamInfo(const ros::Publisher& pub_cam_info, sensor_msgs::CameraInfo& cam_info_msg, ros::Time now) {
		cam_info_msg.header.stamp = now;
		pub_cam_info.publish(cam_info_msg);
	}

	/**
	 * @brief      { publish image }
	 *
	 * @param[in]  img           The image
	 * @param      img_pub       The image pub
	 * @param[in]  img_frame_id  The image frame identifier
	 * @param[in]  t             { parameter_description }
	 */
	void publishImage(cv::Mat img, image_transport::Publisher &img_pub, std::string img_frame_id, ros::Time t) {
		cv_bridge::CvImage cv_image;
		cv_image.image = img;
		cv_image.encoding = sensor_msgs::image_encodings::BGR8;
		cv_image.header.frame_id = img_frame_id;
		cv_image.header.stamp = t;
		img_pub.publish(cv_image.toImageMsg());
	}

private:
	int WIDTH, HEIGHT;
    cv::Mat K_l, K_r, P_l, P_r, R_l, R_r, D_l, D_r;
	int resolution_;
	double frame_rate_;
	bool show_image_, load_zed_config_, rectify_image_;
	double width_, height_;
	std::string left_frame_id_, right_frame_id_;
	std::string config_file_location_;
	std::string undistort_config_file_location_;
	cv::Mat M1l, M2l, M1r,M2r;
};

}


int main(int argc, char **argv) {
    try {
        ros::init(argc, argv, "zed_camera");
        arti::ZedCameraROS zed_ros;
        return EXIT_SUCCESS;
    }
    catch(std::runtime_error& e) {
        ros::shutdown();
        return EXIT_FAILURE;
    }
}
