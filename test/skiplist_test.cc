#include "db/skiplist.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <set>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "util/arena.h"
#include "util/random.h"

namespace leveldb {

typedef uint64_t Key;

struct Comparator {
  int operator()(const Key& a, const Key& b) const {
    if (a < b) {
      return -1;
    } else if (a > b) {
      return +1;
    } else {
      return 0;
    }
  }
};

// 降序比较器，用来确认 SkipList 完全依赖比较器而不是 operator<
struct ReverseComparator {
  int operator()(const Key& a, const Key& b) const { return Comparator()(b, a); }
};

TEST(SkipTest, Empty) {
  Arena arena;
  Comparator cmp;
  SkipList<Key, Comparator> list(cmp, &arena);
  EXPECT_FALSE(list.Contains(10));

  SkipList<Key, Comparator>::Iterator iter(&list);
  EXPECT_FALSE(iter.Valid());
  iter.SeekToFirst();
  EXPECT_FALSE(iter.Valid());
  iter.Seek(100);
  EXPECT_FALSE(iter.Valid());
  iter.SeekToLast();
  EXPECT_FALSE(iter.Valid());
}

TEST(SkipTest, SingleElement) {
  Arena arena;
  Comparator cmp;
  SkipList<Key, Comparator> list(cmp, &arena);
  list.Insert(5);
  EXPECT_TRUE(list.Contains(5));
  EXPECT_FALSE(list.Contains(4));
  EXPECT_FALSE(list.Contains(6));

  SkipList<Key, Comparator>::Iterator iter(&list);
  iter.SeekToFirst();
  ASSERT_TRUE(iter.Valid());
  EXPECT_EQ(5u, iter.key());
  iter.Next();
  EXPECT_FALSE(iter.Valid());

  iter.SeekToLast();
  ASSERT_TRUE(iter.Valid());
  EXPECT_EQ(5u, iter.key());
  iter.Prev();
  EXPECT_FALSE(iter.Valid());

  iter.Seek(0);
  ASSERT_TRUE(iter.Valid());
  EXPECT_EQ(5u, iter.key());
  iter.Seek(5);
  ASSERT_TRUE(iter.Valid());
  EXPECT_EQ(5u, iter.key());
  iter.Seek(6);
  EXPECT_FALSE(iter.Valid());
}

TEST(SkipTest, SequentialInsertAscendingAndDescending) {
  // 顺序插入和逆序插入是跳表最容易出错的两种模式
  for (bool ascending : {true, false}) {
    Arena arena;
    Comparator cmp;
    SkipList<Key, Comparator> list(cmp, &arena);
    const int N = 1000;
    for (int i = 0; i < N; i++) {
      list.Insert(ascending ? i : N - 1 - i);
    }
    SkipList<Key, Comparator>::Iterator iter(&list);
    iter.SeekToFirst();
    for (int i = 0; i < N; i++) {
      ASSERT_TRUE(iter.Valid());
      ASSERT_EQ(static_cast<Key>(i), iter.key());
      iter.Next();
    }
    EXPECT_FALSE(iter.Valid());
  }
}

TEST(SkipTest, ReverseComparator) {
  Arena arena;
  ReverseComparator cmp;
  SkipList<Key, ReverseComparator> list(cmp, &arena);
  for (Key k : {3, 1, 4, 5, 9, 2, 6}) list.Insert(k);

  std::vector<Key> got;
  SkipList<Key, ReverseComparator>::Iterator iter(&list);
  for (iter.SeekToFirst(); iter.Valid(); iter.Next()) got.push_back(iter.key());
  EXPECT_EQ((std::vector<Key>{9, 6, 5, 4, 3, 2, 1}), got);

  // 在降序意义下 Seek(7) 应定位到第一个 "<= 7" 的元素
  iter.Seek(7);
  ASSERT_TRUE(iter.Valid());
  EXPECT_EQ(6u, iter.key());
}

// 参考 leveldb 原版 skiplist_test 的 InsertAndLookup：
// 随机插入，与 std::set 对照验证 Contains、Seek、正向和反向遍历
TEST(SkipTest, InsertAndLookup) {
  const int N = 2000;
  const int R = 5000;
  Random rnd(1000);
  std::set<Key> keys;
  Arena arena;
  Comparator cmp;
  SkipList<Key, Comparator> list(cmp, &arena);
  for (int i = 0; i < N; i++) {
    Key key = rnd.Next() % R;
    if (keys.insert(key).second) {
      list.Insert(key);
    }
  }

  for (int i = 0; i < R; i++) {
    if (list.Contains(i)) {
      ASSERT_EQ(1u, keys.count(i));
    } else {
      ASSERT_EQ(0u, keys.count(i));
    }
  }

  // Simple iterator tests
  {
    SkipList<Key, Comparator>::Iterator iter(&list);
    ASSERT_TRUE(!iter.Valid());

    iter.Seek(0);
    ASSERT_TRUE(iter.Valid());
    ASSERT_EQ(*(keys.begin()), iter.key());

    iter.SeekToFirst();
    ASSERT_TRUE(iter.Valid());
    ASSERT_EQ(*(keys.begin()), iter.key());

    iter.SeekToLast();
    ASSERT_TRUE(iter.Valid());
    ASSERT_EQ(*(keys.rbegin()), iter.key());
  }

  // Forward iteration test
  for (int i = 0; i < R; i++) {
    SkipList<Key, Comparator>::Iterator iter(&list);
    iter.Seek(i);

    // Compare against model iterator
    std::set<Key>::iterator model_iter = keys.lower_bound(i);
    for (int j = 0; j < 3; j++) {
      if (model_iter == keys.end()) {
        ASSERT_TRUE(!iter.Valid());
        break;
      } else {
        ASSERT_TRUE(iter.Valid());
        ASSERT_EQ(*model_iter, iter.key());
        ++model_iter;
        iter.Next();
      }
    }
  }

  // Backward iteration test
  {
    SkipList<Key, Comparator>::Iterator iter(&list);
    iter.SeekToLast();

    // Compare against model iterator
    for (std::set<Key>::reverse_iterator model_iter = keys.rbegin();
         model_iter != keys.rend(); ++model_iter) {
      ASSERT_TRUE(iter.Valid());
      ASSERT_EQ(*model_iter, iter.key());
      iter.Prev();
    }
    ASSERT_TRUE(!iter.Valid());
  }
}

TEST(SkipTest, LargeRandomInsert) {
  // 大量节点会让高度增长到较高层，覆盖 max_height_ 变化的路径
  const int N = 100000;
  Arena arena;
  Comparator cmp;
  SkipList<Key, Comparator> list(cmp, &arena);
  std::vector<Key> keys(N);
  for (int i = 0; i < N; i++) keys[i] = static_cast<Key>(i) * 2;  // 只插偶数
  Random rnd(301);
  for (int i = N - 1; i > 0; i--) std::swap(keys[i], keys[rnd.Uniform(i + 1)]);
  for (Key k : keys) list.Insert(k);

  for (int i = 0; i < N; i++) {
    ASSERT_TRUE(list.Contains(static_cast<Key>(i) * 2));
    ASSERT_FALSE(list.Contains(static_cast<Key>(i) * 2 + 1));
  }

  // 正向遍历严格递增且元素个数正确
  SkipList<Key, Comparator>::Iterator iter(&list);
  int count = 0;
  Key prev = 0;
  for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
    if (count > 0) ASSERT_LT(prev, iter.key());
    prev = iter.key();
    count++;
  }
  EXPECT_EQ(N, count);

