#include "../include/dbs.hpp"

const std::string DBS::SOLVER_NAME = "DBS";

DBS::DBS(Problem* _P) : Solver(_P), max_fragment_size(DEFAULT_MAX_FRAGMENT_SIZE)
{
  solver_name = SOLVER_NAME;
}

DBS::~DBS() {}

// main alg: DBS
void DBS::run()
{
  // set objective function
  auto compare = [](HighLevelNode_p a, HighLevelNode_p b) {
    if (a->f != b->f) return a->f > b->f;
    return false;
  };

  // OPEN
  std::priority_queue<HighLevelNode_p, HighLevelNodes, decltype(compare)> Tree(
      compare);

  // initial node
  auto n = getInitialNode();
  if (!n->valid) {
    info("  ", "failed to find a path");
    return;
  }
  Tree.push(n);

  // start high-level search
  int h_node_num = 1;
  int iteration = 0;
  while (!Tree.empty()) {
    ++iteration;

    // popup one node
    n = Tree.top();
    Tree.pop();

    info(" ", "elapsed:", getSolverElapsedTime(),
         ", explored_node_num:", iteration, ", nodes_num:", h_node_num,
         ", constraints:", n->constraints.size(), ", head-collision:", n->f);

    // check conflict
    auto constraints = getConstraints(n->paths);

    // check limitation
    if (overCompTime()) {
      info(" ", "timeout");
      break;
    }

    if (constraints.empty()) {
      solved = true;
      break;
    }

    // create new nodes
    for (auto c : constraints) {
      auto m = invoke(n, c);  // 根据DBS中的父节点创建子节点
      if (m->valid) {
        Tree.push(m);
        ++h_node_num;
      }
    }
  }

  if (solved) {
    solution = n->paths;
  } else if (Tree.empty()) {
    info(" ", "unsolvable instance");
    unsolvable = true;
  }
}

DBS::HighLevelNode_p DBS::getInitialNode()
{
  auto n = std::make_shared<HighLevelNode>();

  // to manage potential deadlocks
  auto table = new TableFragment(G, max_fragment_size);

  for (int i = 0; i < P->getNum(); ++i) {
    // find a deadlock-free path as much as possible
    auto t_p = Time::now();
    // 当前solution=∅, 为agent i规划路径
    auto p = getPrioritizedPath(i, n->paths, *table);
    elapsed_time_pathfinding += getElapsedTime(t_p);

    // failed to find such a path
    if (p.empty()) {
      // returns a path with potential deadlocks
      auto t_p = Time::now();
      p = getConstrainedPath(i, n);
      elapsed_time_pathfinding += getElapsedTime(t_p);
      // fail to find a path
      if (p.empty()) {
        n->valid = false;
        break;
      }
    }
    n->paths.push_back(p);

    // update tables
    auto t_d = Time::now();
    table->registerNewPath(i, p, true, getRemainedTime());
    elapsed_time_deadlock_detection += getElapsedTime(t_d);
  }

  auto t_d = Time::now();
  delete table;
  elapsed_time_deadlock_detection += getElapsedTime(t_d);

  // counts head-on collisions
  n->f = countsSwapConlicts(n->paths);
  return n;
}

// 根据父节点和约束，扩展子节点
DBS::HighLevelNode_p DBS::invoke(HighLevelNode_p n, Constraint_p c)
{
  auto m = std::make_shared<HighLevelNode>();

  // setup constraints
  m->constraints = n->constraints;
  m->constraints.push_back(c);

  // create new solution
  m->paths = n->paths;
  auto t_d = Time::now();
  m->paths[c->agent] = getConstrainedPath(c->agent, m);
  elapsed_time_deadlock_detection += getElapsedTime(t_d);

  // failed to find a path
  m->valid = !m->paths[c->agent].empty();

  // count head-on collisions
  m->f = countsSwapConlicts(m->paths);

  return m;
}

