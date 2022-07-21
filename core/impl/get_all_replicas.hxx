/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *   Copyright 2020-Present Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#pragma once

#include <couchbase/get_all_replicas.hxx>
#include <couchbase/get_replica_result.hxx>

#include "core/document_id.hxx"
#include "core/error_context/key_value.hxx"

#include <vector>

namespace couchbase::core::impl
{

/**
 * Request structure, that contains everything related to get_all_replicas request.
 *
 * @see couchbase::api::make_all_replicas_request
 * @see couchbase::api::get_all_replicas_options
 *
 * @since 1.0.0
 * @internal
 */
class get_all_replicas_request
{
  public:
    explicit get_all_replicas_request(std::string bucket_name,
                                      std::string scope_name,
                                      std::string collection_name,
                                      std::string document_key,
                                      std::optional<std::chrono::milliseconds> timeout)
      : id_{ std::move(bucket_name), std::move(scope_name), std::move(collection_name), std::move(document_key) }
      , timeout_{ timeout }
    {
    }

    [[nodiscard]] const auto& id() const
    {
        return id_;
    }

    [[nodiscard]] const auto& timeout() const
    {
        return timeout_;
    }

  private:
    core::document_id id_;
    std::optional<std::chrono::milliseconds> timeout_{};
};

} // namespace couchbase::core::impl