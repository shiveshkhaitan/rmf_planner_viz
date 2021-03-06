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

#include <rmf_planner_viz/draw/Graph.hpp>
#include <rmf_planner_viz/draw/Capsule.hpp>

#include <rmf_utils/optional.hpp>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>

#include <unordered_set>
#include <iostream>

namespace rmf_planner_viz {
namespace draw {

static const sf::Vector2f gTextScale(1.f/40.f, -1.f/40.f);

//==============================================================================
class Graph::Implementation
{
public:

  struct MapData
  {
    std::vector<Capsule> bi_lanes;
    std::vector<std::size_t> bi_indices;
    std::vector<sf::VertexArray> bi_lane_arrows;

    std::vector<Capsule> mono_lanes;
    std::vector<std::size_t> mono_indices;
    std::vector<sf::VertexArray> mono_lane_arrows;

    std::vector<sf::CircleShape> waypoints;
    std::unordered_map<std::size_t, sf::Text> waypoints_text;
    std::unordered_map<std::size_t, sf::Text> connector_waypoints_text;
    std::vector<Eigen::Vector2f> waypoint_p;
    std::vector<std::size_t> waypoint_indices;
  };

  static const sf::Color LaneEntryColor;
  static const sf::Color LaneExitColor;
  static const sf::Color WaypointColor;

  static Eigen::Vector2f inf()
  {
    const double infinity = std::numeric_limits<float>::infinity();
    return {infinity, infinity};
  }

  float waypoint_radius() const
  {
    return 0.30*lane_width;
  }

  float lane_width;
  std::unordered_map<std::string, MapData> data;
  rmf_utils::optional<std::string> current_map;
  Fit::Bounds bounds;

  float scale;
  sf::Vector2u left_top_border;
  sf::Vector2u right_bottom_border;

  rmf_utils::optional<Pick> selected;

