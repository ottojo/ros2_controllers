// Copyright 2017 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <array>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include "controller_parameter_server/parameter_server.hpp"

#include "hardware_interface/robot_hardware.hpp"

#include "lifecycle_msgs/msg/state.hpp"

#include "rclcpp/rclcpp.hpp"

#include "rcutils/get_env.h"

#include "ros_controllers/joint_trajectory_controller.hpp"

#include "std_msgs/msg/string.hpp"

#include "test_robot_hardware/test_robot_hardware.hpp"

using lifecycle_msgs::msg::State;

void
spin(rclcpp::executors::MultiThreadedExecutor * exe)
{
  exe->spin();
}

class TestTrajectoryController : public ::testing::Test
{
protected:
  static void SetUpTestCase()
  {
    rclcpp::init(0, nullptr);
  }

  void SetUp()
  {
    test_robot = std::make_shared<test_robot_hardware::TestRobotHardware>();
    test_robot->init();
    joint_names = {{test_robot->joint_name1, test_robot->joint_name2, test_robot->joint_name3}};
    op_mode = {{test_robot->write_op_handle_name1}};

    pub_node = std::make_shared<rclcpp::Node>("trajectory_publisher");
    trajectory_publisher = pub_node->create_publisher<trajectory_msgs::msg::JointTrajectory>(
      controller_name + "/joint_trajectory");
  }

  static void TearDownTestCase()
  {
    rclcpp::shutdown();
  }

