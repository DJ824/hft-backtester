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