// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/search_provider_logos/google_logo_api.h"

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace search_provider_logos {

TEST(GoogleNewLogoApiTest, UsesHttps) {
  // "https://" remains in place, even for .cn.
  EXPECT_EQ(GURL("https://www.google.com/async/ddljson"),
            GetGoogleDoodleURL(GURL("https://www.google.com")));
  EXPECT_EQ(GURL("https://www.google.de/async/ddljson"),
            GetGoogleDoodleURL(GURL("https://www.google.de")));
  EXPECT_EQ(GURL("https://www.google.cn/async/ddljson"),
            GetGoogleDoodleURL(GURL("https://www.google.cn")));
  EXPECT_EQ(GURL("https://www.google.com.cn/async/ddljson"),
            GetGoogleDoodleURL(GURL("https://www.google.com.cn")));

  // But "http://" gets replaced by "https://".
  EXPECT_EQ(GURL("https://www.google.com/async/ddljson"),
            GetGoogleDoodleURL(GURL("http://www.google.com")));
  EXPECT_EQ(GURL("https://www.google.de/async/ddljson"),
            GetGoogleDoodleURL(GURL("http://www.google.de")));
  // ...except for .cn, which is allowed to keep "http://".
  EXPECT_EQ(GURL("http://www.google.cn/async/ddljson"),
            GetGoogleDoodleURL(GURL("http://www.google.cn")));
  EXPECT_EQ(GURL("http://www.google.com.cn/async/ddljson"),
            GetGoogleDoodleURL(GURL("http://www.google.com.cn")));
}

TEST(GoogleNewLogoApiTest, AppendPreliminaryParamsParsing) {
  const std::string base_url = "http://foo.bar/";
  EXPECT_EQ(GURL("http://foo.bar/?async=ntp:1"),
            AppendPreliminaryParamsToDoodleURL(false, GURL(base_url)));
  EXPECT_EQ(GURL("http://foo.bar/?test=param&async=ntp:1"),
            AppendPreliminaryParamsToDoodleURL(false,
                                               GURL(base_url + "?test=param")));
  EXPECT_EQ(GURL("http://foo.bar/?async=inside,ntp:1&test=param"),
            AppendPreliminaryParamsToDoodleURL(
                false, GURL(base_url + "?async=inside&test=param")));
  EXPECT_EQ(GURL("http://foo.bar/?async=inside,ntp:1&async=param"),
            AppendPreliminaryParamsToDoodleURL(
                false, GURL(base_url + "?async=inside&async=param")));
}

TEST(GoogleNewLogoApiTest, AppendPreliminaryParams) {
  const GURL logo_url("https://base.doo/target");

  EXPECT_EQ(GURL("https://base.doo/target?async=ntp:1"),
            AppendPreliminaryParamsToDoodleURL(false, logo_url));
  EXPECT_EQ(GURL("https://base.doo/target?async=ntp:1,graybg:1"),
            AppendPreliminaryParamsToDoodleURL(true, logo_url));
}

TEST(GoogleNewLogoApiTest, AppendFingerprintParam) {
  EXPECT_EQ(GURL("https://base.doo/target?async=es_dfp:fingerprint"),
            AppendFingerprintParamToDoodleURL(GURL("https://base.doo/target"),
                                              "fingerprint"));
  EXPECT_EQ(
      GURL("https://base.doo/target?async=ntp:1,graybg:1,es_dfp:fingerprint"),
      AppendFingerprintParamToDoodleURL(
          GURL("https://base.doo/target?async=ntp:1,graybg:1"), "fingerprint"));
}

TEST(GoogleNewLogoApiTest, ResolvesRelativeUrl) {
  const GURL base_url("https://base.doo/");
  const std::string json = R"json()]}'
{
  "ddljson": {
    "target_url": "/target"
  }
})json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  EXPECT_EQ(GURL("https://base.doo/target"), logo->metadata.on_click_url);
}

TEST(GoogleNewLogoApiTest, DoesNotResolveAbsoluteUrl) {
  const GURL base_url("https://base.doo/");
  const std::string json = R"json()]}'
{
  "ddljson": {
    "target_url": "https://www.doodle.com/target"
  }
})json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  EXPECT_EQ(GURL("https://www.doodle.com/target"), logo->metadata.on_click_url);
}

