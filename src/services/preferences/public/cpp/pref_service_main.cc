// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/preferences/public/cpp/pref_service_main.h"

#include "services/preferences/pref_store_manager_impl.h"
#include "services/service_manager/public/cpp/service.h"

namespace prefs {

std::pair<std::unique_ptr<service_manager::Service>, base::OnceClosure>
CreatePrefService(service_manager::mojom::ServiceRequest request,
                  PrefStore* managed_prefs,
                  PrefStore* supervised_user_prefs,
                  PrefStore* extension_prefs,
                  PrefStore* command_line_prefs,
                  PersistentPrefStore* user_prefs,
                  PersistentPrefStore* incognito_user_prefs_underlay,
                  PrefStore* recommended_prefs,
                  PrefRegistry* pref_registry,
                  std::vector<const char*> persistent_perf_names) {
  auto service = std::make_unique<PrefStoreManagerImpl>(
      std::move(request), managed_prefs, supervised_user_prefs, extension_prefs,
      command_line_prefs, user_prefs, incognito_user_prefs_underlay,
      recommended_prefs, pref_registry, std::move(persistent_perf_names));
  auto quit_closure = service->ShutDownClosure();
  return std::make_pair(std::move(service), std::move(quit_closure));
}

}  // namespace prefs
