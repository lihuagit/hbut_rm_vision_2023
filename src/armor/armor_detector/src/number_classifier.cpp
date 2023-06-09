/**
 * @file number_classifier.cpp
 * @brief 数字分类器 -- 识别数字类，参考陈君的代码
 * @author lihuagit (3190995951@qq.com)
 * @version 1.0
 * @date 2023-03-11
 * 
 */
#include "armor_detector/number_classifier.h"

namespace armor_auto_aim
{

/**
 * @brief Construct a new Number Classifier:: Number Classifier object
 * @param model_path 模型路径
 * @param label_path 模型label路径
 * @param thre 置信度阈值
 */
NumberClassifier::NumberClassifier(
  const std::string & model_path, const std::string & label_path, const double thre)
: threshold(thre)
{
  net_ = cv::dnn::readNetFromONNX(model_path);

  std::ifstream label_file(label_path);
  std::string line;
  while (std::getline(label_file, line)) {
    class_names_.push_back(line[0]);
  }
}

/**
 * @brief 从图像中提取数字区域，并进行透视变换
 * @param src 源图像
 * @param armors 装甲板数组
 */
void NumberClassifier::extractNumbers(const cv::Mat & src, std::vector<Armor> & armors)
{
  // Light length in image
  const int light_length = 12;
  // Image size after warp
  const int warp_height = 28;
  const int small_armor_width = 32;
  const int large_armor_width = 54;
  // Number ROI size
  const cv::Size roi_size(20, 28);

  for (auto & armor : armors) {
    // Warp perspective transform
    cv::Point2f lights_vertices[4] = {
      armor.left_light.bottom, armor.left_light.top, armor.right_light.top,
      armor.right_light.bottom};

    const int top_light_y = (warp_height - light_length) / 2 - 1;
    const int bottom_light_y = top_light_y + light_length;
    const int warp_width = armor.armor_type == SMALL ? small_armor_width : large_armor_width;
    cv::Point2f target_vertices[4] = {
      cv::Point(0, bottom_light_y),
      cv::Point(0, top_light_y),
      cv::Point(warp_width - 1, top_light_y),
      cv::Point(warp_width - 1, bottom_light_y),
    };
    cv::Mat number_image;
    auto rotation_matrix = cv::getPerspectiveTransform(lights_vertices, target_vertices);
    cv::warpPerspective(src, number_image, rotation_matrix, cv::Size(warp_width, warp_height));

    // Get ROI
    number_image =
      number_image(cv::Rect(cv::Point((warp_width - roi_size.width) / 2, 0), roi_size));

    // Binarize
    cv::cvtColor(number_image, number_image, cv::COLOR_RGB2GRAY);
    cv::threshold(number_image, number_image, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    armor.number_img = number_image;
  }
}

/**
 * @brief 对数字区域进行分类
 * @param armors 装甲板数组，已经确定数字区域，并存在number_img
 */
void NumberClassifier::doClassify(std::vector<Armor> & armors)
{
  for (auto & armor : armors) {
    cv::Mat image = armor.number_img.clone();

    // Normalize
    image = image / 255.0;

    // Create blob from image
    cv::Mat blob;
    cv::dnn::blobFromImage(image, blob, 1., cv::Size(28, 20));

    // Set the input blob for the neural network
    net_.setInput(blob);
    // Forward pass the image blob through the model
    cv::Mat outputs = net_.forward();

    // Do softmax
    float max_prob = *std::max_element(outputs.begin<float>(), outputs.end<float>());
    cv::Mat softmax_prob;
    cv::exp(outputs - max_prob, softmax_prob);
    float sum = static_cast<float>(cv::sum(softmax_prob)[0]);
    softmax_prob /= sum;

    double confidence;
    cv::Point class_id_point;
    minMaxLoc(softmax_prob.reshape(1, 1), nullptr, &confidence, nullptr, &class_id_point);
    int label_id = class_id_point.x;

    armor.confidence = confidence;
    armor.number = class_names_[label_id];

    std::stringstream result_ss;
    result_ss << armor.number << ":_" << std::fixed << std::setprecision(1)
              << armor.confidence * 100.0 << "%";
    armor.classfication_result = result_ss.str();
  }

  armors.erase(
    std::remove_if(
      armors.begin(), armors.end(),
      [this](const Armor & armor) {
        if (armor.confidence < threshold || armor.number == 'N') {
          return true;
        }

        bool mismatch = false;
        if (armor.armor_type == LARGE) {
          mismatch = armor.number == 'O' || armor.number == '2' || armor.number == '3' ||
                     armor.number == '4' || armor.number == '5';
        } else if (armor.armor_type == SMALL) {
          mismatch = armor.number == '1' || armor.number == 'B' || armor.number == 'G';
        }
        return mismatch;
      }),
    armors.end());
}

}  // namespace armor_auto_aim
