#include <cstdlib>

#include "caspaxos.h"
#include "net.h"

int main(int argc, char* argv[]) {
  if (argc < 4 || (argc - 1) % 3 != 0) {
    std::cerr << "Usage: " << argv[0] << " <peer_id> <peer_ip> <peer_port> [...]\n";
    return EXIT_FAILURE;
  }

  /* Create a node listening on port 8000 */
  P2P_mesh_node node("node1", 8000);

  node.start();

  for (int i = 1; i < argc; i += 3) {
    std::string peer_id = argv[i];
    std::string peer_ip = argv[i + 1];
    int peer_port = std::atoi(argv[i + 2]);

    if (!node.connect_to_peer(peer_id, peer_ip, peer_port)) {
      std::cerr << "Unable to connect to " << peer_id << "\n";
    } else {
      std::cout << "Connected to " << peer_id << "\n";
    }
  }

  node.broadcast("Hello mesh network!");

  auto str = node.to_string();
  std::cout << str << std::endl;

  using Acceptor = caspaxos::Acceptor<std::string>;
  using Proposer = caspaxos::Proposer<std::string>;

  std::vector<std::shared_ptr<Acceptor>> acceptors;

  for (int i{}; i < 3; ++i) {
    acceptors.push_back(std::make_shared<Acceptor>());
  }

  Proposer proposer(0, acceptors);

  /* Define change function */
  auto change_fn = [](Option current) -> Value_type {
    if (!current) {
      return Value_type("initial");
    } else {
      return Value_type(current->get() + "_modified");
    }
  };

  /* Propose a value */
  auto result = proposer.propose(std::nullopt, std::move(change_fn));

  if (result) {
    std::cout << "Consensus reached: " << result->get() << std::endl;
  } else {
    std::cout << "Failed to reach consensus" << std::endl;
  }

  return EXIT_SUCCESS;
}