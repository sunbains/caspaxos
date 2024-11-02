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


namespace msg {
/** 
 * The message is encoded as:
 * 
 *  <msg_length><msg>
 * 
 * // FIXME: Use variable length encoding for the length.
 * 
 * Currently the length is 4 bytes.
 */
template <size_t Buffer_size>
struct Serialize {
  using Buffer = std::array<char, Buffer_size>;
  using Serialized = std::pair<char*, uint32_t>; 

  /**
   * @brief Serialize the message
   * 
   * @return A pair containing a pointer (inside buffer) to the serialized message and the size of the message
   */
  [[nodiscard]] Serialized operator()(Buffer& buffer, const std::string& msg) const noexcept {
    assert(sizeof(uint32_t) + msg.length() <= Buffer_size);

    auto ptr = buffer.data();

    uint32_t len = htonl(msg.length());
    std::memcpy(ptr, &len, sizeof(len));
    ptr += sizeof(len);

    std::memcpy(ptr, msg.c_str(), msg.length());
    ptr += msg.length();

    return { buffer.data(), ptr - buffer.data() };
  }
};

template <size_t Size>
struct Deserialize {
  constexpr static auto Buffer_size = Size;
  using Buffer = std::array<char, Buffer_size>;

  /**
   * @brief Default constructor for the message
   */
  Deserialize() noexcept {
    m_buffers.reserve(8);
  }

  /**
   * @brief Deserialize the message
   * 
   * @param[in] received_size The number of bytes received
   * 
   * @return true if the message has been fully received, false otherwise
   */
  [[nodiscard]] bool operator()(size_t received_size) noexcept {
    assert(received_size > 0);
    assert(m_len == 0 || m_len + sizeof(m_len) >= m_pos);

    const std::pair<size_t, size_t> pos = { m_pos / Buffer_size, m_pos % Buffer_size };

    assert(pos.second + received_size <= Buffer_size);

    /* Read length */
    if (m_pos < sizeof(m_len)) {
      assert(m_len == 0);

      if (m_pos + received_size < sizeof(m_len)) {
        m_pos += received_size;
        return false;
      } else {
        const auto net_len = *reinterpret_cast<uint32_t*>(m_buffers.front().data());
        m_len = ntohl(net_len);
      }
    }

    m_pos += received_size;

    assert(m_len == 0 || m_len + sizeof(m_len) >= m_pos);
    assert(m_buffers.size() - 1 == m_pos / Buffer_size || (m_pos % Buffer_size) == 0);

    return m_len + sizeof(m_len) == m_pos;
  }

  /**
   * @brief Check if we have received the full message
   * 
   * @return true if we have received the full message, false otherwise
   */
  bool received_full_message() const noexcept {
    return m_pos >= sizeof(m_len) && (m_len + sizeof(m_len)) == m_pos;
  }

  /**
   * @brief Add a new buffer to the buffer list
   * 
   * @return A pointer to the new buffer
   */
  Buffer *add_buffer() noexcept {
    m_buffers.emplace_back(Buffer());
    return &m_buffers.back();
  }

  /**
   * @brief Check if the current buffer is full
   * 
   * @return true if the current buffer is full, false otherwise
   */
  bool overflowed() const noexcept {
    return (m_pos % Buffer_size) == 0 && m_len + sizeof(m_len) > m_pos;
  }

  /**
   * @brief Number of bytes needed to fill the current buffer or the message
   * whicheve is smaller.
   * 
   * @return Number of bytes needed to fill the current buffer or the message
   */
  uint32_t need() const noexcept {
    return std::min(m_len + sizeof(m_len) - m_pos, Buffer_size - (m_pos % Buffer_size));
  }

  /**
   * @brief Clear the buffer, reset the state to empty.
   */
  void clear() noexcept {
    m_pos = 0;
    m_len = 0;
    m_buffers.clear();
  }

  /**
   * @brief Check if the message is empty
   * 
   * @return true if the message is empty, false otherwise
   */
  bool empty() const noexcept {
    return parsing_not_started() || m_buffers.empty();
  }

  /**
   * @brief Check if the message parsing has not started
   * 
   * @return true if the message parsing has not started, false otherwise
   */
  bool parsing_not_started() const noexcept {
    return m_pos == 0 && m_len == 0;
  }

  /**
   * @brief Iterator over the buffers
   */
  struct Iterator {
    using pointer = Buffer::value_type*;
    using value_type = Buffer::value_type;
    using reference = Buffer::value_type&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    Iterator() {}

