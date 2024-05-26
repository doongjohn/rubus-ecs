#include <cstdlib>
#include <chrono>
#include <format>
#include <iostream>

// TODO
// - component toggle
// - archetype cache
// - bulk modification
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
  auto arch_storage = ruecs::ArchetypeStorage{};

  std::cout << "creating entities\n";
  {
    auto entity = arch_storage.create_entity();
    entity.add_component<Position>(3.f, 3.f);
  }
  for (auto i = 1; i <= 4; ++i) {
    auto entity = arch_storage.create_entity();
    entity.add_component<Position>(2.f, 2.f);
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
    auto pos = entity.get_component<Position>();
    if (pos->x != 3.f) {
      entity.remove_component<Position>();
    }
    std::cout << std::format("{},{}\n", pos->x, pos->y);

    auto new_entity = arch_storage.command.create_entity();
    new_entity.add_component<Position>(10.f, 10.f);
    new_entity.add_component<Velocity>(20.f, 20.f);
  }

  std::cout << "command flush\n";
  arch_storage.command.flush();

  for_each_entities(&arch_storage, query_movable) {
    auto pos = entity.get_component<Position>();
    auto vel = entity.get_component<Velocity>();
    pos->x += vel->x;
    pos->y += vel->y;
    std::cout << std::format("{},{} {},{}\n", pos->x, pos->y, vel->x, vel->y);
  }

  for_each_entities(&arch_storage, query_pos) {
    auto pos = entity.get_component<Position>();
    std::cout << std::format("{},{}\n", pos->x, pos->y);
  }

  for_each_entities(&arch_storage, query_player) {
    auto player = entity.get_component<Player>();
    std::cout << std::format("{}\n", player->name);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << std::format("running systems took {}ms\n", duration.count());

  std::cout << "done\n";
  return EXIT_SUCCESS;
}
