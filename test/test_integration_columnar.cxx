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

#include "test_helper_integration.hxx"

#include "core/columnar/agent.hxx"
#include "core/free_form_http_request.hxx"
#include "core/row_streamer.hxx"

#include <future>
#include <string>

TEST_CASE("integration: columnar http component simple request", "[integration]")
{
  test::utils::integration_test_guard integration;

  auto agent = couchbase::core::columnar::agent(integration.io, { { integration.cluster } });

  tao::json::value body{ { "statement", "SELECT * FROM `beer-sample` LIMIT 100" } };

  auto req = couchbase::core::http_request{
    couchbase::core::service_type::analytics,     "POST", {}, "/analytics/service", {}, {},
    couchbase::core::utils::json::generate(body),
  };

  req.timeout = std::chrono::seconds(2);
  req.headers["content-type"] = "application/json";

  couchbase::core::http_response resp;
  {
    auto barrier = std::make_shared<
      std::promise<tl::expected<couchbase::core::http_response, std::error_code>>>();
    auto f = barrier->get_future();
    auto op = agent.free_form_http_request(std::move(req), [barrier](auto resp, auto ec) {
      if (ec) {
        barrier->set_value(tl::unexpected(ec));
        return;
      }
      barrier->set_value(std::move(resp));
    });
    REQUIRE(op.has_value());
    auto r = f.get();
    REQUIRE(r.has_value());
    resp = r.value();
  }

  auto code = resp.status_code();
  REQUIRE(code == 200);
  auto resp_body = resp.body();
  std::string buffered_body{};
  while (true) {
    auto barrier = std::make_shared<std::promise<std::pair<std::string, std::error_code>>>();
    auto f = barrier->get_future();
    resp_body.next([barrier](auto s, auto ec) {
      barrier->set_value({ std::move(s), ec });
    });
    auto [s, ec] = f.get();
    REQUIRE_SUCCESS(ec);
    if (s.empty()) {
      break;
    }
    buffered_body.append(s);
  }
  std::cout << buffered_body << "\n";
}

TEST_CASE("integration: columnar query component simple request", "[integration]")
{
  test::utils::integration_test_guard integration;

  couchbase::core::columnar::agent agent{ integration.io, { { integration.cluster } } };
  couchbase::core::columnar::query_options options{ "SELECT * FROM `beer-sample` LIMIT 5000" };
  options.timeout = std::chrono::seconds(20);
  couchbase::core::columnar::query_result result;
  {
    auto barrier = std::make_shared<
      std::promise<std::pair<couchbase::core::columnar::query_result, std::error_code>>>();
    auto f = barrier->get_future();
    auto resp = agent.execute_query(options, [barrier](auto res, auto ec) mutable {
      barrier->set_value({ std::move(res), ec });
    });
    auto [res, ec] = f.get();
    REQUIRE(resp.has_value());
    REQUIRE_SUCCESS(ec);
    REQUIRE_FALSE(res.metadata().has_value());
    result = std::move(res);
  }
  std::vector<std::string> buffered_rows{};
  while (true) {
    auto barrier = std::make_shared<std::promise<std::pair<std::string, std::error_code>>>();
    auto f = barrier->get_future();
    result.next_row([barrier](auto row, auto ec) mutable {
      barrier->set_value({ std::move(row), ec });
    });
    auto [row, ec] = f.get();
    if (ec) {
      CB_LOG_ERROR("Error! {}", ec.message());
      break;
    }

    if (row.empty()) {
      break;
    }
    REQUIRE_SUCCESS(ec);
    buffered_rows.emplace_back(std::move(row));
  }
  REQUIRE(result.metadata().has_value());
  REQUIRE(result.metadata()->warnings.empty());
  REQUIRE(result.metadata()->metrics.result_count == 5000);
  REQUIRE(buffered_rows.size() == 5000);
}