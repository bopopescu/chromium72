// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <cmath>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/pickle.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "content/common/page_state_serialization.h"
#include "content/public/common/content_paths.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {
namespace {

//-----------------------------------------------------------------------------

template <typename T>
void ExpectEquality(const T& expected, const T& actual) {
  EXPECT_EQ(expected, actual);
}

template <typename T>
void ExpectEquality(const std::vector<T>& expected,
                    const std::vector<T>& actual) {
  EXPECT_EQ(expected.size(), actual.size());
  for (size_t i = 0; i < std::min(expected.size(), actual.size()); ++i)
    ExpectEquality(expected[i], actual[i]);
}

template <>
void ExpectEquality(const network::DataElement& expected,
                    const network::DataElement& actual) {
  EXPECT_EQ(expected.type(), actual.type());
  if (expected.type() == network::DataElement::TYPE_BYTES &&
      actual.type() == network::DataElement::TYPE_BYTES) {
    EXPECT_EQ(std::string(expected.bytes(), expected.length()),
              std::string(actual.bytes(), actual.length()));
  }
  EXPECT_EQ(expected.path(), actual.path());
  EXPECT_EQ(expected.offset(), actual.offset());
  EXPECT_EQ(expected.length(), actual.length());
  EXPECT_EQ(expected.expected_modification_time(),
            actual.expected_modification_time());
  EXPECT_EQ(expected.blob_uuid(), actual.blob_uuid());
}

template <>
void ExpectEquality(const ExplodedHttpBody& expected,
                    const ExplodedHttpBody& actual) {
  EXPECT_EQ(expected.http_content_type, actual.http_content_type);
  EXPECT_EQ(expected.contains_passwords, actual.contains_passwords);
  if (expected.request_body == nullptr || actual.request_body == nullptr) {
    EXPECT_EQ(nullptr, expected.request_body);
    EXPECT_EQ(nullptr, actual.request_body);
  } else {
    EXPECT_EQ(expected.request_body->identifier(),
              actual.request_body->identifier());
    ExpectEquality(*expected.request_body->elements(),
                   *actual.request_body->elements());
  }
}

template <>
void ExpectEquality(const ExplodedFrameState& expected,
                    const ExplodedFrameState& actual) {
  EXPECT_EQ(expected.url_string, actual.url_string);
  EXPECT_EQ(expected.referrer, actual.referrer);
  EXPECT_EQ(expected.referrer_policy, actual.referrer_policy);
  EXPECT_EQ(expected.target, actual.target);
  EXPECT_EQ(expected.state_object, actual.state_object);
  ExpectEquality(expected.document_state, actual.document_state);
  EXPECT_EQ(expected.scroll_restoration_type, actual.scroll_restoration_type);
  EXPECT_EQ(expected.visual_viewport_scroll_offset,
            actual.visual_viewport_scroll_offset);
  EXPECT_EQ(expected.scroll_offset, actual.scroll_offset);
  EXPECT_EQ(expected.item_sequence_number, actual.item_sequence_number);
  EXPECT_EQ(expected.document_sequence_number, actual.document_sequence_number);
  EXPECT_EQ(expected.page_scale_factor, actual.page_scale_factor);
  EXPECT_EQ(expected.scroll_anchor_selector, actual.scroll_anchor_selector);
  EXPECT_EQ(expected.scroll_anchor_offset, actual.scroll_anchor_offset);
  EXPECT_EQ(expected.scroll_anchor_simhash, actual.scroll_anchor_simhash);
  ExpectEquality(expected.http_body, actual.http_body);
  ExpectEquality(expected.children, actual.children);
}

void ExpectEquality(const ExplodedPageState& expected,
                    const ExplodedPageState& actual) {
  ExpectEquality(expected.referenced_files, actual.referenced_files);
  ExpectEquality(expected.top, actual.top);
}

//-----------------------------------------------------------------------------

class PageStateSerializationTest : public testing::Test {
 public:
  void PopulateFrameState(ExplodedFrameState* frame_state) {
    // Invent some data for the various fields.
    frame_state->url_string = base::UTF8ToUTF16("http://dev.chromium.org/");
    frame_state->referrer =
        base::UTF8ToUTF16("https://www.google.com/search?q=dev.chromium.org");
    frame_state->referrer_policy = network::mojom::ReferrerPolicy::kAlways;
    frame_state->target = base::UTF8ToUTF16("foo");
    frame_state->state_object = base::nullopt;
    frame_state->document_state.push_back(base::UTF8ToUTF16("1"));
    frame_state->document_state.push_back(base::UTF8ToUTF16("q"));
    frame_state->document_state.push_back(base::UTF8ToUTF16("text"));
    frame_state->document_state.push_back(
        base::UTF8ToUTF16("dev.chromium.org"));
    frame_state->scroll_restoration_type =
        blink::kWebHistoryScrollRestorationManual;
    frame_state->visual_viewport_scroll_offset = gfx::PointF(10, 15);
    frame_state->scroll_offset = gfx::Point(0, 100);
    frame_state->item_sequence_number = 1;
    frame_state->document_sequence_number = 2;
    frame_state->page_scale_factor = 2.0;
    frame_state->scroll_anchor_selector = base::UTF8ToUTF16("#selector");
    frame_state->scroll_anchor_offset = gfx::PointF(2.5, 3.5);
    frame_state->scroll_anchor_simhash = 12345;
  }

