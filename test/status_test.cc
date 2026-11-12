#include "leveldb/status.h"

#include <string>
#include <utility>

#include "gtest/gtest.h"

namespace leveldb {

TEST(StatusTest, OK) {
  Status s;
  EXPECT_TRUE(s.ok());
  EXPECT_EQ("OK", s.ToString());

  Status ok = Status::OK();
  EXPECT_TRUE(ok.ok());
  EXPECT_FALSE(ok.IsNotFound());
  EXPECT_FALSE(ok.IsCorruption());
  EXPECT_FALSE(ok.IsIOError());
  EXPECT_FALSE(ok.IsNotSupportedError());
  EXPECT_FALSE(ok.IsInvalidArgument());
}

TEST(StatusTest, ErrorCodesAndPredicates) {
  Status nf = Status::NotFound("a");
  EXPECT_FALSE(nf.ok());
  EXPECT_TRUE(nf.IsNotFound());
  EXPECT_FALSE(nf.IsCorruption());

  EXPECT_TRUE(Status::Corruption("a").IsCorruption());
  EXPECT_TRUE(Status::NotSupported("a").IsNotSupportedError());
  EXPECT_TRUE(Status::InvalidArgument("a").IsInvalidArgument());
  EXPECT_TRUE(Status::IOError("a").IsIOError());
}

TEST(StatusTest, ToString) {
  EXPECT_EQ("NotFound: key", Status::NotFound("key").ToString());
  EXPECT_EQ("NotFound: key: k1", Status::NotFound("key", "k1").ToString());
  EXPECT_EQ("Corruption: bad block",
            Status::Corruption("bad block").ToString());
  EXPECT_EQ("Not implemented: x", Status::NotSupported("x").ToString());
  EXPECT_EQ("Invalid argument: x", Status::InvalidArgument("x").ToString());
  EXPECT_EQ("IO error: /tmp/f: No such file",
            Status::IOError("/tmp/f", "No such file").ToString());
}

TEST(StatusTest, EmptyMessages) {
  // msg2 为空时不追加 ": "
  EXPECT_EQ("NotFound: ", Status::NotFound("").ToString());
  EXPECT_EQ("NotFound: a", Status::NotFound("a", "").ToString());
  // msg 为空、msg2 非空时仍会追加 ": "
  EXPECT_EQ("NotFound: : b", Status::NotFound("", "b").ToString());
}

TEST(StatusTest, MessageWithNullBytes) {
  std::string msg("a\0b", 3);
  Status s = Status::Corruption(msg);
  EXPECT_EQ(std::string("Corruption: a\0b", 15), s.ToString());
}

TEST(StatusTest, CopyConstructor) {
  Status a = Status::NotFound("custom NotFound status message");
  Status b(a);
  EXPECT_TRUE(b.IsNotFound());
  EXPECT_EQ(a.ToString(), b.ToString());

  Status ok;
  Status ok_copy(ok);
  EXPECT_TRUE(ok_copy.ok());
}

TEST(StatusTest, CopyAssignment) {
  Status a = Status::IOError("io");
  Status b = Status::Corruption("c");
  b = a;
  EXPECT_TRUE(b.IsIOError());
  EXPECT_EQ("IO error: io", b.ToString());
  EXPECT_EQ("IO error: io", a.ToString());  // 源对象不受影响

  // 赋值为 OK
  b = Status::OK();
  EXPECT_TRUE(b.ok());

  // 自赋值（通过引用绕开编译器的 self-assign 警告）
  Status& a_ref = a;
  a = a_ref;
  EXPECT_EQ("IO error: io", a.ToString());
}

TEST(StatusTest, CopyIsDeep) {
  Status b;
  {
    Status a = Status::NotFound("temp");
    b = a;
  }  // a 析构后 b 仍然有效
  EXPECT_EQ("NotFound: temp", b.ToString());
}

TEST(StatusTest, MoveConstructor) {
  Status a = Status::NotFound("custom NotFound status message");
  Status b(std::move(a));
  EXPECT_TRUE(b.IsNotFound());
  EXPECT_EQ("NotFound: custom NotFound status message", b.ToString());
  EXPECT_TRUE(a.ok());  // NOLINT: 移动构造后源对象为 OK
}

TEST(StatusTest, MoveAssignment) {
  {
    Status ok;
    Status ok2 = std::move(ok);
    EXPECT_TRUE(ok2.ok());
  }
  {
    Status status = Status::NotFound("custom NotFound status message");
    Status status2 = std::move(status);
    EXPECT_TRUE(status2.IsNotFound());
    EXPECT_EQ("NotFound: custom NotFound status message", status2.ToString());
  }
  {
    Status self_moved = Status::IOError("custom IOError status message");
    // 自移动赋值后对象应仍可用（不崩溃、不泄漏）
    Status& self_moved_reference = self_moved;
    self_moved_reference = std::move(self_moved);
    (void)self_moved.ToString();
  }
  {
    Status a = Status::Corruption("x");
    Status b = Status::IOError("y");
    b = std::move(a);
    EXPECT_TRUE(b.IsCorruption());
    EXPECT_EQ("Corruption: x", b.ToString());
  }
}

}  // namespace leveldb
