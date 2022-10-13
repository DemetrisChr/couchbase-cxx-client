/*
 *     Copyright 2022 Couchbase, Inc.
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

#include "test_helper.hxx"
#include "utils/transactions_env.h"

static const tao::json::value content{ { "some_number", 0 } };

TEST_CASE("can get", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    REQUIRE(TransactionsTestEnvironment::upsert_doc(id, content));
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket("default").default_collection());
    auto result = c.transactions()->run([id, coll](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.get(coll, id.key());
        CHECK(doc->key() == id.key());
        CHECK_FALSE(doc->cas().empty());
        CHECK(doc->content<tao::json::value>() == content);
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK_FALSE(result.ctx.ec());
}

TEST_CASE("get returns error if doc doesn't exist", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket("default").default_collection());
    auto result = c.transactions()->run([id, coll](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.get(coll, id.key());
        CHECK(doc->ctx().ec());
        CHECK(doc->ctx().ec() == couchbase::errc::transaction_op::document_not_found_exception);
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK_FALSE(result.unstaging_complete);
    CHECK_FALSE(result.ctx.ec());
}

TEST_CASE("can insert", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket(id.bucket()).scope(id.scope()).collection(id.collection()));
    auto result = c.transactions()->run([id, coll](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.insert(coll, id.key(), content);
        CHECK(doc->key() == id.key());
        CHECK_FALSE(doc->cas().empty());
        CHECK(doc->content<tao::json::value>() == content);
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK(result.unstaging_complete);
    CHECK_FALSE(result.ctx.ec());
    // check that it is really there now
    auto final_doc = TransactionsTestEnvironment::get_doc(id);
    CHECK(final_doc.content_as<tao::json::value>() == content);
}

TEST_CASE("insert fails as expected when doc already exists", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    tao::json::value new_content{ { "something", "else" } };
    REQUIRE(TransactionsTestEnvironment::upsert_doc(id, content));
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket(id.bucket()).scope(id.scope()).collection(id.collection()));
    auto result = c.transactions()->run([id, coll, new_content](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.insert(coll, id.key(), new_content);
        CHECK(doc->ctx().ec());
        CHECK(doc->ctx().ec() == couchbase::errc::transaction_op::document_exists_exception);
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK_FALSE(result.unstaging_complete);
    CHECK(result.ctx.ec() == couchbase::errc::transaction::failed);
    CHECK(result.ctx.cause() == couchbase::errc::transaction_op::document_exists_exception);
    // check that it is really unchanged too.
    auto final_doc = TransactionsTestEnvironment::get_doc(id);
    CHECK(final_doc.content_as<tao::json::value>() == content);
}

TEST_CASE("can replace", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    REQUIRE(TransactionsTestEnvironment::upsert_doc(id, content));
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket("default").default_collection());
    tao::json::value new_content = { { "some_other_number", 3 } };
    auto result = c.transactions()->run([id, coll, new_content](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.get(coll, id.key());
        auto replaced_doc = ctx.replace(doc, new_content);
        CHECK(doc->key() == replaced_doc->key());
        CHECK(doc->cas() != replaced_doc->cas());
        CHECK(doc->content<tao::json::value>() == content);
        CHECK(replaced_doc->content<tao::json::value>() == new_content);
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK(result.unstaging_complete);
    CHECK_FALSE(result.ctx.ec());
    // check that it is really replaced
    auto final_doc = TransactionsTestEnvironment::get_doc(id);
    CHECK(final_doc.content_as<tao::json::value>() == new_content);
}

TEST_CASE("replace fails as expected with bad cas", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    REQUIRE(TransactionsTestEnvironment::upsert_doc(id, content));
    tao::json::value new_content = { { "some_other_number", 3 } };
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket("default").default_collection());
    auto result = c.transactions()->run([id, coll, new_content](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.get(coll, id.key());
        std::reinterpret_pointer_cast<couchbase::core::transactions::transaction_get_result>(doc)->cas(100);
        auto replaced_doc = ctx.replace(doc, new_content);
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK_FALSE(result.unstaging_complete);
    CHECK(result.ctx.ec());
    CHECK(result.ctx.ec() == couchbase::errc::transaction::expired);
    // check that it is unchanged
    auto doc = TransactionsTestEnvironment::get_doc(id);
    REQUIRE(doc.content_as<tao::json::value>() == content);
}

TEST_CASE("can remove", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    REQUIRE(TransactionsTestEnvironment::upsert_doc(id, content));
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket("default").default_collection());
    auto result = c.transactions()->run([id, coll](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.get(coll, id.key());
        auto removed_doc = ctx.remove(doc);
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK(result.unstaging_complete);
    // make sure it is really gone...
    try {
        auto doc = TransactionsTestEnvironment::get_doc(id);
        FAIL("expected doc to not exist");
    } catch (const couchbase::core::transactions::client_error& e) {
        REQUIRE(e.res()->ec == couchbase::errc::key_value::document_not_found);
    }
}
TEST_CASE("remove fails as expected with bad cas", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    REQUIRE(TransactionsTestEnvironment::upsert_doc(id, content));
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket("default").default_collection());
    auto result = c.transactions()->run([id, coll](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.get(coll, id.key());
        // change cas, so remove will fail and retry
        std::reinterpret_pointer_cast<couchbase::core::transactions::transaction_get_result>(doc)->cas(100);
        auto err = ctx.remove(doc);
        CHECK(err.ec());
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK_FALSE(result.unstaging_complete);
    CHECK(result.ctx.ec());
    CHECK(result.ctx.ec() == couchbase::errc::transaction::expired);
}

TEST_CASE("remove fails as expected with missing doc", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket("default").default_collection());
    auto result = c.transactions()->run([id, coll](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.get(coll, id.key());
        CHECK(doc->ctx().ec() == couchbase::errc::transaction_op::document_not_found_exception);
        // the doc is 'blank', so trying to use it results in failure
        auto err = ctx.remove(doc);
        CHECK(err.ec());
        CHECK(err.ec() == couchbase::errc::transaction_op::unknown);
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK_FALSE(result.unstaging_complete);
    CHECK(result.ctx.ec());
    CHECK(result.ctx.ec() == couchbase::errc::transaction::failed);
    CHECK(result.ctx.cause() == couchbase::errc::transaction_op::unknown);
}

TEST_CASE("uncaught exception in lambda will rollback without retry", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    auto coll = std::make_shared<couchbase::collection>(c.bucket("default").default_collection());
    auto result = c.transactions()->run([id, coll](couchbase::transactions::attempt_context& ctx) {
        auto doc = ctx.insert(coll, id.key(), content);
        CHECK_FALSE(doc->ctx().ec());
        throw std::runtime_error("some exception");
    });
    CHECK_FALSE(result.transaction_id.empty());
    CHECK_FALSE(result.unstaging_complete);
    CHECK(result.ctx.ec());
    CHECK(result.ctx.ec() == couchbase::errc::transaction::failed);
    CHECK(result.ctx.cause() == couchbase::errc::transaction_op::unknown);
}

TEST_CASE("can pass per-transaction configs", "[transactions]")
{
    auto id = TransactionsTestEnvironment::get_document_id();
    REQUIRE(TransactionsTestEnvironment::upsert_doc(id, content));
    auto core_cluster = TransactionsTestEnvironment::get_cluster();
    couchbase::cluster c(core_cluster);
    couchbase::transactions::per_transaction_config cfg{};
    cfg.expiration_time(std::chrono::seconds(1));
    auto coll = std::make_shared<couchbase::collection>(c.bucket("default").default_collection());
    auto begin = std::chrono::steady_clock::now();
    auto result = c.transactions()->run(
      [id, coll](couchbase::transactions::attempt_context& ctx) {
          auto doc = ctx.get(coll, id.key());
          std::reinterpret_pointer_cast<couchbase::core::transactions::transaction_get_result>(doc)->cas(100);
          auto err = ctx.remove(doc);
          CHECK(err.ec());
      },
      cfg);
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);
    // should be greater than the expiration time
    CHECK(elapsed > *cfg.expiration_time());
    // but not by too much (default is 15 seconds, we wanted 1, 2 is plenty)
    CHECK(elapsed < (2 * *cfg.expiration_time()));
    // and of course the txn should have expired
    CHECK_FALSE(result.transaction_id.empty());
    CHECK_FALSE(result.unstaging_complete);
    CHECK(result.ctx.ec());
    CHECK(result.ctx.ec() == couchbase::errc::transaction::expired);
}