    /**
     * @brief Constructor for the iterator
     * 
     * @param[in] buffers List of buffers
     * @param[in] len Length of the message, including the length field
     * @param[in] pos Position in the buffer from where to start, including the length field
     */
    Iterator(std::vector<Buffer> &buffers, uint32_t len, uint32_t pos = 0) : m_len(len), m_buffers(buffers) {
      if (m_buffers.empty()) {  
        m_ptr = nullptr;
      } else {
        m_ptr = m_buffers.front().data();
        m_end_ptr = &m_buffers[m_len / Buffer_size][m_len % Buffer_size];
        m_ptr = advance(m_ptr + pos);
      }
    }

    reference operator*() const noexcept {
      return *m_ptr;
    }

    pointer operator->() const noexcept {
      return m_ptr;
    }

    Iterator& operator++() noexcept {
      m_ptr = advance(m_ptr + 1);

      return *this;
    }

    Iterator operator++(int) noexcept {
      auto tmp = *this;

      m_ptr = advance(m_ptr + 1);

      return *tmp;
    }

    bool operator==(const Iterator& other) const noexcept {
      return m_ptr == other.m_ptr;
    }

    bool operator!=(const Iterator& other) const noexcept {
      return !(*this == other);
    }

    /**
     * @brief Check if the pointer has overflowed the current buffer
     * 
     * @param[in] ptr Pointer to check
     * 
     * @return Pointer to the next buffer or nullptr if we are at the end
     */
    pointer advance(pointer ptr) noexcept {
      if (ptr >= m_buffers[m_current].data() + m_buffers[m_current].size()) {
        if (m_current == m_buffers.size() - 1 && ptr >= m_end_ptr) {  
          ptr = nullptr;
        } else {
          ptr = m_buffers[++m_current].data();
        }
      }

      return ptr;
    }


    /**
     * @brief Number of bytes in the message, excluding the length field.
     */
    uint32_t m_len{};

    /**
     * @brief Current buffer index
     */
    uint32_t m_current{};

    /**
     * @brief Pointer inside the current buffer
     */
    pointer m_ptr{};

    /**
     * @brief Pointer to the end of the message
     */
    pointer m_end_ptr{};

    /**
     * @brief List of buffers
     */
    std::vector<Buffer> &m_buffers;
  };

  Iterator begin() noexcept {
    return Iterator(m_buffers, m_pos, 0);
  }

  Iterator end() noexcept {
    return Iterator();
  }

  /**
   * @brief Return an iterator to the buffer at index i
   * 
   * @param[in] i Index into the buffer
   * 
   * @return an iterator to the buffer at index i
   */
  Iterator operator[](size_t i) noexcept {
    return Iterator(m_buffers, m_pos, i);
  } 

  /**
   * @brief Offset into the buffer where the peer ID is stored
   * 
   * The peer ID is stored at offset 2 in the buffer, it's implied..
   */
  std::vector<Buffer> m_buffers{};
 
  /**
   * @brief Length of the message payload.
   */
  uint32_t m_len{};

  /**
   * @brief Logical current buffer index. Given the fixed size buffer we
   * can translate to the buffer index and the offset in that buffer.
   */
  uint32_t m_pos{};
};

} // namespace msg

namespace net {

P2P_mesh::P2P_mesh(const std::string& id, int listen_port)
  : m_port(listen_port),  m_node_id(id) {

  if (initialize_server_socket() != 0) {
    throw std::runtime_error("Failed to initialize server socket");
  }
}

P2P_mesh::~P2P_mesh() noexcept {
  shutdown();
}

void P2P_mesh::start() {
  std::cerr << "Starting P2P mesh" << std::endl;
  /* Ignore SIGPIPE globally for this process  */
  ::signal(SIGPIPE, SIG_IGN);

  /* Start accepting connections in a separate thread. */
  m_running.store(true);

  m_accept_thread = std::thread(&P2P_mesh::accept_connections, this);
}

int P2P_mesh::send(int socket, const char* ptr, size_t len) noexcept {
  while (len > 0) {
    const auto sent = ::send(socket, ptr, len, 0);

    if (sent < 0) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
        continue;
      default:
        return errno;
      }
    } else if (size_t(sent) <= len) {
      assert(len >= size_t(sent));

      ptr += sent;
      len -= sent;
    }
  }

  assert(len == 0 || !m_running);

  return 0;
}

template <size_t Buffer_size>
int P2P_mesh::recv(int socket, msg::Deserialize<Buffer_size>& deserializer) noexcept {
  auto buffer = deserializer.add_buffer();
  auto ptr = buffer->data();
  auto len = sizeof(deserializer.m_len);

  while (m_running) {

    auto bytes_read = ::recv(socket, ptr, len, 0);

    if (bytes_read < 0) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
        continue;
      default:
        return errno;;
      }

    } else if (bytes_read == 0) {

      continue;

    } else if (!deserializer(bytes_read)) {

      if (deserializer.overflowed()) {
        ptr = deserializer.add_buffer()->data();
      } else {
        ptr += bytes_read;
      }

      len = deserializer.need();

    } else {
      return 0;
    }
  }

  return 0;
}

