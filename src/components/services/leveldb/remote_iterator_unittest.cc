// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "base/bind.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/test/scoped_task_environment.h"
#include "components/services/leveldb/leveldb_service_unittests_catalog_source.h"
#include "components/services/leveldb/public/cpp/remote_iterator.h"
#include "components/services/leveldb/public/cpp/util.h"
#include "components/services/leveldb/public/interfaces/leveldb.mojom.h"
#include "services/service_manager/public/cpp/test/test_service.h"
#include "services/service_manager/public/cpp/test/test_service_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace leveldb {
namespace {

template <typename... Args>
void IgnoreAllArgs(Args&&...) {}

template <typename... Args>
void DoCaptures(typename std::decay<Args>::type*... out_args,
                const base::Closure& quit_closure,
                Args... in_args) {
  IgnoreAllArgs((*out_args = std::move(in_args))...);
  quit_closure.Run();
}

template <typename T1>
base::Callback<void(T1)> Capture(T1* t1, const base::Closure& quit_closure) {
  return base::Bind(&DoCaptures<T1>, t1, quit_closure);
}

base::Callback<void(const base::UnguessableToken&)> CaptureToken(
    base::UnguessableToken* t1,
    const base::Closure& quit_closure) {
  return base::Bind(&DoCaptures<const base::UnguessableToken&>, t1,
                    quit_closure);
}

class RemoteIteratorTest : public testing::Test {
 public:
  RemoteIteratorTest()
      : test_service_manager_(test::CreateTestCatalog()),
        test_service_(test_service_manager_.RegisterTestInstance(
            "leveldb_service_unittests")) {}
  ~RemoteIteratorTest() override = default;

 protected:
  service_manager::Connector* connector() { return test_service_.connector(); }

  void SetUp() override {
    connector()->BindInterface("leveldb", &leveldb_);

    mojom::DatabaseError error;
    base::RunLoop run_loop;
    leveldb()->OpenInMemory(base::nullopt, "RemoteIteratorTest",
                            MakeRequest(&database_),
                            Capture(&error, run_loop.QuitClosure()));
    run_loop.Run();
    EXPECT_EQ(mojom::DatabaseError::OK, error);

    std::map<std::string, std::string> data{
        {"a", "first"}, {"b:suffix", "second"}, {"c", "third"}};

    for (auto p : data) {
      // Write a key to the database.
      error = mojom::DatabaseError::INVALID_ARGUMENT;
      base::RunLoop run_loop;
      database_->Put(StdStringToUint8Vector(p.first),
                     StdStringToUint8Vector(p.second),
                     Capture(&error, run_loop.QuitClosure()));
      run_loop.Run();
      EXPECT_EQ(mojom::DatabaseError::OK, error);
    }
  }

  mojom::LevelDBServicePtr& leveldb() { return leveldb_; }
  mojom::LevelDBDatabaseAssociatedPtr& database() { return database_; }

 private:
  base::test::ScopedTaskEnvironment task_environment_;
  service_manager::TestServiceManager test_service_manager_;
  service_manager::TestService test_service_;

  mojom::LevelDBServicePtr leveldb_;
  mojom::LevelDBDatabaseAssociatedPtr database_;

  DISALLOW_COPY_AND_ASSIGN(RemoteIteratorTest);
};

TEST_F(RemoteIteratorTest, Seeking) {
  base::UnguessableToken iterator;
  base::RunLoop run_loop;
  database()->NewIterator(CaptureToken(&iterator, run_loop.QuitClosure()));
  run_loop.Run();
  EXPECT_FALSE(iterator.is_empty());

  RemoteIterator it(database().get(), iterator);
  EXPECT_FALSE(it.Valid());

  it.SeekToFirst();
  EXPECT_TRUE(it.Valid());
  EXPECT_EQ("a", it.key());
  EXPECT_EQ("first", it.value());

  it.SeekToLast();
  EXPECT_TRUE(it.Valid());
  EXPECT_EQ("c", it.key());
  EXPECT_EQ("third", it.value());

  it.Seek("b");
  EXPECT_TRUE(it.Valid());
  EXPECT_EQ("b:suffix", it.key());
  EXPECT_EQ("second", it.value());
}

TEST_F(RemoteIteratorTest, Next) {
  base::UnguessableToken iterator;
  base::RunLoop run_loop;
  database()->NewIterator(CaptureToken(&iterator, run_loop.QuitClosure()));
  run_loop.Run();
  EXPECT_FALSE(iterator.is_empty());

  RemoteIterator it(database().get(), iterator);
  EXPECT_FALSE(it.Valid());

  it.SeekToFirst();
  EXPECT_TRUE(it.Valid());
  EXPECT_EQ("a", it.key());
  EXPECT_EQ("first", it.value());

  it.Next();
  EXPECT_TRUE(it.Valid());
  EXPECT_EQ("b:suffix", it.key());
  EXPECT_EQ("second", it.value());

  it.Next();
  EXPECT_TRUE(it.Valid());
  EXPECT_EQ("c", it.key());
  EXPECT_EQ("third", it.value());

  it.Next();
  EXPECT_FALSE(it.Valid());
}

TEST_F(RemoteIteratorTest, Prev) {
  base::UnguessableToken iterator;
  base::RunLoop run_loop;
  database()->NewIterator(CaptureToken(&iterator, run_loop.QuitClosure()));
  run_loop.Run();
  EXPECT_FALSE(iterator.is_empty());

  RemoteIterator it(database().get(), iterator);
  EXPECT_FALSE(it.Valid());

  it.SeekToLast();
  EXPECT_TRUE(it.Valid());
  EXPECT_EQ("c", it.key());
  EXPECT_EQ("third", it.value());

  it.Prev();
  EXPECT_TRUE(it.Valid());
  EXPECT_EQ("b:suffix", it.key());
  EXPECT_EQ("second", it.value());

  it.Prev();
  EXPECT_TRUE(it.Valid());
  EXPECT_EQ("a", it.key());
  EXPECT_EQ("first", it.value());

  it.Prev();
  EXPECT_FALSE(it.Valid());
}

}  // namespace
}  // namespace leveldb
