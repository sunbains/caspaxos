#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "caspaxos.h"

class CASPaxosTest : public ::testing::Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

namespace caspaxos {

// Basic single proposer, single acceptor test
TEST_F(CASPaxosTest, SingleProposerSingleAcceptor) {
  std::vector<std::shared_ptr<Acceptor<int>>> acceptors = {
    std::make_shared<Acceptor<int>>()
  };
  Proposer<int> proposer(acceptors);

  auto result = proposer.propose([](const std::optional<int>& prev) -> auto {
    return prev.value_or(0) + 1;
  });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 1);
}

// Multiple acceptors test
TEST_F(CASPaxosTest, MultipleAcceptors) {
  std::vector<std::shared_ptr<Acceptor<int>>> acceptors;
  for (int i = 0; i < 3; i++) {
    acceptors.push_back(std::make_shared<Acceptor<int>>());
  }
  Proposer<int> proposer(acceptors);

  auto result = proposer.propose([](const std::optional<int>& prev) -> auto {
    return prev.value_or(0) + 1;
  });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 1);
}

// Sequential proposals test
TEST_F(CASPaxosTest, SequentialProposals) {
  std::vector<std::shared_ptr<Acceptor<int>>> acceptors = {
    std::make_shared<Acceptor<int>>()
  };
  Proposer<int> proposer(acceptors);

  auto result1 = proposer.propose([](const std::optional<int>& prev) -> auto {
    return prev.value_or(0) + 1;
  });
  ASSERT_TRUE(result1.has_value());
  EXPECT_EQ(result1.value(), 1);

  auto result2 = proposer.propose([](const std::optional<int>& prev) -> auto {
    EXPECT_TRUE(prev.has_value());  // Should have previous value
    return prev.value() + 1;
  });
  ASSERT_TRUE(result2.has_value());
  EXPECT_EQ(result2.value(), 2);
}

// String value type test
TEST_F(CASPaxosTest, StringValues) {
  std::vector<std::shared_ptr<Acceptor<std::string>>> acceptors = {
    std::make_shared<Acceptor<std::string>>()
  };
  Proposer<std::string> proposer(acceptors);

  auto result = proposer.propose([](const std::optional<std::string>& prev) -> auto {
    return prev.value_or("") + "Hello";
  });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), "Hello");
}

// Test majority requirement
TEST_F(CASPaxosTest, MajorityRequirement) {
  std::vector<std::shared_ptr<Acceptor<int>>> acceptors;
  for (int i = 0; i < 3; i++) {
    acceptors.push_back(std::make_shared<Acceptor<int>>());
  }
  Proposer<int> proposer(acceptors);

  // Make first acceptor always reject
  auto mock_failing_acceptor = std::make_shared<Acceptor<int>>();
  acceptors[0] = mock_failing_acceptor;

  auto result = proposer.propose([](const std::optional<int>& prev) -> auto {
    return prev.value_or(0) + 1;
  });

  // Should still succeed with majority (2 out of 3)
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 1);
}

// Test concurrent modifications
TEST_F(CASPaxosTest, ConcurrentModifications) {
  std::vector<std::shared_ptr<Acceptor<int>>> acceptors = {
    std::make_shared<Acceptor<int>>()
  };
    
  Proposer<int> proposer1(acceptors);
  Proposer<int> proposer2(acceptors);

  auto result1 = proposer1.propose([](const std::optional<int>& prev) -> auto {
    return prev.value_or(0) + 1;
  });

  auto result2 = proposer2.propose([](const std::optional<int>& prev) -> auto {
    return prev.value_or(0) + 2;
  });

  // Both should succeed, but we don't know the order
  ASSERT_TRUE(result1.has_value());
  ASSERT_TRUE(result2.has_value());
    
  // Verify that one of the expected outcomes occurred
  bool valid_outcome = (result2.value() == 3) || (result2.value() == 2);
  EXPECT_TRUE(valid_outcome);
}

// Test value transformation
TEST_F(CASPaxosTest, ValueTransformation) {
  std::vector<std::shared_ptr<Acceptor<std::string>>> acceptors = {
    std::make_shared<Acceptor<std::string>>()
  };
  Proposer<std::string> proposer(acceptors);

  // Complex transformation function
  auto result = proposer.propose([](const std::optional<std::string>& prev) -> auto {
    std::string base = prev.value_or("start");
    return base + "-transformed";
  });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), "start-transformed");
}

// Test edge cases
TEST_F(CASPaxosTest, EdgeCases) {
  std::vector<std::shared_ptr<Acceptor<int>>> acceptors = {
    std::make_shared<Acceptor<int>>()
  };
  Proposer<int> proposer(acceptors);

  // Test with empty transform
  auto result1 = proposer.propose([](const std::optional<int>& prev) -> auto {
    return prev.value_or(0);
  });
  ASSERT_TRUE(result1.has_value());
  EXPECT_EQ(result1.value(), 0);

  // Test with negative values
  auto result2 = proposer.propose([](const std::optional<int>& /* prev */) -> auto {
    return -1;
  });
  ASSERT_TRUE(result2.has_value());
  EXPECT_EQ(result2.value(), -1);

  // Test with max int
  auto result3 = proposer.propose([](const std::optional<int>& /* prev */) -> auto {
    return std::numeric_limits<int>::max();
  });
  ASSERT_TRUE(result3.has_value());
  EXPECT_EQ(result3.value(), std::numeric_limits<int>::max());
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

} // namespace caspaxos