  /// Publish trajectory msgs with multiple points
  /**
   *  time_from_start - delay between each points
   *  points - vector of trajectories. Each trajectory consists of 3 joints
   */
  void publish(
    const builtin_interfaces::msg::Duration & time_from_start,
    const std::vector<std::array<double, 3>> & points)
  {
    int wait_count = 0;
    auto topic = trajectory_publisher->get_topic_name();
    while (pub_node->count_subscribers(topic) == 0) {
      if (wait_count >= 5) {
        auto error_msg =
          std::string("publishing to ") + topic + " but no node subscribes to it";
        throw std::runtime_error(error_msg);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      ++wait_count;
    }

    auto traj_msg_ptr = std::make_shared<trajectory_msgs::msg::JointTrajectory>();
    std::vector<std::string> joint_names {
      test_robot->joint_name1, test_robot->joint_name2, test_robot->joint_name3
    };
    traj_msg_ptr->joint_names = joint_names;
    traj_msg_ptr->header.stamp.sec = 0;
    traj_msg_ptr->header.stamp.nanosec = 0;
    traj_msg_ptr->points.resize(points.size());

    builtin_interfaces::msg::Duration duration_msg;
    duration_msg.sec = time_from_start.sec;
    duration_msg.nanosec = time_from_start.nanosec;
    rclcpp::Duration duration(duration_msg);
    rclcpp::Duration duration_total(duration_msg);

    size_t index = 0;
    for (; index < points.size(); ++index) {
      traj_msg_ptr->points[index].time_from_start.sec =
        duration_total.nanoseconds() / 1e9;
      traj_msg_ptr->points[index].time_from_start.nanosec =
        duration_total.nanoseconds();
      traj_msg_ptr->points[index].positions.resize(3);
      traj_msg_ptr->points[index].positions[0] = points[index][0];
      traj_msg_ptr->points[index].positions[1] = points[index][1];
      traj_msg_ptr->points[index].positions[2] = points[index][2];
      duration_total = duration_total + duration;
    }

    trajectory_publisher->publish(traj_msg_ptr);
  }

  std::string controller_name = "test_joint_trajectory_controller";

  std::shared_ptr<test_robot_hardware::TestRobotHardware> test_robot;
  std::vector<std::string> joint_names;
  std::vector<std::string> op_mode;

  rclcpp::Node::SharedPtr pub_node;
  rclcpp::Publisher<trajectory_msgs::msg::JointTrajectory>::SharedPtr trajectory_publisher;
};

TEST_F(TestTrajectoryController, wrong_initialization) {
  auto uninitialized_robot = std::make_shared<test_robot_hardware::TestRobotHardware>();
  auto traj_controller = std::make_shared<ros_controllers::JointTrajectoryController>(
    joint_names, op_mode);
  auto ret = traj_controller->init(uninitialized_robot, controller_name);
  if (ret != controller_interface::CONTROLLER_INTERFACE_RET_SUCCESS) {
    FAIL();
  }

  auto unconfigured_state = traj_controller->get_lifecycle_node()->configure();
  EXPECT_EQ(State::PRIMARY_STATE_UNCONFIGURED, unconfigured_state.id());
}

TEST_F(TestTrajectoryController, correct_initialization) {
  auto initialized_robot = std::make_shared<test_robot_hardware::TestRobotHardware>();
  initialized_robot->init();
  auto traj_controller = std::make_shared<ros_controllers::JointTrajectoryController>(
    joint_names, op_mode);
  auto ret = traj_controller->init(initialized_robot, controller_name);
  if (ret != controller_interface::CONTROLLER_INTERFACE_RET_SUCCESS) {
    FAIL();
  }

  auto inactive_state = traj_controller->get_lifecycle_node()->configure();
  EXPECT_EQ(State::PRIMARY_STATE_INACTIVE, inactive_state.id());
  EXPECT_EQ(1.1, initialized_robot->pos1);
  EXPECT_EQ(2.2, initialized_robot->pos2);
  EXPECT_EQ(3.3, initialized_robot->pos3);
}

TEST_F(TestTrajectoryController, configuration) {
  auto traj_controller = std::make_shared<ros_controllers::JointTrajectoryController>(
    joint_names, op_mode);
  auto ret = traj_controller->init(test_robot, controller_name);
  if (ret != controller_interface::CONTROLLER_INTERFACE_RET_SUCCESS) {
    FAIL();
  }

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(traj_controller->get_lifecycle_node()->get_node_base_interface());
  auto future_handle_ = std::async(std::launch::async, spin, &executor);

  auto state = traj_controller->get_lifecycle_node()->configure();
  ASSERT_EQ(state.id(), State::PRIMARY_STATE_INACTIVE);

  // send msg
  builtin_interfaces::msg::Duration time_from_start;
  time_from_start.sec = 1;
  time_from_start.nanosec = 0;
  std::vector<std::array<double, 3>> points {{{3.3, 4.4, 5.5}}};
  publish(time_from_start, points);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  traj_controller->update();
  test_robot->write();

  // no change in hw position
  EXPECT_NE(3.3, test_robot->pos1);
  EXPECT_NE(4.4, test_robot->pos2);
  EXPECT_NE(5.5, test_robot->pos3);

  executor.cancel();
}

TEST_F(TestTrajectoryController, activation) {
  auto traj_controller = std::make_shared<ros_controllers::JointTrajectoryController>(
    joint_names, op_mode);
  auto ret = traj_controller->init(test_robot, controller_name);
  if (ret != controller_interface::CONTROLLER_INTERFACE_RET_SUCCESS) {
    FAIL();
  }

  auto traj_lifecycle_node = traj_controller->get_lifecycle_node();
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(traj_lifecycle_node->get_node_base_interface());

  auto state = traj_lifecycle_node->configure();
  ASSERT_EQ(state.id(), State::PRIMARY_STATE_INACTIVE);

  state = traj_lifecycle_node->activate();
  ASSERT_EQ(state.id(), State::PRIMARY_STATE_ACTIVE);

  // wait for the subscriber and publisher to completely setup
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // send msg
  builtin_interfaces::msg::Duration time_from_start;
  time_from_start.sec = 1;
  time_from_start.nanosec = 0;
  std::vector<std::array<double, 3>> points {{{3.3, 4.4, 5.5}}};
  publish(time_from_start, points);
  // wait for msg is be published to the system
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  executor.spin_once();

  traj_controller->update();
  test_robot->write();

  // change in hw position
  EXPECT_EQ(3.3, test_robot->pos1);
  EXPECT_EQ(4.4, test_robot->pos2);
  EXPECT_EQ(5.5, test_robot->pos3);

  executor.cancel();
}

TEST_F(TestTrajectoryController, reactivation) {
  auto traj_controller = std::make_shared<ros_controllers::JointTrajectoryController>(
    joint_names, op_mode);
  auto ret = traj_controller->init(test_robot, controller_name);
  if (ret != controller_interface::CONTROLLER_INTERFACE_RET_SUCCESS) {
    FAIL();
  }

  auto traj_lifecycle_node = traj_controller->get_lifecycle_node();
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(traj_lifecycle_node->get_node_base_interface());

  auto state = traj_lifecycle_node->configure();
  ASSERT_EQ(state.id(), State::PRIMARY_STATE_INACTIVE);

  state = traj_lifecycle_node->activate();
  ASSERT_EQ(state.id(), State::PRIMARY_STATE_ACTIVE);

  // wait for the subscriber and publisher to completely setup
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // send msg
  builtin_interfaces::msg::Duration time_from_start;
  time_from_start.sec = 1;
  time_from_start.nanosec = 0;
  // *INDENT-OFF*
  std::vector<std::array<double, 3>> points {
    {{3.3, 4.4, 5.5}},
    {{7.7, 8.8, 9.9}},
    {{10.10, 11.11, 12.12}}
  };
  // *INDENT-ON*
  publish(time_from_start, points);
  // wait for msg is be published to the system
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  executor.spin_once();

  traj_controller->update();
  test_robot->write();

  // deactivated
  // wait so controller process the second point when deactivated
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  state = traj_lifecycle_node->deactivate();
  ASSERT_EQ(state.id(), State::PRIMARY_STATE_INACTIVE);
  traj_controller->update();
  test_robot->write();

  // no change in hw position
  EXPECT_EQ(3.3, test_robot->pos1);
  EXPECT_EQ(4.4, test_robot->pos2);
  EXPECT_EQ(5.5, test_robot->pos3);

  // reactivated
  // wait so controller process the third point when reactivated
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  state = traj_lifecycle_node->activate();
  ASSERT_EQ(state.id(), State::PRIMARY_STATE_ACTIVE);
  traj_controller->update();
  test_robot->write();

  // change in hw position to 3rd point
  EXPECT_EQ(10.10, test_robot->pos1);
  EXPECT_EQ(11.11, test_robot->pos2);
  EXPECT_EQ(12.12, test_robot->pos3);

  executor.cancel();
}

TEST_F(TestTrajectoryController, cleanup) {
  auto traj_controller = std::make_shared<ros_controllers::JointTrajectoryController>(
    joint_names, op_mode);
  auto ret = traj_controller->init(test_robot, controller_name);
  if (ret != controller_interface::CONTROLLER_INTERFACE_RET_SUCCESS) {
    FAIL();
  }

  auto traj_lifecycle_node = traj_controller->get_lifecycle_node();
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(traj_lifecycle_node->get_node_base_interface());

  auto state = traj_lifecycle_node->configure();
  ASSERT_EQ(State::PRIMARY_STATE_INACTIVE, state.id());

  state = traj_lifecycle_node->activate();
  ASSERT_EQ(State::PRIMARY_STATE_ACTIVE, state.id());

  // wait for the subscriber and publisher to completely setup
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // send msg
  builtin_interfaces::msg::Duration time_from_start;
  time_from_start.sec = 1;
  time_from_start.nanosec = 0;
  std::vector<std::array<double, 3>> points {{{3.3, 4.4, 5.5}}};
  publish(time_from_start, points);
  // wait for msg is be published to the system
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  executor.spin_once();

  traj_controller->update();
  test_robot->write();

  state = traj_lifecycle_node->deactivate();
  ASSERT_EQ(State::PRIMARY_STATE_INACTIVE, state.id());
  traj_controller->update();
  test_robot->write();

  state = traj_lifecycle_node->cleanup();
  ASSERT_EQ(State::PRIMARY_STATE_UNCONFIGURED, state.id());
  traj_controller->update();
  test_robot->write();

  // shouild be home pose again
  EXPECT_EQ(1.1, test_robot->pos1);
  EXPECT_EQ(2.2, test_robot->pos2);
  EXPECT_EQ(3.3, test_robot->pos3);

  executor.cancel();
}

TEST_F(TestTrajectoryController, correct_initialization_with_config_file) {
  const char * config_file;
  auto ret_get_env = rcutils_get_env("config_file", &config_file);
  if (ret_get_env) {
    FAIL() << ret_get_env;
  }
  // has to be converted to std::string to make it work on windows
  // must be related to STL containers and windows
  std::string file_path = config_file;
  auto ps = std::make_shared<controller_parameter_server::ParameterServer>();
  ps->load_parameters(file_path);

  auto traj_controller = std::make_shared<ros_controllers::JointTrajectoryController>();
  auto ret = traj_controller->init(test_robot, controller_name);
  if (ret != controller_interface::CONTROLLER_INTERFACE_RET_SUCCESS) {
    FAIL();
  }

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(ps);
  auto traj_lifecycle_node = traj_controller->get_lifecycle_node();
  executor.add_node(traj_lifecycle_node->get_node_base_interface());

  auto future_handle = std::async(
    std::launch::async, [&executor]() -> void {
      executor.spin();
    });

  auto state = traj_lifecycle_node->configure();
  ASSERT_EQ(State::PRIMARY_STATE_INACTIVE, state.id());
  EXPECT_EQ(1.1, test_robot->pos1);
  EXPECT_EQ(2.2, test_robot->pos2);
  EXPECT_EQ(3.3, test_robot->pos3);

  state = traj_lifecycle_node->activate();
  ASSERT_EQ(State::PRIMARY_STATE_ACTIVE, state.id());

  // wait for the subscriber and publisher to completely setup
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // send msg
  builtin_interfaces::msg::Duration time_from_start;
  time_from_start.sec = 1;
  time_from_start.nanosec = 0;
  // *INDENT-OFF*
  std::vector<std::array<double, 3>> points {
    {{3.3, 4.4, 5.5}},
    {{7.7, 8.8, 9.9}},
    {{10.10, 11.11, 12.12}}
  };
  // *INDENT-ON*
  publish(time_from_start, points);
  // wait for msg is be published to the system
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  traj_controller->update();
  test_robot->write();

  // deactivated
  // wait so controller process the second point when deactivated
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  state = traj_lifecycle_node->deactivate();
  ASSERT_EQ(state.id(), State::PRIMARY_STATE_INACTIVE);
  traj_controller->update();
  test_robot->write();

  // no change in hw position
  EXPECT_EQ(3.3, test_robot->pos1);
  EXPECT_EQ(4.4, test_robot->pos2);
  EXPECT_EQ(5.5, test_robot->pos3);

  // cleanup
  state = traj_lifecycle_node->cleanup();
  traj_controller->update();
  test_robot->write();
  ASSERT_EQ(State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(1.1, test_robot->pos1);
  EXPECT_EQ(2.2, test_robot->pos2);
  EXPECT_EQ(3.3, test_robot->pos3);

  state = traj_lifecycle_node->configure();
  ASSERT_EQ(State::PRIMARY_STATE_INACTIVE, state.id());
  executor.cancel();
}
