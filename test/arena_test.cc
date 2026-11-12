#include "util/arena.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace leveldb {

namespace {

constexpr size_t kBlockSize = 4096;  // 与 arena.cc 保持一致
constexpr size_t kBlockOverhead = sizeof(char*);
constexpr size_t kAlign = (sizeof(void*) > 8) ? sizeof(void*) : 8;

bool IsAligned(const char* p) {
  return (reinterpret_cast<uintptr_t>(p) & (kAlign - 1)) == 0;
}

}  // namespace

TEST(ArenaTest, Empty) {
  Arena arena;
  EXPECT_EQ(0u, arena.MemoryUsage());
}

TEST(ArenaTest, SmallAllocationsAreContiguous) {
  Arena arena;
  char* a = arena.Allocate(10);
  char* b = arena.Allocate(20);
  char* c = arena.Allocate(1);
  EXPECT_EQ(a + 10, b);
  EXPECT_EQ(b + 20, c);
  // 三次小分配共用同一个块
  EXPECT_EQ(kBlockSize + kBlockOverhead, arena.MemoryUsage());
}

TEST(ArenaTest, FillWholeBlockThenSpill) {
  Arena arena;
  char* first = arena.Allocate(kBlockSize);  // > kBlockSize/4，独立分配
  ASSERT_NE(nullptr, first);
  EXPECT_EQ(kBlockSize + kBlockOverhead, arena.MemoryUsage());

  // 用满一个标准块
  char* p = arena.Allocate(1000);
  char* q = arena.Allocate(kBlockSize - 1000);
  EXPECT_EQ(p + 1000, q);
  EXPECT_EQ(2 * (kBlockSize + kBlockOverhead), arena.MemoryUsage());

  // 块已满，下一次小分配会开新块
  arena.Allocate(1);
  EXPECT_EQ(3 * (kBlockSize + kBlockOverhead), arena.MemoryUsage());
}

TEST(ArenaTest, LargeAllocationGetsOwnBlock) {
  Arena arena;
  // 先把当前块用到只剩 96 字节，保证下面的大分配一定走 fallback
  arena.Allocate(1000);
  arena.Allocate(1000);
  arena.Allocate(1000);
  char* small1 = arena.Allocate(1000);
  const size_t usage_after_small = arena.MemoryUsage();
  EXPECT_EQ(kBlockSize + kBlockOverhead, usage_after_small);

  // 超过 kBlockSize/4 的分配单独开块，大小恰好等于请求大小
  const size_t big = kBlockSize / 4 + 1;
  arena.Allocate(big);
  EXPECT_EQ(usage_after_small + big + kBlockOverhead, arena.MemoryUsage());

  // 大分配不会浪费当前块：下一次小分配仍紧接在 small1 之后
  char* small2 = arena.Allocate(8);
  EXPECT_EQ(small1 + 1000, small2);
  EXPECT_EQ(usage_after_small + big + kBlockOverhead, arena.MemoryUsage());
}

TEST(ArenaTest, QuarterBlockBoundary) {
  // 恰好 kBlockSize/4 不算“大对象”，会开一个标准块
  Arena arena;
  arena.Allocate(kBlockSize / 4);
  EXPECT_EQ(kBlockSize + kBlockOverhead, arena.MemoryUsage());
}

TEST(ArenaTest, VeryLargeAllocation) {
  Arena arena;
  const size_t big = 10 * 1024 * 1024;
  char* p = arena.Allocate(big);
  p[0] = 'a';
  p[big - 1] = 'z';  // 首尾都可写（ASan 下会检查越界）
  EXPECT_EQ(big + kBlockOverhead, arena.MemoryUsage());
}

TEST(ArenaTest, AllocateAlignedIsAligned) {
  Arena arena;
  // 先制造一个非对齐的 alloc_ptr_
  arena.Allocate(1);
  for (size_t n : {1u, 3u, 7u, 8u, 13u, 100u}) {
    char* p = arena.AllocateAligned(n);
    EXPECT_TRUE(IsAligned(p)) << "size=" << n;
    arena.Allocate(1);  // 再次打乱对齐
  }
}

TEST(ArenaTest, AllocateAlignedFallbackIsAligned) {
  Arena arena;
  // 大对象、以及当前块剩余空间不足的情况都要对齐
  EXPECT_TRUE(IsAligned(arena.AllocateAligned(kBlockSize)));
  arena.Allocate(kBlockSize - 3);  // 用掉一个标准块的绝大部分
  EXPECT_TRUE(IsAligned(arena.AllocateAligned(16)));
}

TEST(ArenaTest, AllocationsDoNotOverlap) {
  Arena arena;
  std::vector<std::pair<char*, size_t>> allocated;
  for (size_t i = 1; i <= 200; i++) {
    const size_t n = (i % 7 == 0) ? 2000 : i;  // 混合大小对象
    char* p = (i % 2 == 0) ? arena.AllocateAligned(n) : arena.Allocate(n);
    allocated.emplace_back(p, n);
  }
  std::vector<std::pair<char*, size_t>> sorted = allocated;
  std::sort(sorted.begin(), sorted.end());
  for (size_t i = 1; i < sorted.size(); i++) {
    EXPECT_LE(sorted[i - 1].first + sorted[i - 1].second, sorted[i].first);
  }
}

// 参考 leveldb 原版 arena_test 的 Simple 用例：
// 随机大小分配，写入特定模式后逐字节校验，确保分配的内存互不覆盖。
TEST(ArenaTest, Simple) {
  std::vector<std::pair<size_t, char*>> allocated;
  Arena arena;
  const int N = 100000;
  size_t bytes = 0;
  std::mt19937 rnd(301);
  auto uniform = [&rnd](uint32_t n) { return rnd() % n; };

  for (int i = 0; i < N; i++) {
    size_t s;
    if (i % (N / 10) == 0) {
      s = i;
    } else {
      s = (uniform(4000) == 0)
              ? uniform(6000)
              : (uniform(10) == 0 ? uniform(100) : uniform(20));
    }
    if (s == 0) {
      // Arena 不支持 0 字节分配
      s = 1;
    }
    char* r = (uniform(10) == 0) ? arena.AllocateAligned(s)
                                 : arena.Allocate(s);

    for (size_t b = 0; b < s; b++) {
      // 用 i 的低 8 位填充第 i 次分配
      r[b] = static_cast<char>(i % 256);
    }
    bytes += s;
    allocated.emplace_back(s, r);
    ASSERT_GE(arena.MemoryUsage(), bytes);
    if (i > N / 10) {
      ASSERT_LE(arena.MemoryUsage(), bytes * 1.10);
    }
  }

  for (size_t i = 0; i < allocated.size(); i++) {
    const size_t num_bytes = allocated[i].first;
    const char* p = allocated[i].second;
    for (size_t b = 0; b < num_bytes; b++) {
      ASSERT_EQ(static_cast<int>(i % 256), static_cast<unsigned char>(p[b]))
          << "allocation " << i << " byte " << b;
    }
  }
}

}  // namespace leveldb
