#pragma once

#include <cstddef>
#include <cassert>
#include <typeinfo>
#include <functional>
#include <span>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace ruecs {

struct Entity {
  std::size_t id = 0;
  std::size_t arch_id = 0;
  std::size_t index = 0;

  static std::size_t cur_id;
};

struct Query {
  std::vector<std::size_t> component_ids;

  template <typename... Components>
  inline static auto with_components() -> Query {
    auto result = Query{{typeid(Components).hash_code()...}};
    std::ranges::sort(result.component_ids, std::ranges::less());
    return result;
  }
};

struct Archetype;
struct ArchetypeStorage;

// using SystemFn = void (*)(Archetype &arch, Entity entity, void *ptr);
using SystemFn = std::function<void(Archetype &arch, Entity entity, double delta_time, void *ptr)>;

struct System {
  Query query;
  SystemFn fn;

  System(Query query, SystemFn fn);
};

struct ComponentInfo {
  std::size_t id = 0;
  std::size_t size = 0;
  void (*destructor)(void *component) = nullptr;

  auto operator<=>(const ComponentInfo &other) const -> std::strong_ordering;
};

struct ComponentArray {
  std::size_t id = 0;
  std::size_t each_size = 0;
  std::size_t count = 0;
  void (*destructor)(void *component) = nullptr;
  std::vector<uint8_t> array;

  ComponentArray() = default;
  ComponentArray(std::size_t id, std::size_t each_size, void (*destructor)(void *component));

  auto delete_all() -> void;

  inline auto to_component_info() -> ComponentInfo {
    return {
      .id = id,
      .size = each_size,
      .destructor = destructor,
    };
  }

  auto get_last() -> std::span<uint8_t>;
  auto get_at(std::size_t index) -> std::span<uint8_t>;
  auto set_at(std::size_t index, std::span<uint8_t> value) -> void;

  auto take_out_at(std::size_t index) -> void;
  auto delete_at(std::size_t index) -> void;
};

struct Archetype {
  std::size_t id = 0;
  std::vector<std::size_t> component_ids; // sorted in ascending order
  std::vector<Entity> entities;
  std::vector<ComponentArray> components;

  explicit Archetype(std::size_t id);
  Archetype(std::size_t id, const ComponentInfo &info);
  Archetype(std::size_t id, std::span<ComponentInfo> infos);

  auto delete_all_components() -> void;

  auto has_component(std::size_t component_id) -> bool;
  auto has_components(std::span<const std::size_t> component_ids) -> bool;

  auto get_component_array(std::size_t component_id) -> ComponentArray &;

  template <typename T>
  auto get_component(Entity entity) -> T * {
    assert(entity.arch_id == id);
    assert(entity.index < entities.size());

    const auto component_id = typeid(T).hash_code();
    auto &component_array = get_component_array(component_id);
    return reinterpret_cast<T *>(&component_array.array[entity.index * component_array.each_size]);
  }

  auto new_entity() -> Entity;
  auto add_entity(Entity entity) -> Entity;

  auto take_out_entity(Entity entity) -> void;
  auto delete_entity(Entity entity) -> void;
};

struct ArchetypeStorage {
  std::unordered_map<std::size_t, Archetype> archetypes;

  ArchetypeStorage();
  ~ArchetypeStorage();

  static auto hash_components(std::span<ComponentInfo> const &s) -> std::size_t;

  auto new_entity() -> Entity;
  auto delete_entity(Entity entity) -> void;

  template <typename T, typename... Args>
  auto add_component(Entity &entity, Args &&...args) -> void {
    const auto component_id = typeid(T).hash_code();

    auto &old_arch = archetypes.at(entity.arch_id);
    if (old_arch.has_component(component_id)) {
      return;
    }

    auto it = std::ranges::find_if(old_arch.component_ids, [=](std::size_t id) {
      return id > component_id;
    });
    const auto insert_index = static_cast<std::size_t>(it - old_arch.component_ids.begin());

    // new component infos
    auto component_infos = std::vector<ComponentInfo>(old_arch.components.size() + 1);
    for (auto i = std::size_t{}, x = std::size_t{}; i < old_arch.components.size() + 1; ++i) {
      if (i == insert_index) {
        x = 1;
        component_infos[i] = {component_id, sizeof(T), [](void *component) {
                                static_cast<T *>(component)->~T();
                              }};
      } else {
        component_infos[i] = old_arch.components[i - x].to_component_info();
      }
    }

    // calculate new arch
    const auto new_arch_id = hash_components(component_infos);
    archetypes.try_emplace(new_arch_id, new_arch_id, component_infos);

    auto &new_arch = archetypes.at(new_arch_id);
    auto new_entity = new_arch.add_entity(entity);

    for (auto i = std::size_t{}, x = std::size_t{}; i < new_arch.components.size(); ++i) {
      auto ptr = new_arch.components[i].get_last().data();
      if (i == insert_index) {
        x = 1;
        // construct new component
        new (reinterpret_cast<T *>(ptr)) T{args...};
      } else {
        // copy components
        std::memcpy(ptr, old_arch.components[i - x].get_at(entity.index).data(), old_arch.components[i - x].each_size);
      }
    }

    // remove entity from the old arch
    old_arch.take_out_entity(entity);

    // update entity
    entity = new_entity;
  }

  template <typename T>
  auto remove_component(Entity &entity) -> void {
    const auto component_id = typeid(T).hash_code();

    auto &old_arch = archetypes.at(entity.arch_id);
    if (not old_arch.has_component(component_id)) {
      return;
    }

    auto it = std::ranges::find_if(old_arch.component_ids, [=](std::size_t id) {
      return id > component_id;
    });
    auto remove_index = static_cast<std::size_t>(it - old_arch.component_ids.begin());

    auto component_infos = std::vector<ComponentInfo>(old_arch.components.size() - 1);
    for (auto i = std::size_t{}, x = std::size_t{}; i < component_infos.size(); ++i) {
      if (i == remove_index) {
        x = 1;
      } else {
        component_infos[i] = old_arch.components[i + x].to_component_info();
      }
    }

    // calculate new arch
    auto new_arch_id = hash_components(component_infos);
    archetypes.try_emplace(new_arch_id, new_arch_id, component_infos);

    auto &new_arch = archetypes.at(new_arch_id);
    auto new_entity = new_arch.add_entity(entity);

    for (auto i = std::size_t{}, x = std::size_t{}; i < new_arch.components.size(); ++i) {
      auto ptr = new_arch.components[i].get_last().data();
      if (i == remove_index) {
        // delete removed component
        old_arch.components[i].destructor(old_arch.components[i].get_at(entity.index).data());
        x = 1;
      } else {
        // copy components
        std::memcpy(ptr, old_arch.components[i + x].get_at(entity.index).data(), old_arch.components[i + x].each_size);
      }
    }

    // remove entity from the old arch
    old_arch.take_out_entity(entity);

    // update entity
    entity = new_entity;
  }

  auto run_system(const System &system, double delta_time = 0, void *ptr = nullptr) -> void;
};

} // namespace ruecs

namespace std {
template <>
struct hash<ruecs::Entity> {
  inline auto operator()(const ruecs::Entity &e) const -> size_t {
    return e.id;
  }
};
} // namespace std
