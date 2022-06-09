/** @file

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "InkAPIInternal.h" // Added to include the ssl_hook and lifestyle_hook definitions
#include "tscore/ts_file.h"
#include "P_SSLConfig.h"

void
SSLSecret::loadSecret(const std::string &name1, const std::string &name2, std::string &data1, std::string &data2)
{
  // Call the load secret hooks
  //
  class APIHook *curHook = lifecycle_hooks->get(TS_LIFECYCLE_SSL_SECRET_HOOK);
  TSSecretID secret_name;
  secret_name.cert_name     = name1.data();
  secret_name.cert_name_len = name1.size();
  secret_name.key_name      = name2.data();
  secret_name.key_name_len  = name2.size();
  while (curHook) {
    curHook->blocking_invoke(TS_EVENT_SSL_SECRET, &secret_name);
    curHook = curHook->next();
  }

  data1 = this->getSecret(name1);
  data2 = name2.empty() ? std::string{} : this->getSecret(name2);
  if (data1.empty() || (!name2.empty() && data2.empty())) {
    // If none of them loaded it, assume it is a file
    data1 = loadFile(name1);
    if (!name2.empty()) {
      data2 = loadFile(name2);
    }
  }
}

std::string
SSLSecret::loadFile(const std::string &name)
{
  Debug("ssl_secret", "SSLSecret::loadFile(%s)", name.c_str());
  struct stat statdata;
  // Load the secret and add it to the map
  if (stat(name.c_str(), &statdata) < 0) {
    return std::string{};
  }
  std::error_code error;
  std::string const data = ts::file::load(ts::file::path(name), error);
  if (error) {
    // Loading file failed
    return std::string{};
  }
  Debug("ssl_secret", "Secret data: %.50s", data.c_str());
  if (SSLConfigParams::load_ssl_file_cb) {
    SSLConfigParams::load_ssl_file_cb(name.c_str());
  }
  return data;
}

void
SSLSecret::setSecret(const std::string &name, std::string_view data)
{
  std::scoped_lock lock(secret_map_mutex);
  secret_map[name] = std::string{data};
  // The full secret data can be sensitive. Print only the first 50 bytes.
  Debug("ssl_secret", "Set secret for %s to %.*s", name.c_str(), int(data.size() > 50 ? 50 : data.size()), data.data());
}

std::string
SSLSecret::getSecret(const std::string &name) const
{
  std::scoped_lock lock(secret_map_mutex);
  auto iter = secret_map.find(name);
  if (secret_map.end() == iter) {
    Debug("ssl_secret", "Get secret for %s: not found", name.c_str());
    return std::string{};
  }
  if (iter->second.empty()) {
    Debug("ssl_secret", "Get secret for %s: empty", name.c_str());
    return std::string{};
  }
  // The full secret data can be sensitive. Print only the first 50 bytes.
  Debug("ssl_secret", "Get secret for %s: %.50s", name.c_str(), iter->second.c_str());
  return iter->second;
}

void
SSLSecret::getOrLoadSecret(const std::string &name1, const std::string &name2, std::string &data1, std::string &data2)
{
  Debug("ssl_secret", "lookup up secrets for %s and %s", name1.c_str(), name2.c_str());
  std::scoped_lock lock(secret_map_mutex);
  std::string *const data1ptr = &(secret_map[name1]);
  std::string *const data2ptr = [&]() -> std::string *const {
    if (name2.empty()) {
      data2.clear();
      return &data2;
    }
    return &(secret_map[name2]);
  }();
  // If we can't find either secret, load them both again
  if (data1ptr->empty() || (!name2.empty() && data2ptr->empty())) {
    this->loadSecret(name1, name2, *data1ptr, *data2ptr);
  }
  data1 = *data1ptr;
  data2 = *data2ptr;
}
