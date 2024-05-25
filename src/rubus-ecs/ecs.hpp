#pragma once

#include <cstddef>
#include <cassert>
#include <typeinfo>
#include <functional>
#include <tuple>
#include <span>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace ruecs {

struct EntityId {
  std::size_t id = 0;

  auto operator==(const EntityId &other) const -> bool = default;
};

struct EntityIndex {
  std::size_t i = 0;

  auto operator==(const EntityIndex &other) const -> bool = default;
};

struct ComponentId {
  std::size_t id = 0;

  auto operator<=>(const ComponentId &other) const -> std::strong_ordering = default;
};

struct ArchtypeId {
  std::size_t id = 0;

  auto operator==(const ArchtypeId &other) const -> bool = default;
};

} // namespace ruecs

template <>
struct std::hash<ruecs::ArchtypeId> {
  inline auto operator()(const ruecs::ArchtypeId &id) const -> size_t {
    return id.id;
  }
};

namespace ruecs {

struct ArchetypeStorage;

struct Entity {
  ArchetypeStorage *arch_storage = nullptr;

  inline static std::size_t id_gen = 0;
  EntityId id;
  ArchtypeId arch_id;
  EntityIndex index;

  template <typename T, typename... Args>
  auto add_component(Args &&...args) -> void;

  template <typename T>
  auto remove_component() -> void;

  auto operator==(const Entity &other) const -> bool = default;
};

} // namespace ruecs

template <>
struct std::hash<ruecs::Entity> {
  inline auto operator()(const ruecs::Entity &e) const -> size_t {
    return e.id.id;
  }
};

namespace ruecs {

struct ComponentInfo {
  ComponentId id;
  std::size_t size = 0;
  void (*destructor)(void *component) = nullptr;

  auto operator<=>(const ComponentInfo &other) const -> std::strong_ordering;
};

struct ComponentArray {
  ComponentId id;
  std::size_t each_size = 0;
  std::size_t count = 0;
  void (*destructor)(void *component) = nullptr;
  std::vector<uint8_t> array;

  ComponentArray() = default;
  ComponentArray(ComponentId id, std::size_t each_size, void (*destructor)(void *component));

  auto delete_all() -> void;

  [[nodiscard]] inline auto to_component_info() -> ComponentInfo {
    return {
      .id = id,
      .size = each_size,
      .destructor = destructor,
    };
  }

  [[nodiscard]] auto get_last() -> std::span<uint8_t>;
  [[nodiscard]] auto get_at(EntityIndex index) -> std::span<uint8_t>;
  auto set_at(EntityIndex index, std::span<uint8_t> value) -> void;

  auto take_out_at(EntityIndex index) -> void;
  auto delete_at(EntityIndex index) -> void;
};

struct Archetype {
  ArchtypeId id;
  std::vector<ComponentId> component_ids; // sorted in ascending order
  std::vector<Entity> entities;
  std::vector<ComponentArray> components;

  explicit Archetype(ArchtypeId id);
  Archetype(ArchtypeId id, const ComponentInfo &info);
  Archetype(ArchtypeId id, std::span<ComponentInfo> infos);

  auto delete_all_components() -> void;

  [[nodiscard]] auto has_component(ComponentId id) -> bool;
  [[nodiscard]] auto has_components(std::span<const ComponentId> ids) -> bool;
  [[nodiscard]] auto not_has_components(std::span<const ComponentId> ids) -> bool;

  template <typename T>
  [[nodiscard]] auto get_component(Entity entity) -> T * {
    assert(entity.arch_id == id);
    assert(entity.index.i < entities.size());

    auto it = std::ranges::find(component_ids, ComponentId{typeid(T).hash_code()});
    assert(it != component_ids.end()); // when failed: entity does not have component T

    auto &component_array = components[it - component_ids.begin()];
    return reinterpret_cast<T *>(&component_array.array[entity.index.i * component_array.each_size]);
  }

  auto new_entity(ArchetypeStorage *arch_storage) -> Entity;
  auto add_entity(Entity entity) -> Entity;

  auto take_out_entity(Entity entity) -> void;
  auto delete_entity(Entity entity) -> void;
};

struct Query;

struct ArchetypeStorage {
  std::unordered_map<ArchtypeId, Archetype> archetypes;

  ArchetypeStorage();
  ~ArchetypeStorage();

  static auto get_archetype_id(std::span<ComponentInfo> const &s) -> ArchtypeId;

  [[nodiscard]] auto new_entity() -> Entity;
  auto delete_entity(Entity entity) -> void;

  template <typename T, typename... Args>
  auto add_component(Entity &entity, Args &&...args) -> void {
    const auto component_id = ComponentId{typeid(T).hash_code()};

    auto &old_arch = archetypes.at(entity.arch_id);
    if (old_arch.has_component(component_id)) {
      return;
    }

    auto it = std::ranges::find_if(old_arch.component_ids, [=](ComponentId id) {
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
    const auto new_arch_id = get_archetype_id(component_infos);
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
    const auto component_id = ComponentId{typeid(T).hash_code()};

    auto &old_arch = archetypes.at(entity.arch_id);
    if (not old_arch.has_component(component_id)) {
      return;
    }

    auto it = std::ranges::find_if(old_arch.component_ids, [=](ComponentId id) {
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
    auto new_arch_id = get_archetype_id(component_infos);
    archetypes.try_emplace(new_arch_id, new_arch_id, component_infos);

    auto &new_arch = archetypes.at(new_arch_id);
    auto new_entity = new_arch.add_entity(entity);

    for (auto i = std::size_t{}, x = std::size_t{}; i < new_arch.components.size(); ++i) {
      auto ptr = new_arch.components[i].get_last().data();
      if (i == remove_index) {
        x = 1;
        // delete removed component
        old_arch.components[i].destructor(old_arch.components[i].get_at(entity.index).data());
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
};

template <typename T, typename... Args>
auto Entity::add_component(Args &&...args) -> void {
  arch_storage->add_component<T>(*this, args...);
}

template <typename T>
auto Entity::remove_component() -> void {
  arch_storage->remove_component<T>(*this);
}

struct Query {
  std::vector<ComponentId> includes;
  std::vector<ComponentId> excludes;

  inline static std::unordered_map<ArchtypeId, Archetype>::iterator null_it;
  std::unordered_map<ArchtypeId, Archetype>::iterator it;
  std::size_t index = 0;

  Query();

  template <typename... T>
  auto with() -> Query {
    includes = {{typeid(T).hash_code()}...};
    std::ranges::sort(includes, std::ranges::less());
    return *this;
  }

  template <typename... T>
  auto without() -> Query {
    excludes = {{typeid(T).hash_code()}...};
    std::ranges::sort(excludes, std::ranges::less());
    return *this;
  }

  [[nodiscard]] auto is_query_satisfied(Archetype *arch) const -> bool;
  [[nodiscard]] auto get_next_entity(ArchetypeStorage *arch_storage) -> std::tuple<Archetype *, Entity>;
};

#define for_each_entities(arch_storage, query) \
  for (auto [arch, entity] = (query).get_next_entity(arch_storage); arch != nullptr; \
       std::tie(arch, entity) = (query).get_next_entity(arch_storage))

} // namespace ruecs
