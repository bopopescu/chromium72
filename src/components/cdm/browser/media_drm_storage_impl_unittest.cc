// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cdm/browser/media_drm_storage_impl.h"

#include <memory>
#include <utility>

#include "base/run_loop.h"
#include "base/unguessable_token.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "media/mojo/services/mojo_media_drm_storage.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace cdm {

namespace {

const char kMediaDrmStorage[] = "media.media_drm_storage";
const char kTestOrigin[] = "https://www.testorigin.com:80";
const char kTestOrigin2[] = "https://www.testorigin2.com:80";

void OnMediaDrmStorageInit(base::UnguessableToken* out_origin_id,
                           const base::UnguessableToken& origin_id) {
  DCHECK(out_origin_id);
  DCHECK(origin_id);
  *out_origin_id = origin_id;
}

}  // namespace

class MediaDrmStorageImplTest : public content::RenderViewHostTestHarness {
 public:
  MediaDrmStorageImplTest() {}

  void SetUp() override {
    RenderViewHostTestHarness::SetUp();

    pref_service_.reset(new TestingPrefServiceSimple());
    PrefRegistrySimple* registry = pref_service_->registry();
    MediaDrmStorageImpl::RegisterProfilePrefs(registry);

    media_drm_storage_ =
        CreateAndInitMediaDrmStorage(GURL(kTestOrigin), &origin_id_);
  }

  void TearDown() override {
    media_drm_storage_.reset();
    base::RunLoop().RunUntilIdle();
  }

 protected:
  using SessionData = media::MediaDrmStorage::SessionData;

  std::unique_ptr<media::MediaDrmStorage> CreateMediaDrmStorage(
      content::RenderFrameHost* rfh) {
    media::mojom::MediaDrmStoragePtr media_drm_storage_ptr;
    auto request = mojo::MakeRequest(&media_drm_storage_ptr);

    auto media_drm_storage = std::make_unique<media::MojoMediaDrmStorage>(
        std::move(media_drm_storage_ptr));

    // The created object will be destroyed on connection error.
    new MediaDrmStorageImpl(rfh, pref_service_.get(), std::move(request));

    return std::move(media_drm_storage);
  }

  std::unique_ptr<media::MediaDrmStorage> CreateAndInitMediaDrmStorage(
      const GURL& origin,
      base::UnguessableToken* origin_id) {
    DCHECK(origin_id);

    std::unique_ptr<media::MediaDrmStorage> media_drm_storage =
        CreateMediaDrmStorage(SimulateNavigation(origin));

    media_drm_storage->Initialize(
        base::BindOnce(OnMediaDrmStorageInit, origin_id));

    base::RunLoop().RunUntilIdle();

    // Verify the origin dictionary is created.
    const base::DictionaryValue* storage_dict =
        pref_service_->GetDictionary(kMediaDrmStorage);
    EXPECT_TRUE(storage_dict->FindKey(kTestOrigin));

    DCHECK(*origin_id);
    return media_drm_storage;
  }

  content::RenderFrameHost* SimulateNavigation(const GURL& url) {
    content::RenderFrameHost* rfh = web_contents()->GetMainFrame();
    content::RenderFrameHostTester::For(rfh)->InitializeRenderFrameIfNeeded();

    auto navigation_simulator =
        content::NavigationSimulator::CreateRendererInitiated(url, rfh);
    navigation_simulator->Commit();
    return navigation_simulator->GetFinalRenderFrameHost();
  }

  void OnProvisioned() {
    media_drm_storage_->OnProvisioned(ExpectResult(true));
  }

  void SavePersistentSession(const std::string& session_id,
                             const std::vector<uint8_t>& key_set_id,
                             const std::string& mime_type,
                             bool success = true) {
    media_drm_storage_->SavePersistentSession(
        session_id,
        SessionData(key_set_id, mime_type, media::MediaDrmKeyType::OFFLINE),
        ExpectResult(success));
  }

