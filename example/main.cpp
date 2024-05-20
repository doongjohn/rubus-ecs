#include <cstdlib>
#include <iostream>
#include <rubus-ecs/ecs.hpp>

struct Position {
  float x = 0;
  float y = 0;

  static auto deinit(std::span<uint8_t>) -> void {}
};

struct Velocity {
  float x = 0;
  float y = 0;

  static auto deinit(std::span<uint8_t>) -> void {}
};

auto main() -> int {
  auto archetype_storage = ruecs::ArchetypeStorage{};

  // std::cout << "creating entity\n";
  for (auto i = 0; i < 3; ++i) {
    auto entity = archetype_storage.new_entity();
    archetype_storage.add_component(entity, Position{.x = 10.f, .y = 20.f});
    archetype_storage.add_component(entity, Velocity{.x = 1.f, .y = 1.f});
  }

  const auto system1 = //
    ruecs::System{{
                    typeid(Position).hash_code(),
                  },
                  [](ruecs::Entity entity, ruecs::Archetype &arch) {
                    auto pos = arch.get_component<Position>(entity);
                    // std::cout << std::format("{},{}\n", pos->x, pos->y);
                  }};

  const auto system2 = //
    ruecs::System{{
                    typeid(Position).hash_code(),
                    typeid(Velocity).hash_code(),
                  },
                  [](ruecs::Entity entity, ruecs::Archetype &arch) {
                    auto pos = arch.get_component<Position>(entity);
                    auto vel = arch.get_component<Velocity>(entity);
                    pos->x += vel->x;
                    pos->y += vel->y;
                    // std::cout << std::format("{},{} {},{}\n", pos->x, pos->y, vel->x, vel->y);
                  }};

  std::cout << "running system\n";
  archetype_storage.run_system(system1);
  archetype_storage.run_system(system2);
  archetype_storage.run_system(system1);
  std::cout << "done\n";

  return EXIT_SUCCESS;
}