  // Seek 到奇数会落在下一个偶数上
  iter.Seek(101);
  ASSERT_TRUE(iter.Valid());
  EXPECT_EQ(102u, iter.key());
}

// 简化版的并发测试：SkipList 允许一个写线程与多个读线程并发（无锁读）。
// 写线程按随机顺序插入 key，每插入一个就用 release 语义发布已插入数量；
// 读线程用 acquire 读到数量 c 后，前 c 个 key 必须都能查到，
// 同时任意时刻的遍历结果都必须严格递增。
TEST(SkipTest, ConcurrentReadersWithOneWriter) {
  const int N = 20000;
  const int kReaders = 4;

  std::vector<Key> order(N);
  for (int i = 0; i < N; i++) order[i] = static_cast<Key>(i);
  Random rnd(42);
  for (int i = N - 1; i > 0; i--) std::swap(order[i], order[rnd.Uniform(i + 1)]);

  Arena arena;
  Comparator cmp;
  SkipList<Key, Comparator> list(cmp, &arena);
  std::atomic<int> inserted(0);
  std::atomic<bool> failed(false);

  std::vector<std::thread> readers;
  for (int r = 0; r < kReaders; r++) {
    readers.emplace_back([&, r] {
      Random reader_rnd(r + 1);
      while (!failed.load()) {
        const int c = inserted.load(std::memory_order_acquire);

        // 已发布的 key 必须可见
        for (int j = 0; j < 50 && c > 0; j++) {
          if (!list.Contains(order[reader_rnd.Uniform(c)])) failed = true;
        }

        // 遍历必须有序，且至少能看到 c 个元素
        SkipList<Key, Comparator>::Iterator iter(&list);
        int seen = 0;
        bool first = true;
        Key prev = 0;
        for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
          if (!first && !(prev < iter.key())) failed = true;
          prev = iter.key();
          first = false;
          seen++;
        }
        if (seen < c) failed = true;

        if (c == N) break;
      }
    });
  }

  for (int i = 0; i < N; i++) {
    list.Insert(order[i]);
    inserted.store(i + 1, std::memory_order_release);
  }

  for (auto& t : readers) t.join();
  EXPECT_FALSE(failed.load());
}

}  // namespace leveldb
