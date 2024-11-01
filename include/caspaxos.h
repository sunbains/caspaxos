#pragma once

#include <iostream>
#include <map>
#include <vector>
#include <optional>
#include <memory>
#include <cassert>
#include <functional>
#include <random>

namespace caspaxos {

using Ballot_number = uint64_t;
using Promise_number = Ballot_number;

constexpr auto Null_ballot = std::numeric_limits<Ballot_number>::max();
constexpr auto Null_promise = std::numeric_limits<Promise_number>::max();

template<typename T>
struct Value {
  Value() : m_exists(false) {}
  explicit Value(const T& d) : m_data(d), m_exists(true) {}

  T m_data{};
  bool m_exists{};
};

/**
 * @brief Message sent by proposer during the prepare phase
 */
struct Prepare_message {
  Ballot_number m_ballot{Null_ballot};
};

/**
 * @brief Message sent by proposer during the accept phase
 * 
 * @tparam T The type of the value being proposed
 */
template<typename T>
struct Accept_message {
  Ballot_number m_ballot{Null_ballot};
  Value<T> m_value{};
};

/**
 * @brief Response to a prepare message from an acceptor
 * 
 * @tparam T The type of the value being handled
 */
template<typename T>
struct Prepare_response {
  bool m_success{};
  Promise_number m_promise{Null_promise};
  std::optional<Value<T>> m_accepted_value{};
};
    
/**
 * @brief Response to an accept message from an acceptor
 * 
 * @tparam T The type of the value being handled
 */
template<typename T>
struct Accept_response {
  bool m_success{};
  Promise_number m_promise{Null_promise};
};

template<typename T>
struct Acceptor {

  /**
   * @brief Handles a prepare request from a proposer
   *
   * @param[in] msg The prepare message containing the ballot number
   *
   * @return Prepare response
   */
  [[nodiscard]] Prepare_response<T> prepare(const Prepare_message& msg) noexcept {
     if (msg.m_ballot > m_promise) {
      m_promise = msg.m_ballot;

      return Prepare_response<T>{
        true,
        m_promise,
        m_accepted_value.m_exists ? std::optional<Value<T>>(m_accepted_value) : std::nullopt
      };
    } else {
      return Prepare_response<T>{false, m_promise, std::nullopt};
    }
  }

  /** 
   * @brief Handles an accept request from a proposer
   *
   * @param[in] msg Accept message containing the ballot number and proposed value
   *
   * @return Accept response
   */
  [[nodiscard]] Accept_response<T> accept(const Accept_message<T>& msg) noexcept {
    if (msg.m_ballot >= m_promise) {
      m_promise = msg.m_ballot;
      m_accepted_value = msg.m_value;

      return Accept_response<T>{true, m_promise};
    } else {
      return Accept_response<T>{false, m_promise};
    }
  }

private:
  /** The highest promise made by this acceptor */
  Promise_number m_promise{};

  /** The last accepted value */
  Value<T> m_accepted_value{};
};

template<typename T>
struct Proposer {

  using Acceptors = std::vector<std::shared_ptr<Acceptor<T>>>;
  using Change_fn = std::function<T(const std::optional<T>&)>;

  /**
   * @brief Constructs a proposer with a list of acceptors
   *
   * @param[in] acceptors_list Vector of acceptor pointers
   */
  explicit Proposer(const Acceptors& acceptors_list)
      : m_acceptors(acceptors_list) {}

  /**
   * @brief Proposes a value modification using a transform function
   *
   * @param[in] transform Function that takes the current value (if any) and returns the new value
   *
   * @return Optional containing the accepted value if successful
   *
   * @details This function implements the two-phase CASPaxos protocol:
   *  1. Prepare phase: Sends prepare messages to all acceptors
   *  2. Accept phase: If majority accepts prepare, sends accept messages with transformed value
   *
   * The function will retry with a new ballot number if either phase fails to achieve
   * majority acceptance.
   */
  [[nodiscard]] std::optional<T> propose(Change_fn&& transform) noexcept {
    for (;;) {
      m_ballot = generate_ballot();

      /* Phase 1: Prepare */
      auto prepare_msg = Prepare_message{m_ballot};
      std::vector<Prepare_response<T>> prepare_responses;

      for (const auto& acceptor : m_acceptors) {
        auto response = acceptor->prepare(prepare_msg);

        if (response.m_success) {
          prepare_responses.push_back(response);
        }
      }

      if (prepare_responses.size() <= m_acceptors.size() / 2) {
        /* Failed to get majority */
        continue;
      }

      /* Find highest ballot accepted value */
      std::optional<Value<T>> highest_accepted;

      for (const auto& response : prepare_responses) {

        if (response.m_accepted_value.has_value()) {

          if (!highest_accepted.has_value() || response.m_promise > m_ballot) {
            highest_accepted = response.m_accepted_value;
            m_ballot = response.m_promise;
          }
        }
      }

      /* Transform the value */
      std::optional<T> prev_value;

      if (highest_accepted.has_value()) {
        prev_value = highest_accepted->m_data;
      }

      auto new_value = transform(prev_value);

      /* Phase 2: Accept */
      auto accept_msg = Accept_message<T>{m_ballot, Value<T>(new_value)};
      std::vector<Accept_response<T>> accept_responses;

      for (const auto& acceptor : m_acceptors) {
        auto response = acceptor->accept(accept_msg);

        if (response.m_success) {
          accept_responses.push_back(response);
        }
      }

      if (accept_responses.size() > m_acceptors.size() / 2) {
        /* Success */
        return new_value;
      }
    }
  }

private:
  /**
    * @brief Generates a new unique ballot number
   *
   * @return A ballot number greater than the current ballot
   */
  [[nodiscard]] Ballot_number generate_ballot() noexcept {
    std::uniform_int_distribution<> dis(m_ballot + 1, m_ballot + 100);
    return dis(m_gen);
  }

private:
  /** List of acceptors */
  Acceptors m_acceptors;

  /** Current ballot number */
  Ballot_number m_ballot{Null_ballot};

  /** Random device */
  std::random_device m_rd{};

  /** Mersenne twister engine */
  std::mt19937 m_gen{m_rd()};
};

} // namespace caspaxos
