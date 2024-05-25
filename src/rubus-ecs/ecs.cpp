#include "ecs.hpp"

namespace ruecs {

ComponentArray::ComponentArray(ComponentId id, std::size_t each_size, void (*destructor)(void *))
    : id{id}, each_size{each_size}, destructor{destructor} {}

[[nodiscard]] auto ComponentArray::get_last() -> std::span<uint8_t> {
  assert(count != 0);

  return {array.data() + (count - 1) * each_size, each_size};
}

[[nodiscard]] auto ComponentArray::get_at(EntityIndex index) -> std::span<uint8_t> {
  assert(each_size != 0);
  assert(index.i < count);

  return {array.data() + index.i * each_size, each_size};
}

auto ComponentArray::set_at(EntityIndex index, std::span<uint8_t> value) -> void {
  assert(each_size != 0);
  assert(index.i < count);

  std::memcpy(array.data() + index.i * each_size, value.data(), each_size);
}

auto ComponentArray::take_out_at(EntityIndex index) -> void {
  assert(index.i < count);

  if (each_size != 0) {
    count -= 1;
    if (index.i < count) {
      set_at(index, get_at(EntityIndex{count}));
    }
    array.resize(array.size() - each_size);
  }
}

auto ComponentArray::delete_at(EntityIndex index) -> void {
  assert(index.i < count);

  if (each_size != 0) {
    destructor(array.data() + index.i * each_size);
    take_out_at(index);
  }
}

auto ComponentArray::delete_all() -> void {
  for (auto i = std::size_t{}; i < count; ++i) {
    destructor(array.data() + i * each_size);
  }
}

auto ComponentInfo::operator<=>(const ComponentInfo &other) const -> std::strong_ordering {
  return id <=> other.id;
}

Archetype::Archetype(ArchetypeId id) : id{id} {}

Archetype::Archetype(ArchetypeId id, const ComponentInfo &info) : id{id} {
  component_ids.resize(1);
  component_ids[0] = info.id;

  components.resize(1);
  components[0].id = info.id;
  components[0].each_size = info.size;
  components[0].destructor = info.destructor;
}

Archetype::Archetype(ArchetypeId id, std::span<ComponentInfo> infos) : id{id} {
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

[[nodiscard]] auto Archetype::has_component(ComponentId id) -> bool {
  return std::ranges::find(component_ids, id) != component_ids.end();
}

[[nodiscard]] auto Archetype::has_components(std::span<ComponentId> ids) -> bool {
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

[[nodiscard]] auto Archetype::not_has_components(std::span<ComponentId> ids) -> bool {
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
  auto entity = Entity{
    .arch_storage = arch_storage,
    .id = {++Entity::id_gen},
    .arch_id = id,
    .index = {entities.size()},
  };
  entities.push_back(entity);

  for (auto &component_array : components) {
    component_array.count += 1;
    component_array.array.resize(component_array.array.size() + component_array.each_size);
  }

  return entity;
}

auto Archetype::add_entity(Entity entity) -> Entity {
  entity.arch_id = id;
  entity.index.i = entities.size();
  entities.push_back(entity);

  for (auto &component_array : components) {
    if (component_array.each_size != 0) {
      component_array.count += 1;
      component_array.array.resize(component_array.array.size() + component_array.each_size);
    }
  }

  return entity;
}

auto Archetype::take_out_entity(Entity entity) -> void {
  assert(entity.arch_id == id);
  assert(entity.index.i < entities.size());

  entities[entity.index.i] = entities.back();
  entities.pop_back();

  for (auto &component_array : components) {
    component_array.take_out_at(entity.index);
  }
}

auto Archetype::delete_entity(Entity entity) -> void {
  assert(entity.arch_id == id);
  assert(entity.index.i < entities.size());

  entities[entity.index.i] = entities.back();
  entities.pop_back();

  for (auto &component_array : components) {
    component_array.delete_at(entity.index);
  }
}

ArchetypeStorage::ArchetypeStorage() {
  archetypes.emplace(0, Archetype{ArchetypeId{0}});
}

ArchetypeStorage::~ArchetypeStorage() {
  for (auto &[_, arch] : archetypes) {
    arch.delete_all_components();
  }
}

auto ArchetypeStorage::get_archetype_id(std::span<ComponentInfo> s) -> ArchetypeId {
  // TODO: find a better way to hash multiple integers
  // https://stackoverflow.com/a/72073933
  auto hash = s.size();
  for (const auto &component_info : s) {
    auto x = component_info.id.id;
    x = ((x >> 32) ^ x) * 0x45d9f3b;
    x = ((x >> 32) ^ x) * 0x45d9f3b;
    x = (x >> 32) ^ x;
    hash ^= x + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  return ArchetypeId{hash};
}

[[nodiscard]] auto ArchetypeStorage::new_entity() -> Entity {
  return archetypes.at({0}).new_entity(this);
}

auto ArchetypeStorage::delete_entity(Entity entity) -> void {
  archetypes.at(entity.arch_id).delete_entity(entity);
}

Query::Query() : it{null_it} {}

auto Query::refresh(ArchetypeStorage *arch_storage) -> void {
  if (includes.empty()) {
    archs = {};
  } else {
    archs = arch_storage->archs_of_component.at(includes[0]);
    for (auto i = std::size_t{1}; i < includes.size(); ++i) {
      unorderd_set_intersection(archs, arch_storage->archs_of_component.at(includes[i]));
    }
    for (auto i = std::size_t{0}; i < excludes.size(); ++i) {
      unorderd_set_exclude(archs, arch_storage->archs_of_component.at(excludes[i]));
    }
  }
  it = archs.begin();
}

auto Query::get_next_entity(ArchetypeStorage *arch_storage) -> std::tuple<Archetype *, Entity> {
  while (it != archs.end()) {
    auto arch = &arch_storage->archetypes.at(*it);
    if (index == arch->entities.size()) {
      it = std::next(it);
      index = 0;
    } else {
      return {arch, arch->entities[index++]};
    }
  }

  it = null_it;
  index = 0;
  return {nullptr, {}};
}

} // namespace ruecs
