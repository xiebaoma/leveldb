#include "leveldb/slice.h"

#include <string>

#include "gtest/gtest.h"

namespace leveldb {

TEST(SliceTest, DefaultIsEmpty) {
  Slice s;
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(0u, s.size());
  EXPECT_NE(nullptr, s.data());
  EXPECT_EQ("", s.ToString());
}

TEST(SliceTest, Constructors) {
  const char* cstr = "hello";
  Slice a(cstr);
  EXPECT_EQ(5u, a.size());
  EXPECT_EQ(cstr, a.data());  // 不拷贝，直接引用原数据

  Slice b(cstr, 3);
  EXPECT_EQ(3u, b.size());
  EXPECT_EQ("hel", b.ToString());

  std::string str("world");
  Slice c(str);
  EXPECT_EQ(str.size(), c.size());
  EXPECT_EQ(str.data(), c.data());
  EXPECT_EQ("world", c.ToString());
}

TEST(SliceTest, EmbeddedNullBytes) {
  std::string str("a\0b", 3);
  Slice s(str);
  EXPECT_EQ(3u, s.size());
  EXPECT_EQ('\0', s[1]);
  EXPECT_EQ(str, s.ToString());
}

TEST(SliceTest, CopyAndAssign) {
  Slice a("abc");
  Slice b(a);
  EXPECT_EQ(a.data(), b.data());
  EXPECT_EQ(a.size(), b.size());

  Slice c;
  c = a;
  EXPECT_EQ(a, c);
}

TEST(SliceTest, IndexAndIterators) {
  Slice s("xyz");
  EXPECT_EQ('x', s[0]);
  EXPECT_EQ('z', s[2]);
  EXPECT_EQ(s.data(), s.begin());
  EXPECT_EQ(s.data() + 3, s.end());
  EXPECT_EQ("xyz", std::string(s.begin(), s.end()));
}

TEST(SliceTest, Clear) {
  Slice s("abc");
  s.clear();
  EXPECT_TRUE(s.empty());
  EXPECT_EQ("", s.ToString());
}

TEST(SliceTest, RemovePrefix) {
  Slice s("abcdef");
  s.remove_prefix(2);
  EXPECT_EQ("cdef", s.ToString());
  s.remove_prefix(0);
  EXPECT_EQ("cdef", s.ToString());
  s.remove_prefix(4);
  EXPECT_TRUE(s.empty());
}

TEST(SliceTest, StartsWith) {
  Slice s("prefix_rest");
  EXPECT_TRUE(s.starts_with("prefix"));
  EXPECT_TRUE(s.starts_with(""));
  EXPECT_TRUE(s.starts_with(s));
  EXPECT_FALSE(s.starts_with("rest"));
  EXPECT_FALSE(Slice("ab").starts_with("abc"));
}

TEST(SliceTest, Equality) {
  std::string str("abc");
  EXPECT_TRUE(Slice("abc") == Slice(str));  // 不同指针，相同内容
  EXPECT_FALSE(Slice("abc") != Slice(str));
  EXPECT_TRUE(Slice("abc") != Slice("abd"));
  EXPECT_TRUE(Slice("abc") != Slice("ab"));
  EXPECT_TRUE(Slice() == Slice(""));
}

TEST(SliceTest, Compare) {
  EXPECT_EQ(0, Slice("abc").compare(Slice("abc")));
  EXPECT_LT(Slice("abc").compare(Slice("abd")), 0);
  EXPECT_GT(Slice("abd").compare(Slice("abc")), 0);

  // 一个是另一个的前缀时，短的更小
  EXPECT_LT(Slice("ab").compare(Slice("abc")), 0);
  EXPECT_GT(Slice("abc").compare(Slice("ab")), 0);
  EXPECT_LT(Slice("").compare(Slice("a")), 0);
  EXPECT_EQ(0, Slice("").compare(Slice()));
}

TEST(SliceTest, CompareIsUnsigned) {
  // memcmp 按 unsigned char 比较，0xff 应大于 0x01
  std::string hi("\xff", 1);
  std::string lo("\x01", 1);
  EXPECT_GT(Slice(hi).compare(Slice(lo)), 0);
  EXPECT_LT(Slice(lo).compare(Slice(hi)), 0);
}

}  // namespace leveldb