bool P2P_mesh::connect_to_peer(const std::string& peer_id, const std::string& peer_ip, int peer_port) noexcept {
  assert(peer_id != m_node_id);

  if (!m_running) {
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

  std::string message{};

  message.push_back(m_node_id.length());
  message.append(m_node_id);

  using Serializer = msg::Serialize<1024>;
  auto serializer = Serializer{};
  auto buffer = Serializer::Buffer{};
  auto msg = serializer(buffer, message);

  auto ret = send(sock, msg.first, msg.second);

  if (ret != 0) {
    std::cerr << "Failed to send message to peer " << peer_id << " errno: " << ret << ": " << strerror(ret) << std::endl;
    return false;
  }

  /* Add peer to our list, and start handler thread. */
  std::lock_guard<std::mutex> lock(m_mutex);

  auto& peer = m_peers[peer_id];

  peer.m_ip = peer_ip;
  peer.m_port = peer_port;
  peer.m_socket = sock;
  peer.m_connected = true;
  peer.m_handler = std::thread(&P2P_mesh::handle_peer_connection, this, sock);

  return true;
}

void P2P_mesh::broadcast(const std::string& m) noexcept {
  std::string message{};

  message.push_back(m_node_id.length());
  message.push_back(m.length());
  message.append(m_node_id);
  message.append(m);

  using Serializer = msg::Serialize<202400>;
  auto serializer = Serializer{};
  auto buffer = Serializer::Buffer{};
  auto msg = serializer(buffer, message);

  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto& peer : m_peers) {
    if (peer.second.m_connected) {
      auto ret = send(peer.second.m_socket, msg.first, msg.second);

      if (ret != 0) {
        std::cerr << "Failed to send message to peer " << peer.first << " errno: " << ret << ": " << strerror(ret) << std::endl;
        peer.second.m_connected = false;
      }
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

   int opt{1};
   auto ret = ::setsockopt(m_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

   if (ret != 0) {
     std::cerr << "Failed to set socket options errno: " << ret << ": " << strerror(ret) << std::endl;
     return ret;
   }

   if (::bind(m_server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
     std::cerr << "Failed to bind server socket\n";
     return errno;
   }

   if (::listen(m_server_socket, 10) < 0) {
     std::cerr << "Failed to listen on server socket\n";
     return errno;
   } else {
     std::cerr << "Listening on port " << m_port << std::endl;
     return 0;
   }
}

void P2P_mesh::accept_connections() noexcept {
  while (m_running) {
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
      switch (errno) {
        case EINTR:
        case EAGAIN:
          continue;
        default:
          std::cerr << "select() returned errno: " << errno << ": " << strerror(errno) << std::endl;
          break;
      }
      break;
    }

    if (result == 0) {
      /* Timeout, check running flag */
      continue;
    }

    sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);

    auto client_socket = accept(m_server_socket, (struct sockaddr*)&client_addr, &client_addr_len);

    if (!m_running) {
      break;
    }

    if (client_socket < 0) {
      std::cerr << "Failed to accept connection" << std::endl;
      continue;
    }

    {
      struct timeval tv;

      /* 5 second timeout */
      tv.tv_sec = 5;
      tv.tv_usec = 0;

      auto ret = ::setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

      if (ret != 0) {
        std::cerr << "Failed to set socket option SO_SNDTIMEO errno: " << ret << ": " << strerror(ret) << std::endl;
        continue;
      }
    }

    {
      struct timeval tv;

      /* 1 second timeout */
      tv.tv_sec = 1;
      tv.tv_usec = 0;

      auto ret = ::setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

      if (ret != 0) {
        std::cerr << "Failed to set socket option SO_RCVTIMEO errno: " << ret << ": " << strerror(ret) << std::endl;
        continue;
      }
    }

    // Receive peer ID
    using Deserializer = msg::Deserialize<1024>;
    auto deserializer = Deserializer{};
    auto ret = recv<Deserializer::Buffer_size>(client_socket, deserializer);

    if (ret != 0) {
      std::cerr << "Failed to read peer ID: " << strerror(ret) << std::endl;
      continue;
    }

    /* We have received the full message */
    assert(deserializer.received_full_message());

    auto it = deserializer[sizeof(Deserializer::m_len)];
    auto peer_id_len = *it;

    ++it;

    std::string peer_id(it, deserializer[sizeof(Deserializer::m_len) + 1 + peer_id_len]);

    assert(peer_id != m_node_id);

    // Add new peer
    std::lock_guard<std::mutex> lock(m_mutex);

    auto& peer = m_peers[peer_id];

    peer.m_ip = inet_ntoa(client_addr.sin_addr);
    peer.m_port = ntohs(client_addr.sin_port);
    peer.m_socket = client_socket;
    peer.m_connected = true;
    peer.m_handler = std::thread(&P2P_mesh::handle_peer_connection, this, client_socket);
  }
}

void P2P_mesh::disconnect_peer(const std::string& peer_id) noexcept {
  std::cerr << "Peer " << peer_id << " disconnected" << std::endl;

  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_peers.count(peer_id) > 0) {
    auto& peer = m_peers[peer_id];

    if (peer.m_connected) {
      peer.m_connected = false;
      ::close(peer.m_socket);
    }
  }
}

void P2P_mesh::process_message(const msg::Simple& simple_msg) noexcept {
  using Serializer = msg::Serialize<1024>;

  Serializer serializer{};
  auto buffer = Serializer::Buffer{};
  std::string message{};

  message.push_back(simple_msg.m_from_peer.length());
  message.push_back(simple_msg.m_message.length());
  message.append(simple_msg.m_from_peer);
  message.append(simple_msg.m_message);

  auto msg = serializer(buffer, message);

  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto& peer : m_peers) {
    if (peer.first != simple_msg.m_from_peer && peer.second.m_connected) {
      auto ret = send(peer.second.m_socket, msg.first, msg.second);

      if (ret != 0) {
        std::cerr << "Failed to forward message to peer " << peer.first << " errno: " << ret << ": " << strerror(ret) << std::endl;
        peer.second.m_connected = false;
      }
    }
  }

  for (auto it{m_peers.begin()}; it != m_peers.end(); ++it) {
    assert(it->first != m_node_id);
    if (!it->second.m_connected) {
      ::close(it->second.m_socket);
      //it = m_peers.erase(it);
    } else {
      //++it;
    }
  }

  std::cout << "Message from " << simple_msg.m_from_peer << " : " << simple_msg.m_message << std::endl;
}

