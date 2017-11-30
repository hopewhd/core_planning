/*
 *  Copyright (c) 2015, Nagoya University
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Autoware nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <ros/ros.h>
#include <ros/console.h>
#include <visualization_msgs/MarkerArray.h>
#include <std_msgs/Int32.h>
#include <tf/transform_datatypes.h>

#include <iostream>
#include <vector>
#include <string>

#include "waypoint_follower/libwaypoint_follower.h"
#include "autoware_msgs/LaneArray.h"
#include "autoware_msgs/ConfigLaneStop.h"
#include "autoware_msgs/traffic_light.h"

namespace
{
ros::Publisher g_local_mark_pub;
ros::Publisher g_global_mark_pub;

constexpr int32_t TRAFFIC_LIGHT_RED = 0;
constexpr int32_t TRAFFIC_LIGHT_GREEN = 1;
constexpr int32_t TRAFFIC_LIGHT_UNKNOWN = 2;

std_msgs::ColorRGBA _initial_color;
std_msgs::ColorRGBA _global_color;
std_msgs::ColorRGBA g_local_color;
const double g_global_alpha = 0.2;
const double g_local_alpha = 1.0;
int _closest_waypoint = -1;
visualization_msgs::MarkerArray g_global_marker_array;
visualization_msgs::MarkerArray g_local_waypoints_marker_array;

bool g_config_manual_detection = true;
bool g_use_velocity_visualizer;
double g_graph_height_ratio;
std::vector<double> g_graph_color = { 0.0, 1.0, 0.0, 0.5 };

enum class ChangeFlag : int32_t
{
  straight,
  right,
  left,

  unknown = -1,
};

typedef std::underlying_type<ChangeFlag>::type ChangeFlagInteger;

void publishLocalMarker()
{
  visualization_msgs::MarkerArray marker_array;

  // insert local marker
  marker_array.markers.insert(marker_array.markers.end(), g_local_waypoints_marker_array.markers.begin(),
                              g_local_waypoints_marker_array.markers.end());

  g_local_mark_pub.publish(marker_array);
}

void publishGlobalMarker()
{
  visualization_msgs::MarkerArray marker_array;

  // insert global marker
  marker_array.markers.insert(marker_array.markers.end(), g_global_marker_array.markers.begin(),
                              g_global_marker_array.markers.end());

  g_global_mark_pub.publish(marker_array);
}

void createGlobalLaneArrayVelocityMarker(const autoware_msgs::LaneArray& lane_waypoints_array)
{
  visualization_msgs::MarkerArray tmp_marker_array;
  // display by markers the velocity of each waypoint.
  visualization_msgs::Marker velocity_marker;
  velocity_marker.header.frame_id = "map";
  velocity_marker.header.stamp = ros::Time();
  velocity_marker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
  velocity_marker.action = visualization_msgs::Marker::ADD;
  velocity_marker.scale.z = 0.4;
  velocity_marker.color.r = 1;
  velocity_marker.color.g = 1;
  velocity_marker.color.b = 1;
  velocity_marker.color.a = 1.0;
  velocity_marker.frame_locked = true;

  int count = 1;
  for (auto lane : lane_waypoints_array.lanes)
  {
    velocity_marker.ns = "global_velocity_lane_" + std::to_string(count);
    for (int i = 0; i < static_cast<int>(lane.waypoints.size()); i++)
    {
      // std::cout << _waypoints[i].GetX() << " " << _waypoints[i].GetY() << " " << _waypoints[i].GetZ() << " " <<
      // _waypoints[i].GetVelocity_kmh() << std::endl;
      velocity_marker.id = i;
      geometry_msgs::Point relative_p;
      relative_p.y = 0.5;
      velocity_marker.pose.position = calcAbsoluteCoordinate(relative_p, lane.waypoints[i].pose.pose);
      velocity_marker.pose.position.z += 0.2;

      // double to string
      std::string vel = std::to_string(mps2kmph(lane.waypoints[i].twist.twist.linear.x));
      velocity_marker.text = vel.erase(vel.find_first_of(".") + 2);

      tmp_marker_array.markers.push_back(velocity_marker);
    }
    count++;
  }

  g_global_marker_array.markers.insert(g_global_marker_array.markers.end(), tmp_marker_array.markers.begin(),
                                       tmp_marker_array.markers.end());
}

void createGlobalLaneArrayChangeFlagMarker(const autoware_msgs::LaneArray& lane_waypoints_array)
{
  visualization_msgs::MarkerArray tmp_marker_array;
  // display by markers the velocity of each waypoint.
  visualization_msgs::Marker marker;
  marker.header.frame_id = "map";
  marker.header.stamp = ros::Time();
  marker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
  marker.action = visualization_msgs::Marker::ADD;
  marker.scale.z = 0.4;
  marker.color.r = 1;
  marker.color.g = 1;
  marker.color.b = 1;
  marker.color.a = 1.0;
  marker.frame_locked = true;

  int count = 1;
  for (auto lane : lane_waypoints_array.lanes)
  {
    marker.ns = "global_change_flag_lane_" + std::to_string(count);
    for (int i = 0; i < static_cast<int>(lane.waypoints.size()); i++)
    {
      // std::cout << _waypoints[i].GetX() << " " << _waypoints[i].GetY() << " " << _waypoints[i].GetZ() << " " <<
      // _waypoints[i].GetVelocity_kmh() << std::endl;
      marker.id = i;
      geometry_msgs::Point relative_p;
      relative_p.x = -0.1;
      marker.pose.position = calcAbsoluteCoordinate(relative_p, lane.waypoints[i].pose.pose);
      marker.pose.position.z += 0.2;

      // double to string
      std::string str = "";
      if (lane.waypoints[i].change_flag == static_cast<ChangeFlagInteger>(ChangeFlag::straight))
      {
        str = "S";
      }
      else if (lane.waypoints[i].change_flag == static_cast<ChangeFlagInteger>(ChangeFlag::right))
      {
        str = "R";
      }
      else if (lane.waypoints[i].change_flag == static_cast<ChangeFlagInteger>(ChangeFlag::left))
      {
        str = "L";
      }
      else if (lane.waypoints[i].change_flag == static_cast<ChangeFlagInteger>(ChangeFlag::unknown))
      {
        str = "U";
      }

      marker.text = str;

      tmp_marker_array.markers.push_back(marker);
    }
    count++;
  }

  g_global_marker_array.markers.insert(g_global_marker_array.markers.end(), tmp_marker_array.markers.begin(),
                                       tmp_marker_array.markers.end());
}

void createLocalWaypointVelocityMarker(std_msgs::ColorRGBA color, int closest_waypoint,
                                       const autoware_msgs::lane& lane_waypoint)
{
  // display by markers the velocity of each waypoint.
  visualization_msgs::Marker velocity;
  velocity.header.frame_id = "map";
  velocity.header.stamp = ros::Time();
  velocity.ns = "local_waypoint_velocity";
  velocity.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
  velocity.action = visualization_msgs::Marker::ADD;
  velocity.scale.z = 0.4;
  velocity.color = color;
  velocity.frame_locked = true;

  for (int i = 0; i < static_cast<int>(lane_waypoint.waypoints.size()); i++)
  {
    velocity.id = i;
    geometry_msgs::Point relative_p;
    relative_p.y = -0.65;
    velocity.pose.position = calcAbsoluteCoordinate(relative_p, lane_waypoint.waypoints[i].pose.pose);
    velocity.pose.position.z += 0.2;

    // double to string
    std::ostringstream oss;
    // oss << std::fixed << std::setprecision(2) << mps2kmph(lane_waypoint.waypoints[i].twist.twist.linear.x) << "
    // km/h";
    oss << std::fixed << std::setprecision(1) << mps2kmph(lane_waypoint.waypoints[i].twist.twist.linear.x);
    velocity.text = oss.str();

    g_local_waypoints_marker_array.markers.push_back(velocity);
  }
}

void createGlobalLaneArrayMarker(std_msgs::ColorRGBA color, const autoware_msgs::LaneArray& lane_waypoints_array)
{
  visualization_msgs::Marker lane_waypoint_marker;
  lane_waypoint_marker.header.frame_id = "map";
  lane_waypoint_marker.header.stamp = ros::Time();
  lane_waypoint_marker.ns = "global_lane_array_marker";
  lane_waypoint_marker.type = visualization_msgs::Marker::LINE_STRIP;
  lane_waypoint_marker.action = visualization_msgs::Marker::ADD;
  lane_waypoint_marker.scale.x = 1.0;
  lane_waypoint_marker.color = color;
  lane_waypoint_marker.frame_locked = true;

  int count = 0;
  for (auto lane : lane_waypoints_array.lanes)
  {
    lane_waypoint_marker.points.clear();
    lane_waypoint_marker.id = count;

    for (auto el : lane.waypoints)
    {
      geometry_msgs::Point point;
      point = el.pose.pose.position;
      lane_waypoint_marker.points.push_back(point);
    }
    g_global_marker_array.markers.push_back(lane_waypoint_marker);
    count++;
  }
}

void createLocalVelocityBarGraphMarker(const autoware_msgs::lane& lane_waypoint)
{
  visualization_msgs::Marker velocity_bar_graph_marker;
  velocity_bar_graph_marker.header.frame_id = "map";
  velocity_bar_graph_marker.header.stamp = ros::Time();
  velocity_bar_graph_marker.ns = "local_velocity_bar_graph_marker";
  velocity_bar_graph_marker.type = visualization_msgs::Marker::CYLINDER;
  velocity_bar_graph_marker.action = visualization_msgs::Marker::ADD;
  velocity_bar_graph_marker.scale.x = 0.2;
  velocity_bar_graph_marker.scale.y = 0.2;
  velocity_bar_graph_marker.color.r = g_graph_color[0];
  velocity_bar_graph_marker.color.g = g_graph_color[1];
  velocity_bar_graph_marker.color.b = g_graph_color[2];
  velocity_bar_graph_marker.color.a = g_graph_color[3];
  velocity_bar_graph_marker.frame_locked = true;

  unsigned int count = 0;
  for (auto el : lane_waypoint.waypoints)
  {
    double bar_graph_height = g_graph_height_ratio * el.twist.twist.linear.x;
    velocity_bar_graph_marker.id = count++;
    velocity_bar_graph_marker.pose = el.pose.pose;
    velocity_bar_graph_marker.pose.position.z += bar_graph_height / 2.0;
    // When the the cylinder height is 0 or less, a warning occurs in RViz.
    velocity_bar_graph_marker.scale.z = fabs(bar_graph_height) + 1e-6;
    g_local_waypoints_marker_array.markers.push_back(velocity_bar_graph_marker);
  }
}

void createLocalVelocityLineGraphMarker(const autoware_msgs::lane& lane_waypoint)
{
  visualization_msgs::Marker velocity_line_graph_marker;
  velocity_line_graph_marker.header.frame_id = "map";
  velocity_line_graph_marker.header.stamp = ros::Time();
  velocity_line_graph_marker.ns = "local_velocity_line_graph_marker";
  velocity_line_graph_marker.type = visualization_msgs::Marker::LINE_STRIP;
  velocity_line_graph_marker.action = visualization_msgs::Marker::ADD;
  velocity_line_graph_marker.scale.x = 0.25;
  velocity_line_graph_marker.color.r = g_graph_color[0];
  velocity_line_graph_marker.color.g = g_graph_color[1];
  velocity_line_graph_marker.color.b = g_graph_color[2];
  velocity_line_graph_marker.color.a = g_graph_color[3];
  velocity_line_graph_marker.frame_locked = true;

  for (auto el : lane_waypoint.waypoints)
  {
    geometry_msgs::Point point = el.pose.pose.position;
    point.z += g_graph_height_ratio * el.twist.twist.linear.x;
    velocity_line_graph_marker.points.push_back(point);
  }
  g_local_waypoints_marker_array.markers.push_back(velocity_line_graph_marker);
}

void createGlobalLaneArrayOrientationMarker(const autoware_msgs::LaneArray& lane_waypoints_array)
{
  visualization_msgs::MarkerArray tmp_marker_array;
  visualization_msgs::Marker lane_waypoint_marker;
  lane_waypoint_marker.header.frame_id = "map";
  lane_waypoint_marker.header.stamp = ros::Time();
  lane_waypoint_marker.type = visualization_msgs::Marker::ARROW;
  lane_waypoint_marker.action = visualization_msgs::Marker::ADD;
  lane_waypoint_marker.scale.x = 0.25;
  lane_waypoint_marker.scale.y = 0.05;
  lane_waypoint_marker.scale.z = 0.05;
  lane_waypoint_marker.color.r = 1.0;
  lane_waypoint_marker.color.a = 1.0;
  lane_waypoint_marker.frame_locked = true;

  int count = 1;
  for (auto lane : lane_waypoints_array.lanes)
  {
    lane_waypoint_marker.ns = "global_lane_waypoint_orientation_marker_" + std::to_string(count);

    for (int i = 0; i < static_cast<int>(lane.waypoints.size()); i++)
    {
      lane_waypoint_marker.id = i;
      lane_waypoint_marker.pose = lane.waypoints[i].pose.pose;
      tmp_marker_array.markers.push_back(lane_waypoint_marker);
    }
    count++;
  }

  g_global_marker_array.markers.insert(g_global_marker_array.markers.end(), tmp_marker_array.markers.begin(),
                                       tmp_marker_array.markers.end());
}

void createLocalPathMarker(std_msgs::ColorRGBA color, const autoware_msgs::lane& lane_waypoint)
{
  visualization_msgs::Marker lane_waypoint_marker;
  lane_waypoint_marker.header.frame_id = "map";
  lane_waypoint_marker.header.stamp = ros::Time();
  lane_waypoint_marker.ns = "local_path_marker";
  lane_waypoint_marker.id = 0;
  lane_waypoint_marker.type = visualization_msgs::Marker::LINE_STRIP;
  lane_waypoint_marker.action = visualization_msgs::Marker::ADD;
  lane_waypoint_marker.scale.x = 0.2;
  lane_waypoint_marker.color = color;
  lane_waypoint_marker.frame_locked = true;

  for (unsigned int i = 0; i < lane_waypoint.waypoints.size(); i++)
  {
    geometry_msgs::Point point;
    point = lane_waypoint.waypoints[i].pose.pose.position;
    lane_waypoint_marker.points.push_back(point);
  }
  g_local_waypoints_marker_array.markers.push_back(lane_waypoint_marker);
}

void createLocalPointMarker(const autoware_msgs::lane& lane_waypoint)
{
  visualization_msgs::Marker lane_waypoint_marker;
  lane_waypoint_marker.header.frame_id = "map";
  lane_waypoint_marker.header.stamp = ros::Time();
  lane_waypoint_marker.ns = "local_point_marker";
  lane_waypoint_marker.id = 0;
  lane_waypoint_marker.type = visualization_msgs::Marker::CUBE_LIST;
  lane_waypoint_marker.action = visualization_msgs::Marker::ADD;
  lane_waypoint_marker.scale.x = 0.2;
  lane_waypoint_marker.scale.y = 0.2;
  lane_waypoint_marker.scale.z = 0.2;
  lane_waypoint_marker.color.r = 1.0;
  lane_waypoint_marker.color.a = 1.0;
  lane_waypoint_marker.frame_locked = true;

  for (unsigned int i = 0; i < lane_waypoint.waypoints.size(); i++)
  {
    geometry_msgs::Point point;
    point = lane_waypoint.waypoints[i].pose.pose.position;
    lane_waypoint_marker.points.push_back(point);
  }
  g_local_waypoints_marker_array.markers.push_back(lane_waypoint_marker);
}

void lightCallback(const autoware_msgs::traffic_lightConstPtr& msg)
{
  std_msgs::ColorRGBA global_color;
  global_color.a = g_global_alpha;

  std_msgs::ColorRGBA local_color;
  local_color.a = g_local_alpha;

  switch (msg->traffic_light)
  {
    case TRAFFIC_LIGHT_RED:
      global_color.r = 1.0;
      _global_color = global_color;
      local_color.r = 1.0;
      g_local_color = local_color;
      break;
    case TRAFFIC_LIGHT_GREEN:
      global_color.g = 1.0;
      _global_color = global_color;
      local_color.g = 1.0;
      g_local_color = local_color;
      break;
    case TRAFFIC_LIGHT_UNKNOWN:
      global_color.b = 1.0;
      global_color.g = 0.7;
      _global_color = global_color;
      local_color.b = 1.0;
      local_color.g = 0.7;
      g_local_color = local_color;
      break;
    default:
      ROS_ERROR("unknown traffic_light");
      return;
  }
}

void receiveAutoDetection(const autoware_msgs::traffic_lightConstPtr& msg)
{
  if (!g_config_manual_detection)
    lightCallback(msg);
}

void receiveManualDetection(const autoware_msgs::traffic_lightConstPtr& msg)
{
  if (g_config_manual_detection)
    lightCallback(msg);
}

void configParameter(const autoware_msgs::ConfigLaneStopConstPtr& msg)
{
  g_config_manual_detection = msg->manual_detection;
}

void laneArrayCallback(const autoware_msgs::LaneArrayConstPtr& msg)
{
  g_global_marker_array.markers.clear();
  createGlobalLaneArrayVelocityMarker(*msg);
  // createGlobalLaneArrayMarker(_global_color, *msg);
  createGlobalLaneArrayOrientationMarker(*msg);
  createGlobalLaneArrayChangeFlagMarker(*msg);
  publishGlobalMarker();
}

void finalCallback(const autoware_msgs::laneConstPtr& msg)
{
  g_local_waypoints_marker_array.markers.clear();
  if (_closest_waypoint != -1)
    createLocalWaypointVelocityMarker(g_local_color, _closest_waypoint, *msg);
  createLocalPathMarker(g_local_color, *msg);
  createLocalPointMarker(*msg);
  if (g_use_velocity_visualizer)
  {
    createLocalVelocityBarGraphMarker(*msg);
    createLocalVelocityLineGraphMarker(*msg);
  }
  publishLocalMarker();
}

void closestCallback(const std_msgs::Int32ConstPtr& msg)
{
  _closest_waypoint = msg->data;
}
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "waypoints_marker_publisher");
  ros::NodeHandle nh;
  ros::NodeHandle private_nh("~");

  private_nh.param<bool>("use_velocity_visualizer", g_use_velocity_visualizer, false);
  private_nh.param<double>("graph_height_ratio", g_graph_height_ratio, 1.0);
  private_nh.param<std::vector<double> >("velocity_bar_graph_color", g_graph_color, g_graph_color);

  // subscribe traffic light
  ros::Subscriber light_sub = nh.subscribe("light_color", 10, receiveAutoDetection);
  ros::Subscriber light_managed_sub = nh.subscribe("light_color_managed", 10, receiveManualDetection);

  // subscribe global waypoints
  ros::Subscriber lane_array_sub = nh.subscribe("lane_waypoints_array", 10, laneArrayCallback);
  ros::Subscriber traffic_array_sub = nh.subscribe("traffic_waypoints_array", 10, laneArrayCallback);

  // subscribe local waypoints
  ros::Subscriber final_sub = nh.subscribe("final_waypoints", 10, finalCallback);
  ros::Subscriber closest_sub = nh.subscribe("closest_waypoint", 10, closestCallback);

  // subscribe config
  ros::Subscriber config_sub = nh.subscribe("config/lane_stop", 10, configParameter);

  g_local_mark_pub = nh.advertise<visualization_msgs::MarkerArray>("local_waypoints_mark", 10, true);
  g_global_mark_pub = nh.advertise<visualization_msgs::MarkerArray>("global_waypoints_mark", 10, true);

  // initialize path color
  _initial_color.g = 0.7;
  _initial_color.b = 1.0;
  _global_color = _initial_color;
  _global_color.a = g_global_alpha;
  g_local_color = _initial_color;
  g_local_color.a = g_local_alpha;

  ros::spin();
}
