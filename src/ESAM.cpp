/**\file ESAM.cpp
 *
 * Wrapper class to deal with iSAM2, Factor Graphs and envire graph
 *
 * @author Javier Hidalgo Carrio et. al
 * See LICENSE for the license information
 *
 */

#include "ESAM.hpp"

#ifndef D2R
#define D2R M_PI/180.00 /** Convert degree to radian **/
#endif
#ifndef R2D
#define R2D 180.00/M_PI /** Convert radian to degree **/
#endif

#define DEBUG_PRINTS 1

using namespace envire::sam;

static const gtsam::Symbol invalid_symbol('u', 0);

ESAM::ESAM()
{
    base::Pose pose;
    base::Matrix6d cov_pose;
    BilateralFilterParams bfilter_default;
    OutlierRemovalParams outlier_default;
    SIFTKeypointParams keypoint_default;
    keypoint_default.min_scale = 0.08;
    keypoint_default.nr_octaves = 3;
    keypoint_default.nr_octaves_per_scale = 3;
    keypoint_default.min_contrast = 5.0;

    PFHFeatureParams feature_default;
    feature_default.normal_radius = 0.1;
    feature_default.feature_radius = 1.0;

    float default_downsample = 0.01;

    landmark_var = Eigen::Vector3d(0.01, 0.01, 0.01);

    this->init(pose, cov_pose, 'x', 'l', default_downsample,
            bfilter_default, outlier_default, keypoint_default, feature_default,
            landmark_var);
}

ESAM::ESAM(const ::base::Pose &pose, const ::base::Vector6d &var_pose,
        const char pose_key, const char landmark_key)
{
    BilateralFilterParams bfilter_default;
    OutlierRemovalParams outlier_default;
    SIFTKeypointParams keypoint_default;
    keypoint_default.min_scale = 0.06;
    keypoint_default.nr_octaves = 3;
    keypoint_default.nr_octaves_per_scale = 3;
    keypoint_default.min_contrast = 10.0;

    PFHFeatureParams feature_default;
    feature_default.normal_radius = 0.1;
    feature_default.feature_radius = 1.0;

    float default_downsample = 0.01;

    landmark_var = Eigen::Vector3d(0.01, 0.01, 0.01);

    this->init(pose, var_pose, pose_key, landmark_key, default_downsample,
            bfilter_default, outlier_default, keypoint_default, feature_default,
            landmark_var);
}

ESAM::ESAM(const ::base::Pose &pose, const ::base::Matrix6d &cov_pose,
        const char pose_key, const char landmark_key)
{
    BilateralFilterParams bfilter_default;
    OutlierRemovalParams outlier_default;
    SIFTKeypointParams keypoint_default;
    keypoint_default.min_scale = 0.06;
    keypoint_default.nr_octaves = 3;
    keypoint_default.nr_octaves_per_scale = 3;
    keypoint_default.min_contrast = 10.0;

    PFHFeatureParams feature_default;
    feature_default.normal_radius = 0.1;
    feature_default.feature_radius = 1.0;

    float default_downsample = 0.01;

    landmark_var = Eigen::Vector3d(0.01, 0.01, 0.01);

    this->init(pose, cov_pose, pose_key, landmark_key, default_downsample, bfilter_default,
            outlier_default, keypoint_default, feature_default,
            landmark_var);
}

ESAM::ESAM(const ::base::TransformWithCovariance &pose_with_cov,
        const char pose_key, const char landmark_key,
        const float downsample_size,
        const BilateralFilterParams &bfilter,
        const OutlierRemovalParams &outliers,
        const SIFTKeypointParams &keypoint,
        const PFHFeatureParams &feature,
        const Eigen::Vector3d &landmark_var)
{
    this->init(pose_with_cov, pose_key, landmark_key, downsample_size, bfilter,
            outliers, keypoint, feature, landmark_var);
}

void ESAM::init(const ::base::TransformWithCovariance &pose_with_cov,
        const char pose_key, const char landmark_key,
        const float downsample_size,
        const BilateralFilterParams &bfilter,
        const OutlierRemovalParams &outliers,
        const SIFTKeypointParams &keypoint,
        const PFHFeatureParams &feature,
        const Eigen::Vector3d &landmark_var)
{
    gtsam::Pose3 pose_0(gtsam::Rot3(pose_with_cov.orientation), gtsam::Point3(pose_with_cov.translation));
    gtsam::Matrix cov_matrix = pose_with_cov.cov;
    gtsam::noiseModel::Gaussian::shared_ptr cov_pose_0 = gtsam::noiseModel::Gaussian::Covariance(cov_matrix);

    /** Optimization parameters **/

    // Stop iterating once the change in error between steps is less than this value
    this->optimization_parameters.relativeErrorTol = 1e-5;

    // Do not perform more than N iteration steps
    this->optimization_parameters.maxIterations = 100;

    this->pose_key = pose_key;
    this->landmark_key = landmark_key;
    this->pose_idx = 0;
    this->landmark_idx = 0;


    /** Filter and outlier parameters **/
    this->bfilter_paramaters = bfilter;
    this->outlier_paramaters = outliers;
    this->keypoint_parameters = keypoint;

    /** Feature parameters **/
    this->feature_parameters = feature;

    /** Downsample size **/
    this->downsample_size = downsample_size;

    /** Invalid frame symbol to search for landmarks **/
    this->candidate_to_search_landmarks.reset(new gtsam::Symbol(invalid_symbol));
    this->frame_to_search_landmarks.reset(new gtsam::Symbol(invalid_symbol));

    /** Landmark error form the sensor **/
    this->landmark_var = landmark_var;

    // Add a prior on pose x0. This indirectly specifies where the origin is.
    this->_factor_graph.add(gtsam::PriorFactor<gtsam::Pose3>(gtsam::Symbol(this->pose_key, this->pose_idx), pose_0, cov_pose_0)); // add directly to graph

}

void ESAM::init(const ::base::Pose &pose, const ::base::Matrix6d &cov_pose,
        const char pose_key, const char landmark_key,
        const float downsample_size,
        const BilateralFilterParams &bfilter,
        const OutlierRemovalParams &outliers,
        const SIFTKeypointParams &keypoint,
        const PFHFeatureParams &feature,
        const Eigen::Vector3d &landmark_var)
{

    gtsam::Pose3 pose_0(gtsam::Rot3(pose.orientation), gtsam::Point3(pose.position));
    gtsam::Matrix cov_matrix = cov_pose;
    gtsam::noiseModel::Gaussian::shared_ptr cov_pose_0 = gtsam::noiseModel::Gaussian::Covariance(cov_matrix);

    /** Optimization parameters **/

    // Stop iterating once the change in error between steps is less than this value
    this->optimization_parameters.relativeErrorTol = 1e-5;

    // Do not perform more than N iteration steps
    this->optimization_parameters.maxIterations = 100;

    this->pose_key = pose_key;
    this->landmark_key = landmark_key;
    this->pose_idx = 0;
    this->landmark_idx = 0;

    /** Filter and outlier parameters **/
    this->bfilter_paramaters = bfilter;
    this->outlier_paramaters = outliers;
    this->keypoint_parameters = keypoint;

    /** Feature parameters **/
    this->feature_parameters = feature;

    /** Downsample size **/
    this->downsample_size = downsample_size;

    /** Invalid frame symbol to search for landmarks **/
    this->candidate_to_search_landmarks.reset(new gtsam::Symbol(invalid_symbol));
    this->frame_to_search_landmarks.reset(new gtsam::Symbol(invalid_symbol));

    /** Landmark error form the sensor **/
    this->landmark_var = landmark_var;

    // Add a prior on pose x0. This indirectly specifies where the origin is.
    this->_factor_graph.add(gtsam::PriorFactor<gtsam::Pose3>(gtsam::Symbol(this->pose_key, this->pose_idx), pose_0, cov_pose_0)); // add directly to graph
}

void ESAM::init(const ::base::Pose &pose, const ::base::Vector6d &var_pose,
        const char pose_key, const char landmark_key,
        const float downsample_size,
        const BilateralFilterParams &bfilter,
        const OutlierRemovalParams &outliers,
        const SIFTKeypointParams &keypoint,
        const PFHFeatureParams &feature,
        const Eigen::Vector3d &landmark_var)
{
    gtsam::Pose3 pose_0(gtsam::Rot3(pose.orientation), gtsam::Point3(pose.position));
    gtsam::Vector variances = var_pose;
    gtsam::noiseModel::Diagonal::shared_ptr cov_pose_0 = gtsam::noiseModel::Diagonal::Variances(variances);

    /** Optimzation parameters **/

    // Stop iterating once the change in error between steps is less than this value
    this->optimization_parameters.relativeErrorTol = 1e-5;
    // Do not perform more than N iteration steps
    this->optimization_parameters.maxIterations = 100;

    this->pose_key = pose_key;
    this->landmark_key = landmark_key;
    this->pose_idx = 0;
    this->landmark_idx = 0;

    /** Filter and outlier parameters **/
    this->bfilter_paramaters = bfilter;
    this->outlier_paramaters = outliers;
    this->keypoint_parameters = keypoint;

    /** Feature parameters **/
    this->feature_parameters = feature;

    /** Downsample size **/
    this->downsample_size = downsample_size;

    /** Invalid frame symbol to search for landmarks **/
    this->candidate_to_search_landmarks.reset(new gtsam::Symbol(invalid_symbol));
    this->frame_to_search_landmarks.reset(new gtsam::Symbol(invalid_symbol));

    /** Landmark error form the sensor **/
    this->landmark_var = landmark_var;

    // Add a prior on pose x0. This indirectly specifies where the origin is.
    this->_factor_graph.add(gtsam::PriorFactor<gtsam::Pose3>(
                gtsam::Symbol(this->pose_key, this->pose_idx), pose_0, cov_pose_0)); // add directly to graph
}

