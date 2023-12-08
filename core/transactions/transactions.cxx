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

#include "attempt_context_impl.hxx"

#include "core/cluster.hxx"
#include "core/meta/version.hxx"
#include "core/transactions.hxx"
#include "core/operations/document_query.hxx"

#include "internal/exceptions_internal.hxx"
#include "internal/logging.hxx"
#include "internal/transaction_context.hxx"
#include "internal/transactions_cleanup.hxx"
#include "internal/utils.hxx"

namespace couchbase::core::transactions
{
transactions::transactions(core::cluster cluster, const couchbase::transactions::transactions_config& config)
  : transactions(std::move(cluster), config.build())
{
}

transactions::transactions(core::cluster cluster, const couchbase::transactions::transactions_config::built& config)
  : cluster_(std::move(cluster))
  , config_(config)
  , cleanup_(new transactions_cleanup(cluster_, config_))
{
    CB_TXN_LOG_DEBUG(
      "couchbase transactions {} ({}) creating new transaction object", couchbase::core::meta::sdk_id(), couchbase::core::meta::os());
    // if the config specifies custom metadata collection, lets be sure to open that bucket
    // on the cluster before we start.  NOTE: we actually do call get_and_open_buckets which opens all the buckets
    // on the cluster (that we have permissions to open) in the cleanup.   However, that is happening asynchronously
    // so there's a chance we will fail to have opened the custom metadata collection bucket before trying to make a
    // transaction.   We have to open this one _now_.
    if (config_.metadata_collection) {
        auto barrier = std::make_shared<std::promise<std::error_code>>();
        auto f = barrier->get_future();
        std::atomic<bool> callback_called{ false };
        cluster_.open_bucket(config_.metadata_collection->bucket, [&callback_called, barrier](std::error_code ec) {
            if (callback_called.load()) {
                return;
            }
            callback_called = true;
            barrier->set_value(ec);
        });
        auto err = f.get();
        if (err) {
            auto err_msg =
              fmt::format("error opening metadata_collection bucket '{}' specified in the config!", config_.metadata_collection->bucket);
            CB_TXN_LOG_DEBUG(err_msg);
            throw std::runtime_error(err_msg);
        }
    }
}

transactions::~transactions() = default;

template<typename Handler>
::couchbase::transactions::transaction_result
wrap_run(transactions& txns, const couchbase::transactions::transaction_options& config, std::size_t max_attempts, bool single_query_transaction_mode, Handler&& fn)
{
    transaction_context overall(txns, config);
    std::size_t attempts{ 0 };
    while (attempts++ < max_attempts) {
        // NOTE: new_attempt_context has the exponential backoff built in.  So, after
        // the first time it is called, it has a 1ms delay, then 2ms, etc... capped at 100ms
        // until (for now) a timeout is reached (2x the timeout).   Soon, will build in
        // a max attempts instead.  In any case, the timeout occurs in the logic - adding
        // a max attempts or timeout is just in case a bug prevents timeout, etc...
        overall.new_attempt_context();
        auto barrier = std::make_shared<std::promise<std::optional<couchbase::transactions::transaction_result>>>();
        auto f = barrier->get_future();
        auto finalize_handler = [&, barrier](std::optional<transaction_exception> err,
                                             std::optional<couchbase::transactions::transaction_result> result) {
            CB_LOG_DEBUG("Finalize handler called");
            if (result) {
                return barrier->set_value(result);
            }
            if (err) {
                return barrier->set_exception(std::make_exception_ptr(*err));
            }
            barrier->set_value({});
        };
        try {
            auto ctx = overall.current_attempt_context();
            CB_LOG_DEBUG("Executing txn code");
            fn(*ctx);
            CB_LOG_DEBUG("Executed txn code");
        } catch (...) {
            overall.handle_error(std::current_exception(), single_query_transaction_mode, finalize_handler);
            auto retval = f.get();
            if (retval) {
                // no return value, no exception means retry.
                return *retval;
            }
            continue;
        }
        CB_LOG_DEBUG("About to finalize (Single query mode = {})", single_query_transaction_mode);
        overall.finalize(single_query_transaction_mode, finalize_handler);
        CB_LOG_DEBUG("Trying to get return value");
        auto retval = f.get();
        if (retval) {
            CB_LOG_DEBUG("Got the return value!");
            return *retval;
        }
        continue;
    }
    // only thing to do here is return, but we really exceeded the max attempts
    return overall.get_transaction_result();
}

::couchbase::transactions::transaction_result
transactions::run(logic&& code)
{
    couchbase::transactions::transaction_options config;
    return wrap_run(*this, config, max_attempts_, false, std::move(code));
}

::couchbase::transactions::transaction_result
transactions::run(const couchbase::transactions::transaction_options& config, logic&& code)
{
    return wrap_run(*this, config, max_attempts_, false, std::move(code));
}

std::pair<couchbase::transaction_error_context, couchbase::transactions::transaction_result>
transactions::run(couchbase::transactions::txn_logic&& code, const couchbase::transactions::transaction_options& config)
{
    try {
        return { {}, wrap_run(*this, config, max_attempts_, false, std::move(code)) };
    } catch (const transaction_exception& e) {
        // get transaction_error_context from e and return it in the transaction_result
        return e.get_transaction_result();
    }
}

void
transactions::run(const couchbase::transactions::transaction_options& config, async_logic&& code, txn_complete_callback&& cb)
{
    std::thread([this, config, code = std::move(code), cb = std::move(cb)]() {
        try {
            auto result = wrap_run(*this, config, max_attempts_, false, std::move(code));
            return cb({}, result);
        } catch (const transaction_exception& e) {
            return cb(e, std::nullopt);
        }
    }).detach();
}

void
transactions::run(couchbase::transactions::async_txn_logic&& code,
                  couchbase::transactions::async_txn_complete_logic&& cb,
                  const couchbase::transactions::transaction_options& config)
{
    std::thread([this, config, code = std::move(code), cb = std::move(cb)]() {
        try {
            auto result = wrap_run(*this, config, max_attempts_, false, std::move(code));
            return cb({}, result);
        } catch (const transaction_exception& e) {
            auto [ctx, res] = e.get_transaction_result();
            return cb(ctx, res);
        }
    }).detach();
}

void
transactions::run(async_logic&& code, txn_complete_callback&& cb)
{
    couchbase::transactions::transaction_options config;
    return run(config, std::move(code), std::move(cb));
}

void
transactions::single_query(const std::string& statement,
                           const couchbase::query_options::built& options,
                           std::optional<std::string> query_context,
                           txn_single_query_callback&& cb)
{
    couchbase::transactions::transaction_options config;
    if (options.timeout) {
        config.timeout(options.timeout.value());
    }
    if (options.single_query_transaction_options) {
        auto single_query_options = options.single_query_transaction_options.value();
        if (single_query_options.durability_level) {
            config.durability_level(single_query_options.durability_level.value());
        }
        config.test_factories(single_query_options.attempt_context_hooks, single_query_options.cleanup_hooks);
    }
    // Convert built query_options transaction_query_options
    auto transaction_query_opts = couchbase::transactions::transaction_query_options()
      .encoded_raw_options(options.raw)
      .encoded_positional_parameters(options.positional_parameters)
      .encoded_named_parameters(options.named_parameters)
      .ad_hoc(options.adhoc)
      .profile(options.profile)
      .readonly(options.readonly)
      .metrics(options.metrics)
      .single_query();
    if (options.scan_consistency) {
        transaction_query_opts.scan_consistency(options.scan_consistency.value());
    }
    if (options.client_context_id) {
        transaction_query_opts.client_context_id(options.client_context_id.value());
    }
    if (options.scan_wait) {
        transaction_query_opts.scan_wait(options.scan_wait.value());
    }
    if (options.scan_cap) {
        transaction_query_opts.scan_cap(options.scan_cap.value());
    }
    if (options.pipeline_batch) {
        transaction_query_opts.pipeline_batch(options.pipeline_batch.value());
    }
    if (options.pipeline_cap) {
        transaction_query_opts.pipeline_cap(options.pipeline_cap.value());
    }
    if (options.max_parallelism) {
        transaction_query_opts.max_parallelism(options.max_parallelism.value());
    }

    std::thread([this, config, statement, transaction_query_opts, query_context, cb = std::move(cb)]() {
        try {
            auto barrier = std::make_shared<std::promise<core::operations::query_response>>();
            auto f = barrier->get_future();
            auto result = wrap_run(*this, config, max_attempts_, true, [statement, transaction_query_opts, query_context, barrier](async_attempt_context& ctx) {
                ctx.query(statement, transaction_query_opts, query_context, [barrier](const std::exception_ptr& exc, const std::optional<core::operations::query_response>& resp){
                    if (exc) {
                        CB_LOG_DEBUG("Received exception pointer in query callback");
                        barrier->set_exception(exc);
                        return;
                    }
                    CB_LOG_DEBUG("Received transaction query response, setting it to the shared ptr");
                    CB_LOG_DEBUG("Query response has value {}", resp.value().rows[0]);
                    barrier->set_value(resp.value());
                });
            });
            CB_LOG_DEBUG("TXN Result received");
            sleep(2);
            CB_LOG_DEBUG("Trying to get query response");
            auto query_resp = f.get();
            CB_LOG_DEBUG("Query response future returned");
            return cb({}, query_resp);
        } catch (const transaction_exception& txn_exc) {
            return cb(txn_exc, {});
        }
    }).detach();
}

void
transactions::close()
{
    CB_TXN_LOG_DEBUG("closing transactions");
    cleanup_->close();
    CB_TXN_LOG_DEBUG("transactions closed");
}
} // namespace couchbase::core::transactions
