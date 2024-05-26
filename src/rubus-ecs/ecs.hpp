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
#include <iostream>

namespace ruecs {

struct EntityId {
  std::size_t value = 0;

  auto operator==(const EntityId &other) const -> bool = default;
};

struct EntityIndex {
  std::size_t i = 0;

  auto operator==(const EntityIndex &other) const -> bool = default;
};

struct ComponentId {
  std::size_t value = 0;

  auto operator<=>(const ComponentId &other) const -> std::strong_ordering = default;
};

struct ArchetypeId {
  std::size_t value = 0;

  auto operator==(const ArchetypeId &other) const -> bool = default;
};

} // namespace ruecs

template <>
struct std::hash<ruecs::ComponentId> {
  inline auto operator()(const ruecs::ComponentId &id) const -> std::size_t {
    return id.value;
  }
};

template <>
struct std::hash<ruecs::ArchetypeId> {
  inline auto operator()(const ruecs::ArchetypeId &id) const -> std::size_t {
    return id.value;
  }
};

namespace ruecs {

struct ArchetypeStorage;

struct Entity {
  static inline std::size_t id_gen = 0;
  EntityId id;
  ArchetypeStorage *arch_storage = nullptr;

  auto operator==(const Entity &other) const -> bool = default;

  template <typename T>
  [[nodiscard]] auto get_component() -> T *;

  template <typename T, typename... Args>
  auto add_component(Args &&...args) -> void;

  template <typename T>
  auto remove_component() -> void;
};

} // namespace ruecs

template <>
struct std::hash<ruecs::Entity> {
  inline auto operator()(const ruecs::Entity &e) const -> std::size_t {
    return e.id.value;
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
  auto delete_all() -> void;
};

enum CommandType : std::size_t {
  CreateEntity,
  DeleteEntity,
  AddComponent,
  RemoveComponent,
};

struct Command {
  ArchetypeStorage *arch_storage = nullptr;
  std::vector<uint8_t> buf;

  [[nodiscard]] auto create_entity() -> struct PendingEntity;
  auto delete_entity(Entity entity) -> void;
  auto delete_entity(PendingEntity entity) -> void;

  template <typename T, typename... Args>
  auto add_component(Entity entity, Args &&...args) -> void {
    // command type
    auto i = buf.size();
    buf.resize(buf.size() + sizeof(CommandType));
    new (&buf[i]) CommandType{AddComponent};

    // entity
    i = buf.size();
    buf.resize(buf.size() + sizeof(Entity));
    std::memcpy(&buf[i], &entity, sizeof(Entity));

    // component id
    i = buf.size();
    buf.resize(buf.size() + sizeof(std::size_t));
    new (&buf[i]) std::size_t{typeid(T).hash_code()};

    // destructor
    i = buf.size();
    buf.resize(buf.size() + sizeof(std::size_t));
    void (*destructor)(void *) = [](void *component) {
      static_cast<T *>(component)->~T();
    };
    std::memcpy(&buf[i], &destructor, sizeof(std::size_t));

    // component size
    i = buf.size();
    buf.resize(buf.size() + sizeof(std::size_t));
    new (&buf[i]) std::size_t{sizeof(T)};

    // component
    i = buf.size();
    buf.resize(buf.size() + sizeof(T));
    new (&buf[i]) T{args...};
  }

  template <typename T>
  auto remove_component(Entity entity) -> void {
    // command type
    auto i = buf.size();
    buf.resize(buf.size() + sizeof(CommandType));
    new (&buf[i]) CommandType{RemoveComponent};

    // entity
    i = buf.size();
    buf.resize(buf.size() + sizeof(Entity));
    std::memcpy(&buf[i], &entity, sizeof(Entity));

    // component id
    i = buf.size();
    buf.resize(buf.size() + sizeof(std::size_t));
    new (&buf[i]) std::size_t{typeid(T).hash_code()};
  }

  auto flush() -> void;
};

struct Archetype {
  ArchetypeId id;
  ArchetypeStorage *arch_storage = nullptr;
  std::vector<ComponentId> component_ids; // sorted in ascending order
  std::vector<Entity> entities;
  std::vector<ComponentArray> components;