ESAM::~ESAM()
{
    this->marginals.reset();
}

void ESAM::insertPoseFactor(const char key1, const unsigned long int &idx1,
                const char key2, const unsigned long int &idx2,
                const base::Time &time, const ::base::Pose &delta_pose,
                const ::base::Vector6d &var_delta_pose)
{
    /** Symbols **/
    gtsam::Symbol symbol1 = gtsam::Symbol(key1, idx1);
    gtsam::Symbol symbol2 = gtsam::Symbol(key2, idx2);

    /** Add the delta pose to the factor graph **/
    this->_factor_graph.add(gtsam::BetweenFactor<gtsam::Pose3>(symbol1, symbol2,
                gtsam::Pose3(gtsam::Rot3(delta_pose.orientation), gtsam::Point3(delta_pose.position)),
                gtsam::noiseModel::Diagonal::Variances(var_delta_pose)));

    /** Add the delta pose transformation to envire **/
    ::base::Matrix6d cov(base::Matrix6d::Identity());
    cov.diagonal() = var_delta_pose;
    envire::core::Transform tf(time, delta_pose.position, delta_pose.orientation, cov);
    this->_transform_graph.addTransform(symbol1, symbol2, tf);
}

void ESAM::insertPoseFactor(const char key1, const unsigned long int &idx1,
                const char key2, const unsigned long int &idx2,
                const base::Time &time, const ::base::Pose &delta_pose,
                const ::base::Matrix6d &cov_delta_pose)
{
    /** Symbols **/
    gtsam::Symbol symbol1 = gtsam::Symbol(key1, idx1);
    gtsam::Symbol symbol2 = gtsam::Symbol(key2, idx2);

    /** Add the delta pose to the factor graph **/
    this->_factor_graph.add(gtsam::BetweenFactor<gtsam::Pose3>(symbol1, symbol2,
                gtsam::Pose3(gtsam::Rot3(delta_pose.orientation), gtsam::Point3(delta_pose.position)),
                gtsam::noiseModel::Gaussian::Covariance(cov_delta_pose)));

    /** Add the delta pose transformation to envire **/
    envire::core::Transform tf(time, delta_pose.position, delta_pose.orientation, cov_delta_pose);
    this->_transform_graph.addTransform(symbol1, symbol2, tf);

}

void ESAM::insertBearingRangeFactor(const char p_key, const unsigned long int &p_idx,
                const char l_key, const unsigned long int &l_idx,
                const base::Time &time, const double &bearing_angle, const double &range_distance,
                const ::base::Vector2d &var_measurement)
{
    /** Symbols **/
    gtsam::Symbol p_symbol = gtsam::Symbol(p_key, p_idx);
    gtsam::Symbol l_symbol = gtsam::Symbol(l_key, l_idx);

    /** Add the measurement to the factor graph **/
    this->_factor_graph.add(gtsam::BearingRangeFactor<gtsam::Pose2, gtsam::Point2>(p_symbol, l_symbol,
                gtsam::Rot2(bearing_angle),
                range_distance, gtsam::noiseModel::Diagonal::Variances(var_measurement)));

    /** Add the measurement to envire **/
    ::base::Matrix6d cov(base::Matrix6d::Zero());
    cov(0,0) = var_measurement(1); cov(5,5) = var_measurement(0); //In var first bearing, second range
    ::base::Orientation orient = Eigen::Quaternion <double>
        (Eigen::AngleAxisd(bearing_angle, Eigen::Vector3d::UnitZ()));
    envire::core::Transform tf(time, Eigen::Vector3d(range_distance, 0, 0), orient, cov);
    this->_transform_graph.addTransform(p_symbol, l_symbol, tf);

}

void ESAM::insertLandmarkFactor(const char p_key, const unsigned long int &p_idx,
                const char l_key, const unsigned long int &l_idx,
                const base::Time &time, const base::Vector3d &measurement,
                const ::base::Vector3d &var_measurement)
{
    /** Symbols **/
    gtsam::Symbol p_symbol = gtsam::Symbol(p_key, p_idx);
    gtsam::Symbol l_symbol = gtsam::Symbol(l_key, l_idx);

    /** Add the measurement to the factor graph **/
    this->_factor_graph.add(LandmarkFactor(p_symbol, l_symbol, gtsam::Point3(measurement),
                gtsam::noiseModel::Diagonal::Variances(var_measurement)));

    /** Add the measurement to envire **/
    ::base::Matrix6d cov(base::Matrix6d::Zero());
    cov.block<3,3>(0,0) = var_measurement.asDiagonal();
    envire::core::Transform tf(time, measurement, Eigen::Quaterniond::Identity(), cov);
    this->_transform_graph.addTransform(p_symbol, l_symbol, tf);

}

void ESAM::addDeltaPoseFactor(const base::Time &time, const ::Eigen::Affine3d &delta_tf, const ::base::Vector6d &var_delta_tf)
{
    ::base::Pose delta_pose(delta_tf);
    this->pose_idx++;
    return this->insertPoseFactor(this->pose_key, this->pose_idx-1, this->pose_key, this->pose_idx, time, delta_pose, var_delta_tf);
}

void ESAM::addDeltaPoseFactor(const base::Time &time, const ::base::TransformWithCovariance &delta_pose_with_cov)
{
    ::base::Pose delta_pose(delta_pose_with_cov.translation, delta_pose_with_cov.orientation);
    this->pose_idx++;
    return this->insertPoseFactor(this->pose_key, this->pose_idx-1, this->pose_key, this->pose_idx, time, delta_pose, delta_pose_with_cov.cov);
}

void ESAM::addDeltaPoseFactor(const base::Time &time, const ::base::Pose &delta_pose, const ::base::Vector6d &var_delta_pose)
{
    this->pose_idx++;
    return this->insertPoseFactor(this->pose_key, this->pose_idx-1, this->pose_key, this->pose_idx, time, delta_pose, var_delta_pose);
}

void ESAM::addDeltaPoseFactor(const base::Time &time, const ::base::Pose &delta_pose, const ::base::Matrix6d &cov_delta_pose)
{
    this->pose_idx++;
    return this->insertPoseFactor(this->pose_key, this->pose_idx-1, this->pose_key, this->pose_idx, time, delta_pose, cov_delta_pose);
}

void ESAM::addBearingRangeFactor(const char p_key, const unsigned long int &p_idx, const base::Time &time,
        const double &bearing_angle, const double &range_distance, const ::base::Vector2d &var_measurement)
{

    this->landmark_idx++;
    return this->insertBearingRangeFactor(p_key, p_idx, this->landmark_key, this->landmark_idx-1,
            time, bearing_angle, range_distance, var_measurement);
}

void ESAM::addLandmarkFactor(const char p_key, const unsigned long int &p_idx, const base::Time &time,
        const base::Vector3d &measurement, const ::base::Vector3d &var_measurement)
{

    this->landmark_idx++;
    return this->insertLandmarkFactor(p_key, p_idx, this->landmark_key, this->landmark_idx-1,
            time, measurement, var_measurement);
}

void ESAM::insertPoseValue(const std::string &frame_id, const ::base::TransformWithCovariance &pose_with_cov)
{
    try
    {
        envire::sam::PoseItem::Ptr pose_item(new envire::sam::PoseItem());
        pose_item->setData(pose_with_cov);
        this->_transform_graph.addItemToFrame(frame_id, pose_item);

    }catch(envire::core::UnknownFrameException &ufex)
    {
        std::cerr << ufex.what() << std::endl;
    }
}


void ESAM::insertPoseValue(const char key, const unsigned long int &idx,
        const ::base::TransformWithCovariance &pose_with_cov)
{
    gtsam::Symbol symbol = gtsam::Symbol(key, idx);
    try
    {
        envire::sam::PoseItem::Ptr pose_item(new envire::sam::PoseItem());
        pose_item->setData(pose_with_cov);
        this->_transform_graph.addItemToFrame(symbol, pose_item);

    }catch(envire::core::UnknownFrameException &ufex)
    {
        std::cerr << ufex.what() << std::endl;
    }

}

void ESAM::insertPoseValue(const char key, const unsigned long int &idx,
        const ::base::Pose &pose, const ::base::Matrix6d &cov_pose)
{
    gtsam::Symbol symbol = gtsam::Symbol(key, idx);
    try
    {
        envire::sam::PoseItem::Ptr pose_item(new envire::sam::PoseItem());
        base::TransformWithCovariance pose_with_cov(pose.position, pose.orientation, cov_pose);
        pose_item->setData(pose_with_cov);
        this->_transform_graph.addItemToFrame(symbol, pose_item);

    }catch(envire::core::UnknownFrameException &ufex)
    {
        std::cerr << ufex.what() << std::endl;
    }
}

