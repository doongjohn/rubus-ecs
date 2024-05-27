#include "ecs.hpp"

namespace ruecs {

ComponentArray::ComponentArray(ComponentId id, std::size_t each_size, void (*destructor)(void *))
    : id{id}, each_size{each_size}, destructor{destructor} {}

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
    destructor(array.data() + index.i * each_size);
  }
  take_out_at(index);
}

auto ComponentArray::delete_all() -> void {
  for (auto i = std::size_t{}; i < count; ++i) {
    destructor(array.data() + i * each_size);
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
  // command type
  auto i = buf.size();
  const auto cmd = CommandType::CreateEntity;
  buf.resize(buf.size() + sizeof(CommandType));
  std::memcpy(&buf[i], &cmd, sizeof(CommandType));

  return PendingEntity{this, arch_storage->create_entity()};
}

auto Command::delete_entity(ReadOnlyEntity entity) -> void {
  // command type
  auto i = buf.size();
  const auto cmd = CommandType::DeleteEntity;
  buf.resize(buf.size() + sizeof(CommandType));
  std::memcpy(&buf[i], &cmd, sizeof(CommandType));

  // entity
  i = buf.size();
  buf.resize(buf.size() + sizeof(Entity));
  std::memcpy(&buf[i], &(entity.entity), sizeof(Entity));
}

auto Command::delete_entity(PendingEntity entity) -> void {
  // command type
  auto i = buf.size();
  const auto cmd = CommandType::DeleteEntity;
  buf.resize(buf.size() + sizeof(CommandType));
  std::memcpy(&buf[i], &cmd, sizeof(CommandType));

  // entity
  i = buf.size();
  buf.resize(buf.size() + sizeof(Entity));
  std::memcpy(&buf[i], &(entity.entity), sizeof(Entity));
}

auto Command::run() -> void {
  CommandType cmd;
  for (auto i = std::size_t{}; i < buf.size();) {
    std::memcpy(&cmd, &buf[i], sizeof(CommandType));
    i += sizeof(CommandType);

    switch (cmd) {
    case CommandType::CreateEntity:
      break;
    case CommandType::DeleteEntity: {
      Entity entity;
      std::memcpy(&entity, &buf[i], sizeof(Entity));
      i += sizeof(Entity);

      // NOTE: There can be multiple delete commands for the same entity.
      if (arch_storage->entity_locations.contains(entity)) {
        arch_storage->delete_entity(entity);
      }
    } break;
    case CommandType::AddComponent: {
      Entity entity;
      std::memcpy(&entity, &buf[i], sizeof(Entity));
      i += sizeof(Entity);

      auto component_id = ComponentId{};
      std::memcpy(&(component_id.value), &buf[i], sizeof(std::size_t));
      i += sizeof(std::size_t);

      void (*destructor)(void *);
      std::memcpy(&destructor, &buf[i], sizeof(std::size_t));
      i += sizeof(std::size_t);

      std::size_t component_size;
      std::memcpy(&component_size, &buf[i], sizeof(std::size_t));
      i += sizeof(std::size_t);

      const auto component_ptr = &buf[i];
      i += component_size;

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
            component_infos[i] = {component_id, component_size, destructor};
          } else {
            component_infos[i] = entity_arch->components[i - x].to_component_info();
          }
        }

        // get new arch
        const auto new_arch_id = ArchetypeStorage::get_archetype_id(component_infos);
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
        destructor(component_ptr);
      }
    } break;
    case CommandType::RemoveComponent: {
      Entity entity;
      std::memcpy(&entity, &buf[i], sizeof(Entity));
      i += sizeof(Entity);

      auto component_id = ComponentId{};
      std::memcpy(&(component_id.value), &buf[i], sizeof(std::size_t));
      i += sizeof(std::size_t);

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
        const auto new_arch_id = arch_storage->get_archetype_id(component_infos);
        arch_storage->archetypes.try_emplace(new_arch_id, new_arch_id, arch_storage, component_infos);

        auto new_arch = &arch_storage->archetypes.at(new_arch_id);
        auto new_entity_index = new_arch->add_entity(entity);

        for (auto i = std::size_t{}, x = std::size_t{}; i < entity_arch->components.size(); ++i) {
          if (i == remove_index) {
            x = 1;
            // delete removed component
            entity_arch->components[i].destructor(entity_arch->components[i].get_at(entity_index).data());
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
  buf.clear();
}

auto Command::discard() -> void {
  CommandType cmd;
  for (auto i = std::size_t{}; i < buf.size();) {
    std::memcpy(&cmd, &buf[i], sizeof(CommandType));
    i += sizeof(CommandType);

    switch (cmd) {
    case CommandType::CreateEntity:
      break;
    case CommandType::DeleteEntity: {
      i += sizeof(Entity); // entity
    } break;
    case CommandType::AddComponent: {
      i += sizeof(Entity);      // entity
      i += sizeof(std::size_t); // ComponentId

      void (*destructor)(void *);
      std::memcpy(&destructor, &buf[i], sizeof(std::size_t));
      i += sizeof(std::size_t);

      std::size_t component_size;
      std::memcpy(&component_size, &buf[i], sizeof(std::size_t));
      i += sizeof(std::size_t);

      const auto component_ptr = &buf[i];
      i += component_size;

      destructor(component_ptr);
    } break;
    case CommandType::RemoveComponent: {
      i += sizeof(Entity);      // entity
      i += sizeof(std::size_t); // ComponentId
    } break;
    }
  }
  buf.clear();
}

Archetype::Archetype(ArchetypeId id, ArchetypeStorage *arch_storage) : id{id}, arch_storage{arch_storage} {}

Archetype::Archetype(ArchetypeId id, ArchetypeStorage *arch_storage, const ComponentInfo &info)
    : id{id}, arch_storage{arch_storage} {
  component_ids.resize(1);
  component_ids[0] = info.id;

  components.resize(1);
  components[0].id = info.id;
  components[0].each_size = info.size;
  components[0].destructor = info.destructor;
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
    components[i].destructor = infos[i].destructor;
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

auto ArchetypeStorage::get_archetype_id(std::span<ComponentInfo> infos) -> ArchetypeId {
  // TODO: find a better way to hash multiple integers
  // https://stackoverflow.com/a/72073933
  auto hash = infos.size();
  for (const auto &info : infos) {
    auto x = info.id.value;
    x = ((x >> 32) ^ x) * 0x45d9f3b;
    x = ((x >> 32) ^ x) * 0x45d9f3b;
    x = (x >> 32) ^ x;
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

Query::Query() : it{null_it} {}

auto Query::reset(ArchetypeStorage *arch_storage) -> void {
  // TODO: optimize
  if (includes.empty()) {
    archs = {};
  } else {
    auto &component_locations = arch_storage->component_locations;
    for (auto i = std::size_t{0}; i < includes.size(); ++i) {
      if (not component_locations.contains(includes[i])) {
        archs = {};
        break;
      }
      if (i == 0) {
        archs = component_locations.at(includes[0]);
      } else {
        unorderd_map_intersection(archs, component_locations.at(includes[i]));
      }
    }
    for (auto i = std::size_t{0}; i < excludes.size(); ++i) {
      if (component_locations.contains(excludes[i])) {
        unorderd_map_exclude(archs, component_locations.at(excludes[i]));
      }
    }
  }
  it = archs.begin();
}

auto Query::get_next_entity(Command *command) -> std::tuple<Archetype *, ReadOnlyEntity> {
  while (it != archs.end()) {
    auto arch = (*it).first;
    if (index == arch->entities.size()) {
      it = std::next(it);
      index = 0;
    } else {
      return {arch, {command, arch->entities[index++]}};
    }
  }

  it = null_it;
  index = 0;
  return {nullptr, {}};
}

} // namespace ruecs
