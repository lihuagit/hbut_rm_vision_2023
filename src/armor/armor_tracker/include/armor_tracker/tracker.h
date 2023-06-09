/**
 * @file tracker.h
 * @brief 对预测进行封装，目前使用EKF
 * @author lihuagit (3190995951@qq.com)
 * @version 1.0
 * @date 2023-03-01
 * 
 */
#ifndef ARMOR_TRACKER__COORDSOLVER_HPP
#define ARMOR_TRACKER__COORDSOLVER_HPP
// c++
#include <vector>
#include <string>
#include <deque>

// opencv
#include <opencv2/core.hpp>

// user
#include "armor_tracker/EKF.hpp"

/**
 * @brief 此处定义匀速直线运动模型
 */
struct Predict {
    template<class T>
    void operator()(const T x0[6], T x1[6]) {
        x1[0] = x0[0] + delta_t * x0[1];  //0.1
        x1[1] = x0[1];  //100
        x1[2] = x0[2] + delta_t * x0[3];  //0.1
        x1[3] = x0[3];  //100
        x1[4] = x0[4] + delta_t * x0[5];  //0.01
        x1[5] = x0[5];  //100
    }
    double delta_t;
};

template<class T>
void xyz2pyd(T xyz[3], T pyd[3]) {
    // /*
    //  * 工具函数：将 xyz 转化为 pitch、yaw、distance
    //  */
    // pyd[0] = ceres::atan2(xyz[2], ceres::sqrt(xyz[0]*xyz[0]+xyz[1]*xyz[1]));  // pitch
    // pyd[1] = ceres::atan2(xyz[1], xyz[0]);  // yaw
    // pyd[2] = ceres::sqrt(xyz[0]*xyz[0]+xyz[1]*xyz[1]+xyz[2]*xyz[2]);  // distance

    // 就使用xyz
    pyd[0] = xyz[0];
    pyd[1] = xyz[1];
    pyd[2] = xyz[2];
}

struct Measure {
    /*
     * 工具函数的类封装
     */
    template<class T>
    void operator()(const T x[6], T y[3]) {
        T x_[3] = {x[0], x[2], x[4]};
        xyz2pyd(x_, y);
    }
};

/**
 * @brief 记录装甲板坐标信息
 */
struct Armor
{
    char id;                                // 装甲板id（TODO: 与key功能重合，后续考虑删除）
    bool is_tracking;                       // 是否跟踪
    double time_stamp;                      // 时间戳
    double area;                            // 装甲板面积
    std::string key;                        // 装甲板key
    std::vector<cv::Point2f> positions2d;   // 装甲板角点二维坐标
    cv::Point2f center2d;                   // 装甲板二维坐标中心点
    Eigen::Vector3d center3d_cam;           // 相机坐标系下的坐标
    Eigen::Vector3d center3d_world;         // 世界坐标系下的坐标
    Eigen::Vector3d euler;                  // pnp解算出的欧拉角
    Eigen::Vector3d center3d_filter;        // 滤波值
    Eigen::Vector3d center3d_predict;       // 预测值
    Eigen::Vector3d velocity;               // 速度
    Eigen::Vector2d angle;                  // pitch yaw
};
struct EKF_param {
    Eigen::Matrix<double, 6, 6> Q;     // 预测过程协方差
    Eigen::Matrix<double, 3, 3> R;     // 观测过程协方差
};

class ArmorTracker{
private:
    AdaptiveEKF<6, 3> ekf;  // 创建ekf
    enum State {
        DETECTING,      //检测中
        TRACKING,       //跟踪中
        TEMP_LOST,      //临时丢失
    } tracker_state;    //跟踪状态

public:
    ArmorTracker(   EKF_param param, double max_lost_time_, double max_lost_distance_,
                    int lost_count_threshold_, int match_count_threshold_);
    void init(const Armor& src, double timestmp);
    Armor pre_armor;                    //上一次装甲板
    double pre_timestamp;               //上次装甲板时间戳
    EKF_param ekf_param;                //ekf 参数

    double max_lost_time;               //最大丢失时间
    double max_lost_distance;           //最大距离

    int lost_count;                     //丢失次数
    int lost_count_threshold;           //丢失次数阈值
    int matched_count;                  //匹配成功次数
    int matched_count_threshold;        //匹配成功次数阈值

    void update(const std::vector<Armor> & src, double timestmp);
    bool predictTargetArmor(double shoot_v);
};

#endif // ARMOR_TRACKER__COORDSOLVER_HPP