TEST(GoogleNewLogoApiTest, RequiresHttpsForContainedUrls) {
  const GURL base_url("https://base.doo/");
  const std::string json = R"json()]}'
{
  "ddljson": {
    "doodle_type": "INTERACTIVE",
    "target_url": "http://www.doodle.com/target",
    "fullpage_interactive_url": "http://www.doodle.com/interactive",
    "iframe_width_px": 500,
    "iframe_height_px": 200
  }
})json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  // Since the base URL is secure https://, the contained non-secure http://
  // URLs should be ignored.
  EXPECT_EQ(GURL(), logo->metadata.on_click_url);
  EXPECT_EQ(GURL(), logo->metadata.full_page_url);
}

TEST(GoogleNewLogoApiTest, AcceptsHttpForContainedUrlsIfBaseInsecure) {
  const GURL base_url("http://base.doo/");
  const std::string json = R"json()]}'
{
  "ddljson": {
    "doodle_type": "INTERACTIVE",
    "target_url": "http://www.doodle.com/target",
    "fullpage_interactive_url": "http://www.doodle.com/interactive",
    "iframe_width_px": 500,
    "iframe_height_px": 200
  }
})json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  // Since the base URL itself is non-secure http://, the contained non-secure
  // URLs should also be accepted.
  EXPECT_EQ(GURL("http://www.doodle.com/target"), logo->metadata.on_click_url);
  EXPECT_EQ(GURL("http://www.doodle.com/interactive"),
            logo->metadata.full_page_url);
}

TEST(GoogleNewLogoApiTest, ParsesStaticImage) {
  const GURL base_url("https://base.doo/");
  // Note: The base64 encoding of "abc" is "YWJj".
  const std::string json = R"json()]}'
{
  "ddljson": {
    "target_url": "/target",
    "data_uri": "data:image/png;base64,YWJj"
  }
})json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  EXPECT_EQ("abc", logo->encoded_image->data());
  EXPECT_EQ(LogoType::SIMPLE, logo->metadata.type);
}

TEST(GoogleNewLogoApiTest, ParsesAnimatedImage) {
  const GURL base_url("https://base.doo/");
  // Note: The base64 encoding of "abc" is "YWJj".
  const std::string json = R"json()]}'
{
  "ddljson": {
    "doodle_type": "ANIMATED",
    "target_url": "/target",
    "large_image": {
      "is_animated_gif": true,
      "url": "https://www.doodle.com/image.gif"
    },
    "cta_data_uri": "data:image/png;base64,YWJj"
  }
})json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  EXPECT_EQ(GURL("https://www.doodle.com/image.gif"),
            logo->metadata.animated_url);
  EXPECT_EQ("abc", logo->encoded_image->data());
  EXPECT_EQ(LogoType::ANIMATED, logo->metadata.type);
}

TEST(GoogleNewLogoApiTest, ParsesLoggingUrls) {
  const GURL base_url("https://base.doo/");
  const std::string json = R"json()]}'
{
  "ddljson": {
    "doodle_type": "ANIMATED",
    "target_url": "/target",
    "large_image": {
      "is_animated_gif": true,
      "url": "https://www.doodle.com/image.gif"
    },
    "log_url": "/log?a=b",
    "cta_log_url": "/ctalog?c=d"
  }
})json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  EXPECT_EQ(GURL("https://base.doo/log?a=b"), logo->metadata.log_url);
  EXPECT_EQ(GURL("https://base.doo/ctalog?c=d"), logo->metadata.cta_log_url);
}

TEST(GoogleNewLogoApiTest, ParsesInteractiveDoodle) {
  const GURL base_url("https://base.doo/");
  const std::string json = R"json()]}'
{
  "ddljson": {
    "doodle_type": "INTERACTIVE",
    "fullpage_interactive_url": "/fullpage",
    "target_url": "/target",
    "iframe_width_px": 500,
    "iframe_height_px": 200
  }
})json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  EXPECT_EQ(GURL("https://base.doo/fullpage"), logo->metadata.full_page_url);
  EXPECT_EQ(LogoType::INTERACTIVE, logo->metadata.type);
  EXPECT_EQ(500, logo->metadata.iframe_width_px);
  EXPECT_EQ(200, logo->metadata.iframe_height_px);
}

