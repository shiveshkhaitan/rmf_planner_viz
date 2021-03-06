/*
 * Copyright (C) 2020 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <SFML/Graphics.hpp>
#include <imgui.h>

#include <rmf_planner_viz/draw/Schedule.hpp>

#include <rmf_traffic/geometry/Circle.hpp>
#include <rmf_traffic/schedule/Database.hpp>

#include <rmf_fleet_adapter/agv/parse_graph.hpp>
#include <rmf_performance_tests/rmf_performance_tests.hpp>
#include <rmf_performance_tests/Scenario.hpp>
#include <rmf_planner_viz/draw/Graph.hpp>

#include <Eigen/Geometry>

#include <iostream>

#include "imgui-SFML.h"
#include "planner_debug.hpp"

const std::size_t NotObstacleID = std::numeric_limits<std::size_t>::max();

int main(int argc, char* argv[])
{
  sf::Font font;
  if (!font.loadFromFile("./build/rmf_planner_viz/fonts/OpenSans-Bold.ttf"))
  {
    std::cout <<
              "Failed to load font. Make sure you run the executable from the colcon directory"
              << std::endl;
    return -1;
  }

  if (argc < 2)
  {
    std::cout << "Please provide scenario file name" << std::endl;
    return 0;
  }


  rmf_performance_tests::scenario::Description scenario;
  try
  {
    parse(argv[1], scenario);
  }
  catch (std::runtime_error& e)
  {
    std::cout << e.what() << std::endl;
    return 0;
  }

  using namespace std::chrono_literals;

  const auto& plan_robot = scenario.robots.find(scenario.plan.robot);
  if (plan_robot == scenario.robots.end())
  {
    std::cout << "Plan robot [" << scenario.plan.robot <<
              "]'s traits and profile missing" << std::endl;
    return 0;
  }

  const auto get_wp =
      [&](const rmf_traffic::agv::Graph& graph, const std::string& name)
      {
        return graph.find_waypoint(name)->index();
      };

  const auto start_time = rmf_traffic::Time(rmf_traffic::Duration(0));

  const auto database = std::make_shared<rmf_traffic::schedule::Database>();

  std::vector<rmf_traffic::schedule::Participant> obstacles;

  for (const auto& obstacle : scenario.obstacle_plans)
  {
    const auto& robot = scenario.robots.find(obstacle.robot);

    if (robot == scenario.robots.end())
    {
      std::cout << "Robot [" << obstacle.robot <<
                "] is missing traits and profile. Using traits and profile of plan_robot."
                << std::endl;

      rmf_traffic::agv::Planner planner = rmf_traffic::agv::Planner{
          plan_robot->second,
          {nullptr}
      };

      obstacles.emplace_back(
          rmf_performance_tests::add_obstacle(
              planner, database,
              {
                  start_time + std::chrono::seconds(obstacle.initial_time),
                  get_wp(plan_robot->second.graph(), obstacle.initial_waypoint),
                  obstacle.initial_orientation * M_PI / 180.0
              },
              get_wp(plan_robot->second.graph(), obstacle.goal)
          )
      );
    }
    else
    {
      rmf_traffic::agv::Planner planner = rmf_traffic::agv::Planner{
          robot->second,
          {nullptr}
      };

      obstacles.emplace_back(
          rmf_performance_tests::add_obstacle(
              planner, database,
              {
                  start_time + std::chrono::seconds(obstacle.initial_time),
                  get_wp(robot->second.graph(), obstacle.initial_waypoint),
                  obstacle.initial_orientation * M_PI / 180.0
              },
              get_wp(robot->second.graph(), obstacle.goal)
          )
      );
    }
  }

  for (const auto& obstacle : scenario.obstacle_routes)
  {
    const auto& robot = scenario.robots.at(obstacle.robot);

    obstacles.emplace_back(
        rmf_performance_tests::add_obstacle(
            database,
            robot.vehicle_traits().profile(),
            obstacle.route));
  }

  const auto& plan = scenario.plan;

  rmf_planner_viz::draw::Graph graph_0_drawable(
      plan_robot->second.graph(), 1.0, font);
  std::vector<std::string> map_names = graph_0_drawable.get_map_names();
  std::string chosen_map = argv[2];
  if (graph_0_drawable.current_map())
    chosen_map = *graph_0_drawable.current_map();
  using namespace std::chrono_literals;

  const auto obstacle_validator =
      rmf_traffic::agv::ScheduleRouteValidator::make(
          database, NotObstacleID, plan_robot->second.vehicle_traits().profile());

  rmf_traffic::agv::Planner planner_0(
      plan_robot->second,
      rmf_traffic::agv::Planner::Options(obstacle_validator));

  auto traits = planner_0.get_configuration().vehicle_traits();
  auto plan_participant = rmf_traffic::schedule::make_participant(
      rmf_traffic::schedule::ParticipantDescription{
          "participant_0",
          "test_trajectory",
          rmf_traffic::schedule::ParticipantDescription::Rx::Responsive,
          traits.profile()
      },
      database);
  /// Setup participants

  // set plans for the participants
  using namespace std::chrono_literals;

  std::vector<rmf_traffic::agv::Planner::Start> starts;
  starts.emplace_back(start_time + std::chrono::seconds(
      plan.initial_time),
                      get_wp(plan_robot->second.graph(), plan.initial_waypoint),
                      plan.initial_orientation);

  rmf_traffic::agv::Planner::Goal goal(get_wp(
      plan_robot->second.graph(), plan.goal));

  rmf_planner_viz::draw::Schedule schedule_drawable(
        database, 0.25, chosen_map, start_time + 0s);

  plan_participant.set(planner_0.plan(starts, goal)->get_itinerary());

  sf::RenderWindow app_window(
        sf::VideoMode(1250, 1028),
        "Test Trajectory",
        sf::Style::Default);

  app_window.resetGLStates();

  const auto bounds = schedule_drawable.bounds();

  rmf_planner_viz::draw::Fit fit({bounds}, 0.02);
  std::cout << "initial bounds:\n"
            << " -- min: " << graph_0_drawable.bounds().min.transpose()
            << "\n -- max: " << graph_0_drawable.bounds().max.transpose() << std::endl;
  ImGui::SFML::Init(app_window);

  auto current_time = start_time;
  current_time += std::chrono::milliseconds(std::stoi(argv[3]));
  sf::Clock deltaClock;

  while (app_window.isOpen())
  {
    current_time += std::chrono::milliseconds(std::stoi(argv[4]));
    sf::Event event;
    while (app_window.pollEvent(event))
    {
      ImGui::SFML::ProcessEvent(event);

      if (event.type == sf::Event::Closed)
      {
        return 0;
      }

      if (event.type == sf::Event::Resized)
      {
        sf::FloatRect visibleArea(0, 0, event.size.width, event.size.height);
        app_window.setView(sf::View(visibleArea));
      }
    }

    ImGui::SFML::Update(app_window, deltaClock.restart());

    graph_0_drawable.choose_map(chosen_map);

    app_window.clear();

    schedule_drawable.timespan(current_time);

    sf::RenderStates states;
    fit.apply_transform(states.transform, app_window.getSize());
    app_window.draw(graph_0_drawable, states);
    app_window.draw(schedule_drawable, states);

    ImGui::SFML::Render();

    app_window.display();
  }
}
