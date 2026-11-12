#include "util/random.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace leveldb {

namespace {

// 直接用 64 位取模实现的参考版本：seed = seed * 16807 % (2^31 - 1)
uint32_t ReferenceNext(uint64_t* seed) {
  *seed = (*seed * 16807) % 2147483647ULL;
  return static_cast<uint32_t>(*seed);
}

}  // namespace

TEST(RandomTest, KnownSequenceFromSeedOne) {
  // Park-Miller "minimal standard" 生成器的公开参考值
  Random rnd(1);
  EXPECT_EQ(16807u, rnd.Next());
  EXPECT_EQ(282475249u, rnd.Next());
  EXPECT_EQ(1622650073u, rnd.Next());
  EXPECT_EQ(984943658u, rnd.Next());
  EXPECT_EQ(1144108930u, rnd.Next());
}

TEST(RandomTest, TenThousandthValue) {
  // Park & Miller 论文给出的校验值：种子为 1 时第 10000 个数是 1043618065
  Random rnd(1);
  uint32_t v = 0;
  for (int i = 0; i < 10000; i++) v = rnd.Next();
  EXPECT_EQ(1043618065u, v);
}

TEST(RandomTest, MatchesReferenceImplementation) {
  // 校验快速取模技巧 ((x << 31) % M == x) 与直接取模结果一致
  for (uint32_t s : {1u, 2u, 301u, 0xdeadbeefu, 2147483646u, 123456789u}) {
    Random rnd(s);
    uint64_t ref = s & 0x7fffffffu;
    if (ref == 0 || ref == 2147483647u) ref = 1;
    for (int i = 0; i < 100000; i++) {
      ASSERT_EQ(ReferenceNext(&ref), rnd.Next()) << "seed=" << s << " i=" << i;
    }
  }
}

TEST(RandomTest, BadSeedsAreReplaced) {
  // 0、2^31-1，以及屏蔽最高位后等于它们的种子，都会被替换为 1
  for (uint32_t s : {0u, 2147483647u, 0x80000000u, 0xffffffffu}) {
    Random rnd(s);
    Random one(1);
    EXPECT_EQ(one.Next(), rnd.Next()) << "seed=" << s;
  }
}

TEST(RandomTest, SameSeedSameSequence) {
  Random a(42), b(42);
  for (int i = 0; i < 1000; i++) {
    ASSERT_EQ(a.Next(), b.Next());
  }
}

TEST(RandomTest, NextStaysInRange) {
  Random rnd(301);
  for (int i = 0; i < 100000; i++) {
    uint32_t v = rnd.Next();
    ASSERT_GE(v, 1u);
    ASSERT_LE(v, 2147483646u);
  }
}

TEST(RandomTest, UniformRangeAndDistribution) {
  Random rnd(301);
  const int kBuckets = 10;
  const int kSamples = 100000;
  std::vector<int> counts(kBuckets, 0);
  for (int i = 0; i < kSamples; i++) {
    uint32_t v = rnd.Uniform(kBuckets);
    ASSERT_LT(v, static_cast<uint32_t>(kBuckets));
    counts[v]++;
  }
  // 每个桶期望 10000 次，允许 ±5% 的偏差
  for (int c : counts) {
    EXPECT_GT(c, 9500);
    EXPECT_LT(c, 10500);
  }

  // n == 1 时只能返回 0
  for (int i = 0; i < 100; i++) EXPECT_EQ(0u, rnd.Uniform(1));
}

TEST(RandomTest, OneIn) {
  Random rnd(301);
  for (int i = 0; i < 100; i++) EXPECT_TRUE(rnd.OneIn(1));

  const int kSamples = 100000;
  int hits = 0;
  for (int i = 0; i < kSamples; i++) {
    if (rnd.OneIn(4)) hits++;
  }
  // 期望约 25000 次
  EXPECT_GT(hits, 24000);
  EXPECT_LT(hits, 26000);
}

TEST(RandomTest, Skewed) {
  Random rnd(301);
  const int kMaxLog = 10;
  int small = 0;  // 落在 [0, 2) 的次数
  const int kSamples = 100000;
  for (int i = 0; i < kSamples; i++) {
    uint32_t v = rnd.Skewed(kMaxLog);
    ASSERT_LT(v, 1u << kMaxLog);
    if (v < 2) small++;
  }
  // 指数偏向小值：base 取 0 或 1 时结果必定 < 2，概率至少 2/11
  EXPECT_GT(small, kSamples * 2 / 11 * 9 / 10);

  // max_log == 0 时只能返回 0
  for (int i = 0; i < 100; i++) EXPECT_EQ(0u, rnd.Skewed(0));
}

}  // namespace leveldb
