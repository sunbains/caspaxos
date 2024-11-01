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

namespace net {

P2P_mesh::P2P_mesh(const std::string& id, int listen_port)
  : m_port(listen_port), m_running(true), m_node_id(id) {

  if (initialize_server_socket() != 0) {
    throw std::runtime_error("Failed to initialize server socket");
  }
}

P2P_mesh::~P2P_mesh() noexcept {
  shutdown();
}

void P2P_mesh::start() noexcept {
  /* Start accepting connections in a separate thread. */
  m_running.store(true);
  m_accept_thread = std::thread(&P2P_mesh::accept_connections, this);
}

bool P2P_mesh::connect_to_peer(const std::string& peer_id, const std::string& peer_ip, int peer_port) noexcept {
  if (!m_running.load()) {
    return false;
  }

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

  /* Add peer to our list, and start handler thread. */
  std::lock_guard<std::mutex> lock(m_mutex);

  auto& peer = m_peers[peer_id];

  peer.m_ip = peer_ip;
  peer.m_port = peer_port;
  peer.m_socket = sock;
  peer.m_connected = true;
  peer.m_handler = std::thread(&P2P_mesh::handle_peer_connection, this, peer_id);

  return true;
}

void P2P_mesh::broadcast(const std::string& m) noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);

  const std::string message = "FROM " + m_node_id + ": " + m;

  for (auto& peer : m_peers) {
    if (peer.second.m_connected) {
      ::send(peer.second.m_socket, message.c_str(), message.length(), 0);
    }
  }
}

std::string P2P_mesh::to_string() noexcept {
  std::ostringstream os;
  std::lock_guard<std::mutex> lock(m_mutex);

  os << "peers: { "; 

  for (const auto& peer : m_peers) {
    os << "id: { " << peer.first
       << ", ip: " << peer.second.m_ip
       << ", port: " << peer.second.m_port
       << ", connected: "
       << (peer.second.m_connected ? "true" : "false")
       << "}, ";
  }

  os << "}";

  return os.str();
}

int P2P_mesh::initialize_server_socket() noexcept {
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

void P2P_mesh::accept_connections() noexcept {
  while (m_running.load()) {
    fd_set read_set;

    FD_ZERO(&read_set);
    FD_SET(m_server_socket, &read_set);

    // Use select with timeout to make accept interruptible
    struct timeval timeout;

    /* 1 second timeout */
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int result = select(m_server_socket + 1, &read_set, nullptr, nullptr, &timeout);

    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    if (result == 0) continue;  // Timeout, check running flag

    sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);

    auto client_socket = accept(m_server_socket, (struct sockaddr*)&client_addr, &client_addr_len);

    if (!m_running.load()) {
      break;
    }

    if (client_socket < 0) {
      std::cerr << "Failed to accept connection" << std::endl;
      continue;
    }

    struct timeval tv;

    /* 5 second timeout */
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    ::setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Receive peer ID
    std::array<char, 1024> buffer{};

    recv(client_socket, buffer.data(), buffer.size() - 1, 0);

    std::string peer_id(buffer.data());

    // Add new peer
    std::lock_guard<std::mutex> lock(m_mutex);

    auto& peer = m_peers[peer_id];

    peer.m_ip = inet_ntoa(client_addr.sin_addr);
    peer.m_port = ntohs(client_addr.sin_port);
    peer.m_socket = client_socket;
    peer.m_connected = true;
    peer.m_handler = std::thread(&P2P_mesh::handle_peer_connection, this, peer_id);
  }
}

void P2P_mesh::disconnect_peer(const std::string& peer_id) noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_peers.count(peer_id) > 0) {
    auto& peer = m_peers[peer_id];

    if (peer.m_connected) {
      peer.m_connected = false;
      ::close(peer.m_socket);
    }
  }
}

void P2P_mesh::handle_peer_connection(std::string peer_id) noexcept {
  std::array<char, 1024> buffer{};

  while (m_running.load()) {
    memset(buffer.data(), 0, buffer.size());
    int bytes_read = recv(m_peers[peer_id].m_socket, buffer.data(), buffer.size() - 1, 0);


    if (bytes_read <= 0) {
      disconnect_peer(peer_id);
      break;
    }

    std::string message(buffer.data());

    if (message == "_$SHUTDOWN$_") {
      disconnect_peer(peer_id);
      break;
    }

    process_message(peer_id, message);
  }
}

void P2P_mesh::process_message(const std::string& from_peer, const std::string& message) noexcept {

  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto& peer : m_peers) {
    if (peer.first != from_peer && peer.second.m_connected) {
      std::string forward_msg = "FROM " + from_peer + ": " + message;
      auto sz = send(peer.second.m_socket, forward_msg.c_str(), forward_msg.length(), 0);

      if (sz < 0) {
        std::cerr << "Failed to forward message to peer " << peer.first << std::endl;

        switch (errno) {
          case EPIPE:
          case ECONNRESET:
            disconnect_peer(peer.first);
            break;
          case EINTR:
          case EAGAIN:
            continue;
          default:
            std::cerr << "send() returned errno: " << errno << std::endl;
            break;
        }
      }
    }
  }

  // Print received message
  std::cout << "Message from " << from_peer << ": " << message << "'\n";
}

void P2P_mesh::shutdown() noexcept {
  if (!m_running.exchange(false)) {
    /* Already shut down */
    return;
  }

  /* Send shutdown message to all peers */
  {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& peer : m_peers) {
      if (peer.second.m_connected) {
        /* Send shutdown signal */
        std::string shutdownMsg = "_$SHUTDOWN$_";
        send(peer.second.m_socket, shutdownMsg.c_str(), shutdownMsg.length(), 0);
        
        ::close(peer.second.m_socket);
        peer.second.m_connected = false;
      }
    }
  }

  /* Close server socket to interrupt accept() */
  ::close(m_server_socket);

  /* Wait for accept thread to finish */
  if (m_accept_thread.joinable()) {
    m_accept_thread.join();
  }

  /* Wait for all peer handler threads to finish */
  {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& peer : m_peers) {
      if (peer.second.m_handler.joinable()) {
        peer.second.m_handler.join();
      }
    }

    m_peers.clear();
  }

  /* Notify any waiting threads */
  {
    std::lock_guard<std::mutex> lock(m_shutdown_mutex);

    m_shutdown_cv.notify_all();
  }

  std::cout << "Node " << m_node_id << " shut down successfully" << std::endl;
}

} // namespace net