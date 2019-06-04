// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PREFS_PERSISTENT_PREF_STORE_UNITTEST_H_
#define COMPONENTS_PREFS_PERSISTENT_PREF_STORE_UNITTEST_H_

namespace base {
namespace test {
class ScopedTaskEnvironment;
}
}  // namespace base

class PersistentPrefStore;

// Calls CommitPendingWrite() on |store| with a callback. Verifies that the
// callback runs on the appropriate sequence. |scoped_task_environment| is the
// test's ScopedTaskEnvironment. This function is meant to be reused in the
// tests of various PersistentPrefStore implementations.
void TestCommitPendingWriteWithCallback(
    PersistentPrefStore* store,
    base::test::ScopedTaskEnvironment* scoped_task_environment);

#endif  // COMPONENTS_PREFS_PERSISTENT_PREF_STORE_UNITTEST_H_
