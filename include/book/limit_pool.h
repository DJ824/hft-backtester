class LimitPool {
  struct Node {
    Limit limit;
    Node *next;
  };

  Node *free_ = nullptr;

  static Node *make_node() {
    Node *n = nullptr;
    if (posix_memalign(reinterpret_cast<void **>(&n), 64, sizeof(Node)) != 0) {
      throw std::bad_alloc{};
    }
    ::new(&n->limit) Limit();
    n->next = nullptr;
    return n;
  }

public:
  LimitPool() = default;

  ~LimitPool() {
    Node *n = free_;
    while (n) {
      Node *next = n->next;
      n->limit.~Limit();
      std::free(n);
      n = next;
    }
  }

  Limit *acquire(int32_t price, bool side) {
    Node *node = free_ ? free_ : make_node();
    if (free_) {
      free_ = node->next;
    }
    Limit *l = &node->limit;
    l->price_ = price;
    l->volume_ = 0;
    l->side_ = side;
    return l;
  }

  void release(Limit *l) {
    l->~Limit();
    Node *n = reinterpret_cast<Node *>(l);
    n->next = free_;
    free_ = n;
  }

  LimitPool(const LimitPool &) = delete;
  LimitPool &operator=(const LimitPool &) = delete;
};

/*
 * signal and continue
 * wait:
 *  ++cv.count
 *  v(mutex)    // release monitor lock
 *  p(cv.sem)
 *  p(mutex)    // interrupt could be checked here before we reacquire lock
 *  --cv.count
 *
 * signal:
 *  if (cv.count > 0)
 *    v(cv.sem)
 *
 * signal and exit
 * wait:
 *  ++cv.count
 *  v(mutex)
 *  p(cv.sem)
 *
 * signal:
 *  if (cv.count > 0)
 *    --cv.count
 *    v(cv.sem)
 *  else v(mutex)
 *
 * hoare style
 * wait:
 *  ++cv.count
 *  if (next_count > 0)
 *    v(next_sem)
 *  else v(mutex)
 *  p(cv.sem)
 *  --cv.count
 *
 * signal:
 *  if (cv.count > 0)
 *    ++next_count
 *    v(cv.sem)
 *    p(next_sem)
 *    --next_count
 *
 * lamports bakery algo:
 * while (true):
 *  choose[i] = 1
 *  num[i] = 1 + max(num[i] -> num[N])
 *  choose[i] = 0
 *  for (int j = 1; j <= N; j++)
 *    while (choose[j] != 0) {}
 *    while ((num[j] != 0) && (num[j], j) < (num[i], i)) {}
 *
 *    CS
 *
 *    num[i] = 0;
 *
 *
 *  no delay:
 *  if we have 2 process p1 and p2, and p1 is in the remainder, num[1] = 0
 *  if p1 is in the remainder, we know choose[j] == 0, thus the first spin will skip
 *  also, since num[i] == 0, it makes the first condition of the second spin to be false, skipping it as well
 *  thus p2 will be able to use the CS as many times as it likes while p1 stays in remainder
 *
 *  no starvation:
 *  2 processes p1 and p2,
 *  if p1 were to stay in the entry section, that means either:
 *  a) choosing[2] == 1, but for p2 to enter the cs, choosing[2] has to equal 0, thus p1 would not block on the first spin
 *  b) if p1 were to block on the second spin, that means num[2] < num[1], p2 got assigned a lower number
 *  however, once p2 enters and leaves the cs, it sets num[2] to 0
 *  if p2 were to continue, it would get assigned a higher num value than p1, since we take the max of all assigned nums so far,
 *  so now num[1] < num[2], and this time, p1 will be able to enter the cs, with p2 spinning on the second while loop
 *
 *  no deadlock:
 *  2 processes p1 and p2
 *  say both processes enter the bakery at the same time
 *  even if the computed num[i] is the same, the process with a lower id will be able to enter the cs
 *  if one process did not compute a number yet, the other will also be able to enter the cs
 *  there is no case where both processes will be stuck in the entry section
 *
 */

