#include "../include/solver.hpp"

#include <fstream>
#include <iomanip>
#include <typeinfo>

MinimumSolver::MinimumSolver(Problem* _P)
    : solver_name(""),
      P(_P),
      G(_P->getG()),
      MT(_P->getMT()),
      max_comp_time(P->getMaxCompTime()),
      solved(false),
      unsolvable(false),
      comp_time(0)
{
}

void MinimumSolver::solve()
{
  start();
  exec();
  end();
}

void MinimumSolver::start() { t_start = Time::now(); }

void MinimumSolver::end() { comp_time = getSolverElapsedTime(); }

int MinimumSolver::getSolverElapsedTime() const
{
  return getElapsedTime(t_start);
}

// -----------------------------------------------
// base class with utilities
// -----------------------------------------------

Solver::Solver(Problem* _P)
    : MinimumSolver(_P),
      verbose(false),
      distance_table(P->getNum(),
                     std::vector<int>(G->getNodesSize(), G->getNodesSize())),
      table_goals(G->getNodesSize(), false),
      elapsed_time_pathfinding(0),
      elapsed_time_deadlock_detection(0)
{
}

Solver::~Solver() {}

// -------------------------------
// main
// -------------------------------
void Solver::exec()
{
  // create distance table
  info("  pre-processing, create distance table by BFS & create goal table");
  createDistanceTable();  // 用于计算h-value
  for (int i = 0; i < P->getNum(); ++i) table_goals[P->getGoal(i)->id] = true;
  info("  done, elapsed: ", getSolverElapsedTime());

  // main
  run();
}

// -------------------------------
// utilities for time
// -------------------------------
int Solver::getRemainedTime() const
{
  return std::max(0, max_comp_time - getSolverElapsedTime());
}

bool Solver::overCompTime() const
{
  return getSolverElapsedTime() >= max_comp_time;
}

// -------------------------------
// utilities for debug
// -------------------------------
void Solver::info() const
{
  if (verbose) std::cout << std::endl;
}

void Solver::halt(const std::string& msg) const
{
  std::cout << "error@" << solver_name << ": " << msg << std::endl;
  this->~Solver();
  std::exit(1);
}

void Solver::warn(const std::string& msg) const
{
  std::cout << "warn@ " << solver_name << ": " << msg << std::endl;
}

// -------------------------------
// utilities for log
// -------------------------------
void Solver::makeLog(const std::string& logfile)
{
  std::ofstream log;
  log.open(logfile, std::ios::out);
  makeLogBasicInfo(log);
  makeLogSolution(log);
  log.close();
}

void Solver::makeLogBasicInfo(std::ofstream& log)
{
  log << "instance=" << P->getInstanceFileName() << "\n";
  log << "agents=" << P->getNum() << "\n";
  if (!P->isRandomGraph()) {
    Grid* grid = reinterpret_cast<Grid*>(P->getG());
    log << "map_file=" << grid->getMapFileName() << "\n";
  }
  log << "seed=" << P->getSeed() << "\n";
  log << "solver=" << solver_name << "\n";
  log << "solved=" << solved << "\n";
  log << "unsolvable=" << unsolvable << "\n";
  log << "comp_time=" << getCompTime() << "\n";
  log << "elapsed_pathfinding=" << elapsed_time_pathfinding << "\n";
  log << "elapsed_deadlock_detection=" << elapsed_time_deadlock_detection
      << "\n";
}

void Solver::makeLogSolution(std::ofstream& log)
{
  log << "starts=";
  for (int i = 0; i < P->getNum(); ++i) {
    Node* v = P->getStart(i);
    if (!P->isRandomGraph()) {
      log << "(" << v->pos.x << "," << v->pos.y << "),";
    } else {
      log << v->id << ",";
    }
  }
  log << "\ngoals=";
  for (int i = 0; i < P->getNum(); ++i) {
    Node* v = P->getGoal(i);
    if (!P->isRandomGraph()) {
      log << "(" << v->pos.x << "," << v->pos.y << "),";
    } else {
      log << v->id << ",";
    }
  }
  log << "\n";
  log << "sum-of-path-length:"
      << std::accumulate(solution.begin(), solution.end(), 0,
                         [](int acc, Path p) { return acc + p.size() - 1; })
      << "\n";
  log << "plan=\n";
  for (int i = 0; i < (int)solution.size(); ++i) {
    log << i << ":";
    for (auto v : solution[i]) log << v->id << ",";
    log << "\n";
  }
}

// -------------------------------
// print
// -------------------------------
void Solver::printResult()
{
  int cost = 0;
  if (solved)
    for (auto p : solution) cost += p.size() - 1;

  std::cout << "solved=" << solved << ", solver=" << std::right << std::setw(8)
            << solver_name << ", comp_time(ms)=" << std::right << std::setw(8)
            << getCompTime() << ", sum of path length=" << std::right
            << std::setw(8) << cost << std::endl;
}

void Solver::printHelpWithoutOption(const std::string& solver_name)
{
  std::cout << solver_name << "\n"
            << "  (no option)" << std::endl;
}

