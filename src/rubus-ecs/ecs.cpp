#include "ecs.hpp"

namespace ruecs {

std::size_t Entity::cur_id = 0;

System::System(const std::vector<std::size_t> &query, const SystemFn &&fn) : query{query}, fn{fn} {}

ComponentArray::ComponentArray(std::size_t id, std::size_t each_size, void (*destructor)(void *))
    : id{id}, each_size{each_size}, destructor{destructor} {}

ComponentArray::~ComponentArray() {
  for (auto i = std::size_t{}; i < count; ++i) {
    destructor(array.data() + i * each_size);
  }
}

auto ComponentArray::get_last() -> std::span<uint8_t> {
  if (count == 0) {
    return {};
  }
  return {array.data() + (count - 1) * each_size, each_size};
}

auto ComponentArray::get_at(std::size_t index) -> std::span<uint8_t> {
  assert(index < count);

  auto byte_index = index * each_size;
  return {array.data() + byte_index, each_size};
}

auto ComponentArray::set_at(std::size_t index, std::span<uint8_t> value) -> void {
  assert(index < count);

  auto byte_index = index * each_size;
  std::memcpy(array.data() + byte_index, value.data(), each_size);
}

auto ComponentArray::take_out_at(std::size_t index) -> void {
  assert(index < count);

  count -= 1;
  if (index < count) {
    set_at(index, get_at(count));
  }
  array.resize(array.size() - each_size);
}

auto ComponentArray::delete_at(std::size_t index) -> void {
  assert(index < count);

  destructor(array.data() + index * each_size);
  take_out_at(index);
}

auto ComponentInfo::operator<=>(const ComponentInfo &other) const -> std::strong_ordering {
  return this->id <=> other.id;
}

Archetype::Archetype(std::size_t id) : id{id} {}

Archetype::Archetype(std::size_t id, const ComponentInfo &info) : id{id} {
  component_ids.resize(1);
  component_ids[0] = info.id;

  components.resize(1);
  components[0].id = info.id;
  components[0].each_size = info.size;
  components[0].destructor = info.destructor;
}

Archetype::Archetype(std::size_t id, std::span<ComponentInfo> infos) : id{id} {
  component_ids.resize(infos.size());
  for (auto i = std::size_t{}; i < infos.size(); ++i) {
    component_ids[i] = infos[i].id;
  }

  components.resize(infos.size());
  for (auto i = std::size_t{}; i < infos.size(); ++i) {
    components[i].id = infos[i].id;
    components[i].each_size = infos[i].size;
    components[i].destructor = infos[i].destructor;
  }
}

auto Archetype::has_component(std::size_t component_id) -> bool {
  return std::ranges::find(component_ids, component_id) != component_ids.end();
}

auto Archetype::has_components(std::span<const std::size_t> component_ids) -> bool {
  const auto &A = this->component_ids;
  const auto &B = component_ids;

  auto i = std::size_t{};
  auto j = std::size_t{};

  while (i < A.size() && j < B.size()) {
    if (A[i] == B[j]) {
      ++i;
      ++j;
    } else if (A[i] < B[j]) {
      ++i;
    } else {
      return false;
    }
  }

  return j == B.size();
}

auto Archetype::get_component_array(std::size_t component_id) -> ComponentArray & {
  auto it = std::ranges::find(component_ids, component_id);
  assert(it != component_ids.end());
  return components[it - component_ids.begin()];
}

auto Archetype::new_entity_uninitialized() -> Entity {
  auto entity = Entity{.id = ++Entity::cur_id, .arch_id = id, .index = entities.size()};
  entities.push_back(entity);

  for (auto &component_array : components) {
    component_array.count += 1;
    component_array.array.resize(component_array.array.size() + component_array.each_size);
  }

  return entity;
}

auto Archetype::add_entity_uninitialized(Entity entity) -> Entity {
  entity.arch_id = id;
  entity.index = entities.size();
  entities.push_back(entity);

  for (auto &component_array : components) {
    component_array.count += 1;
    component_array.array.resize(component_array.array.size() + component_array.each_size);
  }

  return entity;
}

auto Archetype::take_out_entity(Entity entity) -> void {
  assert(entity.arch_id == id);
  assert(entity.index < entities.size());

  entities[entity.index] = entities.back();
  entities.pop_back();

  for (auto &component_array : components) {
    component_array.take_out_at(entity.index);
  }
}

auto Archetype::delete_entity(Entity entity) -> void {
  assert(entity.arch_id == id);
  assert(entity.index < entities.size());

  entities[entity.index] = entities.back();
  entities.pop_back();

  for (auto &component_array : components) {
    component_array.delete_at(entity.index);
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
  return archetypes.at(0).new_entity_uninitialized();
}

auto ArchetypeStorage::delete_entity(Entity entity) -> void {
  archetypes.at(entity.arch_id).delete_entity(entity);
}

auto ArchetypeStorage::run_system(const System &system, void *ptr) -> void {
  for (auto &[_, arch] : archetypes) { // TODO: use somthing better than `unorderd_map` for faster iteration
    if (arch.has_components(system.query)) {
      for (auto entity : arch.entities) {
        system.fn(entity, arch, ptr);
      }
    }
  }
}

} // namespace ruecs
