#include "ecs.hpp"

#include <ranges>

namespace ruecs {

ComponentArray::ComponentArray(ComponentId id, std::size_t each_size, void (*fn_destructor)(void *))
    : id{id}, each_size{each_size}, fn_destructor{fn_destructor} {}

[[nodiscard]] auto ComponentArray::get_last() -> std::span<uint8_t> {
  assert(count != 0);

  if (each_size == 0) {
    return array;
  } else {
    return {array.data() + (count - 1) * each_size, each_size};
  }
}

[[nodiscard]] auto ComponentArray::get_at(EntityIndex index) -> std::span<uint8_t> {
  assert(index.i < count);

  if (each_size == 0) {
    return array;
  } else {
    return {array.data() + index.i * each_size, each_size};
  }
}

auto ComponentArray::set_at(EntityIndex index, std::span<uint8_t> value) -> void {
  assert(index.i < count);

  if (each_size != 0) {
    std::memcpy(array.data() + index.i * each_size, value.data(), each_size);
  }
}

auto ComponentArray::take_out_at(EntityIndex index) -> void {
  assert(index.i < count);

  if (each_size != 0) {
    if (index.i < count - 1) {
      set_at(index, get_last());
    }
  }
  count -= 1;
  array.resize(array.size() - each_size);
}

auto ComponentArray::delete_at(EntityIndex index) -> void {
  assert(index.i < count);

  if (each_size != 0) {
    fn_destructor(array.data() + index.i * each_size);
  }
  take_out_at(index);
}

auto ComponentArray::delete_all() -> void {
  for (auto i = std::size_t{}; i < count; ++i) {
    fn_destructor(array.data() + i * each_size);
  }
  count = 0;
  array.clear();
}

auto ComponentInfo::operator<=>(const ComponentInfo &other) const -> std::strong_ordering {
  return id <=> other.id;
}

Command::Command(ArchetypeStorage *arch_storage) : arch_storage{arch_storage} {}

Command::~Command() {
  discard();
}

auto Command::create_entity() -> PendingEntity {
  aligned_buf.emplace_back<CommandType>(CommandType::CreateEntity);
  auto entity = arch_storage->create_entity();
  return PendingEntity{this, arch_storage, entity.id};
}

auto Command::delete_entity(ReadOnlyEntity entity) -> void {
  aligned_buf.emplace_back<CommandType>(CommandType::DeleteEntity);
  aligned_buf.emplace_back<Entity>(Entity{entity.id, entity.arch_storage});
}

auto Command::delete_entity(PendingEntity entity) -> void {
  aligned_buf.emplace_back<CommandType>(CommandType::DeleteEntity);
  aligned_buf.emplace_back<Entity>(Entity{entity.id, entity.arch_storage});
}

auto Command::run() -> void {
  for (auto i = std::size_t{}; i < aligned_buf.size();) {
    switch (aligned_buf.get<CommandType>(i)) {
    case CommandType::CreateEntity:
      break;
    case CommandType::DeleteEntity: {
      auto &entity = aligned_buf.get<Entity>(i);

      // NOTE: There can be multiple delete commands for the same entity.
      if (arch_storage->entity_locations.contains(entity)) {
        arch_storage->delete_entity(entity);
      }
    } break;
    case CommandType::AddComponent: {
      auto &entity = aligned_buf.get<Entity>(i);
      auto component_id = ComponentId{aligned_buf.get<std::size_t>(i)};
      auto fn_destructor = aligned_buf.get<void (*)(void *)>(i);
      auto component_size = aligned_buf.get<std::size_t>(i);
      auto component_index = aligned_buf.get<std::size_t>(i);
      auto component_ptr = aligned_buf.get_ptr_at(component_index);
      i = component_index + component_size;

      // entity must exist
      assert(arch_storage->entity_locations.contains(entity));

      auto &entity_loc = arch_storage->entity_locations.at(entity);
      auto entity_arch = entity_loc.arch;
      auto entity_index = entity_loc.index;

      // check if the entity has this component
      if (not entity_arch->has_component(component_id)) {
        auto it = std::ranges::find_if(entity_arch->component_ids, [=](ComponentId id) {
          return id > component_id;
        });
        const auto insert_index = static_cast<std::size_t>(it - entity_arch->component_ids.begin());

        // setup component infos
        auto component_infos = std::vector<ComponentInfo>(entity_arch->components.size() + 1);
        for (auto i = std::size_t{}, x = std::size_t{}; i < entity_arch->components.size() + 1; ++i) {
          if (i == insert_index) {
            x = 1;
            component_infos[i] = {component_id, component_size, fn_destructor};
          } else {
            component_infos[i] = entity_arch->components[i - x].to_component_info();
          }
        }

        // get new arch
        const auto new_arch_id = ArchetypeStorage::calculate_archetype_id(component_infos);
        arch_storage->archetypes.try_emplace(new_arch_id, new_arch_id, arch_storage, component_infos);
        arch_storage->component_locations.try_emplace(component_id);

        auto new_arch = &arch_storage->archetypes.at(new_arch_id);
        auto new_entity_index = new_arch->add_entity(entity);

        for (auto i = std::size_t{}, x = std::size_t{}; i < new_arch->components.size(); ++i) {
          auto ptr = new_arch->components[i].get_last().data();
          if (i == insert_index) {
            x = 1;
            // construct new component
            std::memcpy(ptr, component_ptr, component_size);
            arch_storage->component_locations.at(component_id).try_emplace(new_arch, i);
          } else {
            // copy components
            std::memcpy(ptr, entity_arch->components[i - x].get_at(entity_index).data(),
                        entity_arch->components[i - x].each_size);
            arch_storage->component_locations.at(entity_arch->components[i - x].id).try_emplace(new_arch, i);
          }
        }

        // take out entity from the old arch
        entity_arch->take_out_entity(entity_index);

        // update entity location
        entity_loc.arch = new_arch;
        entity_loc.index = new_entity_index;
      } else {
        fn_destructor(component_ptr);
      }
    } break;
    case CommandType::RemoveComponent: {
      auto &entity = aligned_buf.get<Entity>(i);
      auto component_id = ComponentId{aligned_buf.get<std::size_t>(i)};

      // entity must exist
      assert(arch_storage->entity_locations.contains(entity));

      auto &entity_loc = arch_storage->entity_locations.at(entity);
      auto entity_arch = entity_loc.arch;
      auto entity_index = entity_loc.index;

      // check if the entity has this component
      if (entity_arch->has_component(component_id)) {
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
        const auto new_arch_id = arch_storage->calculate_archetype_id(component_infos);
        arch_storage->archetypes.try_emplace(new_arch_id, new_arch_id, arch_storage, component_infos);

        auto new_arch = &arch_storage->archetypes.at(new_arch_id);
        auto new_entity_index = new_arch->add_entity(entity);

        for (auto i = std::size_t{}, x = std::size_t{}; i < entity_arch->components.size(); ++i) {
          if (i == remove_index) {
            x = 1;
            // delete removed component
            entity_arch->components[i].fn_destructor(entity_arch->components[i].get_at(entity_index).data());
          } else {
            // copy components
            auto ptr = new_arch->components[i - x].get_last().data();
            std::memcpy(ptr, entity_arch->components[i].get_at(entity_index).data(),
                        entity_arch->components[i].each_size);
            arch_storage->component_locations.at(entity_arch->components[i].id).try_emplace(new_arch, i - x);
          }
        }

        // take out entity from the old arch
        entity_arch->take_out_entity(entity_index);

        // update entity location
        entity_loc.arch = new_arch;
        entity_loc.index = new_entity_index;
      }
    } break;
    }
  }
  aligned_buf.clear();
}

auto Command::discard() -> void {
  for (auto i = std::size_t{}; i < aligned_buf.size();) {
    switch (aligned_buf.get<CommandType>(i)) {
    case CommandType::CreateEntity:
      break;
    case CommandType::DeleteEntity: {
      aligned_buf.get<Entity>(i);
    } break;
    case CommandType::AddComponent: {
      aligned_buf.get<Entity>(i);      // entity
      aligned_buf.get<std::size_t>(i); // ComponentId
      auto fn_destructor = aligned_buf.get<void (*)(void *)>(i);
      auto component_size = aligned_buf.get<std::size_t>(i);
      auto component_index = aligned_buf.get<std::size_t>(i);
      auto component_ptr = aligned_buf.get_ptr_at(component_index);
      i = component_index + component_size;
      fn_destructor(component_ptr);
    } break;
    case CommandType::RemoveComponent: {
      aligned_buf.get<Entity>(i);      // entity
      aligned_buf.get<std::size_t>(i); // ComponentId
    } break;
    }
  }
  aligned_buf.clear();
}

Archetype::Archetype(ArchetypeId id, ArchetypeStorage *arch_storage) : id{id}, arch_storage{arch_storage} {}

Archetype::Archetype(ArchetypeId id, ArchetypeStorage *arch_storage, const ComponentInfo &info)
    : id{id}, arch_storage{arch_storage} {
  component_ids.resize(1);
  component_ids[0] = info.id;

  components.resize(1);
  components[0].id = info.id;
  components[0].each_size = info.size;
  components[0].fn_destructor = info.fn_destructor;
}

Archetype::Archetype(ArchetypeId id, ArchetypeStorage *arch_storage, std::span<ComponentInfo> infos)
    : id{id}, arch_storage{arch_storage} {
  component_ids.resize(infos.size());
  for (auto i = std::size_t{}; i < infos.size(); ++i) {
    component_ids[i] = infos[i].id;
  }

  components.resize(infos.size());
  for (auto i = std::size_t{}; i < infos.size(); ++i) {
    components[i].id = infos[i].id;
    components[i].each_size = infos[i].size;
    components[i].fn_destructor = infos[i].fn_destructor;
  }
}

auto Archetype::delete_all_entities() -> void {
  for (auto entity : entities) {
    arch_storage->entity_locations.erase(entity);
  }
  entities.clear();

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

auto Archetype::add_entity(Entity entity) -> EntityIndex {
  assert(arch_storage->entity_locations.at(entity).arch != this);

  entities.push_back(entity);

  for (auto &component_array : components) {
    component_array.count += 1;
    component_array.array.resize(component_array.array.size() + component_array.each_size);
  }

  return {entities.size() - 1};
}

auto Archetype::take_out_entity(EntityIndex index) -> void {
  assert(not entities.empty());

  if (index.i < entities.size() - 1) {
    entities[index.i] = entities.back();
    arch_storage->entity_locations.at(entities[index.i]).index = index;
  }
  entities.pop_back();

  for (auto &component_array : components) {
    component_array.take_out_at(index);
  }
}

auto Archetype::delete_entity(EntityIndex index) -> void {
  assert(not entities.empty());

  if (index.i < entities.size() - 1) {
    entities[index.i] = entities.back();
    arch_storage->entity_locations.at(entities[index.i]).index = index;
  }
  entities.pop_back();

  for (auto &component_array : components) {
    component_array.delete_at(index);
  }
}

ArchetypeStorage::ArchetypeStorage() {
  archetypes.emplace(0, Archetype{ArchetypeId{0}, this});
}

ArchetypeStorage::~ArchetypeStorage() {
  delete_all_archetypes();
}

auto ArchetypeStorage::delete_all_archetypes() -> void {
  for (auto &[_, arch] : archetypes) {
    arch.delete_all_entities();
  }
}

auto ArchetypeStorage::calculate_archetype_id(std::span<ComponentInfo> infos) -> ArchetypeId {
  // TODO: find a better way to hash multiple integers
  // https://stackoverflow.com/a/72073933
  auto hash = infos.size();
  for (const auto &info : infos) {
    auto x = info.id.value;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    hash ^= x + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  return {hash};
}

[[nodiscard]] auto ArchetypeStorage::create_entity() -> Entity {
  auto arch = &archetypes.at({0});
  auto entity = Entity{
    .id = {++Entity::id_gen},
    .arch_storage = this,
  };
  entity_locations.try_emplace(entity, arch, EntityIndex{arch->entities.size()});
  arch->entities.push_back(entity);
  return entity;
}

auto ArchetypeStorage::delete_entity(Entity entity) -> void {
  auto entity_loc = entity_locations.at(entity);
  auto entity_arch = entity_loc.arch;
  auto entity_index = entity_loc.index;
  entity_arch->delete_entity(entity_index);
  entity_locations.erase(entity);
}

Query::Query(ArchetypeStorage *arch_storage) : arch_storage{arch_storage} {}

auto Query::update_archs() -> void {
  arch_count = arch_storage->archetypes.size();
  archs.clear();
  auto &component_locations = arch_storage->component_locations;

  // includes
  if (includes.empty()) {
    for (const auto &[_, component_map] : component_locations) {
      for (const auto arch : component_map) {
        archs.insert(arch);
      }
    }
  } else {
    auto it = component_locations.find(includes[0]);
    if (it != component_locations.end() && not it->second.empty()) {
      archs = it->second;
      for (const auto include : std::views::drop(includes, 1)) {
        auto it = component_locations.find(include);
        if (it == component_locations.end()) {
          archs.clear();
          break;
        }
        unorderd_map_intersection(archs, component_locations.at(include));
        if (archs.empty()) {
          break;
        }
      }
    }
  }

  // excludes
  for (const auto exclude : excludes) {
    auto it = component_locations.find(exclude);
    if (it != component_locations.end()) {
      unorderd_map_exclude(archs, it->second);
      if (archs.empty()) {
        break;
      }
    }
  }
}

auto Query::start() -> void {
  if (arch_count != arch_storage->archetypes.size()) {
    update_archs();
  }
  archs_it = archs.begin();
  index = 0;
}

auto Query::get_next_entity(Command *command) -> ReadOnlyEntity {
  while (archs_it != archs.end()) {
    auto arch = (*archs_it).first;
    if (index == arch->entities.size()) {
      archs_it = std::next(archs_it);
      index = 0;
    } else {
      auto entity = arch->entities[index];
      return {command, arch_storage, arch, {index++}, entity.id};
    }
  }

  index = 0;
  return {};
}

} // namespace ruecs
