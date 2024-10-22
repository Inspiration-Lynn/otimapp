#include <fragment.hpp>

#include "gtest/gtest.h"

void printPath(int agent_id, Path p)
{
  std::cout << "agent " << agent_id << "'s path: " << std::endl;
  for (auto node : p) {
    node->println();
  }
}

TEST(TableFragment, swapDeadlock)
{
  int agentId = 0;
  auto G = Grid("3x3.map");
  auto table = TableFragment(&G);

  Path p1 = {G.getNode(0), G.getNode(1), G.getNode(2)};
  printPath(agentId++, p1);
  auto c1 = table.registerNewPath(agentId, p1);  // put agent 0's path to table
  ASSERT_EQ(c1, nullptr);

  Path p2 = {G.getNode(3), G.getNode(2), G.getNode(1)};
  printPath(agentId++, p2);
  auto c2 = table.registerNewPath(agentId, p2);
  ASSERT_NE(c2, nullptr);  // p1 & p2: swap deadlock
}

// cycle deadlock
TEST(TableFragment, cycleDeadlock)
{
  int agentId = 0;
  auto G = Grid("3x3.map");
  auto table = TableFragment(&G);

  Path p1 = {G.getNode(0), G.getNode(3), G.getNode(6)};
  printPath(agentId++, p1);
  auto c1 = table.registerNewPath(agentId, p1);
  ASSERT_EQ(c1, nullptr);

  Path p2 = {G.getNode(3), G.getNode(4), G.getNode(5)};
  printPath(agentId++, p2);
  auto c2 = table.registerNewPath(agentId, p2);
  ASSERT_EQ(c2, nullptr);

  Path p3 = {G.getNode(7), G.getNode(4), G.getNode(1)};
  printPath(agentId++, p3);
  auto c3 = table.registerNewPath(agentId, p3);
  ASSERT_EQ(c3, nullptr);

  Path p4 = {G.getNode(2), G.getNode(1), G.getNode(0)};
  printPath(agentId++, p4);
  auto c4 = table.registerNewPath(agentId, p4);
  ASSERT_NE(c4, nullptr);
}

// 自循环不会产生死锁
TEST(TableFragment, selfLoop)
{
  auto G = Grid("8x8.map");
  auto table = TableFragment(&G);

  // self loop
  Path p = {G.getNode(8), G.getNode(9), G.getNode(17), G.getNode(16),
            G.getNode(8)};
  printPath(0, p);
  auto c = table.registerNewPath(0, p);
  ASSERT_EQ(c, nullptr);
}
