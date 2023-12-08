/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *   Copyright 2023-Present Couchbase, Inc.
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

#include <memory>
#include <optional>

#include <couchbase/durability_level.hxx>

namespace couchbase::core::transactions
{
/** @internal */
struct attempt_context_testing_hooks;

/** @internal */
struct cleanup_testing_hooks;
} // namespace couchbase::core::transactions

namespace couchbase::transactions
{
struct single_query_transaction_options {
    auto durability_level(couchbase::durability_level durability_level) -> single_query_transaction_options&;

    struct built {
        std::optional<couchbase::durability_level> durability_level;
        std::shared_ptr<core::transactions::attempt_context_testing_hooks> attempt_context_hooks;
        std::shared_ptr<core::transactions::cleanup_testing_hooks> cleanup_hooks;
    };

    /**
     * Validates options and returns them as an immutable value.
     *
     * @return consistent options as an immutable value
     *
     * @exception std::system_error with code errc::common::invalid_argument if the options are not valid
     *
     * @since 1.0.0
     * @internal
     */
    [[nodiscard]] auto build() const -> built;

    /** @private */
    auto test_factories(std::shared_ptr<core::transactions::attempt_context_testing_hooks> hooks,
                        std::shared_ptr<core::transactions::cleanup_testing_hooks> cleanup_hooks)
      -> single_query_transaction_options&;

  private:
    std::optional<couchbase::durability_level> durability_level_{};
    std::shared_ptr<core::transactions::attempt_context_testing_hooks> attempt_context_hooks_{};
    std::shared_ptr<core::transactions::cleanup_testing_hooks> cleanup_hooks_{};
};
} // namespace couchbase::transactions