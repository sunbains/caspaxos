#pragma once

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>
#include <condition_variable>
#include <unordered_map>
#include <vector>
#include <array>
#include <cassert>

namespace msg {
  struct Simple { 
    std::string m_from_peer;
    std::string m_message;
  };

template <size_t Size> struct Deserialize;

static const char SHUTDOWN_MESSAGE[] = "_/SHUTDOWN\\_";

} // namespace msg
namespace net {

struct P2P_mesh {
    /**
     * @brief Constructor for the P2P mesh node. Throws an exception if the server socket fails to initialize.
     * 
     * @param[in] id The ID of the node
     * @param[in] listen_port The port to listen for incoming connections
     */
    P2P_mesh(const std::string& id, int listen_port);

    /**
     * @brief Destructor for the P2P mesh node
     */
    ~P2P_mesh() noexcept;

    /**
     * @brief Connect to a peer
     * 
     * @param[in] peer_id The ID of the peer
     * @param[in] peer_ip The IP address of the peer
     * @param[in] peer_port The port of the peer
     * 
     * @return true if the connection was successful, false otherwise
     */
    [[nodiscard]] bool connect_to_peer(const std::string& peer_id, const std::string& peer_ip, int peer_port) noexcept;

    /**
     * @brief Broadcast a message to all peers
     * 
     * @param[in] message The message to broadcast
     */
    void broadcast(const std::string& message) noexcept;

    /**
     * @brief Shutdown the P2P mesh node
     */
    void shutdown() noexcept;

    /**
     * @brief List all peers
     * 
     * @return A string containing the list of peers
     */
    [[nodiscard]] std::string to_string() noexcept;

    /**
     * @brief Start the P2P mesh node
     */
    void start();

private:

    /**
     * @brief Send a message to a peer
     * 
     * @param[in] socket The socket to send the message to
     * @param[in] ptr The message to send
     * @param[in] len The length of the message
     * 
     * @return 0 on success, errno on failure
     */
    int send(int socket, const char* ptr, size_t len) noexcept;

    /**
     * @brief Receive a message from a peer
     * 
     * @param[in] socket The socket to receive the message from
     * @param[in] deserializer The deserializer to use
     * 
     * @return 0 on success, errno on failure
     */
    template <size_t Buffer_size>
    int recv(int socket, msg::Deserialize<Buffer_size>& deserializer) noexcept;

    /**
     * @brief Initialize the server socket
     * 
     * @return 0 on success, errno on failure
     */
    [[nodiscard]] int initialize_server_socket() noexcept;

    /**
     * @brief Accept connections from peers
     */
    void accept_connections() noexcept;

    /**
     * @brief Handle a peer connection
     * 
     * @param[in] socket The socket of the connecting peer
     */
    void handle_peer_connection(int socket) noexcept;

    /**
     * @brief Process a message from a peer
     * 
     * @param[in] message The message to process
     */
    void process_message(const msg::Simple& message) noexcept;

    /**
     * @brief Disconnect a peer
     * 
     * @param[in] peer_id The ID of the peer to disconnect
     */
    void disconnect_peer(const std::string& peer_id) noexcept;

private:
    struct Peer {
      std::string m_ip{};
      int m_port{};
      int m_socket{};
      bool m_connected{};
      std::thread m_handler{};
    };

    int m_port{};

    std::mutex m_shutdown_mutex;

    int m_server_socket{};

    std::string m_node_id{};

    mutable std::mutex m_mutex;

    std::thread m_accept_thread{};

    mutable std::atomic<int> m_running{};

    std::condition_variable m_shutdown_cv;

    std::unordered_map<std::string, Peer> m_peers{};
};

} // namespace net