// -------------------------------
// distance
// 对于agent i, 计算当前s节点到目标节点的距离(用于计算h-value)
// -------------------------------
int Solver::pathDist(const int i, Node* const s) const
{
  return distance_table[i][s->id];
}

int Solver::pathDist(const int i) const { return pathDist(i, P->getStart(i)); }

// 为每个目标节点构建一张到其他节点的最短路径距离表
// 用于h的计算
void Solver::createDistanceTable()
{
  for (int i = 0; i < P->getNum(); ++i) {
    // breadth first search
    std::queue<Node*> OPEN;
    Node* n = P->getGoal(i);  // 目标位置
    OPEN.push(n);
    distance_table[i][n->id] = 0;
    while (!OPEN.empty()) {
      n = OPEN.front();
      OPEN.pop();
      const int d_n = distance_table[i][n->id];
      for (auto m : n->neighbor) {
        const int d_m = distance_table[i][m->id];
        if (d_n + 1 >= d_m) continue;
        distance_table[i][m->id] = d_n + 1;
        OPEN.push(m);
      }
    }
  }
}

// -------------------------------
// utilities for getting path
// -------------------------------
Solver::CompareAstarNodes Solver::compareAstarNodesDefault = [](AstarNode* a,
                                                                AstarNode* b) {
  // f = g + h
  if (a->f != b->f) return a->f > b->f;
  if (a->g != b->g) return a->g < b->g;
  return false;
};

// single agent path finding: A* (根据死锁约束进行了改造)
Path Solver::getPath(const int id, CheckInvalidMove checkInvalidNode,
                     CompareAstarNodes compare)
{
  // 获取开始位置和目标位置
  Node* const s = P->getStart(id);
  Node* const g = P->getGoal(id);

  // GC 被用作垃圾回收的容器，保存所有创建的 AstarNode
  // 指针，以便在后续需要时统一处理（如释放内存）
  AstarNodes GC;  // garbage collection ??
  auto createNewNode = [&GC](Node* v, int g, int f, AstarNode* p) {
    // 𝑓(𝑛)=𝑔(𝑛)+ℎ(𝑛)
    // 𝑔(𝑛): 𝑐𝑜𝑠𝑡_𝑠𝑜_𝑓𝑎𝑟，表示当前点𝑛到起点的距离
    // ℎ(𝑛): 启发函数，表示当前点𝑛到目标点的距离
    AstarNode* new_node = new AstarNode{v, g, f, p};
    GC.push_back(new_node);
    return new_node;
  };

  // OPEN and CLOSE list
  std::priority_queue<AstarNode*, AstarNodes, decltype(compare)> OPEN(compare);
  std::vector<bool> CLOSE(G->getNodesSize(), false);  // 已扩展的node存在CLOSE中

  // initial node
  AstarNode* n = createNewNode(s, 0, pathDist(id, s), nullptr);
  OPEN.push(n);

  // main loop
  bool invalid = true;
  while (!OPEN.empty()) {
    // check time limit
    if (overCompTime()) break;

    // minimum node
    n = OPEN.top();
    OPEN.pop();

    // check CLOSE list
    if (CLOSE[n->v->id]) continue;
    CLOSE[n->v->id] = true;

    // check goal condition
    if (n->v == g) {
      invalid = false;
      break;
    }

    // expand
    Nodes C = n->v->neighbor;
    std::shuffle(C.begin(), C.end(), *MT);  // randomize
    for (auto u : C) {
      // already searched?
      if (CLOSE[u->id]) continue;
      // check constraints
      if (checkInvalidNode(u, n->v)) continue;  // 违反了约束，如包含路径(u->v)
      int g_cost = n->g + 1;
      OPEN.push(createNewNode(u, g_cost, g_cost + pathDist(id, u), n));
    }
  }

  Path path;
  if (!invalid) {           // success
    while (n != nullptr) {  // 构建path，该path满足约束
      path.push_back(n->v);
      n = n->p;
    }
    std::reverse(path.begin(), path.end());
  }

  // free
  for (auto p : GC) delete p;

  return path;
}

Path Solver::getPrioritizedPath(const int id, const Plan& paths,
                                TableFragment& table)
{
  Node* const g = P->getGoal(id);

  // DBS中OPEN优先级队列的比较函数
  // 首先比较f-value, f-value相同的情况下加入tie-breaking策略
  auto compare = [&](AstarNode* a, AstarNode* b) {
    if (a->f != b->f) return a->f > b->f;
    // tie break: shorter from fragment is better
    int fragments_a = table.t_from[a->v->id].size();
    int fragments_b = table.t_from[b->v->id].size();
    if (fragments_a != fragments_b) return fragments_a > fragments_b;
    if (a->g != b->g) return a->g < b->g;
    return a->v->id < b->v->id;
  };

  // move: (parent->child)是否合法
  auto checkInvalidNode = [&](Node* child, Node* parent) {
    // condition 1, avoid goals
    if (child != g && table_goals[child->id]) return true;
    // condition 2, avoid potential deadlocks
    // to fragment table[parent]
    for (auto c : table.t_to[parent->id]) {
      if (c->path.front() == child) return true;
    }

    return false;
  };

  return getPath(id, checkInvalidNode, compare);
}