TEST(GoogleNewLogoApiTest, ParsesInteractiveDoodleWithNewWindowAsSimple) {
  const GURL base_url("https://base.doo/");
  // Note: The base64 encoding of "abc" is "YWJj".
  const std::string json = R"json()]}'
    {
      "ddljson": {
        "doodle_type": "INTERACTIVE",
        "target_url": "/search?q=interactive",
        "fullpage_interactive_url": "/play",
        "launch_interactive_behavior": "NEW_WINDOW",
        "data_uri": "data:image/png;base64,YWJj",
        "iframe_width_px": 500,
        "iframe_height_px": 200
      }
    })json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  EXPECT_EQ(LogoType::SIMPLE, logo->metadata.type);
  EXPECT_EQ(GURL("https://base.doo/play"), logo->metadata.on_click_url);
  EXPECT_EQ(GURL("https://base.doo/play"), logo->metadata.full_page_url);
  EXPECT_EQ(0, logo->metadata.iframe_width_px);
  EXPECT_EQ(0, logo->metadata.iframe_height_px);
  EXPECT_EQ("abc", logo->encoded_image->data());
}

TEST(GoogleNewLogoApiTest, DefaultsInteractiveIframeSize) {
  const GURL base_url("https://base.doo/");
  const std::string json = R"json()]}'
    {
      "ddljson": {
        "doodle_type": "INTERACTIVE",
        "target_url": "/search?q=interactive",
        "fullpage_interactive_url": "/play"
      }
    })json";

  bool failed = false;
  std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
      base_url, std::make_unique<std::string>(json), base::Time(), &failed);

  ASSERT_FALSE(failed);
  ASSERT_TRUE(logo);
  EXPECT_EQ(LogoType::INTERACTIVE, logo->metadata.type);
  EXPECT_EQ(GURL("https://base.doo/play"), logo->metadata.full_page_url);
  EXPECT_EQ(500, logo->metadata.iframe_width_px);
  EXPECT_EQ(200, logo->metadata.iframe_height_px);
}

TEST(GoogleNewLogoApiTest, ParsesCapturedApiResult) {
  const GURL base_url("https://base.doo/");

  base::FilePath test_data_dir;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SOURCE_ROOT, &test_data_dir));
  test_data_dir = test_data_dir.AppendASCII("components")
                      .AppendASCII("test")
                      .AppendASCII("data")
                      .AppendASCII("search_provider_logos");

  const struct TestCase {
    const char* file;
    bool has_image_data;
  } test_cases[] = {
      {"ddljson_android0.json", true}, {"ddljson_android0_fp.json", false},
      {"ddljson_android1.json", true}, {"ddljson_android1_fp.json", false},
      {"ddljson_android2.json", true}, {"ddljson_android2_fp.json", false},
      {"ddljson_android3.json", true}, {"ddljson_android3_fp.json", false},
      {"ddljson_android4.json", true}, {"ddljson_android4_fp.json", false},
      {"ddljson_desktop0.json", true}, {"ddljson_desktop0_fp.json", false},
      {"ddljson_desktop1.json", true}, {"ddljson_desktop1_fp.json", false},
      {"ddljson_desktop2.json", true}, {"ddljson_desktop2_fp.json", false},
      {"ddljson_desktop3.json", true}, {"ddljson_desktop3_fp.json", false},
      {"ddljson_desktop4.json", true}, {"ddljson_desktop4_fp.json", false},
      {"ddljson_ios0.json", true},     {"ddljson_ios0_fp.json", false},
      {"ddljson_ios1.json", true},     {"ddljson_ios1_fp.json", false},
      {"ddljson_ios2.json", true},     {"ddljson_ios2_fp.json", false},
      {"ddljson_ios3.json", true},     {"ddljson_ios3_fp.json", false},
      {"ddljson_ios4.json", true},     {"ddljson_ios4_fp.json", false},
  };

  for (const TestCase& test_case : test_cases) {
    std::string json;
    ASSERT_TRUE(base::ReadFileToString(
        test_data_dir.AppendASCII(test_case.file), &json))
        << test_case.file;

    bool failed = false;
    std::unique_ptr<EncodedLogo> logo = ParseDoodleLogoResponse(
        base_url, std::make_unique<std::string>(json), base::Time(), &failed);

    EXPECT_FALSE(failed) << test_case.file;
    EXPECT_TRUE(logo) << test_case.file;
    bool has_image_data = logo && logo->encoded_image.get();
    EXPECT_EQ(has_image_data, test_case.has_image_data) << test_case.file;
  }
}

}  // namespace search_provider_logos