void ESAM::insertLandmarkValue(const char l_key, const unsigned long int &l_idx,
         const ::base::Vector3d &measurement)
{
    gtsam::Symbol symbol = gtsam::Symbol(l_key, l_idx);
    try
    {
        envire::sam::LandmarkItem::Ptr landmark_item(new envire::sam::LandmarkItem());
        landmark_item->setData(measurement);
        this->_transform_graph.addItemToFrame(symbol, landmark_item);

    }catch(envire::core::UnknownFrameException &ufex)
    {
        std::cerr << ufex.what() << std::endl;
    }
}

void ESAM::addPoseValue(const ::base::TransformWithCovariance &pose_with_cov)
{
    /** Add the frame to the transform graph **/
    gtsam::Symbol frame_id = gtsam::Symbol(this->pose_key, this->pose_idx);
    this->_transform_graph.addFrame(frame_id);

    /** Insert the item **/
    return this->insertPoseValue(this->pose_key, this->pose_idx, pose_with_cov);
}

void ESAM::addLandmarkValue(const ::base::Vector3d &measurement)
{
    /** Add the Landmark to the transform graph **/
    gtsam::Symbol frame_id = gtsam::Symbol(this->landmark_key, this->landmark_idx);
    this->_transform_graph.addFrame(frame_id);

    /** Insert the item **/
    return this->insertLandmarkValue(this->landmark_key, this->landmark_idx, measurement);
}

base::TransformWithCovariance& ESAM::getLastPoseValueAndId(std::string &frame_id_string)
{
    gtsam::Symbol frame_id = gtsam::Symbol(this->pose_key, this->pose_idx);
    try
    {
        /** Get Item return an iterator to the first element **/
        envire::sam::PoseItem &pose_item = *(this->_transform_graph.getItem<envire::sam::PoseItem>(frame_id));
        frame_id_string = static_cast<std::string>(frame_id);
        return pose_item.getData();

    }catch(envire::core::UnknownFrameException &ufex)
    {
        std::cerr << ufex.what() << std::endl;
        throw "Pose Item do not found\n";
    }
}

const std::string ESAM::currentPoseId()
{
    return static_cast<std::string>(gtsam::Symbol(this->pose_key, this->pose_idx));
}

const std::string ESAM::currentLandmarkId()
{
    return static_cast<std::string>(gtsam::Symbol(this->landmark_key, this->landmark_idx));
}

void ESAM::optimize()
{
    gtsam::Values initialEstimate;

    std::cout<<"GETTING THE ESTIMATES\n";

    /** Initial estimates for poses **/
    for(register unsigned int i=0; i<this->pose_idx+1; ++i)
    {
        gtsam::Symbol frame_id(this->pose_key, i);
        //frame_id.print();
        try
        {
            /** Get Item return an iterator to the first element **/
            envire::sam::PoseItem &pose_item = *(this->_transform_graph.getItem<envire::sam::PoseItem>(frame_id));
            gtsam::Pose3 pose(gtsam::Rot3(pose_item.getData().orientation), gtsam::Point3(pose_item.getData().translation));
            initialEstimate.insert(frame_id, pose);
        }catch(envire::core::UnknownFrameException &ufex)
        {
            std::cerr << ufex.what() << std::endl;
            return;
        }
    }

    /** Initial estimates for landmarks **/
    for(register unsigned int i=0; i<this->landmark_idx; ++i)
    {
        gtsam::Symbol frame_id(this->landmark_key, i);
        //frame_id.print();
        try
        {
            /** Get Item return an iterator to the first element **/
            envire::sam::LandmarkItem &landmark_item =
                *(this->_transform_graph.getItem<envire::sam::LandmarkItem>(frame_id));
            gtsam::Point3 landmark(landmark_item.getData());
            initialEstimate.insert(frame_id, landmark);
        }catch(envire::core::UnknownFrameException &ufex)
        {
            std::cerr << ufex.what() << std::endl;
            return;
        }
    }

    std::cout<<"FINISHED GETTING ESTIMATES\n";

    initialEstimate.print("\nInitial Estimate:\n"); // print

    /** Create the optimizer ... **/
    gtsam::GaussNewtonOptimizer optimizer(this->_factor_graph, initialEstimate, this->optimization_parameters);

    /** Optimize **/
    gtsam::Values result = optimizer.optimize();
    result.print("Final Result:\n");

    std::cout<<"OPTIMIZE\n";

    /** Save the marginals **/
    this->marginals.reset(new gtsam::Marginals(this->_factor_graph, result));

    /** Store the result back in the transform graph **/
    gtsam::Values::iterator key_value = result.begin();
    for(; key_value != result.end(); ++key_value)
    {
        try
        {
            gtsam::Symbol const &frame_id(key_value->key);

            if(frame_id.chr() == this->pose_key)
            {
                /** Get Item return an iterator to the first element **/
                envire::sam::PoseItem &pose_item =
                   *( this->_transform_graph.getItem<envire::sam::PoseItem>(frame_id));
                base::TransformWithCovariance result_pose_with_cov;
                boost::shared_ptr<gtsam::Pose3> pose = boost::reinterpret_pointer_cast<gtsam::Pose3>(key_value->value.clone());
                result_pose_with_cov.translation = pose->translation().vector();
                result_pose_with_cov.orientation = pose->rotation().toQuaternion();
                result_pose_with_cov.cov = this->marginals->marginalCovariance(key_value->key);
                pose_item.setData(result_pose_with_cov);
            }
            else if(frame_id.chr() == this->landmark_key)
            {
                /** Get Item return an iterator to the first element **/
                envire::sam::LandmarkItem &landmark_item = 
                   *(this->_transform_graph.getItem<envire::sam::LandmarkItem>(frame_id));
                boost::shared_ptr<gtsam::Point3> point = boost::reinterpret_pointer_cast<gtsam::Point3>(key_value->value.clone());
                landmark_item.setData(base::Vector3d(point->x(), point->y(), point->z()));
            }
        }catch(envire::core::UnknownFrameException &ufex)
        {
            std::cerr << ufex.what() << std::endl;
            return;
        }
    }
}

::base::TransformWithCovariance ESAM::getTransformPose(const std::string &frame_id)
{
    ::base::TransformWithCovariance tf_cov;
    try
    {
        /** Get Item return an iterator to the first element **/
        envire::sam::PoseItem &pose_item = *(this->_transform_graph.getItem<envire::sam::PoseItem>(frame_id));
        return pose_item.getData();
    }catch(envire::core::UnknownFrameException &ufex)
    {
        std::cerr << ufex.what() << std::endl;
    }

    return tf_cov;
}

::base::samples::RigidBodyState ESAM::getRbsPose(const std::string &frame_id)
{
    ::base::samples::RigidBodyState rbs_pose;
    try
    {
        ::base::TransformWithCovariance tf_pose;

        /** Get Item return an iterator to the first element **/
        envire::sam::PoseItem &pose_item = *(this->_transform_graph.getItem<envire::sam::PoseItem>(frame_id));
        tf_pose = pose_item.getData();
        rbs_pose.position = tf_pose.translation;
        rbs_pose.orientation = tf_pose.orientation;
        rbs_pose.cov_position = tf_pose.cov.block<3,3>(0,0);
        rbs_pose.cov_orientation = tf_pose.cov.block<3,3>(3,3);
    }catch(envire::core::UnknownFrameException &ufex)
    {
        std::cerr << ufex.what() << std::endl;
    }

    return rbs_pose;
}

std::vector< ::base::samples::RigidBodyState > ESAM::getRbsPoses()
{
    std::vector< ::base::samples::RigidBodyState > rbs_poses;

    for(register unsigned int i=0; i<this->pose_idx+1; ++i)
    {
        gtsam::Symbol frame_id(this->pose_key, i);
        rbs_poses.push_back(this->getRbsPose(frame_id));
    }

    return rbs_poses;
}

PCLPointCloud &ESAM::getPointCloud(const std::string &frame_id)
{
    try
    {
        /** Get Item return an iterator to the first element **/
        envire::sam::PointCloudItem &point_cloud_item = *(this->_transform_graph.getItem<envire::sam::PointCloudItem>(frame_id));
        return point_cloud_item.getData();
    }catch(envire::core::UnknownFrameException &ufex)
    {
        std::cerr << ufex.what() << std::endl;
        throw "getPointCloud: point cloud not found\n";
    }
}

void ESAM::mergePointClouds(PCLPointCloud &merged_point_cloud, bool downsample)
{
    merged_point_cloud.clear();
    for(register unsigned int i=0; i<this->pose_idx+1; ++i)
    {
        gtsam::Symbol frame_id(this->pose_key, i);
        //std::cout<<"MERGING POINT CLOUDS: ";
        //frame_id.print();
        if (this->_transform_graph.containsItems<envire::sam::PointCloudItem>(frame_id))
        {
            PCLPointCloud local_points = this->getPointCloud(frame_id);
            base::TransformWithCovariance tf_cov = this->getTransformPose(frame_id);
            this->transformPointCloud(local_points, tf_cov.getTransform());
            merged_point_cloud += local_points;
            //std::cout<<"local_points.size(); "<<local_points.size()<<"\n";
        }
    }

    /** Downsample **/
    if (downsample)
    {
        PCLPointCloudPtr merged_point_cloud_ptr = boost::make_shared<PCLPointCloud>(merged_point_cloud);
        PCLPointCloudPtr downsample_point_cloud (new PCLPointCloud);
        this->downsample (merged_point_cloud_ptr, this->downsample_size, downsample_point_cloud);

        merged_point_cloud = *downsample_point_cloud;
    }
}

