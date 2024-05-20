#pragma once

#include <cstddef>
#include <cassert>
#include <typeinfo>
#include <functional>
#include <span>
#include <vector>
#include <unordered_map>
#include <format>
#include <iostream>

namespace ruecs {

struct Entity {
  std::size_t id = 0;
  std::size_t arch = 0;
  std::size_t index = 0;
};

template <typename T>
concept Component = requires(std::span<uint8_t> value) {
  { T::deinit(value) } -> std::same_as<void>;
};

struct Archetype;

struct System {
  std::vector<std::size_t> query;
  std::function<void(Entity entity, Archetype &arch)> fn;

  System(const std::vector<std::size_t> &query, const std::function<void(Entity entity, Archetype &arch)> &fn);
};

struct ComponentArray {
  std::size_t id = 0;
  std::size_t each_size = 0;
  std::size_t count = 0;
  std::vector<uint8_t> array;
  std::function<void(std::span<uint8_t>)> fn_deinit;

  ComponentArray() = default;
  ComponentArray(std::size_t id, std::size_t each_size, std::function<void(std::span<uint8_t>)> fn_deinit);
  ~ComponentArray();

  auto get_at(std::size_t index) -> std::span<uint8_t>;
  auto set_at(std::size_t index, std::span<uint8_t> value) -> void;
  auto push_back(std::span<uint8_t> value) -> void;
  auto remove_at(std::size_t index) -> void;
};

struct ComponentInfo {
  std::size_t id = 0;
  std::size_t size = 0;
  std::function<void(std::span<uint8_t>)> fn_deinit;

  auto operator<=>(const ComponentInfo &other) const -> std::strong_ordering;
};

struct Archetype {
  std::size_t id = 0;
  std::vector<Entity> entities;
  std::vector<std::size_t> component_ids; // sorted in ascending order
  std::vector<ComponentArray> components;

  Archetype() = default;
  explicit Archetype(std::size_t id);
  Archetype(std::size_t id, const ComponentInfo &info);
  Archetype(std::size_t id, std::span<ComponentInfo> infos);

  auto has_component(std::size_t component_id) -> bool;
  auto has_components(std::span<const std::size_t> component_ids) -> bool;
  auto get_component_array_of(std::size_t component_id) -> ComponentArray &;

  template <Component T>
  auto get_component(Entity entity) -> T * {
    assert(entity.arch == id);
    assert(entity.index < entities.size());

    const auto component_id = typeid(T).hash_code();
    auto &component_array = get_component_array_of(component_id);
    return reinterpret_cast<T *>(&component_array.array[entity.index * component_array.each_size]);
  }

  auto add_entity(Entity &entity, std::span<std::span<uint8_t>> components) -> void;
  auto remove_entity(Entity entity) -> void;
};

struct ArchetypeStorage {
  std::unordered_map<std::size_t, Archetype> archetypes;

  ArchetypeStorage();

  static auto hash_components(std::span<ComponentInfo> const &s) -> std::size_t;

  auto new_entity() -> Entity;
  auto delete_entity(Entity entity) -> void;

  template <Component T>
  auto add_component(Entity &entity, T &&component) -> void {
    const auto component_id = typeid(T).hash_code();

    auto &old_arch = archetypes.at(entity.arch);
    if (old_arch.has_component(component_id)) {
      return;
    }

    auto component_infos = std::vector<ComponentInfo>(old_arch.components.size() + 1);
    auto components = std::vector<std::span<uint8_t>>(old_arch.components.size() + 1);

    auto it = std::ranges::find_if(old_arch.component_ids, [=](std::size_t id) {
      return id > component_id;
    });
    auto insert_index = std::distance(old_arch.component_ids.begin(), it);

    // copy old component
    for (auto i = std::size_t{}; i < insert_index; ++i) {
      auto &component_array = old_arch.components[i];
      component_infos[i] = ComponentInfo{component_array.id, component_array.each_size, component_array.fn_deinit};
      components[i] = component_array.get_at(entity.index);
    }
    // add new component
    component_infos[insert_index] = ComponentInfo{component_id, sizeof(T), T::deinit};
    components[insert_index] = std::span<uint8_t>{reinterpret_cast<uint8_t *>(&component), sizeof(T)};
    // copy old component
    for (auto i = insert_index; i < old_arch.components.size(); ++i) {
      auto &component_array = old_arch.components[i];
      component_infos[i + 1] = ComponentInfo{component_array.id, component_array.each_size, component_array.fn_deinit};
      components[i + 1] = component_array.get_at(entity.index);
    }

    // calculate new arch
    auto new_arch_id = hash_components(component_infos);
    if (not archetypes.contains(new_arch_id)) {
      archetypes.emplace(new_arch_id, Archetype{new_arch_id, component_infos});
    }

    auto old_entity = entity;

    // add entity to the new arch
    auto &new_arch = archetypes.at(new_arch_id);
    new_arch.add_entity(entity, components);

    // remove entity from the old arch
    old_arch.remove_entity(old_entity);
  }

  template <Component T>
  auto remove_component(Entity &entity) -> void {
    // TODO
  }

  auto run_system(const System &system) -> void;
};

} // namespace ruecs