  void PopulateHttpBody(
      ExplodedHttpBody* http_body,
      std::vector<base::Optional<base::string16>>* referenced_files) {
    http_body->request_body = new network::ResourceRequestBody();
    http_body->request_body->set_identifier(12345);
    http_body->contains_passwords = false;
    http_body->http_content_type = base::UTF8ToUTF16("text/foo");

    std::string test_body("foo");
    http_body->request_body->AppendBytes(test_body.data(), test_body.size());

    base::FilePath path(FILE_PATH_LITERAL("file.txt"));
    http_body->request_body->AppendFileRange(base::FilePath(path), 100, 1024,
                                             base::Time::FromDoubleT(9999.0));

    referenced_files->emplace_back(path.AsUTF16Unsafe());
  }

  void PopulateFrameStateForBackwardsCompatTest(
      ExplodedFrameState* frame_state,
      bool is_child) {
    frame_state->url_string = base::UTF8ToUTF16("http://chromium.org/");
    frame_state->referrer = base::UTF8ToUTF16("http://google.com/");
    frame_state->referrer_policy = network::mojom::ReferrerPolicy::kDefault;
    if (!is_child)
      frame_state->target = base::UTF8ToUTF16("target");
    frame_state->scroll_restoration_type =
        blink::kWebHistoryScrollRestorationAuto;
    frame_state->visual_viewport_scroll_offset = gfx::PointF(-1, -1);
    frame_state->scroll_offset = gfx::Point(42, -42);
    frame_state->item_sequence_number = 123;
    frame_state->document_sequence_number = 456;
    frame_state->page_scale_factor = 2.0f;

    frame_state->document_state.push_back(base::UTF8ToUTF16(
        "\n\r?% WebKit serialized form state version 8 \n\r=&"));
    frame_state->document_state.push_back(base::UTF8ToUTF16("form key"));
    frame_state->document_state.push_back(base::UTF8ToUTF16("1"));
    frame_state->document_state.push_back(base::UTF8ToUTF16("foo"));
    frame_state->document_state.push_back(base::UTF8ToUTF16("file"));
    frame_state->document_state.push_back(base::UTF8ToUTF16("2"));
    frame_state->document_state.push_back(base::UTF8ToUTF16("file.txt"));
    frame_state->document_state.push_back(base::UTF8ToUTF16("displayName"));

    if (!is_child) {
      frame_state->http_body.http_content_type = base::UTF8ToUTF16("foo/bar");
      frame_state->http_body.request_body = new network::ResourceRequestBody();
      frame_state->http_body.request_body->set_identifier(789);

      std::string test_body("first data block");
      frame_state->http_body.request_body->AppendBytes(test_body.data(),
                                                       test_body.size());

      frame_state->http_body.request_body->AppendFileRange(
          base::FilePath(FILE_PATH_LITERAL("file.txt")), 0,
          std::numeric_limits<uint64_t>::max(), base::Time::FromDoubleT(0.0));

      std::string test_body2("data the second");
      frame_state->http_body.request_body->AppendBytes(test_body2.data(),
                                                       test_body2.size());

      ExplodedFrameState child_state;
      PopulateFrameStateForBackwardsCompatTest(&child_state, true);
      frame_state->children.push_back(child_state);
    }
  }

  void PopulatePageStateForBackwardsCompatTest(ExplodedPageState* page_state) {
    page_state->referenced_files.push_back(base::UTF8ToUTF16("file.txt"));
    PopulateFrameStateForBackwardsCompatTest(&page_state->top, false);
  }