  explicit Archetype(ArchetypeId id, ArchetypeStorage *arch_storage);
  Archetype(ArchetypeId id, ArchetypeStorage *arch_storage, const ComponentInfo &info);
  Archetype(ArchetypeId id, ArchetypeStorage *arch_storage, std::span<ComponentInfo> infos);

  auto delete_all_components() -> void;

  [[nodiscard]] auto has_component(ComponentId id) -> bool;
  [[nodiscard]] auto has_components(std::span<ComponentId> ids) -> bool;
  [[nodiscard]] auto not_has_components(std::span<ComponentId> ids) -> bool;

  auto add_entity(Entity entity) -> EntityIndex;
  auto take_out_entity(EntityIndex index) -> void;
  auto delete_entity(EntityIndex index) -> void;
};

struct EntityLocation {
  Archetype *arch;
  EntityIndex index;
};

struct ComponentLocation {
  Archetype *arch;
  std::size_t index = 0;
};

using ComponentMap = std::unordered_map<ArchetypeId, ComponentLocation>;

struct ArchetypeStorage {
  Command command;

  std::unordered_map<ArchetypeId, Archetype> archetypes;
  std::unordered_map<Entity, EntityLocation> entity_locations;
  std::unordered_map<ComponentId, ComponentMap> component_locations;

  ArchetypeStorage();
  ~ArchetypeStorage();

  static auto get_archetype_id(std::span<ComponentInfo> s) -> ArchetypeId;

  [[nodiscard]] auto create_entity() -> Entity;
  auto delete_entity(Entity entity) -> void;

  template <typename T, typename... Args>
  auto add_component(Entity entity, Args &&...args) -> void {
    auto &entity_loc = entity_locations.at(entity);
    auto entity_arch = entity_loc.arch;
    auto entity_index = entity_loc.index;

    // check if the entity has this component
    const auto component_id = ComponentId{typeid(T).hash_code()};
    if (entity_arch->has_component(component_id)) {
      return;
    }

    const auto it = std::ranges::find_if(entity_arch->component_ids, [=](ComponentId id) {
      return id > component_id;
    });
    const auto insert_index = static_cast<std::size_t>(it - entity_arch->component_ids.begin());

    // setup component infos
    auto component_infos = std::vector<ComponentInfo>(entity_arch->components.size() + 1);
    for (auto i = std::size_t{}, x = std::size_t{}; i < component_infos.size(); ++i) {
      if (i == insert_index) {
        x = 1;
        component_infos[i] = {component_id, sizeof(T), [](void *component) {
                                static_cast<T *>(component)->~T();
                              }};
      } else {
        component_infos[i] = entity_arch->components[i - x].to_component_info();
      }
    }

    // get new arch
    const auto new_arch_id = get_archetype_id(component_infos);
    archetypes.try_emplace(new_arch_id, new_arch_id, this, component_infos);
    component_locations.try_emplace(component_id);

    auto new_arch = &archetypes.at(new_arch_id);
    auto new_entity_index = new_arch->add_entity(entity);

    for (auto i = std::size_t{}, x = std::size_t{}; i < new_arch->components.size(); ++i) {
      auto ptr = new_arch->components[i].get_last().data();
      if (i == insert_index) {
        x = 1;
        // construct new component
        new (ptr) T{args...};
        component_locations.at(component_id).try_emplace(new_arch->id, new_arch, i);
      } else {
        // copy components
        std::memcpy(ptr, entity_arch->components[i - x].get_at(entity_index).data(),
                    entity_arch->components[i - x].each_size);
        component_locations.at(entity_arch->components[i - x].id).try_emplace(new_arch->id, new_arch, i);
      }
    }

    // take out entity from the old arch
    entity_arch->take_out_entity(entity_index);

    // update entity location
    entity_loc.arch = new_arch;
    entity_loc.index = new_entity_index;
  }

