/**
 * @file serial_node.cpp
 * @brief 
 * @author lihuagit (3190995951@qq.com)
 * @version 1.0
 * @date 2023-03-04
 * 
 */

#include "serial/serial_node.h"


SerialDriver::SerialDriver(const rclcpp::NodeOptions & options)
: Node("rm_serial_driver", options),
  owned_ctx_{new IoContext(2)},
  serial_driver_{new drivers::serial_driver::SerialDriver(*owned_ctx_)}
{
  RCLCPP_INFO(get_logger(), "Start SerialDriver!");

  getParams();
  
  // Create Publisher
  joint_state_pub_ = this->create_publisher<sensor_msgs::msg::JointState>(
    "/joint_states", rclcpp::QoS(rclcpp::KeepLast(1)));

  try {
    serial_driver_->init_port(device_name_, *device_config_);
    if (!serial_driver_->port()->is_open()) {
      serial_driver_->port()->open();
      receive_thread_ = std::thread(&SerialDriver::receiveData, this);
    }
  } catch (const std::exception & ex) {
    RCLCPP_ERROR(
      get_logger(), "Error creating serial port: %s - %s", device_name_.c_str(), ex.what());
    throw ex;
  }

  // Create Subscription
  target_sub_ = this->create_subscription<armor_interfaces::msg::TargetInfo>(
    "/processor/target", rclcpp::SensorDataQoS(),
    std::bind(&SerialDriver::sendData, this, std::placeholders::_1));
}

SerialDriver::~SerialDriver()
{
  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }

  if (serial_driver_->port()->is_open()) {
    serial_driver_->port()->close();
  }

  if (owned_ctx_) {
    owned_ctx_->waitForExit();
  }
}

/**
 * @brief 发送数据
 * @param msg 
 */
void SerialDriver::sendData(const armor_interfaces::msg::TargetInfo::SharedPtr msg)
{
  try {
     std::vector<uint8_t> data;
    //TODO:自定义发送格式
    
    /* 创建一个JSON数据对象(链表头结点) */
    
    char* str = NULL;
    
    //"date":[x,y];
    cJSON* cjson_date=cJSON_CreateArray();
    cJSON_AddItemToArray(cjson_date,cJSON_CreateNumber(-(msg->euler.x)));
    cJSON_AddItemToArray(cjson_date,cJSON_CreateNumber(-(msg->euler.y)));

    //dat
    cJSON* cjson_dat=cJSON_CreateObject();
    cJSON_AddItemToObject(cjson_dat,"date",cjson_date);
    cJSON_AddStringToObject(cjson_dat,"mode","visual");

    //send
    cJSON* cjson_send=cJSON_CreateObject();
    cJSON_AddStringToObject(cjson_send,"cmd","ctr_mode");

    cJSON_AddItemToObject(cjson_send,"dat",cjson_dat);

    str = cJSON_PrintUnformatted(cjson_send);
    int str_len = strlen(str);
    for(int i=0;i<str_len;i++)
    {
      data.push_back(str[i]);
    }
    data.push_back('\n');

    // if(data.size()>100) 
    {
        serial_driver_->port()->send(data);
        // auto final_time = this->now();
        // auto latency = (final_time - start_time).seconds() * 1000;
		    //  RCLCPP_INFO_STREAM(this->get_logger(), "detectArmors used: " << latency << "ms");
        RCLCPP_INFO(get_logger(), "SerialDriver sending data: %s", data.data());
        RCLCPP_INFO(get_logger(), "SerialDriver sending data: %d", str_len);
    }
  } catch (const std::exception & ex) {
    RCLCPP_ERROR(get_logger(), "Error while sending data: %s", ex.what());
    reopenPort();
  }
}

/**
 * @brief 不断接收电控数据
 */