  void ReadBackwardsCompatPageState(const std::string& suffix,
                                    int version,
                                    ExplodedPageState* page_state) {
    base::FilePath path;
    base::PathService::Get(content::DIR_TEST_DATA, &path);
    path = path.AppendASCII("page_state")
               .AppendASCII(
                   base::StringPrintf("serialized_%s.dat", suffix.c_str()));

    std::string file_contents;
    if (!base::ReadFileToString(path, &file_contents)) {
      ADD_FAILURE() << "File not found: " << path.value();
      return;
    }

    std::string trimmed_file_contents;
    EXPECT_TRUE(
        base::RemoveChars(file_contents, "\r\n", &trimmed_file_contents));

    std::string saved_encoded_state;
    // PageState is encoded twice; once via EncodePageState, and again
    // via Base64Decode, so we need to Base64Decode to get the original
    // encoded PageState.
    EXPECT_TRUE(
        base::Base64Decode(trimmed_file_contents, &saved_encoded_state));

#if defined(OS_ANDROID)
    // Because version 11 of the file format unfortunately bakes in the device
    // scale factor on Android, perform this test by assuming a preset device
    // scale factor, ignoring the device scale factor of the current device.
    const float kPresetDeviceScaleFactor = 2.0f;
    EXPECT_TRUE(DecodePageStateWithDeviceScaleFactorForTesting(
        saved_encoded_state, kPresetDeviceScaleFactor, page_state));
#else
    EXPECT_EQ(version,
              DecodePageStateForTesting(saved_encoded_state, page_state));
#endif
  }

