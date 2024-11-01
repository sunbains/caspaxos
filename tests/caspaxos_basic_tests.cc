#include <cstdlib>
#include <iostream>
#include <string>
#include <cassert>

#include "caspaxos.h"

/* Very basic tests, no networking, no view change etc. */

void run_tests() {
  // Test 1: Basic single proposer, single acceptor
  {
      std::vector<std::shared_ptr<Acceptor<int>>> acceptors = {
          std::make_shared<Acceptor<int>>()
      };

      Proposer<int> proposer(acceptors);

      auto result = proposer.propose([](const std::optional<int>& prev) {
          return prev.value_or(0) + 1;
      });

      assert(result.has_value());
      assert(result.value() == 1);

      std::cout << "Test 1 passed: Basic single proposer, single acceptor\n";
  }

  // Test 2: Multiple acceptors
  {
      std::vector<std::shared_ptr<Acceptor<int>>> acceptors;

      for (int i{}; i < 3; ++i) {
          acceptors.push_back(std::make_shared<Acceptor<int>>());
      }

      Proposer<int> proposer(acceptors);

      auto result = proposer.propose([](const std::optional<int>& prev) {
          return prev.value_or(0) + 1;
      });

      assert(result.has_value());
      assert(result.value() == 1);
      std::cout << "Test 2 passed: Multiple acceptors\n";
  }

  // Test 3: Sequential proposals
  {
      std::vector<std::shared_ptr<Acceptor<int>>> acceptors = {
          std::make_shared<Acceptor<int>>()
      };
      Proposer<int> proposer(acceptors);

      auto result1 = proposer.propose([](const std::optional<int>& prev) {
          return prev.value_or(0) + 1;
      });
      assert(result1.value() == 1);

      auto result2 = proposer.propose([](const std::optional<int>& prev) {
          return prev.value() + 1;
      });
      assert(result2.value() == 2);

      std::cout << "Test 3 passed: Sequential proposals\n";
  }

  // Test 4: String values
  {
      std::vector<std::shared_ptr<Acceptor<std::string>>> acceptors = {
          std::make_shared<Acceptor<std::string>>()
      };
      Proposer<std::string> proposer(acceptors);

      auto result = proposer.propose([](const std::optional<std::string>& prev) {
          return prev.value_or("") + "Hello";
      });

      assert(result.has_value());
      assert(result.value() == "Hello");
      std::cout << "Test 4 passed: String values\n";
  }

  std::cout << "All tests passed!\n";
}

iint main() {
  run_tests();
  return EXIT_SUCCESS;;
}