void ESAM::mergePointClouds(base::samples::Pointcloud &base_point_cloud, bool downsample)
{
    PCLPointCloud pcl_point_cloud;
    this->mergePointClouds(pcl_point_cloud, downsample);

    //std::cout<<"merged_points.size(); "<<pcl_point_cloud.size()<<"\n";
    base_point_cloud.points.clear();
    base_point_cloud.colors.clear();
    envire::sam::fromPCLPointCloud<PointType>(base_point_cloud, pcl_point_cloud);
    //std::cout<<"base merged point cloud.size(); "<<base_point_cloud.points.size()<<"\n";
}

void ESAM::currentPointCloud(base::samples::Pointcloud &base_point_cloud, bool downsample)
{
    /** Get the current point cloud **/
    gtsam::Symbol frame_id = gtsam::Symbol(this->pose_key, this->pose_idx-1);

    /* Clear point cloud **/
    base_point_cloud.points.clear();
    base_point_cloud.colors.clear();

    if (this->_transform_graph.containsItems<envire::sam::PointCloudItem>(frame_id))
    {
        /** Get point cloud **/
        PCLPointCloud current_point_cloud = this->getPointCloud(frame_id);

        /** Downsample **/
        if (downsample)
        {
            PCLPointCloudPtr current_point_cloud_ptr = boost::make_shared<PCLPointCloud>(current_point_cloud);
            PCLPointCloudPtr downsample_point_cloud (new PCLPointCloud);
            this->downsample (current_point_cloud_ptr, this->downsample_size, downsample_point_cloud);
            current_point_cloud = *downsample_point_cloud;
        }

        /** Convert to base point cloud **/
        envire::sam::fromPCLPointCloud<PointType>(base_point_cloud, current_point_cloud);
    }
}

void ESAM::currentPointCloudtoPLY(const std::string &prefixname, bool downsample)
{
    base::samples::Pointcloud base_point_cloud;

    /** Get the current point cloud **/
    gtsam::Symbol frame_id = gtsam::Symbol(this->pose_key, this->pose_idx-1);

    if (this->_transform_graph.containsItems<envire::sam::PointCloudItem>(frame_id))
    {
        /** Get the point cloud in the frame **/
        PCLPointCloud current_point_cloud = this->getPointCloud(frame_id);

        /** Downsample **/
        if (downsample)
        {
            PCLPointCloudPtr current_point_cloud_ptr = boost::make_shared<PCLPointCloud>(current_point_cloud);
            PCLPointCloudPtr downsample_point_cloud (new PCLPointCloud);
            this->downsample (current_point_cloud_ptr, this->downsample_size, downsample_point_cloud);
            current_point_cloud = *downsample_point_cloud;
        }

        /** Convert to base point cloud **/
        envire::sam::fromPCLPointCloud<PointType>(base_point_cloud, current_point_cloud);
    }

    /** Write to PLY **/
    std::string filename; filename = prefixname + static_cast<std::string>(frame_id) + ".ply";
    this->writePlyFile(base_point_cloud, filename);
}


void ESAM::printMarginals()
{
    std::cout.precision(3);
    for(register unsigned int i=0; i<this->pose_idx+1; ++i)
    {
        gtsam::Symbol frame_id(this->pose_key, i);
        std::cout <<this->pose_key<<i<<" covariance:\n" << this->marginals->marginalCovariance(frame_id) << std::endl;
    }

    for(register unsigned int i=0; i<this->landmark_idx; ++i)
    {
        gtsam::Symbol frame_id(this->landmark_key, i);
        std::cout <<this->landmark_key<<i<<" covariance:\n" << this->marginals->marginalCovariance(frame_id) << std::endl;
    }
}


void ESAM::pushPointCloud(const ::base::samples::Pointcloud &base_point_cloud, const int height, const int width)
{
    #ifdef DEBUG_PRINTS
    std::cout<<"Transform point cloud\n";
    std::cout<<"Number points: "<<base_point_cloud.points.size()<<"\n";
    std::cout<<"Number colors: "<<base_point_cloud.colors.size()<<"\n";
    #endif

    /** Convert to pcl point cloud **/
    PCLPointCloudPtr pcl_point_cloud(new PCLPointCloud);
    envire::sam::toPCLPointCloud<PointType>(base_point_cloud, *pcl_point_cloud);
    pcl_point_cloud->height = height;
    pcl_point_cloud->width = width;

    #ifdef DEBUG_PRINTS
    std::cout<<"Convert point cloud\n";
    std::cout<<"pcl_point_cloud.size(): "<<pcl_point_cloud->size()<<"\n";
    std::cout<<"pcl_point_cloud.heigh: "<<pcl_point_cloud->height<<"\n";
    std::cout<<"pcl_point_cloud.width: "<<pcl_point_cloud->width<<"\n";
    #endif

    /** Bilateral filter **/
    PCLPointCloudPtr filter_point_cloud (new PCLPointCloud);
    this->bilateralFilter(pcl_point_cloud, bfilter_paramaters.spatial_width,
                        bfilter_paramaters.range_sigma, filter_point_cloud);
    #ifdef DEBUG_PRINTS
    std::cout<<"Filter point cloud\n";
    std::cout<<"filter_point_cloud.size(): "<<filter_point_cloud->size()<<"\n";
    std::cout<<"filter_point_cloud.heigh: "<<filter_point_cloud->height<<"\n";
    std::cout<<"filter_point_cloud.width: "<<filter_point_cloud->width<<"\n";
    #endif

    pcl_point_cloud.reset();

    /** Remove Outliers **/
    PCLPointCloudPtr radius_point_cloud(new PCLPointCloud);
    if (outlier_paramaters.type == RADIUS)
    {
        /** Radius need organized point clouds **/
        this->radiusOutlierRemoval(filter_point_cloud, outlier_paramaters.parameter_one,
                outlier_paramaters.parameter_two, radius_point_cloud);
    }
    else
    {
        radius_point_cloud = filter_point_cloud;
    }

    filter_point_cloud.reset();
    #ifdef DEBUG_PRINTS
    std::cout<<"Radius point cloud\n";
    std::cout<<"radius_point_cloud.size(): "<<radius_point_cloud->size()<<"\n";
    std::cout<<"radius_point_cloud.heigh: "<<radius_point_cloud->height<<"\n";
    std::cout<<"radius_point_cloud.width: "<<radius_point_cloud->width<<"\n";
    #endif

    /** Downsample, lost the organized point cloud **/
    PCLPointCloudPtr downsample_point_cloud (new PCLPointCloud);
    this->downsample (radius_point_cloud, this->downsample_size, downsample_point_cloud);

    radius_point_cloud.reset();

    #ifdef DEBUG_PRINTS
    std::cout<<"Downsample point cloud\n";
    std::cout<<"downsample_points.size(): "<<downsample_point_cloud->size()<<"\n";
    std::cout<<"Point width: " << downsample_point_cloud->width<<" Height : "<<downsample_point_cloud->height << std::endl;
    std::cout<<"Point cloud downsampled size: " << downsample_point_cloud->width * downsample_point_cloud->height << " data points." << std::endl;
    #endif

    /** Statistical outlier removal **/
    PCLPointCloudPtr statistical_point_cloud(new PCLPointCloud);
    if (outlier_paramaters.type == STATISTICAL)
    {
        this->statisticalOutlierRemoval(downsample_point_cloud, outlier_paramaters.parameter_one,
                outlier_paramaters.parameter_two, statistical_point_cloud);
    }
    else
    {
        statistical_point_cloud = downsample_point_cloud;
    }

    downsample_point_cloud.reset();

    #ifdef DEBUG_PRINTS
    std::cout<<"Statistical outlier point cloud\n";
    std::cout<<"statistical_points.size(): "<<statistical_point_cloud->size()<<"\n";
    #endif

    /** Remove point without color **/
    PCLPointCloudPtr final_point_cloud(new PCLPointCloud);
    this->removePointsWithoutColor (statistical_point_cloud, final_point_cloud);
    statistical_point_cloud.reset();

    #ifdef DEBUG_PRINTS
    std::cout<<"Final outlier point cloud\n";
    std::cout<<"final_points.size(): "<<final_point_cloud->size()<<"\n";
    #endif

    /** Get current point cloud in the node **/
    gtsam::Symbol frame_id = gtsam::Symbol(this->pose_key, this->pose_idx);
    size_t number_pointclouds = this->_transform_graph.getItemCount<envire::sam::PointCloudItem>(frame_id);

    std::cout<<"FRAME ID: ";
    frame_id.print();
    std::cout<<" with "<<number_pointclouds<<" point clouds\n";

    /** Merge with the existing point cloud **/
    if (number_pointclouds)
    {
        /** Get the current point cloud **/
        /** Get Item return an iterator to the first element **/
        envire::sam::PointCloudItem &point_cloud_item = *(this->_transform_graph.getItem<envire::sam::PointCloudItem>(frame_id));

        /** Concatenate fields **/
        point_cloud_item.getData() += *final_point_cloud;

        /** Downsample the union **/
        PCLPointCloudPtr point_cloud_in_node = boost::make_shared<PCLPointCloud>(point_cloud_item.getData());
        PCLPointCloudPtr downsample_point_cloud (new PCLPointCloud);
        this->uniformsample(point_cloud_in_node, 2.0 * this->downsample_size, downsample_point_cloud);
        point_cloud_item.setData(*downsample_point_cloud.get());

        #ifdef DEBUG_PRINTS
        std::cout<<"Merging Point cloud with the existing one\n";
        std::cout<<"Number points: "<<point_cloud_item.getData().size()<<"\n";
        #endif

    }
    else
    {
        envire::sam::PointCloudItem::Ptr point_cloud_item(new PointCloudItem);
        point_cloud_item->setData(*final_point_cloud);
        this->_transform_graph.addItemToFrame(frame_id, point_cloud_item);

        #ifdef DEBUG_PRINTS
        std::cout<<"First time to push Point cloud\n";
        std::cout<<"Number points: "<<point_cloud_item->getData().size()<<"\n";
        #endif
    }

    final_point_cloud.reset();

    #ifdef DEBUG_PRINTS
    std::cout<<"END!!\n";
    #endif

    return;
}