  Implementation(
      const rmf_traffic::agv::Graph& graph,
      const float lane_width,
      const sf::Font& font)
  {
    this->lane_width = lane_width;
    std::unordered_map<std::size_t, std::unordered_set<std::size_t>> used_lanes;
    std::unordered_set<std::size_t> used_vertices;
    
    for (std::size_t i=0; i < graph.num_waypoints(); ++i)
    {
      const auto& waypoint = graph.get_waypoint(i);
      sf::Text text;
      text.setFont(font);
      text.setCharacterSize(24);
      if (waypoint.name())
      {
        std::string name = *waypoint.name();
        name += " (";
        name += std::to_string(waypoint.index());
        name += ")";
        text.setString(name);
      }
      else
        text.setString(std::to_string(waypoint.index()));
      
      text.setScale(gTextScale);
      
      sf::FloatRect text_rect = text.getLocalBounds();
      text.setOrigin(text_rect.width * 0.5f, text_rect.height * 0.5f);
      text.setPosition(waypoint.get_location().x(), waypoint.get_location().y());

      text.setFillColor(sf::Color(192, 192, 192));

      auto& map_data = data[waypoint.get_map_name()];
      map_data.waypoints_text.emplace(waypoint.index(), text);
    }

    for (std::size_t i=0; i < graph.num_lanes(); ++i)
    {
      const auto& lane = graph.get_lane(i);
      const auto j0 = lane.entry().waypoint_index();
      const auto j1 = lane.exit().waypoint_index();

      const auto& w0 = graph.get_waypoint(j0);
      const auto& w1 = graph.get_waypoint(j1);

      if (w0.get_map_name() != w1.get_map_name())
      {
        // add text that tells of a connection to another level
        auto& w0_map_data = data[w0.get_map_name()];
        auto& w0_text = w0_map_data.waypoints_text[j0];

        auto& w1_map_data = data[w1.get_map_name()];
        auto& w1_text = w1_map_data.waypoints_text[j1];
        std::string w1_str = w1_text.getString();
        
        sf::Text text;
        text.setFont(font);
        text.setCharacterSize(w0_text.getCharacterSize());
        text.setColor(sf::Color(144,238,144));
        text.setString("[" + w1.get_map_name() + "::" + w1_str + "]");

        sf::FloatRect text_rect = text.getLocalBounds();
        text.setOrigin(text_rect.width * 0.5f, text_rect.height * 0.5f);

        sf::FloatRect w0_text_rect = w0_text.getLocalBounds();
        text.setPosition(w0.get_location().x(), 
          w0.get_location().y() + w0_text_rect.height * gTextScale.y);
        text.setScale(gTextScale);
        
        w0_map_data.connector_waypoints_text.emplace(j0, text);
        continue;
      }

      const bool inserted = used_lanes[j0].insert(j1).second;

      if (!inserted)
        continue;

      auto& map_data = data[w0.get_map_name()];

      if (!current_map)
        current_map = w0.get_map_name();

      const bool bidirectional = static_cast<bool>(graph.lane_from(j1, j0));
      if (bidirectional)
        used_lanes[j1].insert(j0);

      const auto& p0 = w0.get_location();
      const auto& p1 = w1.get_location();

      for (const auto& p : {p0, p1})
      {
        for (std::size_t i=0; i < 2; ++i)
        {
          if (p[i] < bounds.min[i])
            bounds.min[i] = p[i];

          if (bounds.max[i] < p[i])
            bounds.max[i] = p[i];
        }
      }

      sf::Vertex v0;
      v0.position = sf::Vector2f(p0.x(), p0.y());
      v0.color = LaneEntryColor;

      sf::Vertex v1;
      v1.position = sf::Vector2f(p1.x(), p1.y());
      v1.color = bidirectional? LaneEntryColor : LaneExitColor;

      Capsule capsule(v0, v1, lane_width/2.0);
      if (bidirectional)
      {
        map_data.bi_lanes.push_back(std::move(capsule));
        map_data.bi_indices.push_back(i);
      }
      else
      {
        map_data.mono_lanes.push_back(std::move(capsule));
        map_data.mono_indices.push_back(i);
        map_data.bi_lane_arrows.push_back(add_lane_arrow(v0, v1));
      }

      const float r_wp = waypoint_radius();

      if (used_vertices.insert(j0).second)
      {
        sf::CircleShape w0_shape(r_wp);
        w0_shape.setOrigin(r_wp, r_wp);
        w0_shape.setPosition(p0.x(), p0.y());
        w0_shape.setFillColor(WaypointColor);
        map_data.waypoints.emplace_back(std::move(w0_shape));
        map_data.waypoint_p.push_back({p0.x(), p0.y()});
        map_data.waypoint_indices.push_back(j0);
      }

      if (used_vertices.insert(j1).second)
      {
        sf::CircleShape w1_shape(r_wp);
        w1_shape.setOrigin(r_wp, r_wp);
        w1_shape.setPosition(p1.x(), p1.y());
        w1_shape.setFillColor(WaypointColor);
        map_data.waypoints.emplace_back(std::move(w1_shape));
        map_data.waypoint_p.push_back({p1.x(), p1.y()});
        map_data.waypoint_indices.push_back(j1);
      }
    }

    bounds.min -= Eigen::Vector2f::Constant(lane_width/2.0);
    bounds.max += Eigen::Vector2f::Constant(lane_width/2.0);
  }

  void highlight(const Pick chosen)
  {
    change_color(
          chosen,
          sf::Color::Cyan,
          sf::Color::Yellow,
          sf::Color::Magenta);
  }

  void unhighlight(const Pick chosen)
  {
    change_color(
          chosen,
          LaneEntryColor,
          LaneExitColor,
          WaypointColor);
  }

