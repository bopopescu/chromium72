// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browser_switcher/alternative_browser_driver.h"

#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

#include <algorithm>

namespace browser_switcher {

using StringType = base::FilePath::StringType;

namespace {

StringType UTF8ToNative(base::StringPiece src) {
#if defined(OS_WIN)
  return base::UTF8ToWide(src);
#elif defined(OS_POSIX)
  return src.as_string();
#else
#error "Invalid platform for browser_switcher"
#endif
}

base::ListValue UTF8VectorToListValue(
    const std::vector<base::StringPiece>& src) {
  base::ListValue out;
  out.GetList().reserve(src.size());
  for (base::StringPiece str : src)
    out.GetList().push_back(base::Value(str));
  return out;
}

}  // namespace

class AlternativeBrowserDriverTest : public testing::Test {};

TEST_F(AlternativeBrowserDriverTest, CreateCommandLine) {
  AlternativeBrowserDriverImpl driver;
  driver.SetBrowserPath("/usr/bin/true");
  auto params = UTF8VectorToListValue({"a", "b", "c"});
  driver.SetBrowserParameters(&params);
  auto cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  const base::CommandLine::StringVector& argv = cmd_line.argv();
  EXPECT_EQ(5u, argv.size());
  EXPECT_EQ(UTF8ToNative("/usr/bin/true"), argv[0]);
  EXPECT_EQ(UTF8ToNative("a"), argv[1]);
  EXPECT_EQ(UTF8ToNative("b"), argv[2]);
  EXPECT_EQ(UTF8ToNative("c"), argv[3]);
  EXPECT_EQ(UTF8ToNative("http://example.com/"), argv[4]);
}

TEST_F(AlternativeBrowserDriverTest, CreateCommandLineExpandsUrl) {
  AlternativeBrowserDriverImpl driver;
  driver.SetBrowserPath("/usr/bin/true");
  auto params = UTF8VectorToListValue({"--flag=${url}#fragment"});
  driver.SetBrowserParameters(&params);
  auto cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  const base::CommandLine::StringVector& argv = cmd_line.argv();
  EXPECT_EQ(2u, argv.size());
  EXPECT_EQ(UTF8ToNative("/usr/bin/true"), argv[0]);
  EXPECT_EQ(UTF8ToNative("--flag=http://example.com/#fragment"), argv[1]);
}

#if defined(OS_WIN)
TEST_F(AlternativeBrowserDriverTest, CreateCommandLineExpandsEnvVars) {
  _putenv("A=AAA");
  _putenv("B=BBB");
  _putenv("CC=CCC");
  _putenv("D=DDD");
  AlternativeBrowserDriverImpl driver;
  driver.SetBrowserPath("something.exe");
  auto params = UTF8VectorToListValue(
      {"%A%", "%B%", "before_%CC%_between_%D%_after", "%NONEXISTENT%"});
  driver.SetBrowserParameters(&params);
  auto cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  const base::CommandLine::StringVector& argv = cmd_line.argv();
  EXPECT_EQ(6u, argv.size());
  EXPECT_EQ(L"something.exe", argv[0]);
  EXPECT_EQ(L"AAA", argv[1]);
  EXPECT_EQ(L"BBB", argv[2]);
  EXPECT_EQ(L"before_CCC_between_DDD_after", argv[3]);
  EXPECT_EQ(L"%NONEXISTENT%", argv[4]);
  EXPECT_EQ(L"http://example.com/", argv[5]);
}

TEST_F(AlternativeBrowserDriverTest,
       AppendCommandLineArgumentDoesntExpandUrlContent) {
  _putenv("A=AAA");
  AlternativeBrowserDriverImpl driver;
  driver.SetBrowserPath("something.exe");
  auto cmd_line = driver.CreateCommandLine(GURL("http://evil.com/%A%"));
  EXPECT_EQ(2u, cmd_line.argv().size());
  EXPECT_EQ(L"something.exe", cmd_line.argv()[0]);
  EXPECT_EQ(L"http://evil.com/%A%", cmd_line.argv()[1]);

  auto params = UTF8VectorToListValue({"${url}"});
  driver.SetBrowserParameters(&params);
  cmd_line = driver.CreateCommandLine(GURL("http://evil.com/%A%"));
  EXPECT_EQ(2u, cmd_line.argv().size());
  EXPECT_EQ(L"something.exe", cmd_line.argv()[0]);
  EXPECT_EQ(L"http://evil.com/%A%", cmd_line.argv()[1]);
}
#endif  // defined(OS_WIN)

#if defined(OS_MACOSX)
TEST_F(AlternativeBrowserDriverTest, CreateCommandLineUsesOpen) {
  // Use `open(1)' to launch browser paths that aren't absolute.
  AlternativeBrowserDriverImpl driver;

  // Default to launching Safari.
  driver.SetBrowserPath("");
  auto cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  EXPECT_EQ(4u, cmd_line.argv().size());
  EXPECT_EQ("open", cmd_line.argv()[0]);
  EXPECT_EQ("-a", cmd_line.argv()[1]);
  EXPECT_EQ("Safari", cmd_line.argv()[2]);
  EXPECT_EQ("http://example.com/", cmd_line.argv()[3]);

  // Expand some "${...}" variables.
  driver.SetBrowserPath("${safari}");
  cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  EXPECT_EQ(4u, cmd_line.argv().size());
  EXPECT_EQ("open", cmd_line.argv()[0]);
  EXPECT_EQ("-a", cmd_line.argv()[1]);
  EXPECT_EQ("Safari", cmd_line.argv()[2]);
  EXPECT_EQ("http://example.com/", cmd_line.argv()[3]);

  // Path looks like an application name => use `open'.
  driver.SetBrowserPath("Safari");
  cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  EXPECT_EQ(4u, cmd_line.argv().size());
  EXPECT_EQ("open", cmd_line.argv()[0]);
  EXPECT_EQ("-a", cmd_line.argv()[1]);
  EXPECT_EQ("Safari", cmd_line.argv()[2]);
  EXPECT_EQ("http://example.com/", cmd_line.argv()[3]);
}

TEST_F(AlternativeBrowserDriverTest, CreateCommandLineContainsUrl) {
  // Use `open(1)' to launch browser paths that aren't absolute.
  AlternativeBrowserDriverImpl driver;

  // Args come after `--args', e.g.:
  //     open -a Safari http://example.com/ --args abc def
  driver.SetBrowserPath("");
  auto params = UTF8VectorToListValue({"abc", "def"});
  driver.SetBrowserParameters(&params);
  auto cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  EXPECT_EQ(7u, cmd_line.argv().size());
  EXPECT_EQ("open", cmd_line.argv()[0]);
  EXPECT_EQ("-a", cmd_line.argv()[1]);
  EXPECT_EQ("Safari", cmd_line.argv()[2]);
  EXPECT_EQ("http://example.com/", cmd_line.argv()[3]);
  EXPECT_EQ("--args", cmd_line.argv()[4]);
  EXPECT_EQ("abc", cmd_line.argv()[5]);
  EXPECT_EQ("def", cmd_line.argv()[6]);

  // If args contain "${url}", only put the URL together with the other
  // args. e.g.:
  //     open -a Safari --args abc http://example.com/ def
  params = UTF8VectorToListValue({"abc", "${url}", "def"});
  driver.SetBrowserParameters(&params);
  cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  EXPECT_EQ(7u, cmd_line.argv().size());
  EXPECT_EQ("open", cmd_line.argv()[0]);
  EXPECT_EQ("-a", cmd_line.argv()[1]);
  EXPECT_EQ("Safari", cmd_line.argv()[2]);
  EXPECT_EQ("--args", cmd_line.argv()[3]);
  EXPECT_EQ("abc", cmd_line.argv()[4]);
  EXPECT_EQ("http://example.com/", cmd_line.argv()[5]);
  EXPECT_EQ("def", cmd_line.argv()[6]);
}
#endif  // defined(OS_MACOSX)

#if defined(OS_POSIX)
TEST_F(AlternativeBrowserDriverTest, CreateCommandLineExpandsTilde) {
  setenv("HOME", "/home/foobar", true);

  AlternativeBrowserDriverImpl driver;
  driver.SetBrowserPath("/usr/bin/true");
  auto params = UTF8VectorToListValue({"~/file.txt", "/tmp/backup.txt~"});
  driver.SetBrowserParameters(&params);
  auto cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  const base::CommandLine::StringVector& argv = cmd_line.argv();
  EXPECT_EQ(4u, argv.size());
  EXPECT_EQ(UTF8ToNative("/usr/bin/true"), argv[0]);
  EXPECT_EQ(UTF8ToNative("/home/foobar/file.txt"), argv[1]);
  EXPECT_EQ(UTF8ToNative("/tmp/backup.txt~"), argv[2]);
  EXPECT_EQ(UTF8ToNative("http://example.com/"), argv[3]);
}

TEST_F(AlternativeBrowserDriverTest, CreateCommandLineExpandsEnvVars) {
  setenv("A", "AAA", true);
  setenv("B", "BBB", true);
  setenv("CC", "CCC", true);
  setenv("D", "DDD", true);

  AlternativeBrowserDriverImpl driver;
  driver.SetBrowserPath("/usr/bin/true");
  auto params = UTF8VectorToListValue(
      {"$A", "${B}", "before_${CC}_between_${D}_after", "$NONEXISTENT"});
  driver.SetBrowserParameters(&params);
  auto cmd_line = driver.CreateCommandLine(GURL("http://example.com/"));
  const base::CommandLine::StringVector& argv = cmd_line.argv();
  EXPECT_EQ(6u, argv.size());
  EXPECT_EQ(UTF8ToNative("/usr/bin/true"), argv[0]);
  EXPECT_EQ(UTF8ToNative("AAA"), argv[1]);
  EXPECT_EQ(UTF8ToNative("BBB"), argv[2]);
  EXPECT_EQ("before_CCC_between_DDD_after", argv[3]);
  EXPECT_EQ("", argv[4]);
  EXPECT_EQ(UTF8ToNative("http://example.com/"), argv[5]);
}

TEST_F(AlternativeBrowserDriverTest, CreateCommandLineDoesntExpandUrlContent) {
  setenv("A", "AAA", true);
  setenv("B", "BBB", true);

  AlternativeBrowserDriverImpl driver;
  driver.SetBrowserPath("/usr/bin/true");
  auto cmd_line = driver.CreateCommandLine(GURL("http://evil.com/$A${B}"));
  EXPECT_EQ(2u, cmd_line.argv().size());
  EXPECT_EQ(UTF8ToNative("/usr/bin/true"), cmd_line.argv()[0]);
  EXPECT_EQ("http://evil.com/$A$%7BB%7D", cmd_line.argv()[1]);

  auto params = UTF8VectorToListValue({"${url}"});
  driver.SetBrowserParameters(&params);
  cmd_line = driver.CreateCommandLine(GURL("http://evil.com/$A${B}"));
  EXPECT_EQ(2u, cmd_line.argv().size());
  EXPECT_EQ(UTF8ToNative("/usr/bin/true"), cmd_line.argv()[0]);
  EXPECT_EQ("http://evil.com/$A$%7BB%7D", cmd_line.argv()[1]);
}
#endif  // defined(OS_POSIX)

}  // namespace browser_switcher