int ESAM::keypointsPointCloud(const boost::shared_ptr<gtsam::Symbol> &frame_id, const float normal_radius, const float feature_radius)
{
    /** Get the point cloud in the node **/
    /** Get Item return an iterator to the first element **/
    envire::sam::PointCloudItem &point_cloud_item = *(this->_transform_graph.getItem<envire::sam::PointCloudItem>(*frame_id));
    PCLPointCloudPtr point_cloud_ptr = boost::make_shared<PCLPointCloud>(point_cloud_item.getData());

    std::cout<<"FRAME ID: ";
    frame_id->print();

    /** Downsample **/
    PCLPointCloudPtr downsample_point_cloud (new PCLPointCloud);
    this->downsample (point_cloud_ptr, 5.0 * this->downsample_size, downsample_point_cloud);

    #ifdef DEBUG_PRINTS
    std::cout<<"DOWNSAMPLE SIZE: "<< 5.0 * this->downsample_size <<"\n";
    std::cout<<"NORMAL RADIUS: "<< normal_radius <<"\n";
    #endif

    /**  Compute surface normals **/
    pcl::PointCloud<pcl::Normal>::Ptr normals (new pcl::PointCloud<pcl::Normal>);
    this->computeNormals (downsample_point_cloud, normal_radius, normals);

    /** Compute keypoints **/
    pcl::PointCloud<pcl::PointWithScale>::Ptr keypoints (new pcl::PointCloud<pcl::PointWithScale>);
    this->detectKeypoints (point_cloud_ptr, keypoint_parameters.min_scale,
            keypoint_parameters.nr_octaves, keypoint_parameters.nr_octaves_per_scale,
            keypoint_parameters.min_contrast, keypoints);

    #ifdef DEBUG_PRINTS
    std::cout<<"DETECTED "<<keypoints->size()<<" KEYPOINTS\n";
    this->printKeypoints(keypoints);
    #endif

    /**  Compute PFH features **/
    pcl::PointCloud<pcl::FPFHSignature33>::Ptr descriptors (new pcl::PointCloud<pcl::FPFHSignature33>);
    if (keypoints->size() > 0)
    {
        /** Store the keypoints in the envire node **/
        envire::sam::KeypointItem::Ptr keypoints_item (new KeypointItem);
        keypoints_item->setData(*keypoints);
        this->_transform_graph.addItemToFrame(*frame_id, keypoints_item);

        /** Compute the features descriptors **/
        this->computeFPFHFeaturesAtKeypoints (downsample_point_cloud, normals, keypoints, feature_radius, descriptors);

        #ifdef DEBUG_PRINTS
        std::cout<<"DETECTED "<<descriptors->size()<<" FEATURE DESCRIPTORS\n";
        #endif

        /** Store the features descriptors in the envire node **/
        envire::sam::FPFHDescriptorItem::Ptr descriptors_item (new FPFHDescriptorItem);
        descriptors_item->setData(*descriptors);
        this->_transform_graph.addItemToFrame(*frame_id, descriptors_item);
       // std::cout<<"FRAME: "<<static_cast<std::string>(*frame_id)<<" HAS "<<items.size()<<" ELEMENTS\n";
    }

    return keypoints->size();
}

boost::shared_ptr<gtsam::Symbol> ESAM::computeAlignedBoundingBox()
{
    /** Check that there is more than one frame **/
    if (this->pose_idx ==0)
        return boost::shared_ptr<gtsam::Symbol>(new gtsam::Symbol(invalid_symbol));

    /** Get the previous frame pose **/
    boost::shared_ptr<gtsam::Symbol> prev_frame_id(new gtsam::Symbol(this->pose_key, this->pose_idx-1));

    /** Get Item return an iterator to the first element **/
    envire::sam::PoseItem &prev_pose_item = *(this->_transform_graph.getItem<envire::sam::PoseItem>(*prev_frame_id));
    boost::shared_ptr<base::TransformWithCovariance> prev_pose = boost::make_shared<base::TransformWithCovariance>(prev_pose_item.getData());

    std::cout<<"FOR FRAME "<<static_cast<std::string>(*prev_frame_id)<<"\n";

    /** Get the current frame pose **/
    gtsam::Symbol current_frame_id = gtsam::Symbol(this->pose_key, this->pose_idx);

    /** Get Item return an iterator to the first element **/
    envire::sam::PoseItem &current_pose_item = *(this->_transform_graph.getItem<envire::sam::PoseItem>(current_frame_id));
    boost::shared_ptr<base::TransformWithCovariance> current_pose = boost::make_shared<base::TransformWithCovariance>(prev_pose_item.getData());

    /** Computer standard deviation **/
    Eigen::Vector3d std_prev_pose = prev_pose->cov.block<3,3>(0,0).diagonal().array().sqrt();
    Eigen::Vector3d std_current_pose = current_pose->cov.block<3,3>(0,0).diagonal().array().sqrt();
    std_prev_pose[0] = 0.05; std_current_pose[0] = 0.05;
    std_prev_pose[1] = 0.4; std_current_pose[1] = 0.4;
    std_prev_pose[2] = 1.0; std_current_pose[2] = 1.0;

    /** Compute Bounding box limits in the global frame **/
    Eigen::Vector3d front_limit(current_pose->translation);
    Eigen::Vector3d rear_limit(prev_pose->translation);
    //std::cout<<"FRONT BOUNDING LIMITS:\n"<<front_limit<<"\n";
    //std::cout<<"REAR BOUNDING LIMITS:\n"<<rear_limit<<"\n";

    /** Increase the limits using the standard deviation **/
    for (register int i=0; i<3; ++i)
    {
        if (front_limit[i] > rear_limit[i])
        {
            front_limit[i] += std_current_pose[i];
            rear_limit[i] -= std_prev_pose[i];
        }
        else
        {
            front_limit[i] -= std_current_pose[i];
            rear_limit[i] += std_prev_pose[i];
        }
    }

    /** Set the Bounding box **/
    envire::core::AlignedBoundingBox::Ptr bounding_box(new envire::core::AlignedBoundingBox);
    bounding_box->extend(front_limit);
    bounding_box->extend(rear_limit);

    /** Assign the Bounding box to the item **/
    prev_pose_item.setBoundary(bounding_box);

    //std::cout<<"FRAME ID: ";
    //prev_frame_id->print();
    //std::cout<<"FRONT BOUNDING LIMITS:\n"<<front_limit<<"\n";
    //std::cout<<"REAR BOUNDING LIMITS:\n"<<rear_limit<<"\n";
    //std::cout<<"CENTER:\n"<<prev_pose_item.centerOfBoundary()<<"\n";

    return prev_frame_id;
}

void ESAM::computeKeypoints()
{
    /** Compute aligned bounding box from the previous to the current frame **/
    std::cout<<"COMPUTE BOUNDING BOX\n";
    boost::shared_ptr<gtsam::Symbol> frame_id = this->computeAlignedBoundingBox();

    /** Compute the keypoints in case of valid frame and it has point cloud **/
    if ((*frame_id != invalid_symbol) && (this->_transform_graph.containsItems<envire::sam::PointCloudItem>(*frame_id)))
    {
        /** Compute the keypoints and features of the frame **/
        std::cout<<"KEYPOINTS AND FEATURES DESCRIPTORS\n";
        this->keypointsPointCloud(frame_id, this->feature_parameters.normal_radius, this->feature_parameters.feature_radius);

        /** Move the candidates to the frames to search **/
        this->frames_to_search = this->candidates_to_search;
        this->frame_to_search_landmarks = this->candidate_to_search_landmarks;

        /** Find next frame intersections **/
        std::cout<<"CONTAINER FRAME ID: "; frame_id->print();
        this->containsFrames(frame_id, this->candidates_to_search);

        /** Store the frame to search for landmarks in the global variable **/
        this->candidate_to_search_landmarks.reset(new gtsam::Symbol(*frame_id));

    }

    return;
}

void ESAM::detectLandmarks(const base::Time &time)
{
    std::cout<<"DETECTING LANDMARKS FOR FRAME: "<<static_cast<std::string>(*this->frame_to_search_landmarks)<<"\n";
    std::cout<<"TO SEARCH IN "<<this->frames_to_search.size()<<" FRAMES\n";

    /** Verify that we can search for the landmarks **/
    if (this->frames_to_search.size() > 0 &&
            (*this->frame_to_search_landmarks) != invalid_symbol)
    {
        /** Features Correspondences **/
        this->featuresCorrespondences(time, this->frame_to_search_landmarks, this->frames_to_search);

    }

    return;
}

