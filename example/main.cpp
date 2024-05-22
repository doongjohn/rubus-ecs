#include <cstdlib>
#include <format>
#include <chrono>
#include <iostream>

#include <rubus-ecs/ecs.hpp>

struct Position {
  float x = 0;
  float y = 0;
};

struct Velocity {
  float x = 0;
  float y = 0;
};

struct Player {
  std::string name;
};

auto main() -> int {
  auto archstore = ruecs::ArchetypeStorage{};

  std::cout << "creating entities\n";
  for (auto i = 1; i <= 4; ++i) {
    auto entity = archstore.new_entity();
    archstore.add_component<Position>(entity, 10.f, 20.f);
    archstore.add_component<Velocity>(entity, 1.f, 1.f);
    if (i % 3 == 0) {
      archstore.remove_component<Velocity>(entity);
    }
    if (i % 2 == 0) {
      archstore.add_component<Player>(entity, "player");
    }
  }

  std::cout << "creating systems\n";
  auto systems = std::vector<ruecs::System>{};

  systems.emplace_back(ruecs::Query::with_components<Position>(),
                       [](ruecs::Entity entity, ruecs::Archetype &arch, void *) {
                         auto pos = arch.get_component<Position>(entity);
                         std::cout << std::format("{},{}\n", pos->x, pos->y);
                       });

  systems.emplace_back(ruecs::Query::with_components<Position, Velocity>(),
                       [](ruecs::Entity entity, ruecs::Archetype &arch, void *) {
                         auto pos = arch.get_component<Position>(entity);
                         auto vel = arch.get_component<Velocity>(entity);
                         pos->x += vel->x;
                         pos->y += vel->y;
                         std::cout << std::format("{},{} {},{}\n", pos->x, pos->y, vel->x, vel->y);
                       });

  systems.emplace_back(ruecs::Query::with_components<Player>(),
                       [](ruecs::Entity entity, ruecs::Archetype &arch, void *) {
                         auto player = arch.get_component<Player>(entity);
                         std::cout << std::format("{}\n", player->name);
                       });

  std::cout << "running systems\n";
  auto start = std::chrono::high_resolution_clock::now();
  for (const auto &system : systems) {
    archstore.run_system(system);
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::cout << "done\n";

  auto duration = duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << std::format("system time taken: {}ms\n", duration.count());

  return EXIT_SUCCESS;
}