  void TestBackwardsCompat(int version) {
    std::string suffix = base::StringPrintf("v%d", version);

#if defined(OS_ANDROID)
    // Unfortunately, the format of version 11 is different on Android, so we
    // need to use a special reference file.
    if (version == 11) {
      suffix = std::string("v11_android");
    }
#endif

    ExplodedPageState decoded_state;
    ExplodedPageState expected_state;
    PopulatePageStateForBackwardsCompatTest(&expected_state);
    ReadBackwardsCompatPageState(suffix, version, &decoded_state);

    ExpectEquality(expected_state, decoded_state);
  }
};

TEST_F(PageStateSerializationTest, BasicEmpty) {
  ExplodedPageState input;

  std::string encoded;
  EncodePageState(input, &encoded);

  ExplodedPageState output;
  EXPECT_TRUE(DecodePageState(encoded, &output));

  ExpectEquality(input, output);
}

TEST_F(PageStateSerializationTest, BasicFrame) {
  ExplodedPageState input;
  PopulateFrameState(&input.top);

  std::string encoded;
  EncodePageState(input, &encoded);

  ExplodedPageState output;
  EXPECT_TRUE(DecodePageState(encoded, &output));

  ExpectEquality(input, output);
}

TEST_F(PageStateSerializationTest, BasicFramePOST) {
  ExplodedPageState input;
  PopulateFrameState(&input.top);
  PopulateHttpBody(&input.top.http_body, &input.referenced_files);

  std::string encoded;
  EncodePageState(input, &encoded);

  ExplodedPageState output;
  EXPECT_TRUE(DecodePageState(encoded, &output));

  ExpectEquality(input, output);
}

TEST_F(PageStateSerializationTest, BasicFrameSet) {
  ExplodedPageState input;
  PopulateFrameState(&input.top);

  // Add some child frames.
  for (int i = 0; i < 4; ++i) {
    ExplodedFrameState child_state;
    PopulateFrameState(&child_state);
    input.top.children.push_back(child_state);
  }

  std::string encoded;
  EncodePageState(input, &encoded);

  ExplodedPageState output;
  EXPECT_TRUE(DecodePageState(encoded, &output));

  ExpectEquality(input, output);
}

TEST_F(PageStateSerializationTest, BasicFrameSetPOST) {
  ExplodedPageState input;
  PopulateFrameState(&input.top);

  // Add some child frames.
  for (int i = 0; i < 4; ++i) {
    ExplodedFrameState child_state;
    PopulateFrameState(&child_state);

    // Simulate a form POST on a subframe.
    if (i == 2)
      PopulateHttpBody(&child_state.http_body, &input.referenced_files);

    input.top.children.push_back(child_state);
  }

  std::string encoded;
  EncodePageState(input, &encoded);

  ExplodedPageState output;
  DecodePageState(encoded, &output);

  ExpectEquality(input, output);
}

TEST_F(PageStateSerializationTest, BadMessagesTest1) {
  base::Pickle p;
  // Version 14
  p.WriteInt(14);
  // Empty strings.
  for (int i = 0; i < 6; ++i)
    p.WriteInt(-1);
  // Bad real number.
  p.WriteInt(-1);

  std::string s(static_cast<const char*>(p.data()), p.size());

  ExplodedPageState output;
  EXPECT_FALSE(DecodePageState(s, &output));
}

TEST_F(PageStateSerializationTest, BadMessagesTest2) {
  double d = 0;
  base::Pickle p;
  // Version 14
  p.WriteInt(14);
  // Empty strings.
  for (int i = 0; i < 6; ++i)
    p.WriteInt(-1);
  // More misc fields.
  p.WriteData(reinterpret_cast<const char*>(&d), sizeof(d));
  p.WriteInt(1);
  p.WriteInt(1);
  p.WriteInt(0);
  p.WriteInt(0);
  p.WriteInt(-1);
  p.WriteInt(0);
  // WebForm
  p.WriteInt(1);
  p.WriteInt(blink::WebHTTPBody::Element::kTypeData);

  std::string s(static_cast<const char*>(p.data()), p.size());

  ExplodedPageState output;
  EXPECT_FALSE(DecodePageState(s, &output));
}

// Tests that LegacyEncodePageState, which uses the pre-mojo serialization
// format, produces the exact same blob as it did when the test was written.
// This ensures that the implementation is frozen, which is needed to correctly
// test compatibility and migration.
TEST_F(PageStateSerializationTest, LegacyEncodePageStateFrozen) {
  ExplodedPageState actual_state;
  PopulatePageStateForBackwardsCompatTest(&actual_state);

  std::string actual_encoded_state;
  LegacyEncodePageStateForTesting(actual_state, 25, &actual_encoded_state);

  base::FilePath path;
  base::PathService::Get(content::DIR_TEST_DATA, &path);
  path = path.AppendASCII("page_state").AppendASCII("serialized_v25.dat");

  std::string file_contents;
  ASSERT_TRUE(base::ReadFileToString(path, &file_contents))
      << "File not found: " << path.value();

  std::string trimmed_file_contents;
  EXPECT_TRUE(base::RemoveChars(file_contents, "\n", &trimmed_file_contents));

  std::string expected_encoded_state;
  EXPECT_TRUE(
      base::Base64Decode(trimmed_file_contents, &expected_encoded_state));

  ExpectEquality(actual_encoded_state, expected_encoded_state);
}

TEST_F(PageStateSerializationTest, ScrollAnchorSelectorLengthLimited) {
  ExplodedPageState input;
  PopulateFrameState(&input.top);

  std::string excessive_length_string(kMaxScrollAnchorSelectorLength + 1, 'a');

  input.top.scroll_anchor_selector = base::UTF8ToUTF16(excessive_length_string);

  std::string encoded;
  EncodePageState(input, &encoded);

  ExplodedPageState output;
  DecodePageState(encoded, &output);

  // We should drop all the scroll anchor data if the length is over the limit.
  EXPECT_FALSE(output.top.scroll_anchor_selector);
  EXPECT_EQ(output.top.scroll_anchor_offset, gfx::PointF());
  EXPECT_EQ(output.top.scroll_anchor_simhash, 0ul);
}

// Change to #if 1 to enable this code. Run this test to generate data, based on
// the current serialization format, for the BackwardsCompat_vXX tests. This
// will generate an expected.dat in the temp directory, which should be moved
// //content/test/data/page_state/serialization_vXX.dat. A corresponding test
// case for that version should also then be added below. You need to add such
// a test for any addition/change to the schema of serialized page state.
// If you're adding a field whose type is defined externally of
// page_state.mojom, add an backwards compat test for that field specifically
// by dumping a state object with only that field populated. See, e.g.,
// BackwardsCompat_UrlString as an example.
//
// IMPORTANT: this code dumps the serialization as the *current* version, so if
// generating a backwards compat test for v23, the tree must be synced to a
// revision where page_state_serialization.cc:kCurrentVersion == 23.
#if 0
TEST_F(PageStateSerializationTest, DumpExpectedPageStateForBackwardsCompat) {
  ExplodedPageState state;
  PopulatePageStateForBackwardsCompatTest(&state);

  std::string encoded;
  EncodePageState(state, &encoded);

  std::string base64;
  base::Base64Encode(encoded, &base64);

  base::FilePath path;
  base::PathService::Get(base::DIR_TEMP, &path);
  path = path.AppendASCII("expected.dat");

  FILE* fp = base::OpenFile(path, "wb");
  ASSERT_TRUE(fp);

  const size_t kRowSize = 76;
  for (size_t offset = 0; offset < base64.size(); offset += kRowSize) {
    size_t length = std::min(base64.size() - offset, kRowSize);
    std::string segment(&base64[offset], length);
    segment.push_back('\n');
    ASSERT_EQ(1U, fwrite(segment.data(), segment.size(), 1, fp));
  }

  fclose(fp);
}
#endif

#if !defined(OS_ANDROID)
// TODO(darin): Re-enable for Android once this test accounts for systems with
//              a device scale factor not equal to 2.
TEST_F(PageStateSerializationTest, BackwardsCompat_v11) {
  TestBackwardsCompat(11);
}
#endif

TEST_F(PageStateSerializationTest, BackwardsCompat_v12) {
  TestBackwardsCompat(12);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v13) {
  TestBackwardsCompat(13);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v14) {
  TestBackwardsCompat(14);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v15) {
  TestBackwardsCompat(15);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v16) {
  TestBackwardsCompat(16);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v18) {
  TestBackwardsCompat(18);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v20) {
  TestBackwardsCompat(20);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v21) {
  TestBackwardsCompat(21);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v22) {
  TestBackwardsCompat(22);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v23) {
  TestBackwardsCompat(23);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v24) {
  TestBackwardsCompat(24);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v25) {
  TestBackwardsCompat(25);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v26) {
  TestBackwardsCompat(26);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_v27) {
  TestBackwardsCompat(27);
}

// Add your new backwards compat test for future versions *above* this
// comment block; field-specific tests go *below* this comment block.
// Any field additions require a new version and backcompat test; only fields
// with external type definitions require their own dedicated test.
// See DumpExpectedPageStateForBackwardsCompat for more details.
// If any of the below tests fail, you likely made a backwards incompatible
// change to a definition that page_state.mojom relies on. Ideally you should
// find a way to avoid making this change; if that's not possible, contact the
// page state serialization owners to figure out a resolution.

TEST_F(PageStateSerializationTest, BackwardsCompat_ReferencedFiles) {
  ExplodedPageState state;
  state.referenced_files.push_back(base::UTF8ToUTF16("file.txt"));

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("referenced_files", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_UrlString) {
  ExplodedPageState state;
  state.top.url_string = base::ASCIIToUTF16("http://chromium.org");

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("url_string", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_Referrer) {
  ExplodedPageState state;
  state.top.referrer = base::ASCIIToUTF16("http://www.google.com");

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("referrer", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_Target) {
  ExplodedPageState state;
  state.top.target = base::ASCIIToUTF16("http://www.google.com");

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("target", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_StateObject) {
  ExplodedPageState state;
  state.top.state_object = base::ASCIIToUTF16("state");

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("state_object", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_DocumentState) {
  ExplodedPageState state;
  state.top.document_state.push_back(base::ASCIIToUTF16(
      "\n\r?% WebKit serialized form state version 8 \n\r=&"));

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("document_state", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_ScrollRestorationType) {
  ExplodedPageState state;
  state.top.scroll_restoration_type = blink::kWebHistoryScrollRestorationManual;

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("scroll_restoration_type", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_VisualViewportScrollOffset) {
  ExplodedPageState state;
  state.top.visual_viewport_scroll_offset = gfx::PointF(42.2, -42.2);

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("visual_viewport_scroll_offset", 26,
                               &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_ScrollOffset) {
  ExplodedPageState state;
  state.top.scroll_offset = gfx::Point(1, -1);

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("scroll_offset", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_ReferrerPolicy) {
  ExplodedPageState state;
  state.top.referrer_policy = network::mojom::ReferrerPolicy::kAlways;

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("referrer_policy", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

TEST_F(PageStateSerializationTest, BackwardsCompat_HttpBody) {
  ExplodedPageState state;
  ExplodedHttpBody& http_body = state.top.http_body;

  http_body.request_body = new network::ResourceRequestBody();
  http_body.request_body->set_identifier(12345);
  http_body.contains_passwords = false;
  http_body.http_content_type = base::UTF8ToUTF16("text/foo");

  std::string test_body("foo");
  http_body.request_body->AppendBytes(test_body.data(), test_body.size());

  http_body.request_body->AppendBlob("some_uuid");

  base::FilePath path(FILE_PATH_LITERAL("file.txt"));
  http_body.request_body->AppendFileRange(base::FilePath(path), 100, 1024,
                                          base::Time::FromDoubleT(9999.0));

  ExplodedPageState saved_state;
  ReadBackwardsCompatPageState("http_body", 26, &saved_state);
  ExpectEquality(state, saved_state);
}

// Add new backwards compat field-specific tests here.  See comment above for
// where to put backwards compat version tests.

}  // namespace
}  // namespace content
