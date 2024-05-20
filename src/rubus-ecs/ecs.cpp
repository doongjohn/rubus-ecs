#include "ecs.hpp"

#include <algorithm>

namespace ruecs {

System::System(const std::vector<std::size_t> &query, const std::function<void(Entity entity, Archetype &arch)> &fn)
    : query{query}, fn{fn} {
  std::ranges::sort(this->query, std::ranges::less());
}

ComponentArray::ComponentArray(std::size_t id, std::size_t each_size, std::function<void(std::span<uint8_t>)> fn_deinit)
    : id{id}, each_size{each_size}, fn_deinit{std::move(fn_deinit)} {}

ComponentArray::~ComponentArray() {
  for (auto i = std::size_t{}; i < count; ++i) {
    fn_deinit({array.data() + i * each_size, each_size});
  }
}

auto ComponentArray::get_at(std::size_t index) -> std::span<uint8_t> {
  if (index >= count) {
    return {};
  }
  auto byte_index = index * each_size;
  return {array.data() + byte_index, each_size};
}

auto ComponentArray::set_at(std::size_t index, std::span<uint8_t> value) -> void {
  if (index >= count) {
    return;
  }
  auto byte_index = index * each_size;
  std::memcpy(array.data() + byte_index, value.data(), each_size);
}

auto ComponentArray::push_back(std::span<uint8_t> value) -> void {
  array.resize(array.size() + each_size);
  count += 1;
  set_at(count - 1, value);
}

auto ComponentArray::remove_at(std::size_t index) -> void {
  assert(index < count);

  fn_deinit({array.data() + index * each_size, each_size});
  if (index < count - 1) {
    set_at(index, get_at(count - 1));
  }
  array.pop_back();
  count -= 1;
}

auto ComponentInfo::operator<=>(const ComponentInfo &other) const -> std::strong_ordering {
  return this->id <=> other.id;
}

Archetype::Archetype(std::size_t id) : id{id} {}

Archetype::Archetype(std::size_t id, const ComponentInfo &info) : id{id} {
  component_ids = std::vector<std::size_t>(1);
  components = std::vector<ComponentArray>(1);
  component_ids[0] = info.id;
  components[0].id = info.id;
  components[0].each_size = info.size;
  components[0].fn_deinit = info.fn_deinit;
}

Archetype::Archetype(std::size_t id, std::span<ComponentInfo> infos) : id{id} {
  component_ids = std::vector<std::size_t>(infos.size());
  components = std::vector<ComponentArray>(infos.size());
  for (auto i = std::size_t{}; i < infos.size(); ++i) {
    component_ids[i] = infos[i].id;
  }
  for (auto i = std::size_t{}; i < infos.size(); ++i) {
    components[i].id = infos[i].id;
    components[i].each_size = infos[i].size;
    components[i].fn_deinit = infos[i].fn_deinit;
  }
}

auto Archetype::has_component(std::size_t component_id) -> bool {
  return std::ranges::find(component_ids, component_id) != component_ids.end();
}

auto Archetype::has_components(std::span<const std::size_t> component_ids) -> bool {
  auto i = std::size_t{};
  for (auto &component_array : components) {
    if (component_array.id == component_ids[i]) {
      i += 1;
      if (i == component_ids.size()) {
        return true;
      }
    }
  }
  return false;
}

auto Archetype::get_component_array_of(std::size_t component_id) -> ComponentArray & {
  auto it = std::ranges::find(component_ids, component_id);
  assert(it != component_ids.end());

  auto index = it - component_ids.begin();
  return components[index];
}

auto Archetype::add_entity(Entity &entity, std::span<std::span<uint8_t>> components) -> void {
  entity.arch = id;
  entity.index = entities.size();
  entities.push_back(entity);

  for (auto i = std::size_t{}; i < this->components.size(); ++i) {
    this->components[i].push_back(components[i]);
  }
}

auto Archetype::remove_entity(Entity entity) -> void {
  assert(entity.arch == id);
  assert(entity.index < entities.size());

  entities[entity.index] = entities.back();
  entities.pop_back();

  for (auto &component_array : components) {
    component_array.remove_at(entity.index);
  }
}

ArchetypeStorage::ArchetypeStorage() {
  archetypes.emplace(0, Archetype{0});
}

auto ArchetypeStorage::hash_components(std::span<ComponentInfo> const &s) -> std::size_t {
  // https://stackoverflow.com/a/72073933
  auto hash = s.size();
  for (const auto &component_info : s) {
    auto x = component_info.id;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    hash ^= x + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  return hash;
}

auto ArchetypeStorage::new_entity() -> Entity {
  static auto id = std::size_t{};
  auto entity = Entity{
    .id = ++id,
    .arch = 0,
    .index = 0,
  };
  archetypes.at(0).add_entity(entity, {});
  return entity;
}

auto ArchetypeStorage::delete_entity(Entity entity) -> void {
  archetypes.at(entity.arch).remove_entity(entity);
}

auto ArchetypeStorage::run_system(const System &system) -> void {
  for (auto &[_, arch] : archetypes) {
    if (arch.has_components(system.query)) {
      for (auto entity : arch.entities) {
        system.fn(entity, arch);
      }
    }
  }
}

} // namespace ruecs
