#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <cstdlib>
#include <array>

#include "net.h"

/**
 * Mesh topology where each node can connect to multiple peers
 * Asynchronous message handling using threads
 * Automatic message forwarding between peers
 * Connection management and cleanup
 * Thread-safe peer list management
 * Basic error handling
 * -
 * Uses TCP sockets for reliable communication
 * Supports dynamic peer discovery and connection
 * Handles peer disconnections gracefully
 * Includes message broadcasting capability
 * Thread-safe design for concurrent operations
*/

P2P_mesh_node::P2P_mesh_node(const std::string& id, int listen_port)
  : m_port(listen_port), m_running(true), m_node_id(id) {

  if (initialize_server_socket() != 0) {
    throw std::runtime_error("Failed to initialize server socket");
  }
}

P2P_mesh_node::~P2P_mesh_node() noexcept {
  m_running = false;
    
  /* Close all connections */
  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto& peer : m_peers) {
    if (peer.second.m_connected) {
      ::close(peer.second.m_socket);
    }
  }

  /* Close server socket */
  ::close(m_server_socket);

  /* Join all worker threads. */
  for (auto& thread : m_threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

void P2P_mesh_node::start() noexcept {
  /* Start accepting connections in a separate thread. */
  m_threads.emplace_back(&P2P_mesh_node::accept_connections, this);
}

bool P2P_mesh_node::connect_to_peer(const std::string& peer_id, const std::string& peer_ip, int peer_port) noexcept {
  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock < 0) {
    return false;
  }

  sockaddr_in peer_addr{};

  peer_addr.sin_family = AF_INET;
  peer_addr.sin_port = htons(peer_port);
        
  if (::inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr) <= 0) {
    return false;
  }

  if (::connect(sock, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
    return false;
  }

  /* Send our node ID */
  ::send(sock, m_node_id.c_str(), m_node_id.length(), 0);

  /* Add peer to our list */
  std::lock_guard<std::mutex> lock(m_mutex);

  m_peers[peer_id] = { peer_ip, peer_port, sock, true};

  /* Start handler thread for this peer */
  m_threads.emplace_back(&P2P_mesh_node::handle_peer_connection, this, peer_id);

  return true;
}

void P2P_mesh_node::broadcast(const std::string& m) noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);

  const std::string message = "FROM " + m_node_id + ": " + m;

  for (auto& peer : m_peers) {
    if (peer.second.m_connected) {
      ::send(peer.second.m_socket, message.c_str(), message.length(), 0);
    }
  }
}

std::string P2P_mesh_node::to_string() noexcept {
  std::ostringstream os;
  std::lock_guard<std::mutex> lock(m_mutex);

  os << "peers: { "; 

  for (const auto& peer : m_peers) {
    os << "id: { " << peer.first
       << ", ip: " << peer.second.m_port
       << ", port: " << peer.second.m_port
       << ",connected: "
       << (peer.second.m_connected ? "true" : "false")
       << "}, ";
  }

  os << "}";

  return os.str();
}

int P2P_mesh_node::initialize_server_socket() noexcept {
  m_server_socket = ::socket(AF_INET, SOCK_STREAM, 0);

   if (m_server_socket < 0) {
     return errno;
   }

   sockaddr_in server_addr{};

   server_addr.sin_family = AF_INET;
   server_addr.sin_addr.s_addr = INADDR_ANY;
   server_addr.sin_port = htons(m_port);

   int opt = 1;
   ::setsockopt(m_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

   if (::bind(m_server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
     std::cerr << "Failed to bind server socket\n";
     return errno;
   }

   if (::listen(m_server_socket, 10) < 0) {
     std::cerr << "Failed to listen on server socket\n";
     return errno;
   }

   return 0;
}

void P2P_mesh_node::accept_connections() noexcept {
  while (m_running) {
    sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    int client_socket = ::accept(m_server_socket, (struct sockaddr*)&client_addr, &client_addr_len);

    if (client_socket < 0) {
      std::cerr << "Failed to accept connection\n";
      continue;
    }

    /* Receive peer ID. */
    std::array<char, 1024> buffer{};

    recv(client_socket, buffer.data(), buffer.size() - 1, 0);

    std::string peer_id(buffer.data());

    /* Add new peer. */
    std::lock_guard<std::mutex> lock(m_mutex);

    m_peers[peer_id] = {
      inet_ntoa(client_addr.sin_addr),
      ntohs(client_addr.sin_port),
      client_socket,
      true
    };

    /* Start handler thread for this peer. */
    m_threads.emplace_back(&P2P_mesh_node::handle_peer_connection, this, peer_id);
  }
}

void P2P_mesh_node::handle_peer_connection(std::string peer_id) noexcept {
  std::array<char, 1024> buffer{};

  while (m_running) {
    memset(buffer.data(), 0, buffer.size());
    int bytes_read = recv(m_peers[peer_id].m_socket, buffer.data(), buffer.size() - 1, 0);
            
    if (bytes_read <= 0) {
      // Connection closed or error
      std::lock_guard<std::mutex> lock(m_mutex);

      m_peers[peer_id].m_connected = false;
      ::close(m_peers[peer_id].m_socket);

      break;
    }

    // Process received message
    std::string message(buffer.data());

    process_message(peer_id, message);
  }
}

void P2P_mesh_node::process_message(const std::string& from_peer, const std::string& message) noexcept {

  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto& peer : m_peers) {
    if (peer.first != from_peer && peer.second.m_connected) {
      std::string forward_msg = "FROM " + from_peer + ": " + message;
      send(peer.second.m_socket, forward_msg.c_str(), forward_msg.length(), 0);
    }
  }

  // Print received message
  std::cout << "Message from " << from_peer << ": " << message << "'\n";
}


