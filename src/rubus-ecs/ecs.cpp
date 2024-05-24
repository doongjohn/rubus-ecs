#include "ecs.hpp"

namespace ruecs {

ComponentArray::ComponentArray(std::size_t id, std::size_t each_size, void (*destructor)(void *))
    : id{id}, each_size{each_size}, destructor{destructor} {}

auto ComponentArray::delete_all() -> void {
  for (auto i = std::size_t{}; i < count; ++i) {
    destructor(array.data() + i * each_size);
  }
}

[[nodiscard]] auto ComponentArray::get_last() -> std::span<uint8_t> {
  assert(count != 0);

  return {array.data() + (count - 1) * each_size, each_size};
}

[[nodiscard]] auto ComponentArray::get_at(std::size_t index) -> std::span<uint8_t> {
  assert(index < count);

  return {array.data() + index * each_size, each_size};
}

auto ComponentArray::set_at(std::size_t index, std::span<uint8_t> value) -> void {
  assert(index < count);

  std::memcpy(array.data() + index * each_size, value.data(), each_size);
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

auto Archetype::delete_all_components() -> void {
  for (auto &component_array : components) {
    component_array.delete_all();
  }
}

[[nodiscard]] auto Archetype::has_component(std::size_t id) -> bool {
  return std::ranges::find(component_ids, id) != component_ids.end();
}

[[nodiscard]] auto Archetype::has_components(std::span<const std::size_t> ids) -> bool {
  const auto &A = component_ids;
  const auto &B = ids;

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

[[nodiscard]] auto Archetype::not_has_components(std::span<const std::size_t> ids) -> bool {
  const auto &A = component_ids;
  const auto &B = ids;

  auto i = std::size_t{};
  auto j = std::size_t{};

  while (i < A.size() && j < B.size()) {
    if (A[i] == B[j]) {
      return false;
    } else if (A[i] < B[j]) {
      ++i;
    } else {
      ++j;
    }
  }

  return true;
}

auto Archetype::new_entity(ArchetypeStorage *arch_storage) -> Entity {
  auto entity = Entity{.arch_storage = arch_storage, .id = ++Entity::id_gen, .arch_id = id, .index = entities.size()};
  entities.push_back(entity);

  for (auto &component_array : components) {
    component_array.count += 1;
    component_array.array.resize(component_array.array.size() + component_array.each_size);
  }

  return entity;
}

auto Archetype::add_entity(Entity entity) -> Entity {
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

ArchetypeStorage::~ArchetypeStorage() {
  for (auto &[_, arch] : archetypes) {
    arch.delete_all_components();
  }
}

auto ArchetypeStorage::hash_components(std::span<ComponentInfo> const &s) -> std::size_t {
  // TODO: find a better way to hash multiple integers
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

[[nodiscard]] auto ArchetypeStorage::new_entity() -> Entity {
  return archetypes.at(0).new_entity(this);
}

auto ArchetypeStorage::delete_entity(Entity entity) -> void {
  archetypes.at(entity.arch_id).delete_entity(entity);
}

Query::Query() : it{null_it} {}

inline auto Query::is_query_satisfied(Archetype *arch) const -> bool {
  return arch->has_components(includes) && arch->not_has_components(excludes);
}

auto Query::get_next_entity(ArchetypeStorage *arch_storage) -> std::tuple<Archetype *, Entity> {
  if (it == null_it) {
    it = arch_storage->archetypes.begin();
  }

  // TODO: use somthing better than `std::unorderd_map` for faster iteration
  // - https://martin.ankerl.com/2022/08/27/hashmap-bench-01/
  // - https://github.com/martinus/unordered_dense
  // - https://github.com/ktprime/emhash
  // - https://github.com/greg7mdp/parallel-hashmap
  while (it != arch_storage->archetypes.end()) {
    auto arch = &it->second;
    if (index == arch->entities.size()) {
      it = std::next(it);
      index = 0;
      continue;
    }
    if (is_query_satisfied(arch)) {
      return {arch, arch->entities[index++]};
    }
    index += 1;
  }

  it = null_it;
  index = 0;
  return {nullptr, {}};
}

} // namespace ruecs
