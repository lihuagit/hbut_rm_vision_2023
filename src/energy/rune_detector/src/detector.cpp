// Copyright (c) 2022 ChenJun
// Licensed under the MIT License.

#include "rune_detector/detector.hpp"

#include <array>
#include <iostream>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>

namespace rm_power_rune
{
cv::Mat Detector::binarize(const cv::Mat & src)
{
  // Split the image into three channels
  std::vector<cv::Mat> channels;
  cv::split(src, channels);

  // Subtract between the red and blue channels
  cv::Mat diff;
  if (detect_color == Color::RED) {
    // 0-R 1-G 2-B
    cv::subtract(channels[0], channels[2], diff);
  } else {
    cv::subtract(channels[2], channels[0], diff);
  }

  // Threshold the image
  cv::threshold(diff, bin_, bin_thresh, 255, cv::THRESH_BINARY);

  // Close morphological operation
  cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
  cv::morphologyEx(bin_, bin_, cv::MORPH_CLOSE, kernel);

  return bin_;
}

cv::Mat Detector::floodfill()
{
  // Inverse the image
  cv::Mat inv;
  cv::bitwise_not(bin_, inv);

  cv::Mat tmp = inv.clone();
  // Floodfill the image from top left corner
  floodFill(inv, cv::Point(0, 0), cv::Scalar(0));
  floodfilled_ = inv;

  // If there is closed area at the top left corner, which causes the floodfill to fail,
  if (countNonZero(floodfilled_) > floodfilled_.rows * floodfilled_.cols * 0.5) {
    // Floodfill again from the bottom right corner
    floodFill(tmp, cv::Point(bin_.cols - 1, bin_.rows - 1), cv::Scalar(0));
    floodfilled_ = tmp;
  }

  return floodfilled_;
}

bool Detector::findArmor(cv::RotatedRect & armor)
{
  // Find the contours
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(floodfilled_, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  // TODO
  cv::Mat bin_clone = floodfilled_.clone();
  cv::cvtColor(bin_clone, bin_clone, cv::COLOR_GRAY2RGB);

  std::vector<cv::RotatedRect> armors;
  std::vector<cv::RotatedRect> strips;
  for (const auto & contour : contours) {
    // Approximate the contour
    auto rect = cv::minAreaRect(contour);

    // Use area to check if the contour is part of a rune
    if (rect.size.area() > min_armor_area) {
      // Use ratio to distinguish rect is armor or not
      float ratio = rect.size.width > rect.size.height ? rect.size.width / rect.size.height
                                                       : rect.size.height / rect.size.width;

      cv::putText(
        bin_clone, std::to_string(rect.size.area()), rect.center, cv::FONT_HERSHEY_SIMPLEX, 0.5,
        cv::Scalar(0, 255, 0), 2);

      if (min_armor_ratio < ratio && ratio < max_armor_ratio && rect.size.area() < max_armor_area) {
        armors.emplace_back(rect);
      } else {
        strips.emplace_back(rect);
      }
    } else {
      continue;
    }
  }

  // cv::imshow("armor", bin_clone);
  // cv::waitKey(1);

  // Find available armor
  auto armor_it = armors.begin();
  while (armor_it != armors.end()) {
    bool available = true;

    auto strip_it = strips.begin();
    while (strip_it != strips.end()) {
      cv::Point2f armor_pts[4], strip_pts[4];
      armor_it->points(armor_pts);
      strip_it->points(strip_pts);

      // Calculate the min distance between the armor and the strip
      double min_dist = std::numeric_limits<double>::max();
      for (const auto & armor_pt : armor_pts) {
        for (const auto & strip_pt : strip_pts) {
          auto dist = cv::norm(armor_pt - strip_pt);
          min_dist = std::min(min_dist, dist);
        }
      }

      // Normalize the distance
      min_dist /= std::max(armor_it->size.width, armor_it->size.height);

      // The armor is close enough to the strip
      if (min_dist < 0.25) {
        available = false;
        strip_it = strips.erase(strip_it);
      } else {
        strip_it++;
      }
    }

    if (!available) {
      armor_it = armors.erase(armor_it);
    } else {
      armor_it++;
    }
  }

  if (armors.size() == 1) {
    this->armor_ = armors[0];
    armor = armors[0];
    return true;
  } else {
    return false;
  }
}

bool Detector::findCenter(cv::Point2f & center)
{
  cv::Point2f armor_pts[4];
  armor_.points(armor_pts);

  // Judge which side is the long side
  if (cv::norm(armor_pts[0] - armor_pts[1]) > cv::norm(armor_pts[1] - armor_pts[2])) {
    // 0-1 is the long side
    sorted_pts = {armor_pts[0], armor_pts[1], armor_pts[2], armor_pts[3]};
  } else {
    // 1-2 is the long side
    sorted_pts = {armor_pts[1], armor_pts[2], armor_pts[3], armor_pts[0]};
  }

  // Judge which side the flowing water lights are on
  auto short_side = sorted_pts[1] - sorted_pts[2];

  auto getROI = [&](const std::vector<cv::Point> & roi_pts) -> cv::Mat {
    cv::Mat mask = cv::Mat::zeros(bin_.size(), CV_8UC1);
    std::vector<std::vector<cv::Point>> vpts = {roi_pts};
    cv::fillPoly(mask, vpts, cv::Scalar(255));
    return bin_.mul(mask);
  };

  std::vector<cv::Point> lights_roi1_pts = {
    sorted_pts[0] + 3 * short_side, sorted_pts[1] + 3 * short_side, sorted_pts[1], sorted_pts[0]};
  double sum1 = cv::sum(getROI(lights_roi1_pts))[0];

  std::vector<cv::Point> lights_roi2_pts = {
    sorted_pts[2] - 3 * short_side, sorted_pts[3] - 3 * short_side, sorted_pts[3], sorted_pts[2]};
  double sum2 = cv::sum(getROI(lights_roi2_pts))[0];

  if (sum1 > sum2) {
    // lights are near 0-1 side
  } else {
    // lights are near 1-2 side
    sorted_pts = {sorted_pts[2], sorted_pts[3], sorted_pts[0], sorted_pts[1]};
    short_side = sorted_pts[1] - sorted_pts[2];
  }

  // Get the roi of the center
  cv::Point2f roi_center = armor_.center + short_side * 6.0;
  cv::Mat mask = cv::Mat::zeros(bin_.size(), CV_8UC1);
  int radius = cv::norm(short_side) * 8.0;
  cv::circle(mask, roi_center, radius, cv::Scalar(255), -1);
  cv::Mat roi = bin_.mul(mask);

  // TODO
  cv::Mat bin_clone = bin_.clone();
  cv::cvtColor(bin_clone, bin_clone, cv::COLOR_GRAY2RGB);
  cv::circle(bin_clone, roi_center, radius, cv::Scalar(0, 255, 0), 2);

  // Find the center
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(roi, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  for (const auto & contour : contours) {
    cv::Rect rect = cv::boundingRect(contour);

    cv::putText(
      bin_clone, std::to_string(rect.area()), rect.br(), cv::FONT_HERSHEY_SIMPLEX, 0.5,
      cv::Scalar(0, 255, 0), 2);

    cv::putText(
      bin_clone, std::to_string(rect.size().aspectRatio()), rect.tl(), cv::FONT_HERSHEY_SIMPLEX, 0.5,
      cv::Scalar(0, 255, 0), 2);

    if (250 < rect.area() && rect.area() < 400) {
      center = center_ = (rect.br() + rect.tl()) * 0.5;
      return true;
    }
  }

  // TODO
  // cv::imshow("center", bin_clone);
  // cv::waitKey(1);

  return false;
}

}  // namespace rm_power_rune