// high level: 为agent id考虑约束规划路径
Path DBS::getConstrainedPath(const int id, HighLevelNode_p node)
{
  Node* const g = P->getGoal(id);

  // extract relevant constraints
  Constraints constraints;
  for (auto c : node->constraints) {
    if (c->agent == id) constraints.push_back(c);
  }

  auto checkInvalidMove = [&](Node* child, Node* parent) {
    // condition 1, avoid goals
    if (child != g && table_goals[child->id]) return true;
    // condition 2, follow constraints
    for (auto c : constraints) {
      if (c->child == child && c->parent == parent) return true;
    }
    return false;
  };

  // for tie-breaking
  std::vector<std::vector<int>> from_to_table(G->getNodesSize());
  for (int i = 0; i < (int)node->paths.size(); ++i) {
    if (i == id) continue;
    auto p = node->paths[i];
    for (int t = 1; t < (int)p.size(); ++t) {
      from_to_table[p[t - 1]->id].push_back(p[t]->id);
    }
  }

  auto compare = [&](AstarNode* a, AstarNode* b) {
    // greedy search
    if (pathDist(id, a->v) != pathDist(id, b->v))
      return pathDist(id, a->v) > pathDist(id, b->v);
    // tie break, avoid swap conflicts
    auto table_a = from_to_table[a->p->v->id];
    auto table_b = from_to_table[b->p->v->id];
    bool swap_a =
        std::find(table_a.begin(), table_a.end(), a->v->id) != table_a.end();
    bool swap_b =
        std::find(table_b.begin(), table_b.end(), b->v->id) != table_b.end();
    if (swap_a != swap_b) return (int)swap_a < (int)swap_b;
    // tie break, distance so far
    if (a->g != b->g) return a->g < b->g;
    return false;
  };

  // use A-star search
  return Solver::getPath(id, checkInvalidMove, compare);
}

DBS::Constraints DBS::getConstraints(const Plan& paths)
{
  Constraints constraints = {};
  auto table = new TableFragment(G, max_fragment_size);

  // main loop
  for (int i = 0; i < P->getNum(); ++i) {
    auto t_d = Time::now();
    auto c = table->registerNewPath(i, paths[i], false, getRemainedTime());
    elapsed_time_deadlock_detection += getElapsedTime(t_d);
    // found potential deadlocks
    if (c != nullptr) {
      // 根据找到的潜在死锁，创建约束
      for (int i = 0; i < (int)c->agents.size(); ++i) {
        constraints.push_back(std::make_shared<Constraint>(
            c->agents[i], c->path[i], c->path[i + 1]));
      }
      break;
    }
  }

  auto t_d = Time::now();
  delete table;
  elapsed_time_deadlock_detection += getElapsedTime(t_d);

  return constraints;
}

// 计算一个solution中的swap冲突个数
int DBS::countsSwapConlicts(const Plan& paths)
{
  std::vector<std::vector<int>> to_from_table(G->getNodesSize());
  int cnt = 0;
  for (auto p : paths) {
    for (int t = 1; t < (int)p.size(); ++t) {
      // 依次取出每一对：u->v
      auto u = p[t - 1];
      auto v = p[t];
      // check head-on collisions
      for (auto i : to_from_table[v->id]) {
        if (i == u->id) ++cnt;
      }
      // register
      to_from_table[u->id].push_back(v->id);
    }
  }
  return cnt;
}

void DBS::setParams(int argc, char* argv[])
{
  struct option longopts[] = {
      {"max-fragment-size", required_argument, 0, 'f'},
      {0, 0, 0, 0},
  };
  optind = 1;  // reset
  int opt, longindex;
  while ((opt = getopt_long(argc, argv, "f:", longopts, &longindex)) != -1) {
    switch (opt) {
      case 'f':
        max_fragment_size = std::atoi(optarg);
        break;
      default:
        break;
    }
  }
}

void DBS::printHelp()
{
  std::cout << SOLVER_NAME << "\n"

            << "  -f --max-fragment-size"
            << "        "
            << "maximum fragment size"

            << std::endl;
}
