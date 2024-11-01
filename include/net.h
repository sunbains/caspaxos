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

namespace net {
struct Message {
    // FIXME: This is a temporary buffer size. We need to handle large messages better.
    constexpr static auto Buffer_size = 112400;
    using Buffer = std::array<char, Buffer_size>;

    /**
     * @brief Default constructor for the message
     */
    Message() noexcept = default;

    /**
     * @brief Constructor for the message
     * 
     * @param[in] from_peer The ID of the peer that sent the message
     * @param[in] message The message to send
     */
    Message(const std::string& from_peer, const std::string& message) noexcept : m_from_peer(from_peer), m_message(message) {
      assert(from_peer.length() + message.length() + 2 <= Buffer_size);
    }

    /**
     * @brief Serialize the message
     * 
     * @return A pair containing a pointer (inside buffer) to the serialized message and the size of the message
     */
    [[nodiscard]] std::pair<char*, size_t> serialize(Buffer& buffer) const noexcept;

    /**
     * @brief Deserialize the message
     */
    void deserialize(const Buffer& buffer) noexcept;

    /**
     * @brief Convert the message to a string
     * 
     * @return A string representation of the message
     */
    [[nodiscard]] std::string to_string() const noexcept;

    /**
     * @brief Clear the message
     */
    void clear() noexcept {
      m_from_peer.clear();
      m_message.clear();
    }

    std::string m_from_peer{};
    std::string m_message{};
};

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
     * @param[in] peer_id The ID of the connecting peer
     */
    void handle_peer_connection(std::string peer_id) noexcept;

    /**
     * @brief Process a message from a peer
     * 
     * @param[in] message The message to process
     */
    void process_message(const Message& message) noexcept;

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

    std::atomic<bool> m_running{};

    std::condition_variable m_shutdown_cv;

    std::mutex m_shutdown_mutex;

    int m_server_socket{};

    std::string m_node_id{};

    mutable std::mutex m_mutex;

    std::thread m_accept_thread{};

    std::unordered_map<std::string, Peer> m_peers{};
};

} // namespace net