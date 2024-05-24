#include <cstdlib>
#include <format>
#include <chrono>
#include <iostream>

#include <rubus-ecs/ecs.hpp>

// TODO
// - modify (add/remove component, new/delete entity) after iteration
// - archetype cache

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
  auto arch_storage = ruecs::ArchetypeStorage{};

  std::cout << "creating entities\n";
  for (auto i = 1; i <= 4; ++i) {
    auto entity = arch_storage.new_entity();
    entity.add_component<Position>(10.f, 20.f);
    entity.add_component<Velocity>(1.f, 1.f);
    if (i % 3 == 0) {
      entity.remove_component<Velocity>();
    }
    if (i % 2 == 0) {
      entity.add_component<Player>("player");
    }
  }

  std::cout << "creating queries\n";
  auto query_pos = ruecs::Query{}.with<Position>();
  auto query_movable = ruecs::Query{}.with<Position, Velocity>();
  auto query_player = ruecs::Query{}.with<Player>();

  std::cout << "running systems\n";
  auto start = std::chrono::high_resolution_clock::now();

  for_each_entities(&arch_storage, query_pos) {
    auto pos = arch->get_component<Position>(entity);
    std::cout << std::format("{},{}\n", pos->x, pos->y);
  }

  for_each_entities(&arch_storage, query_movable) {
    auto pos = arch->get_component<Position>(entity);
    auto vel = arch->get_component<Velocity>(entity);
    pos->x += vel->x;
    pos->y += vel->y;
    std::cout << std::format("{},{} {},{}\n", pos->x, pos->y, vel->x, vel->y);
  }

  for_each_entities(&arch_storage, query_pos) {
    auto pos = arch->get_component<Position>(entity);
    std::cout << std::format("{},{}\n", pos->x, pos->y);
  }

  for_each_entities(&arch_storage, query_player) {
    auto player = arch->get_component<Player>(entity);
    std::cout << std::format("{}\n", player->name);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << std::format("running systems took {}ms\n", duration.count());

  std::cout << "done\n";
  return EXIT_SUCCESS;
}