  void LoadPersistentSession(const std::string& session_id,
                             const std::vector<uint8_t>& expected_key_set_id,
                             const std::string& expected_mime_type) {
    media_drm_storage_->LoadPersistentSession(
        session_id, ExpectResult(std::make_unique<SessionData>(
                        expected_key_set_id, expected_mime_type,
                        media::MediaDrmKeyType::OFFLINE)));
  }

  void LoadPersistentSessionAndExpectFailure(const std::string& session_id) {
    media_drm_storage_->LoadPersistentSession(
        session_id, ExpectResult(std::unique_ptr<SessionData>()));
  }

  void RemovePersistentSession(const std::string& session_id,
                               bool success = true) {
    media_drm_storage_->RemovePersistentSession(session_id,
                                                ExpectResult(success));
  }

  void SaveAndLoadPersistentSession(const std::string& session_id,
                                    const std::vector<uint8_t>& key_set_id,
                                    const std::string& mime_type) {
    SavePersistentSession(session_id, key_set_id, mime_type);
    LoadPersistentSession(session_id, key_set_id, mime_type);
  }

  media::MediaDrmStorage::ResultCB ExpectResult(bool expected_result) {
    return base::BindOnce(&MediaDrmStorageImplTest::CheckResult,
                          base::Unretained(this), expected_result);
  }

  media::MediaDrmStorage::LoadPersistentSessionCB ExpectResult(
      std::unique_ptr<SessionData> expected_session_data) {
    return base::BindOnce(&MediaDrmStorageImplTest::CheckLoadedSession,
                          base::Unretained(this),
                          std::move(expected_session_data));
  }

  void CheckResult(bool expected_result, bool result) {
    EXPECT_EQ(expected_result, result);
  }

  void CheckLoadedSession(std::unique_ptr<SessionData> expected_session_data,
                          std::unique_ptr<SessionData> session_data) {
    if (!expected_session_data) {
      EXPECT_FALSE(session_data);
      return;
    }

    EXPECT_EQ(expected_session_data->key_set_id, session_data->key_set_id);
    EXPECT_EQ(expected_session_data->mime_type, session_data->mime_type);
  }

  std::unique_ptr<TestingPrefServiceSimple> pref_service_;
  std::unique_ptr<media::MediaDrmStorage> media_drm_storage_;
  base::UnguessableToken origin_id_;
};

// MediaDrmStorageImpl should write origin ID to persistent storage when
// Initialize is called. Later call to Initialize should return the same origin
// ID. The second MediaDrmStorage won't call Initialize until the first one is
// fully initialized.
// TODO(yucliu): Test origin ID is re-generated after clearing licenses.
TEST_F(MediaDrmStorageImplTest, Initialize_OriginIdNotChanged) {
  base::UnguessableToken original_origin_id = origin_id_;
  ASSERT_TRUE(original_origin_id);

  base::UnguessableToken origin_id;
  std::unique_ptr<media::MediaDrmStorage> storage =
      CreateAndInitMediaDrmStorage(GURL(kTestOrigin), &origin_id);
  EXPECT_EQ(origin_id, original_origin_id);
}

// Two MediaDrmStorage call Initialize concurrently. The second MediaDrmStorage
// will NOT wait for the first one to be initialized. Both instances should get
// the same origin ID.
TEST_F(MediaDrmStorageImplTest, Initialize_Concurrent) {
  content::RenderFrameHost* rfh = SimulateNavigation(GURL(kTestOrigin2));

  std::unique_ptr<media::MediaDrmStorage> storage1 = CreateMediaDrmStorage(rfh);
  std::unique_ptr<media::MediaDrmStorage> storage2 = CreateMediaDrmStorage(rfh);

  base::UnguessableToken origin_id_1;
  storage1->Initialize(base::BindOnce(OnMediaDrmStorageInit, &origin_id_1));
  base::UnguessableToken origin_id_2;
  storage2->Initialize(base::BindOnce(OnMediaDrmStorageInit, &origin_id_2));

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(origin_id_1, origin_id_2);
}

