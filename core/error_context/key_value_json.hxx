#pragma once

#include <couchbase/fmt/key_value_status_code.hxx>
#include <couchbase/fmt/retry_reason.hxx>
#include <couchbase/key_value_error_context.hxx>

#include <fmt/format.h>
#include <tao/json/forward.hpp>

namespace tao::json
{
template<>
struct traits<couchbase::key_value_error_context> {
    template<template<typename...> class Traits>
    static void assign(tao::json::basic_value<Traits>& v, const couchbase::key_value_error_context& ctx)
    {
        if (ctx.last_dispatched_to()) {
            v["last_dispatched_to"] = ctx.last_dispatched_to().value();
        }
        if (ctx.last_dispatched_from()) {
            v["last_dispatched_from"] = ctx.last_dispatched_from().value();
        }
        if (ctx.retry_attempts()) {
            v["retry_attempts"] = ctx.retry_attempts();
        }
        if (!ctx.retry_reasons().empty()) {
            std::vector<tao::json::basic_value<Traits>> reasons{};
            for (couchbase::retry_reason r : ctx.retry_reasons()) {
                reasons.emplace_back(fmt::format("{}", r));
            }
            v["retry_reasons"] = reasons;
        }
        if (!ctx.operation_id().empty()) {
            v["operation_id"] = ctx.operation_id();
        }
        if (!ctx.id().empty()) {
            v["id"] = ctx.id();
        }
        if (ctx.opaque() > 0) {
            v["opaque"] = ctx.opaque();
        }
        if (!ctx.bucket().empty()) {
            v["bucket"] = ctx.bucket();
        }
        if (!ctx.scope().empty()) {
            v["scope"] = ctx.scope();
        }
        if (!ctx.collection().empty()) {
            v["collection"] = ctx.collection();
        }
        if (ctx.status_code()) {
            v["status"] = fmt::format("{}", ctx.status_code().value());
        }
        if (ctx.error_map_info()) {
            tao::json::basic_value<Traits> error_map_info;
            error_map_info["name"] = ctx.error_map_info()->name();
            error_map_info["desc"] = ctx.error_map_info()->description();
            v["error_map_info"] = error_map_info;
        }
        if (ctx.extended_error_info()) {
            tao::json::basic_value<Traits> extended_error_info;
            extended_error_info["ref"] = ctx.extended_error_info()->reference();
            extended_error_info["context"] = ctx.extended_error_info()->context();
            v["extended_error_info"] = extended_error_info;
        }
    }
};
} // namespace tao::json
