// Copyright 2019 kvedder@seas.upenn.edu
// School of Engineering and Applied Sciences,
// University of Pennsylvania
//
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ========================================================================

#include "cs/path_finding/astar.h"

#include <algorithm>
#include <limits>
#include <string>

#include "cs/motion_planning/pid.h"
#include "cs/util/physics.h"
#include "shared/util/array_util.h"

#include "config_reader/macros.h"

namespace cs {
namespace motion_planning {

namespace params {
CONFIG_FLOAT(rotation_drive_threshold, "control.rotation_drive_threshold");
CONFIG_FLOAT(rotation_p, "control.rotation_p");
CONFIG_FLOAT(translation_p, "control.translation_p");
CONFIG_FLOAT(goal_deadzone_tra, "control.goal_deadzone_tra");
CONFIG_FLOAT(goal_deadzone_rot, "control.goal_deadzone_rot");

CONFIG_FLOAT(kMaxTraAcc, "limits.kMaxTraAcc");
CONFIG_FLOAT(kMaxTraVel, "limits.kMaxTraVel");
CONFIG_FLOAT(kMaxRotAcc, "limits.kMaxRotAcc");
CONFIG_FLOAT(kMaxRotVel, "limits.kMaxRotVel");

CONFIG_FLOAT(robot_radius, "pf.kRobotRadius");
CONFIG_FLOAT(safety_margin, "pf.kSafetyMargin");

CONFIG_FLOAT(translational_cost_scale_factor, "od.kTranslationCostScaleFactor");

}  // namespace params

util::Twist PIDController::DriveToPose(const util::Map& dynamic_map,
                                       const util::Pose& waypoint) {
  est_world_pose_ = state_estimator_.GetEstimatedPose();
  est_velocity_ = state_estimator_.GetEstimatedVelocity();
  complete_map_ = map_.Merge(dynamic_map);

  const auto proposed_command = ProposeCommand(waypoint);
  const auto limited_command = ApplyCommandLimits(proposed_command);

  if (!IsCommandColliding(limited_command)) {
    return limited_command;
  }

  static constexpr int kNumAlt = 11;
  auto costs = array_util::MakeArray<kNumAlt>(0.0f);
  std::array<util::Twist, kNumAlt> alternate_commands = {{
      {0, 0, 0},
      {0, 0, params::CONFIG_kMaxRotVel},
      {0, 0, -params::CONFIG_kMaxRotVel},
      {est_velocity_.tra, params::CONFIG_kMaxRotVel},
      {est_velocity_.tra, -params::CONFIG_kMaxRotVel},
      {est_velocity_.tra / 2, params::CONFIG_kMaxRotVel},
      {est_velocity_.tra / 2, -params::CONFIG_kMaxRotVel},
      {est_velocity_.tra / 2, params::CONFIG_kMaxRotVel / 2},
      {est_velocity_.tra / 2, -params::CONFIG_kMaxRotVel / 2},
      {est_velocity_.tra, params::CONFIG_kMaxRotVel / 2},
      {est_velocity_.tra, -params::CONFIG_kMaxRotVel / 2},
  }};

  for (auto& cmd : alternate_commands) {
    cmd = ApplyCommandLimits(cmd);
  }

  for (size_t i = 0; i < alternate_commands.size(); ++i) {
    costs[i] = AlternateCommandCost(limited_command, alternate_commands[i]);
  }

  for (size_t i = 0; i < alternate_commands.size(); ++i) {
    std::cout << "Alternate command: " << alternate_commands[i]
              << " Cost: " << costs[i] << std::endl;
  }

  size_t best_idx = array_util::ArgMin(costs);

  std::cout << "Best: " << best_idx << std::endl;
  // If every command causes a crash, we command full brakes, limited by
  // acceleration constraints.
  if (costs[best_idx] >= std::numeric_limits<float>::max()) {
    best_idx = 0;
  }
  std::cout << "Command Colliding. Proposed: " << proposed_command
            << " Alternate: " << alternate_commands[best_idx] << std::endl;
  return alternate_commands[best_idx];
}

float PIDController::AlternateCommandCost(const util::Twist& desired,
                                          const util::Twist& alternate) const {
  const auto delta_pose = desired - alternate;
  if (IsCommandColliding(alternate)) {
    return std::numeric_limits<float>::max();
  }
  return math_util::Sq(delta_pose.tra.lpNorm<1>() *
                       params::CONFIG_translational_cost_scale_factor) +
         fabs(delta_pose.rot);
}

bool PIDController::AtPose(const util::Pose& pose) const {
  const Eigen::Vector2f tra_delta = est_world_pose_.tra - pose.tra;
  if (tra_delta.squaredNorm() < Sq(params::CONFIG_goal_deadzone_tra)) {
    const float waypoint_angle_delta =
        math_util::AngleDiff(est_world_pose_.rot, pose.rot);
    NP_FINITE(waypoint_angle_delta);
    if (std::abs(waypoint_angle_delta) < params::CONFIG_goal_deadzone_rot) {
      return true;
    }
  }
  return false;
}

util::Twist PIDController::ProposeCommand(const util::Pose& waypoint) const {
  const Eigen::Vector2f tra_delta = est_world_pose_.tra - waypoint.tra;

  // Handle final turn to face waypoint's angle.
  if (tra_delta.squaredNorm() < Sq(params::CONFIG_goal_deadzone_tra)) {
    const float waypoint_angle_delta =
        math_util::AngleDiff(est_world_pose_.rot, waypoint.rot);
    if (std::abs(waypoint_angle_delta) < params::CONFIG_goal_deadzone_rot) {
      return {0, 0, 0};
    }
    // Rotate in place to final pose rotation.
    return {0, 0, -waypoint_angle_delta * params::CONFIG_rotation_p};
  }

  const float robot_angle = est_world_pose_.rot;
  const Eigen::Vector2f robot_to_waypoint_delta =
      waypoint.tra - est_world_pose_.tra;
  const float robot_to_waypoint_angle =
      std::atan2(robot_to_waypoint_delta.y(), robot_to_waypoint_delta.x());

  NP_FINITE(robot_angle);
  NP_FINITE_VEC2(robot_to_waypoint_delta);
  NP_FINITE(robot_to_waypoint_angle);

  const float robot_to_waypoint_angle_delta =
      math_util::AngleDiff(robot_angle, robot_to_waypoint_angle);
  NP_FINITE_MSG(robot_to_waypoint_angle_delta,
                "Robot angle: " << robot_angle << " robot to waypoint angle: "
                                << robot_to_waypoint_angle);
  NP_CHECK_VAL(robot_to_waypoint_angle_delta >= -(kPi + kEpsilon) &&
                   robot_to_waypoint_angle_delta <= (kPi + kEpsilon),
               robot_to_waypoint_angle_delta);
  float x = 0;
  if (std::abs(robot_to_waypoint_angle_delta) <
      params::CONFIG_rotation_drive_threshold) {
    x = robot_to_waypoint_delta.norm();
  }
  return {x * params::CONFIG_translation_p,
          0,
          -robot_to_waypoint_angle_delta * params::CONFIG_rotation_p};
}

util::Twist PIDController::ApplyCommandLimits(util::Twist c) const {
  return util::physics::ApplyCommandLimits(c,
                                           state_estimator_.GetLaserTimeDelta(),
                                           est_velocity_,
                                           params::CONFIG_kMaxTraVel,
                                           params::CONFIG_kMaxTraAcc,
                                           params::CONFIG_kMaxRotVel,
                                           params::CONFIG_kMaxRotAcc);
}

bool PIDController::IsCommandColliding(
    const util::Twist& commanded_velocity) const {
  const float rollout_duration = state_estimator_.GetLaserTimeDelta();
  const float& robot_radius = params::CONFIG_robot_radius;
  const float& safety_margin = params::CONFIG_safety_margin;

  static constexpr bool kDebug = false;
  const TrajectoryRollout tr(
      est_world_pose_, est_velocity_, commanded_velocity, rollout_duration);
  for (const auto& w : complete_map_.walls) {
    if (tr.IsColliding(w, robot_radius + safety_margin)) {
      if (kDebug) {
        ROS_INFO("Current command: (%f, %f), %f",
                 commanded_velocity.tra.x(),
                 commanded_velocity.tra.y(),
                 commanded_velocity.rot);
        ROS_INFO("End pose: (%f, %f), %f",
                 tr.final_pose.tra.x(),
                 tr.final_pose.tra.y(),
                 tr.final_pose.rot);
        ROS_INFO("Colliding with observed wall: (%f, %f) <-> (%f, %f)",
                 w.p1.x(),
                 w.p1.y(),
                 w.p2.x(),
                 w.p2.y());
      }
      return true;
    }
  }
  return false;
}

}  // namespace motion_planning
}  // namespace cs
