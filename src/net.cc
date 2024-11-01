#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <cstdlib>
#include <signal.h>

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

std::pair<char*, size_t> Message::serialize(Buffer &buffer) const noexcept {
  assert(!m_from_peer.empty());

  auto ptr = buffer.data();

  *ptr = m_from_peer.length();
  ++ptr;

  std::memcpy(ptr, m_from_peer.c_str(), m_from_peer.length());
  ptr += m_from_peer.length();

  *ptr = m_message.length();
  ++ptr;

  std::memcpy(ptr, m_message.c_str(), m_message.length());
  ptr += m_message.length();

  return { buffer.data(), ptr - buffer.data() };
}

void Message::deserialize(const Buffer &buffer) noexcept {
  auto ptr = buffer.data();

  m_from_peer = std::string(ptr + 1, *ptr);
  ptr += m_from_peer.length() + 1;

  m_message = std::string(ptr + 1, *ptr);
}

std::string Message::to_string() const noexcept {
  return "from: " + m_from_peer + ", message: " + m_message;
}

P2P_mesh::P2P_mesh(const std::string& id, int listen_port)
  : m_port(listen_port), m_running(true), m_node_id(id) {

  if (initialize_server_socket() != 0) {
    throw std::runtime_error("Failed to initialize server socket");
  }
}

P2P_mesh::~P2P_mesh() noexcept {
  shutdown();
}

void P2P_mesh::start() {
  /* Ignore SIGPIPE globally for this process  */
  ::signal(SIGPIPE, SIG_IGN);

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

  auto buffer = Message::Buffer{};
  auto message = Message{m_node_id, m}.serialize(buffer);

  for (auto& peer : m_peers) {
    if (peer.second.m_connected) {
      ::send(peer.second.m_socket, message.first, message.second, 0);
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
    auto buffer = Message::Buffer{};

    recv(client_socket, buffer.data(), buffer.size() - 1, 0);

    auto message = Message();
    
    message.deserialize(buffer);

    // Add new peer
    std::lock_guard<std::mutex> lock(m_mutex);

    auto& peer = m_peers[message.m_from_peer];

    peer.m_ip = inet_ntoa(client_addr.sin_addr);
    peer.m_port = ntohs(client_addr.sin_port);
    peer.m_socket = client_socket;
    peer.m_connected = true;
    peer.m_handler = std::thread(&P2P_mesh::handle_peer_connection, this, message.m_from_peer);
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

void P2P_mesh::process_message(const Message& message) noexcept {
  auto buffer = Message::Buffer{};
  auto msg = message.serialize(buffer);

  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto& peer : m_peers) {
    if (peer.first != message.m_from_peer && peer.second.m_connected) {
      const auto sz = send(peer.second.m_socket, msg.first, msg.second, 0);

      if (sz < 0) {
        std::cerr << "Failed to forward message to peer " << peer.first << std::endl;

        switch (errno) {
          case EPIPE:
          case ECONNRESET:
            std::cerr << "Connection reset!" << std::endl;
            peer.second.m_connected = false;
            ::close(peer.second.m_socket);
            break;
          case EINTR:
          case EAGAIN:
            continue;
          default:
            std::cerr << "send() returned errno: " << errno << std::endl;
            return;
        }
      }
    }
  }

  std::cout << "Message from " << message.m_from_peer << ": " << message.m_message << "'" << std::endl;
  std::cout.flush();
}

void P2P_mesh::handle_peer_connection(std::string peer_id) noexcept {
  auto buffer = Message::Buffer{};

  while (m_running.load()) {
    auto bytes_read = recv(m_peers[peer_id].m_socket, buffer.data(), buffer.size() - 1, 0);

    if (bytes_read < 0) {

      switch (errno) {
        case EINTR:
        case EAGAIN:
          continue;
        case EPIPE:
        case ECONNRESET:
          disconnect_peer(peer_id);
          return;
        default:
          std::cerr << "recv(" << m_peers[peer_id].m_socket << ") returned errno: " << errno << std::endl;
          return;
      }

    } else if (bytes_read == 0) {

      continue;

    } else {
      auto message = Message();

      message.deserialize(buffer);

      if (message.m_message == "_$SHUTDOWN$_") {
        disconnect_peer(peer_id);
        break;
      } else {
        process_message(message);
      }
    }
  }
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
        auto buffer = Message::Buffer{};
        auto msg = Message{m_node_id, "_$SHUTDOWN$_"}.serialize(buffer);

        /* Send shutdown signal */
        send(peer.second.m_socket, msg.first, msg.second, 0);
        
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