bool ESAM::intersects(const gtsam::Symbol &frame1, const gtsam::Symbol &frame2)
{
    /** Get Spatial item of the first frame **/
    /** Get Item return an iterator to the first element **/
    envire::sam::PoseItem &pose_item1 = *(this->_transform_graph.getItem<envire::sam::PoseItem>(frame1));

    /** Get Spatial item of the second frame **/
    /** Get Item return an iterator to the first element **/
    envire::sam::PoseItem &pose_item2 = *(this->_transform_graph.getItem<envire::sam::PoseItem>(frame2));

    /** Check intersection **/
    return pose_item1.intersects(pose_item2);
}

bool ESAM::contains(const boost::shared_ptr<gtsam::Symbol> &container_frame, const boost::shared_ptr<gtsam::Symbol> &query_frame)
{
    /** Get Spatial item of the source frame **/
    /** Get Item return an iterator to the first element **/
    envire::sam::PoseItem &pose_item1 = *(this->_transform_graph.getItem<envire::sam::PoseItem>(*container_frame));

    /** Get Spatial item of the query frame **/
    /** Get Item return an iterator to the first element **/
    envire::sam::PoseItem &pose_item2 = *(this->_transform_graph.getItem<envire::sam::PoseItem>(*query_frame));

    /** Check intersection **/
    if (*container_frame > *query_frame)
    {
        return (pose_item1.contains(pose_item2.getData().translation) ||
                pose_item1.contains(pose_item2.centerOfBoundary()));
    }
    else
    {
        return (pose_item1.contains(pose_item2.getData().translation));
    }
}

void ESAM::containsFrames (const boost::shared_ptr<gtsam::Symbol> &container_frame_id, std::vector< boost::shared_ptr<gtsam::Symbol> > &frames_to_search)
{
    frames_to_search.clear();

    for(register unsigned int i=0; i<this->pose_idx+1; ++i)
    {
        boost::shared_ptr<gtsam::Symbol> target_frame_id(new gtsam::Symbol(this->pose_key, i));
        if (*target_frame_id != *container_frame_id)
        {
            std::cout<<"TARGET FRAME ID: "; target_frame_id->print();

            if (this->contains(container_frame_id, target_frame_id))
            {
                std::cout<<"CONTAINS FOUND!\n";
                frames_to_search.push_back(std::move(target_frame_id));

                if (std::fabs(container_frame_id->index() - target_frame_id->index()) > 10.00)
                {
                    std::cout<<"POTENTIAL LOOP CLOSE CONTAINER: "<<container_frame_id->index()<<" TARGET "<< target_frame_id->index()<<"\n";
                }

            }
            else
            {
                std::cout<<"NO FOUND!\n";
            }

            if (container_frame_id->index() > 88 && container_frame_id->index() < 91)
            {
                if (target_frame_id->index() > 18 && target_frame_id->index() < 22)
                {
                    frames_to_search.push_back(std::move(target_frame_id));
                    std::cout<<"ARTIFICIAL LOOP CLOSURE "<<container_frame_id->index()<<" with "<<target_frame_id->index()<<"\n";
                }
            }
        }
    }
}

void ESAM::featuresCorrespondences(const base::Time &time, const boost::shared_ptr<gtsam::Symbol> &frame_id,
        const std::vector< boost::shared_ptr<gtsam::Symbol> > &frames_to_search)
{
    std::cout<<"CORRESPONDENCE FEATURES: "<<static_cast<std::string>(*frame_id)<<"\n";

    /** At least we found one landmark **/
    bool found_landmarks = false;

    /** Return in case there is not keypoints and features descriptors **/
    if (!this->_transform_graph.containsItems<KeypointItem>(*frame_id) ||
            !this->_transform_graph.containsItems<FPFHDescriptorItem>(*frame_id))
    {

        std::cout<<"Frame does not contain keypoints and features\n";
        return;
    }

    /** Get the source pose **/
    /** Get Item return an iterator to the first element **/
    envire::sam::PoseItem &source_pose = *(this->_transform_graph.getItem<envire::sam::PoseItem>(*frame_id));

    /** Get the source keypoints **/
    envire::sam::KeypointItem &source_keypoints_item = *(this->_transform_graph.getItem<envire::sam::KeypointItem>(*frame_id));
    pcl::PointCloud<pcl::PointWithScale>::Ptr source_keypoints = boost::make_shared< pcl::PointCloud<pcl::PointWithScale> >(source_keypoints_item.getData());

    /** Get the source descriptors **/
    envire::sam::FPFHDescriptorItem &source_descriptors_item = *(this->_transform_graph.getItem<envire::sam::FPFHDescriptorItem>(*frame_id));
    pcl::PointCloud<pcl::FPFHSignature33>::Ptr source_descriptors = boost::make_shared<pcl::PointCloud<pcl::FPFHSignature33> >(source_descriptors_item.getData());

    std::vector< boost::shared_ptr<gtsam::Symbol> >::const_iterator it = frames_to_search.begin();
    for(; it != frames_to_search.end(); ++it)
    {
        /** In case the frame has keypoints and features descriptors **/
        if (this->_transform_graph.containsItems<envire::sam::KeypointItem>(*(*it)) &&
                this->_transform_graph.containsItems<envire::sam::FPFHDescriptorItem>(*(*it)))
        {
            /** Get the target pose **/
            envire::sam::KeypointItem &target_keypoints_item = *(this->_transform_graph.getItem<envire::sam::KeypointItem>(*(*it)));

            /** Get Item return an iterator to the first element **/
            envire::sam::PoseItem &target_pose = *(this->_transform_graph.getItem<envire::sam::PoseItem>(*(*it)));

            /** Get the target keypoints **/
            pcl::PointCloud<pcl::PointWithScale>::Ptr target_keypoints = boost::make_shared< pcl::PointCloud<pcl::PointWithScale> >(target_keypoints_item.getData());

            /** Get the target descriptors **/
            /** Get Item return an iterator to the first element **/
            envire::sam::FPFHDescriptorItem &target_descriptors_item = *(this->_transform_graph.getItem<envire::sam::FPFHDescriptorItem>(*(*it)));
            pcl::PointCloud<pcl::FPFHSignature33>::Ptr target_descriptors = boost::make_shared<pcl::PointCloud<pcl::FPFHSignature33> >(target_descriptors_item.getData());

            /** Find features correspondences **/
            std::vector<int> source2target;
            std::vector<float> k_squared_distances;
            this->findFPFHFeatureCorrespondences(source_descriptors, target_descriptors, source2target, k_squared_distances);

            std::cout << "TARGET FRAME " << static_cast<std::string>(*(*it)) << " HAS" << target_descriptors->size() <<" DESCRIPTORS\n";

            /** Compute the median correspondence score **/
            std::vector<float> temp(k_squared_distances);
            std::sort(temp.begin (), temp.end ());
            float median_score = temp[temp.size ()/2.0];

            /** Set the percentage of the media **/
            float percentage = 1.0;

            /** Evaluate the keypoints with highest score (small squared
             * distance)  **/
            for (register unsigned int i=0; i<source_keypoints->size(); ++i)
            {
                /** Get the points **/
                Eigen::Vector3d p_source (source_keypoints->points[i].x,
                                            source_keypoints->points[i].y,
                                            source_keypoints->points[i].z);

                int j = source2target[i];
                Eigen::Vector3d p_target (target_keypoints->points[j].x,
                                            target_keypoints->points[j].y,
                                            target_keypoints->points[j].z);
                std::cout<<"IN LOCAL FRAME\n";
                std::cout<<"SOURCE POINT: "<<p_source[0]<<"TARGET POINT: "<<p_target[0]<<"\n";
                std::cout<<"SOURCE POINT: "<<p_source[1]<<"TARGET POINT: "<<p_target[1]<<"\n";
                std::cout<<"SOURCE POINT: "<<p_source[2]<<"TARGET POINT: "<<p_target[2]<<"\n";

                /** Transform the point in the global frame **/
                Eigen::Vector3d p_source_global = source_pose.getData().getTransform() * p_source;
                Eigen::Vector3d p_target_global = target_pose.getData().getTransform() * p_target;

                std::cout<<"IN GLOBAL FRAME\n";
                std::cout<<"SOURCE POINT: "<<p_source_global[0]<<"TARGET POINT: "<<p_target_global[0]<<"\n";
                std::cout<<"SOURCE POINT: "<<p_source_global[1]<<"TARGET POINT: "<<p_target_global[1]<<"\n";
                std::cout<<"SOURCE POINT: "<<p_source_global[2]<<"TARGET POINT: "<<p_target_global[2]<<"\n";

                Eigen::Vector3d innovation = p_source_global - p_target_global;

                std::cout<<"DIFF NORM: "<<innovation.norm()<<"\n";

                /** Get the uncertainty of both poses **/
                base::TransformWithCovariance add_tf(source_pose.getData());// * target_pose.getData());
                Eigen::Matrix3d add_cov = static_cast<Eigen::Matrix3d>(add_tf.cov.block<3,3>(0,0)) +
                    static_cast<Eigen::Matrix3d>(this->landmark_var.asDiagonal());

                std::cout<<"ADD COVARIANCE:\n"<<add_cov<<"\n";

                /** Compute Mahalanobis **/
                const float mahalanobis = innovation.transpose() * add_cov.inverse() * innovation;

                //if (this->acceptPointDistance(mahalanobis, this->landmark_var.size()))
                //{
                    std::cout<<"POINT PASSED MAHALANOBIS TEST("<<mahalanobis<<")\n";
                    std::cout<<"MEDIAN SCORE ("<<median_score<<") PERCENTAGE ("<<percentage<<")\n";

                    if (k_squared_distances[i] > percentage * median_score)
                    {
                        std::cout<<"MARCHING SCORE REJECTED!\n";
                    }
                    else
                    {
                        /** Set found landmarks to true **/
                        if (found_landmarks == false)
                        {
                            found_landmarks = true;
                        }

                        std::cout<<"CURRENT LANDMARK ID: "<<this->currentLandmarkId()<<"\n";
                        /** Insert landmark measurement into the factor graph **/
                        this->insertLandmarkFactor(frame_id->chr(), frame_id->index(),
                                this->landmark_key, this->landmark_idx, time,
                                p_source, this->landmark_var);


                        /** Insert landmark measurement into the factor graph **/
                        this->insertLandmarkFactor((*it)->chr(), (*it)->index(),
                                this->landmark_key, this->landmark_idx, time,
                                p_target, this->landmark_var);

                        /** Insert landmark value into the envire graph **/
                        this->insertLandmarkValue(this->landmark_key, this->landmark_idx, p_source_global);

                        /** Increase landmark index **/
                        this->landmark_idx++;
                    }
                //}
                //else
                //{
                //    std::cout<<"MAHALANOBIS REJECTED!\n";
                //}
            }
        }
    }

    if (found_landmarks)
    {
        /** Optimize ESAM **/
        std::cout<<"OPTIMIZE!!!\n";
        this->optimize();

        /** Marginals **/
        //std::cout<<"MARGINALS!!!\n";
        //this->printMarginals();

    }

    return;
}