TEST_F(MediaDrmStorageImplTest, Initialize_DifferentOrigins) {
  base::UnguessableToken origin_id_1 = origin_id_;
  ASSERT_TRUE(origin_id_1);

  base::UnguessableToken origin_id_2;
  auto storage2 =
      CreateAndInitMediaDrmStorage(GURL(kTestOrigin2), &origin_id_2);
  ASSERT_TRUE(origin_id_2);

  EXPECT_NE(origin_id_1, origin_id_2);
}

TEST_F(MediaDrmStorageImplTest, OnProvisioned) {
  OnProvisioned();
  base::RunLoop().RunUntilIdle();

  // Verify the origin dictionary is created.
  const base::DictionaryValue* storage_dict =
      pref_service_->GetDictionary(kMediaDrmStorage);
  EXPECT_TRUE(storage_dict->FindKey(kTestOrigin));
}

TEST_F(MediaDrmStorageImplTest, OnProvisioned_Twice) {
  OnProvisioned();
  SaveAndLoadPersistentSession("session_id", {1, 0}, "mime/type1");
  // Provisioning again will clear everything associated with the origin.
  OnProvisioned();
  LoadPersistentSessionAndExpectFailure("session_id");
  base::RunLoop().RunUntilIdle();
}

TEST_F(MediaDrmStorageImplTest, SaveSession_Unprovisioned) {
  SaveAndLoadPersistentSession("session_id", {1, 0}, "mime/type1");
  base::RunLoop().RunUntilIdle();
}

TEST_F(MediaDrmStorageImplTest, SaveSession_SaveTwice) {
  OnProvisioned();
  SaveAndLoadPersistentSession("session_id", {1, 0}, "mime/type1");
  SaveAndLoadPersistentSession("session_id", {2, 3}, "mime/type2");
  base::RunLoop().RunUntilIdle();
}

TEST_F(MediaDrmStorageImplTest, SaveAndLoadSession_LoadTwice) {
  OnProvisioned();
  SaveAndLoadPersistentSession("session_id", {1, 0}, "mime/type");
  LoadPersistentSession("session_id", {1, 0}, "mime/type");
  base::RunLoop().RunUntilIdle();
}

TEST_F(MediaDrmStorageImplTest, SaveAndLoadSession_SpecialCharacters) {
  OnProvisioned();
  SaveAndLoadPersistentSession("session.id", {1, 0}, "mime.type");
  SaveAndLoadPersistentSession("session/id", {1, 0}, "mime/type");
  SaveAndLoadPersistentSession("session-id", {1, 0}, "mime-type");
  SaveAndLoadPersistentSession("session_id", {1, 0}, "mime_type");
  SaveAndLoadPersistentSession("session,id", {1, 0}, "mime,type");
  base::RunLoop().RunUntilIdle();
}

TEST_F(MediaDrmStorageImplTest, LoadSession_Unprovisioned) {
  LoadPersistentSessionAndExpectFailure("session_id");
  base::RunLoop().RunUntilIdle();
}

TEST_F(MediaDrmStorageImplTest, RemoveSession_Success) {
  OnProvisioned();
  SaveAndLoadPersistentSession("session_id", {1, 0}, "mime/type");
  RemovePersistentSession("session_id", true);
  LoadPersistentSessionAndExpectFailure("session_id");
  base::RunLoop().RunUntilIdle();
}

TEST_F(MediaDrmStorageImplTest, RemoveSession_InvalidSession) {
  OnProvisioned();
  SaveAndLoadPersistentSession("session_id", {1, 0}, "mime/type");
  RemovePersistentSession("invalid_session_id", true);
  // Can still load "session_id" session.
  LoadPersistentSession("session_id", {1, 0}, "mime/type");
  base::RunLoop().RunUntilIdle();
}

}  // namespace cdm
