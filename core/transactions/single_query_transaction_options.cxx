/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2021-Present Couchbase, Inc.
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

#include <couchbase/durability_level.hxx>
#include <couchbase/transactions/single_query_transaction_options.hxx>

#include <core/transactions/attempt_context_testing_hooks.hxx>
#include <core/transactions/cleanup_testing_hooks.hxx>

namespace couchbase::transactions
{
auto
single_query_transaction_options::durability_level(couchbase::durability_level durability_level) -> single_query_transaction_options&
{
    durability_level_ = durability_level;
    return *this;
}

auto
single_query_transaction_options::build() const -> built
{
    return { durability_level_, attempt_context_hooks_, cleanup_hooks_ };
}

auto
single_query_transaction_options::test_factories(std::shared_ptr<core::transactions::attempt_context_testing_hooks> hooks,
                                                 std::shared_ptr<core::transactions::cleanup_testing_hooks> cleanup_hooks)
  -> single_query_transaction_options&
{
    attempt_context_hooks_ = hooks;
    cleanup_hooks_ = cleanup_hooks;
    return *this;
}
} // namespace couchbase::transactions