void ESAM::printFactorGraph(const std::string &title)
{
    this->_factor_graph.print(title);
}

void ESAM::graphViz(const std::string &filename)
{
    envire::core::GraphViz viz;
    viz.write(this->_transform_graph, filename);
}

void ESAM::writePlyFile(const base::samples::Pointcloud& points, const std::string& file)
{
    std::ofstream data( file.c_str() );

    data << "ply" << "\n";
    data << "format ascii 1.0\n";

    data << "element vertex " << points.points.size() <<  "\n";
    data << "property float x\n";
    data << "property float y\n";
    data << "property float z\n";

    if( !points.colors.empty() )
    {
    data << "property uchar red\n";
    data << "property uchar green\n";
    data << "property uchar blue\n";
    data << "property uchar alpha\n";
    }
    data << "end_header\n";

    for( size_t i = 0; i < points.points.size(); i++ )
    {
    data 
        << points.points[i].x() << " "
        << points.points[i].y() << " "
        << points.points[i].z() << " ";
    if( !points.colors.empty() )
    {
        data 
        << (int)(points.colors[i].x()*255) << " "
        << (int)(points.colors[i].y()*255) << " "
        << (int)(points.colors[i].z()*255) << " "
        << (int)(points.colors[i].w()*255) << " ";
    }
    data << "\n";
    }
}


int ESAM::getPoseCorrespodences(std::vector<int> &pose_correspodences)
{
    pose_correspodences.clear();
    std::vector< boost::shared_ptr<gtsam::Symbol> >::const_iterator it = this->frames_to_search.begin();
    for(; it != this->frames_to_search.end(); ++it)
    {
        pose_correspodences.push_back(static_cast<int>((*it)->index()));
    }

    return static_cast<int>(this->frame_to_search_landmarks->index());
}

void ESAM::transformPointCloud(const ::base::samples::Pointcloud & pc, ::base::samples::Pointcloud & transformed_pc, const Eigen::Affine3d& transformation)
{
    std::cout<<"Static function transform point cloud\n";
    transformed_pc.points.clear();
    for(std::vector< ::base::Point >::const_iterator it = pc.points.begin(); it != pc.points.end(); it++)
    {
        transformed_pc.points.push_back(transformation * (*it));
    }
    transformed_pc.colors = pc.colors;
}

void ESAM::transformPointCloud(::base::samples::Pointcloud & pc, const Eigen::Affine3d& transformation)
{
    for(std::vector< ::base::Point >::iterator it = pc.points.begin(); it != pc.points.end(); it++)
    {
        *it = (transformation * (*it));
    }
}

void ESAM::transformPointCloud(pcl::PointCloud< PointType >&pcl_pc, const Eigen::Affine3d& transformation)
{
    for(std::vector< PointType, Eigen::aligned_allocator<PointType> >::iterator it = pcl_pc.begin();
            it != pcl_pc.end(); it++)
    {
        Eigen::Vector3d point (it->x, it->y, it->z);
        point = transformation * point;
        PointType pcl_point;
        pcl_point.x = point[0]; pcl_point.y = point[1]; pcl_point.z = point[2];
        pcl_point.rgb = it->rgb;
        *it = pcl_point;
    }
}

void ESAM::downsample (PCLPointCloud::Ptr &points, float leaf_size, PCLPointCloud::Ptr &downsampled_out)
{

  pcl::VoxelGrid<PointType> vox_grid;
  vox_grid.setLeafSize (leaf_size, leaf_size, leaf_size);
  vox_grid.setInputCloud (points);
  vox_grid.filter (*downsampled_out);

  return;
}

void ESAM::uniformsample (PCLPointCloud::Ptr &points, float radius_search, PCLPointCloud::Ptr &uniformsampled_out)
{
    pcl::PointCloud<int> sampled_indices;

    pcl::UniformSampling<PointType> uniform_sampling;
    uniform_sampling.setInputCloud (points);
    uniform_sampling.setRadiusSearch (radius_search);
    uniform_sampling.compute (sampled_indices);
    pcl::copyPointCloud (*points, sampled_indices.points, *uniformsampled_out);
    std::cout << "Original total points: " << points->size () << "; Uniform Sampling: " << uniformsampled_out->size () << std::endl;
}

void ESAM::removePointsWithoutColor (const PCLPointCloud::Ptr &points, PCLPointCloud::Ptr &points_out)
{
    points_out->clear();

    /* The output is an unorganized point cloud **/
    for(size_t i = 0; i < points->size(); ++i)
    {
        PointType const &pcl_point(points->points[i]);
        if (pcl_point.rgb > 0.00)
        {
            points_out->push_back(pcl_point);
        }
    }
    points_out->width = points_out->size();
    points_out->height = 1;
}

void ESAM::bilateralFilter(const PCLPointCloud::Ptr &points, const double &spatial_width, const double &range_sigma , PCLPointCloud::Ptr &filtered_out)
{
    pcl::FastBilateralFilter<PointType> b_filter;

    /** Configure Bilateral filter **/
    b_filter.setSigmaS(spatial_width);
    b_filter.setSigmaR(range_sigma);

    b_filter.setInputCloud(points);
    filtered_out->width = points->width;
    filtered_out->height = points->height;
    std::cout<<"width: "<<filtered_out->width<<"\n";
    std::cout<<"height: "<<filtered_out->height<<"\n";
    b_filter.filter(*filtered_out);
}

void ESAM::radiusOutlierRemoval(PCLPointCloud::Ptr &points, const double &radius, const double &min_neighbors, PCLPointCloud::Ptr &outliersampled_out)
{
    pcl::RadiusOutlierRemoval<PointType> ror;

    ror.setRadiusSearch(radius);
    ror.setMinNeighborsInRadius(min_neighbors);

    #ifdef DEBUG_PRINTS
    std::cout<<"RADIUS FILTER\n";
    std::cout<<"radius: "<< radius<<"\n";
    std::cout<<"min_neighbors: "<< min_neighbors<<"\n";
    #endif
    ror.setInputCloud(points);
    ror.filter (*outliersampled_out);
}

void ESAM::statisticalOutlierRemoval(PCLPointCloud::Ptr &points, const double &mean_k, const double &std_mul, PCLPointCloud::Ptr &outliersampled_out)
{
    pcl::StatisticalOutlierRemoval<PointType> sor;

    sor.setMeanK(mean_k);
    sor.setStddevMulThresh(std_mul);

    #ifdef DEBUG_PRINTS
    std::cout<<"STATISTICAL FILTER\n";
    std::cout<<"mean_k: "<<mean_k<<"\n";
    std::cout<<"std_mul: "<<std_mul<<"\n";
    #endif
    sor.setInputCloud(points);
    sor.filter (*outliersampled_out);
}

void ESAM::computeNormals (PCLPointCloud::Ptr &points,
                                float normal_radius,
                                pcl::PointCloud<pcl::Normal>::Ptr &normals_out)
{
  pcl::NormalEstimation<PointType, pcl::Normal> norm_est;

  // Use a FLANN-based KdTree to perform neighborhood searches
  //norm_est.setSearchMethod (pcl::KdTreeFLANN<PointType>::Ptr (new pcl::KdTreeFLANN<PointType>));
  norm_est.setSearchMethod (pcl::search::KdTree<PointType>::Ptr (new pcl::search::KdTree<PointType>));


  // Specify the size of the local neighborhood to use when computing the surface normals
  norm_est.setRadiusSearch (normal_radius);

  // Set the input points
  norm_est.setInputCloud (points);

  // Estimate the surface normals and store the result in "normals_out"
  norm_est.compute (*normals_out);
}