  template <typename T>
  auto remove_component(Entity entity) -> void {
    auto &entity_loc = entity_locations.at(entity);
    auto entity_arch = entity_loc.arch;
    auto entity_index = entity_loc.index;

    // check if the entity has this component
    const auto component_id = ComponentId{typeid(T).hash_code()};
    if (not entity_arch->has_component(component_id)) {
      return;
    }

    const auto it = std::ranges::find_if(entity_arch->component_ids, [=](ComponentId id) {
      return id == component_id;
    });
    const auto remove_index = static_cast<std::size_t>(it - entity_arch->component_ids.begin());

    // new component infos
    auto component_infos = std::vector<ComponentInfo>(entity_arch->components.size() - 1);
    for (auto i = std::size_t{}, x = std::size_t{}; i < component_infos.size(); ++i) {
      if (i == remove_index) {
        x = 1;
      }
      component_infos[i] = entity_arch->components[i + x].to_component_info();
    }

    // get new arch
    const auto new_arch_id = get_archetype_id(component_infos);
    archetypes.try_emplace(new_arch_id, new_arch_id, this, component_infos);

    auto new_arch = &archetypes.at(new_arch_id);
    auto new_entity_index = new_arch->add_entity(entity);

    for (auto i = std::size_t{}, x = std::size_t{}; i < entity_arch->components.size(); ++i) {
      if (i == remove_index) {
        x = 1;
        // delete removed component
        entity_arch->components[i].destructor(entity_arch->components[i].get_at(entity_index).data());
      } else {
        // copy components
        auto ptr = new_arch->components[i - x].get_last().data();
        std::memcpy(ptr, entity_arch->components[i].get_at(entity_index).data(), entity_arch->components[i].each_size);
        component_locations.at(entity_arch->components[i].id).try_emplace(new_arch->id, new_arch, i - x);
      }
    }

    // take out entity from the old arch
    entity_arch->take_out_entity(entity_index);

    // update entity location
    entity_loc.arch = new_arch;
    entity_loc.index = new_entity_index;
  }
};

template <typename T>
[[nodiscard]] auto Entity::get_component() -> T * {
  auto entity_loc = arch_storage->entity_locations.at(*this);
  auto entity_arch = entity_loc.arch;

  auto component_loc = arch_storage->component_locations.at({typeid(T).hash_code()});
  assert(component_loc.contains(entity_arch->id));

  auto &component_array = entity_arch->components[component_loc.at(entity_arch->id).index];
  return reinterpret_cast<T *>(&component_array.array[entity_loc.index.i * component_array.each_size]);
}

template <typename T, typename... Args>
auto Entity::add_component(Args &&...args) -> void {
  arch_storage->add_component<T>(*this, args...);
}

template <typename T>
auto Entity::remove_component() -> void {
  arch_storage->remove_component<T>(*this);
}

struct ReadOnlyEntity {
  Entity entity;

  template <typename T>
  [[nodiscard]] auto get_component() -> T * {
    // TODO: optimze
    return entity.get_component<T>();
  }

  template <typename T, typename... Args>
  auto add_component(Args &&...args) -> void {
    entity.arch_storage->command.add_component<T>(entity, args...);
  }

  template <typename T>
  auto remove_component() -> void {
    entity.arch_storage->command.remove_component<T>(entity);
  }
};

struct PendingEntity {
  Entity entity;

  template <typename T, typename... Args>
  auto add_component(Args &&...args) -> void {
    entity.arch_storage->command.add_component<T>(entity, args...);
  }

  template <typename T>
  auto remove_component() -> void {
    entity.arch_storage->command.remove_component<T>(entity);
  }
};

struct Query {
  std::vector<ComponentId> includes;
  std::vector<ComponentId> excludes;

  ComponentMap archs;
  ComponentMap::iterator it;
  static inline ComponentMap::iterator null_it;
  std::size_t index = 0;

  Query();

  template <typename Map, typename Key = typename Map::key_type>
  static inline auto unorderd_map_intersection(Map &s, const Map &other) -> void {
    auto it = s.begin();
    while (it != s.end()) {
      if (not other.contains((*it).first)) {
        it = s.erase(it);
      } else {
        it = std::next(it);
      }
    }
  }

  template <typename Map, typename Key = typename Map::key_type>
  static inline auto unorderd_map_exclude(Map &s, const Map &exclude) -> void {
    for (const auto &[k, v] : exclude) {
      s.erase(k);
    }
  }

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

  auto reset(ArchetypeStorage *arch_storage) -> void;
  [[nodiscard]] auto get_next_entity() -> std::tuple<Archetype *, ReadOnlyEntity>;
};

#define for_each_entities(arch_storage, query) \
  (query).reset(arch_storage); \
  for (auto [arch, entity] = (query).get_next_entity(); arch != nullptr; \
       std::tie(arch, entity) = (query).get_next_entity())

} // namespace ruecs