void SerialDriver::receiveData()
{
  std::vector<uint8_t> data;

  while (rclcpp::ok()) {
    try {
      data.resize(200);
      int rec_len = serial_driver_->port()->receive(data);

      if(rec_len >= 150) continue;

      data[rec_len-1] = '\0';

      double imu_yaw = 0, imu_pitch = 0;

      // 解析json
      cJSON *root = cJSON_Parse((char*)data.data());
      if(!root)
      {
        RCLCPP_INFO(get_logger(), "Error before: [%s]",cJSON_GetErrorPtr());
        continue;
      }
      else
      {
        cJSON *dat = cJSON_GetObjectItem(root, "dat");
        if(!dat)
        {
          RCLCPP_INFO(get_logger(), "Error before: [%s]",cJSON_GetErrorPtr());
          continue;
        }
        else
        {
          imu_yaw = cJSON_GetObjectItem(dat, "imu_yaw")->valuedouble;
          imu_pitch = cJSON_GetObjectItem(dat, "imu_pitch")->valuedouble;
          // RCLCPP_INFO(get_logger(), "imu_yaw: %f", imu_yaw);
          //  RCLCPP_INFO(get_logger(), "imu_pitch: %f", imu_pitch);
        }
      }

      // TODO:收到电控数据
      // RCLCPP_INFO(get_logger(), "SerialDriver receiving data: %s", data.data());
      // // RCLCPP_INFO(get_logger(), "SerialDriver receiving len: %d", rec_len);

      if(isnan(imu_yaw) || isnan(imu_pitch)) continue;
    try
      {
        sensor_msgs::msg::JointState joint_state;
        joint_state.header.stamp = this->now();
        joint_state.name.push_back("pitch_joint");
        joint_state.name.push_back("yaw_joint");
        joint_state.position.push_back(imu_pitch);
        joint_state.position.push_back(imu_yaw);
        joint_state_pub_->publish(joint_state);
      }catch (const std::exception & ex) {
        RCLCPP_ERROR(get_logger(), "Error while receiving data: %s", ex.what());
      }

    } catch (const std::exception & ex) {
      RCLCPP_ERROR(get_logger(), "Error while receiving data: %s", ex.what());
      reopenPort();
    }
  }
}


/**
 * @brief 重启串口
 */
void SerialDriver::reopenPort()
{
  RCLCPP_WARN(get_logger(), "Attempting to reopen port");
  try {
    if (serial_driver_->port()->is_open()) {
      serial_driver_->port()->close();
    }
    serial_driver_->port()->open();
    RCLCPP_INFO(get_logger(), "Successfully reopened port");
  } catch (const std::exception & ex) {
    RCLCPP_ERROR(get_logger(), "Error while reopening port: %s", ex.what());
    if (rclcpp::ok()) {
      rclcpp::sleep_for(std::chrono::seconds(1));
      reopenPort();
    }
  }
}

void SerialDriver::getParams()
{
  using FlowControl = drivers::serial_driver::FlowControl;
  using Parity = drivers::serial_driver::Parity;
  using StopBits = drivers::serial_driver::StopBits;

  uint32_t baud_rate{};
  auto fc = FlowControl::NONE;
  auto pt = Parity::NONE;
  auto sb = StopBits::ONE;

  try {
    device_name_ = declare_parameter<std::string>("device_name", "");
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The device name provided was invalid");
    throw ex;
  }

  try {
    baud_rate = declare_parameter<int>("baud_rate", 0);
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The baud_rate provided was invalid");
    throw ex;
  }

  try {
    const auto fc_string = declare_parameter<std::string>("flow_control", "none");

    if (fc_string == "none") {
      fc = FlowControl::NONE;
    } else if (fc_string == "hardware") {
      fc = FlowControl::HARDWARE;
    } else if (fc_string == "software") {
      fc = FlowControl::SOFTWARE;
    } else {
      throw std::invalid_argument{
        "The flow_control parameter must be one of: none, software, or hardware."};
    }
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The flow_control provided was invalid");
    throw ex;
  }

  try {
    const auto pt_string = declare_parameter<std::string>("parity", "none");

    if (pt_string == "none") {
      pt = Parity::NONE;
    } else if (pt_string == "odd") {
      pt = Parity::ODD;
    } else if (pt_string == "even") {
      pt = Parity::EVEN;
    } else {
      throw std::invalid_argument{"The parity parameter must be one of: none, odd, or even."};
    }
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The parity provided was invalid");
    throw ex;
  }

  try {
    const auto sb_string = declare_parameter<std::string>("stop_bits", "1");

    if (sb_string == "1" || sb_string == "1.0") {
      sb = StopBits::ONE;
    } else if (sb_string == "1.5") {
      sb = StopBits::ONE_POINT_FIVE;
    } else if (sb_string == "2" || sb_string == "2.0") {
      sb = StopBits::TWO;
    } else {
      throw std::invalid_argument{"The stop_bits parameter must be one of: 1, 1.5, or 2."};
    }
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The stop_bits provided was invalid");
    throw ex;
  }

  device_config_ =
    std::make_unique<drivers::serial_driver::SerialPortConfig>(baud_rate, fc, pt, sb);
}


#include "rclcpp_components/register_node_macro.hpp"

// Register the component with class_loader.
// This acts as a sort of entry point, allowing the component to be discoverable when its library
// is being loaded into a running process.
RCLCPP_COMPONENTS_REGISTER_NODE(SerialDriver)