  void change_color(
      Pick chosen,
      const sf::Color& lane_entry_color,
      const sf::Color& lane_exit_color,
      const sf::Color& waypoint_color)
  {
    if (chosen.type == ElementType::Waypoint)
    {
      for (auto& entry : data)
      {
        auto& map_data = entry.second;
        for (std::size_t i=0; i < map_data.waypoints.size(); ++i)
        {
          if (map_data.waypoint_indices[i] == chosen.index)
          {
            map_data.waypoints[i].setFillColor(waypoint_color);
            return;
          }
        }
      }
    }
    else
    {
      for (auto& entry : data)
      {
        auto& map_data = entry.second;
        for (std::size_t i=0; i < map_data.bi_lanes.size(); ++i)
        {
          if (map_data.bi_indices[i] == chosen.index)
          {
            map_data.bi_lanes[i]
                .set_start_color(lane_entry_color)
                .set_end_color(lane_entry_color);
            return;
          }
        }

        for (std::size_t i=0; i < map_data.mono_lanes.size(); ++i)
        {
          if (map_data.mono_indices[i] == chosen.index)
          {
            map_data.mono_lanes[i]
                .set_start_color(lane_entry_color)
                .set_end_color(lane_exit_color);
            return;
          }
        }
      }
    }
  }