void ESAM::computePFHFeatures (PCLPointCloud::Ptr &points,
                      pcl::PointCloud<pcl::Normal>::Ptr &normals,
                      float feature_radius,
                      pcl::PointCloud<pcl::PFHSignature125>::Ptr &descriptors_out)
{
    // Create a PFHEstimation object
    pcl::PFHEstimation<PointType, pcl::Normal, pcl::PFHSignature125> pfh_est;

    // Set it to use a FLANN-based KdTree to perform its neighborhood searches
    pfh_est.setSearchMethod (pcl::search::KdTree<PointType>::Ptr (new pcl::search::KdTree<PointType>));

    // Specify the radius of the PFH feature
    pfh_est.setRadiusSearch (feature_radius);

    // Set the input points and surface normals
    pfh_est.setInputCloud (points);
    pfh_est.setInputNormals (normals);

    // Compute the features
    pfh_est.compute (*descriptors_out);

    return;
}

void ESAM::detectKeypoints (PCLPointCloud::Ptr &points,
          float min_scale, int nr_octaves, int nr_scales_per_octave, float min_contrast,
          pcl::PointCloud<pcl::PointWithScale>::Ptr &keypoints_out)
{
    pcl::SIFTKeypoint<PointType, pcl::PointWithScale> sift_detect;

    // Use a FLANN-based KdTree to perform neighborhood searches
    sift_detect.setSearchMethod (pcl::search::KdTree<PointType>::Ptr (new pcl::search::KdTree<PointType>));

    // Set the detection parameters
    sift_detect.setScales (min_scale, nr_octaves, nr_scales_per_octave);
    sift_detect.setMinimumContrast (min_contrast);

    // Set the input
    sift_detect.setInputCloud (points);

    // Detect the keypoints and store them in "keypoints_out"
    sift_detect.compute (*keypoints_out);

    return;
}

void ESAM::computePFHFeaturesAtKeypoints (PCLPointCloud::Ptr &points,
                           pcl::PointCloud<pcl::Normal>::Ptr &normals,
                           pcl::PointCloud<pcl::PointWithScale>::Ptr &keypoints, float feature_radius,
                           pcl::PointCloud<pcl::PFHSignature125>::Ptr &descriptors_out)
{
    // Create a PFHEstimation object
    pcl::PFHEstimation<PointType, pcl::Normal, pcl::PFHSignature125> pfh_est;

    // Set it to use a FLANN-based KdTree to perform its neighborhood searches
    pfh_est.setSearchMethod (pcl::search::KdTree<PointType>::Ptr (new pcl::search::KdTree<PointType>));

    // Specify the radius of the PFH feature
    pfh_est.setRadiusSearch (feature_radius);

    /* This is a little bit messy: since our keypoint detection returns PointWithScale points, but we want to
    * use them as an input to our PFH estimation, which expects clouds of PointXYZRGBA points.  To get around this,
    * we'll use copyPointCloud to convert "keypoints" (a cloud of type PointCloud<PointWithScale>) to
    * "keypoints_xyzrgb" (a cloud of type PointCloud<PointXYZRGBA>).  Note that the original cloud doesn't have any RGB
    * values, so when we copy from PointWithScale to PointXYZRGBA, the new r,g,b fields will all be zero.
    */

    PCLPointCloud::Ptr keypoints_xyzrgb (new PCLPointCloud);
    pcl::copyPointCloud (*keypoints, *keypoints_xyzrgb);

    // Use all of the points for analyzing the local structure of the cloud
    pfh_est.setSearchSurface (points);
    pfh_est.setInputNormals (normals);

    // But only compute features at the keypoints
    pfh_est.setInputCloud (keypoints_xyzrgb);

    // Compute the features
    pfh_est.compute (*descriptors_out);

    return;
}

void ESAM::computeFPFHFeaturesAtKeypoints (PCLPointCloud::Ptr &points,
                           pcl::PointCloud<pcl::Normal>::Ptr &normals,
                           pcl::PointCloud<pcl::PointWithScale>::Ptr &keypoints, float feature_radius,
                           pcl::PointCloud<pcl::FPFHSignature33>::Ptr &descriptors_out)
{
    // Create a FPFHEstimation object
    pcl::FPFHEstimation<PointType, pcl::Normal, pcl::FPFHSignature33> fpfh_est;

    // Set it to use a FLANN-based KdTree to perform its neighborhood searches
    fpfh_est.setSearchMethod (pcl::search::KdTree<PointType>::Ptr (new pcl::search::KdTree<PointType>));

    // Specify the radius of the PFH feature
    fpfh_est.setRadiusSearch (feature_radius);

    /* This is a little bit messy: since our keypoint detection returns PointWithScale points, but we want to
    * use them as an input to our PFH estimation, which expects clouds of PointXYZRGBA points.  To get around this,
    * we'll use copyPointCloud to convert "keypoints" (a cloud of type PointCloud<PointWithScale>) to
    * "keypoints_xyzrgb" (a cloud of type PointCloud<PointXYZRGBA>).  Note that the original cloud doesn't have any RGB
    * values, so when we copy from PointWithScale to PointXYZRGBA, the new r,g,b fields will all be zero.
    */

    PCLPointCloud::Ptr keypoints_xyzrgb (new PCLPointCloud);
    pcl::copyPointCloud (*keypoints, *keypoints_xyzrgb);

    // Use all of the points for analyzing the local structure of the cloud
    fpfh_est.setSearchSurface (points);
    fpfh_est.setInputNormals (normals);

    // But only compute features at the keypoints
    fpfh_est.setInputCloud (keypoints_xyzrgb);

    // Compute the features
    fpfh_est.compute (*descriptors_out);

    return;
}

void ESAM::findPFHFeatureCorrespondences (pcl::PointCloud<pcl::PFHSignature125>::Ptr &source_descriptors,
                      pcl::PointCloud<pcl::PFHSignature125>::Ptr &target_descriptors,
                      std::vector<int> &correspondences_out, std::vector<float> &correspondence_scores_out)
{

    // Resize the output vector
    correspondences_out.resize (source_descriptors->size ());
    correspondence_scores_out.resize (source_descriptors->size ());

    // Use a KdTree to search for the nearest matches in feature space
    pcl::search::KdTree<pcl::PFHSignature125> descriptor_kdtree;
    descriptor_kdtree.setInputCloud (target_descriptors);

    // Find the index of the best match for each keypoint, and store it in "correspondences_out"
    const int k = 1;
    std::vector<int> k_indices (k);
    std::vector<float> k_squared_distances (k);
    for (size_t i = 0; i < source_descriptors->size (); ++i)
    {
        descriptor_kdtree.nearestKSearch (*source_descriptors, i, k, k_indices, k_squared_distances);
        correspondences_out[i] = k_indices[0];
        correspondence_scores_out[i] = k_squared_distances[0];
    }

    return;
}

void ESAM::findFPFHFeatureCorrespondences (pcl::PointCloud<pcl::FPFHSignature33>::Ptr &source_descriptors,
                      pcl::PointCloud<pcl::FPFHSignature33>::Ptr &target_descriptors,
                      std::vector<int> &correspondences_out, std::vector<float> &correspondence_scores_out)
{
    // Resize the output vector
    correspondences_out.resize (source_descriptors->size ());
    correspondence_scores_out.resize (source_descriptors->size ());

    // Use a KdTree to search for the nearest matches in feature space
    pcl::search::KdTree<pcl::FPFHSignature33> descriptor_kdtree;
    descriptor_kdtree.setInputCloud (target_descriptors);

    // Find the index of the best match for each keypoint, and store it in "correspondences_out"
    const int k = 1;
    std::vector<int> k_indices (k);
    std::vector<float> k_squared_distances (k);
    for (size_t i = 0; i < source_descriptors->size (); ++i)
    {
        descriptor_kdtree.nearestKSearch (*source_descriptors, i, k, k_indices, k_squared_distances);
        correspondences_out[i] = k_indices[0];
        correspondence_scores_out[i] = k_squared_distances[0];
    }

    return;
}

void ESAM::printKeypoints(const pcl::PointCloud<pcl::PointWithScale>::Ptr keypoints)
{

    for (size_t i = 0; i < keypoints->size (); ++i)
    {
        /** Get the point data **/
        const pcl::PointWithScale & p = keypoints->points[i];

        std::cout<<"KEYPOINT: "<<p.x<<" "<<p.y<<" "<<p.z<<"\n";
    }

    return;
}

bool ESAM::acceptPointDistance(const float &mahalanobis2, const int dof)
{
    std::cout << "[MAHALANOBIS_DISTANCE] mahalanobis2: " << mahalanobis2 <<std::endl;
    std::cout << "[MAHALANOBIS_DISTANCE] dof: " << dof <<std::endl;

    /** Only significance of alpha = 5% is computed **/
    switch (dof)
    {
        case 1:
            if (mahalanobis2 < 3.84)
                return true;
            else
                return false;
        case 2:
            if (mahalanobis2 < 5.99)
                return true;
            else
                return false;
        case 3:
            if (mahalanobis2 < 7.81)
                return true;
            else
                return false;
        case 4:
            if (mahalanobis2 < 9.49)
                return true;
            else
                return false;
        default:
            return false;
    }
}

