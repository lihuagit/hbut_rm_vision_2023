// Copyright 2022 Chen Jun
// Licensed under the MIT License.

#ifndef RUNE_DETECTOR__DETECTOR_HPP_
#define RUNE_DETECTOR__DETECTOR_HPP_

// OpenCV
#include <opencv2/core/types.hpp>
#include <opencv2/opencv.hpp>

// STD
#include <utility>
#include <vector>

namespace rm_power_rune
{
class Detector
{
public:
  cv::Mat binarize(const cv::Mat & src);

  cv::Mat floodfill();

  bool findArmor(cv::RotatedRect & armor);

  bool findCenter(cv::Point2f & center);

  enum class Color {
    RED,
    BLUE,
  } detect_color;

  int bin_thresh;

  int min_armor_area, max_armor_area;
  double min_armor_ratio, max_armor_ratio;
  double min_strip_ratio, max_strip_ratio;

  std::array<cv::Point2f, 4> sorted_pts;

private:
  cv::Mat bin_;
  cv::Mat floodfilled_;

  cv::RotatedRect armor_;
  cv::Point2f center_;
};

}  // namespace rm_power_rune

#endif  // RUNE_DETECTOR__DETECTOR_HPP_