  sf::VertexArray add_lane_arrow(const sf::Vertex& v0, const sf::Vertex& v1)
  {
    sf::VertexArray arr(sf::Triangles);
    sf::Vertex v;
    v.color = sf::Color::Red;

    // center
    sf::Vector2 center = (v0.position + v1.position) * 0.5f;
    sf::Vector2 diff = v1.position - v0.position;
    float lengthsq = diff.x * diff.x + diff.y * diff.y;
    float length = sqrt(lengthsq);
    auto diff_norm = diff / length;

    float center_spacing = 0.0625f;
    sf::Vector2 forward_vec = diff_norm * (0.5f + center_spacing);
    
    auto diff_perp = sf::Vector2(-diff_norm.y, diff_norm.x);
    float side = 0.25f;

    v.position = center + diff_perp * -side + center_spacing * diff_norm;
    arr.append(v);
    v.position = center + diff_perp * side + center_spacing * diff_norm;
    arr.append(v);
    v.position = center + forward_vec;
    arr.append(v);

    return arr;
  };
};

const sf::Color Graph::Implementation::LaneEntryColor = sf::Color::White;
const sf::Color Graph::Implementation::LaneExitColor = sf::Color(255/3, 255/3, 255/3);
const sf::Color Graph::Implementation::WaypointColor = sf::Color::Blue;

//==============================================================================
Graph::Graph(
    const rmf_traffic::agv::Graph& graph,
    const float lane_width,
    const sf::Font& font)
  : _pimpl(rmf_utils::make_impl<Implementation>(graph, lane_width, font))
{
  // Do nothing
}

//==============================================================================
bool Graph::choose_map(const std::string& name)
{
  const auto it = _pimpl->data.find(name);
  if (it == _pimpl->data.end())
  {
    _pimpl->current_map = rmf_utils::nullopt;
    return false;
  }

  _pimpl->current_map = name;
  return true;
}

//==============================================================================
const std::string* Graph::current_map() const
{
  if (_pimpl->current_map)
    return &_pimpl->current_map.value();

  return nullptr;
}

//==============================================================================
const Fit::Bounds& Graph::bounds() const
{
  return _pimpl->bounds;
}

//==============================================================================
rmf_utils::optional<Graph::Pick> Graph::pick(float x, float y) const
{
  if (!_pimpl->current_map)
    return rmf_utils::nullopt;

  const Eigen::Vector2f p_l(x, y);

  const float r_wp = _pimpl->waypoint_radius();

  if (p_l.x() < _pimpl->bounds.min.x() - r_wp)
    return rmf_utils::nullopt;

  if (p_l.y() < _pimpl->bounds.min.y() - r_wp)
    return rmf_utils::nullopt;

  if (_pimpl->bounds.max.x() + r_wp < p_l.x())
    return rmf_utils::nullopt;

  if (_pimpl->bounds.max.y() + r_wp < p_l.y())
    return rmf_utils::nullopt;

  const auto& map_data = _pimpl->data.at(*_pimpl->current_map);
  assert(map_data.waypoints.size() == map_data.waypoint_indices.size());
  assert(map_data.waypoints.size() == map_data.waypoint_p.size());
  for (std::size_t i=0; i < map_data.waypoints.size(); ++i)
  {
    const Eigen::Vector2f wp_p = map_data.waypoint_p.at(i);
    if ((wp_p - p_l).norm() <= r_wp)
    {
      return Graph::Pick{
        ElementType::Waypoint,
        map_data.waypoint_indices.at(i)
      };
    }
  }

  for (std::size_t i=0; i < map_data.bi_lanes.size(); ++i)
  {
    if (map_data.bi_lanes.at(i).pick(p_l.x(), p_l.y()))
    {
      return Graph::Pick{
        ElementType::Lane,
        map_data.bi_indices.at(i)
      };
    }
  }

  for (std::size_t i=0; i < map_data.mono_lanes.size(); ++i)
  {
    if (map_data.mono_lanes.at(i).pick(p_l.x(), p_l.y()))
    {
      return Graph::Pick{
        ElementType::Lane,
        map_data.mono_indices.at(i)
      };
    }
  }

  return rmf_utils::nullopt;
}

//==============================================================================
void Graph::select(Pick chosen)
{
  if (_pimpl->selected)
    _pimpl->unhighlight(*_pimpl->selected);

  _pimpl->selected = chosen;

  if (_pimpl->selected)
    _pimpl->highlight(*_pimpl->selected);
}


//==============================================================================
void Graph::deselect()
{
  _pimpl->selected = rmf_utils::nullopt;
}

//==============================================================================
rmf_utils::optional<Graph::Pick> Graph::selected() const
{
  return _pimpl->selected;
}

//==============================================================================
void Graph::draw(sf::RenderTarget& target, sf::RenderStates states) const
{
  if (!_pimpl->current_map)
    return;

  const auto& map_data = _pimpl->data.at(*_pimpl->current_map);
  for (const auto& s : map_data.mono_lanes)
    target.draw(s, states);

  for (const auto& s : map_data.bi_lanes)
    target.draw(s, states);

  for (const auto& s : map_data.bi_lane_arrows)
    target.draw(s, states);

  for (const auto& s : map_data.waypoints)
    target.draw(s, states);

  for (const auto& s : map_data.waypoints_text)
    target.draw(s.second, states);

  for (const auto& s : map_data.connector_waypoints_text)
    target.draw(s.second, states);
}

void Graph::set_text_size(uint sz)
{
  for (auto& iter : _pimpl->data)
  {
    auto& map_data = iter.second;
    for (auto& s : map_data.waypoints_text)
    {
      sf::Text& text = s.second;
      text.setCharacterSize(sz);

      sf::FloatRect text_rect = text.getLocalBounds();
      text.setOrigin(text_rect.width * 0.5f, text_rect.height * 0.5f);
    }

    for (auto& s : map_data.connector_waypoints_text)
    {
      sf::Text& text = s.second;
      text.setCharacterSize(sz);

      sf::FloatRect text_rect = text.getLocalBounds();
      text.setOrigin(text_rect.width * 0.5f, text_rect.height * 0.5f);
      
      sf::Text& parent_text = map_data.waypoints_text[s.first];
      sf::FloatRect parent_rect = parent_text.getLocalBounds();
      sf::Vector2f pos = parent_text.getPosition();
      text.setPosition(pos.x, pos.y + parent_rect.height * gTextScale.y); 
    }
  }
}

std::vector<std::string> Graph::get_map_names()
{
  std::vector<std::string> names;
  for (auto& s : _pimpl->data)
    names.push_back(s.first);
  return names;
}

} // namespace draw
} // namespace rmf_planner_viz