void P2P_mesh::handle_peer_connection(int socket) noexcept {
  using Deserializer = msg::Deserialize<1024>;
  auto deserializer = Deserializer{};

  while (m_running) {

    deserializer.clear();

    auto ret = recv<Deserializer::Buffer_size>(socket, deserializer);

    if (ret != 0) { 
      std::cerr << "Failed to read message from peer: " << strerror(ret) << std::endl;
      break;
    }

    assert(deserializer.received_full_message() || deserializer.parsing_not_started() || !m_running);

    if (deserializer.parsing_not_started() || !m_running) {
      break;
    }

    auto it = deserializer[sizeof(Deserializer::m_len)];

    uint8_t peer_id_len = *it;
    ++it;

    uint8_t msg_len = *it;
    ++it;

    msg::Simple simple_msg{};

    {
      auto it = deserializer[sizeof(Deserializer::m_len) + 2];
      simple_msg.m_from_peer = std::string(it, deserializer[sizeof(Deserializer::m_len) + 2 + peer_id_len]);
    }

    {
      auto it = deserializer[sizeof(Deserializer::m_len) + 2 + peer_id_len];
      simple_msg.m_message = std::string(it, deserializer[sizeof(Deserializer::m_len) + 2 + peer_id_len + msg_len]);
    }

    if (simple_msg.m_message == msg::SHUTDOWN_MESSAGE) {
      std::cerr << "Peer " << simple_msg.m_from_peer << " received shutdown message" << std::endl;
      disconnect_peer(simple_msg.m_from_peer);
      break;
    } else {
      process_message(simple_msg);
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

    for (auto& [peer_id, peer] : m_peers) {
      if (!peer.m_connected) {
        continue;
      }

      using Serializer = msg::Serialize<64>;

      Serializer serializer{};
      auto buffer = Serializer::Buffer{};

      std::string message{};

      message.push_back(m_node_id.length());
      message.push_back(sizeof(msg::SHUTDOWN_MESSAGE) - 1);
      message.append(m_node_id);
      message.append(msg::SHUTDOWN_MESSAGE);

      auto msg = serializer(buffer, message);

      assert(peer.m_socket > 0);

        /* Send shutdown signal */
      auto ret = send(peer.m_socket, msg.first, msg.second);

      if (ret != 0) {
          std::cerr << "Failed to send shutdown message to peer " << peer_id << " errno: " << ret << ": " << strerror(ret) << std::endl;
      }
        
      ::close(peer.m_socket);
      peer.m_connected = false;
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