// Part of Measurement Kit <https://measurement-kit.github.io/>.
// Measurement Kit is free software under the BSD license. See AUTHORS
// and LICENSE for more information on the copying conditions.
#ifndef SRC_LIBMEASUREMENT_KIT_OONI_COLLECTOR_CLIENT_IMPL_HPP
#define SRC_LIBMEASUREMENT_KIT_OONI_COLLECTOR_CLIENT_IMPL_HPP

// This file implements the OONI collector client protocol
// See <https://github.com/TheTorProject/ooni-spec/blob/master/oonib.md>

#include <measurement_kit/common/nlohmann/json.hpp>

#include "src/libmeasurement_kit/common/mock.hpp"
#include "src/libmeasurement_kit/http/http.hpp"
#include "src/libmeasurement_kit/ooni/collector_client.hpp"
#include "src/libmeasurement_kit/ooni/error.hpp"
#include "src/libmeasurement_kit/report/error.hpp"

#include <fstream>

namespace mk {
namespace ooni {
namespace collector {

using namespace mk::http;
using namespace mk::net;

/*
 _          _
| |__   ___| |_ __   ___ _ __ ___
| '_ \ / _ \ | '_ \ / _ \ '__/ __|
| | | |  __/ | |_) |  __/ |  \__ \
|_| |_|\___|_| .__/ \___|_|  |___/
             |_|
*/

void post(SharedPtr<Transport> transport, std::string url_extra, std::string body,
          Callback<Error, nlohmann::json> callback, Settings conf = {},
          SharedPtr<Reactor> = Reactor::global(), SharedPtr<Logger> = Logger::global());

template <MK_MOCK_AS(http::request_sendrecv, http_request_sendrecv)>
void post_impl(SharedPtr<Transport> transport, std::string append_to_url,
               std::string body, Callback<Error, nlohmann::json> callback,
               Settings settings, SharedPtr<Reactor> reactor, SharedPtr<Logger> logger) {
    std::string url = "";
    Headers headers;
    if (settings.find("collector_base_url") == settings.end()) {
        callback(MissingCollectorBaseUrlError(), nullptr);
        return;
    }
    if (settings.find("collector_front_domain") != settings.end()) {
        mk::http::Url base_url = mk::http::parse_url(settings["collector_base_url"]);
        url = "https://";
        url += settings["collector_front_domain"];
        url += base_url.path; // XXX should we do more?
        headers["Host"] = base_url.address; // XXX this is confusing that it's called address
    } else {
        url = settings["collector_base_url"];
    }
    url += append_to_url;
    settings["http/url"] = url;
    settings["http/method"] = "POST";
    if (body != "") {
        headers["Content-Type"] = "application/json";
    }
    http_request_sendrecv(transport, settings, headers, body,
                          [=](Error err, SharedPtr<Response> response) {
                              if (err) {
                                  callback(err, nullptr);
                                  return;
                              }
                              if (response->status_code / 100 != 2) {
                                  callback(HttpRequestFailedError(), nullptr);
                                  return;
                              }
                              // If response is empty, don't parse it
                              if (response->body == "") {
                                  callback(NoError(), nullptr);
                                  return;
                              }
                              nlohmann::json reply;
                              try {
                                  reply = nlohmann::json::parse(response->body);
                              } catch (const std::exception &) {
                                  callback(JsonProcessingError(), nullptr);
                                  return;
                              }
                              callback(NoError(), reply);
                          },
                          reactor, logger);
}

Error valid_entry(nlohmann::json entry);

/*
             _
  __ _ _ __ (_)
 / _` | '_ \| |
| (_| | |_) | |
 \__,_| .__/|_|
      |_|
*/

template <MK_MOCK_AS(http::request_connect, http_request_connect)>
void connect_impl(Settings settings, Callback<Error, SharedPtr<Transport>> callback,
                  SharedPtr<Reactor> reactor, SharedPtr<Logger> logger) {
    std::string url;
    if (settings.find("collector_base_url") == settings.end()) {
        callback(MissingCollectorBaseUrlError(), nullptr);
        return;
    }
    if (settings.find("collector_front_domain") != settings.end()) {
        mk::http::Url base_url = mk::http::parse_url(settings["collector_base_url"]);
        url = "https://";
        url += settings["collector_front_domain"];
        url += base_url.path;
    } else {
        url = settings["collector_base_url"];
    }
    settings["http/url"] = url;
    http_request_connect(settings, callback, reactor, logger);
}

template <MK_MOCK_AS(collector::post, collector_post)>
void create_report_impl(SharedPtr<Transport> transport, nlohmann::json entry,
                        Callback<Error, std::string> callback,
                        Settings settings, SharedPtr<Reactor> reactor,
                        SharedPtr<Logger> logger) {

    nlohmann::json request;
    Error err = valid_entry(entry);
    if (err != NoError()) {
        callback(err, "");
        return;
    }
    request["software_name"] = entry["software_name"];
    request["software_version"] = entry["software_version"];
    request["probe_asn"] = entry["probe_asn"];
    request["probe_cc"] = entry["probe_cc"];
    request["test_name"] = entry["test_name"];
    request["test_version"] = entry["test_version"];
    request["test_start_time"] = entry["test_start_time"];
    if (entry["input_hashes"] != nullptr) {
        request["input_hashes"] = entry["input_hashes"];
    } else {
        request["input_hashes"] = nlohmann::json::array();
    }
    request["data_format_version"] = entry["data_format_version"];
    request["format"] = "json";
    std::string body = request.dump();

    collector_post(transport, "/report", body,
                   [=](Error err, nlohmann::json reply) {
                       if (err) {
                           callback(err, "");
                           return;
                       }
                       std::string report_id;
                       try {
                           report_id = reply.at("report_id");
                       } catch (const std::exception &) {
                           callback(JsonProcessingError(), "");
                           return;
                       }
                       // TODO(bassosimone): the code for setting up the
                       // report needs improvements, still it's nice to see
                       // how it's easy to pass it to our controller :^).
                       logger->emit_event_ex("status.report_create",
                           nlohmann::json::object({
                               {"report_id", report_id}
                           }));
                       callback(NoError(), report_id);
                   },
                   settings, reactor, logger);
}

template <MK_MOCK_AS(collector::connect, collector_connect),
          MK_MOCK_AS(collector::create_report, collector_create_report)>
void connect_and_create_report_impl(nlohmann::json entry,
                                    Callback<Error, std::string> callback,
                                    Settings settings, SharedPtr<Reactor> reactor,
                                    SharedPtr<Logger> logger) {
    collector_connect(settings, [=](Error error, SharedPtr<Transport> txp) {
        if (error) {
            callback(error, "");
            return;
        }
        collector_create_report(txp, entry, [=](Error error, std::string rid) {
            txp->close([=]() {
                callback(error, rid);
            });
        }, settings, reactor, logger);
    }, reactor, logger);
}

template <MK_MOCK_AS(collector::post, collector_post)>
void update_report_impl(SharedPtr<Transport> transport, std::string report_id,
                        nlohmann::json entry, Callback<Error> callback,
                        Settings settings, SharedPtr<Reactor> reactor,
                        SharedPtr<Logger> logger) {

    /*
     * If needed, overwrite the `report_id` field with what was
     * passed us by the server, which should be authoritative.
     *
     * Note that `entry` is passed by copy so changing it has no
     * effect outside of this function.
     *
     * This action must be performed in addition to `valid_entry()`
     * because that function does not check for report_id.
     *
     * We warn if `report_id` is not present because higher layers
     * should already have set the report-id's value.
     */
    if (entry["report_id"] == nullptr) {
        logger->warn("collector: forcing report_id which was not set");
        entry["report_id"] = report_id;
    }

    Error err = valid_entry(entry);
    if (err != NoError()) {
        callback(err);
        return;
    }
    nlohmann::json request{{"format", "json"}};
    request["content"] = entry;
    std::string body = request.dump();
    collector_post(transport, "/report/" + report_id, body,
                   [=](Error err, nlohmann::json) {
                       callback(err);
                   },
                   settings, reactor, logger);
}

template <MK_MOCK_AS(collector::connect, collector_connect),
          MK_MOCK_AS(collector::update_report, collector_update_report)>
void connect_and_update_report_impl(std::string report_id, nlohmann::json entry,
                                    Callback<Error> callback,
                                    Settings settings, SharedPtr<Reactor> reactor,
                                    SharedPtr<Logger> logger) {
    collector_connect(settings, [=](Error error, SharedPtr<Transport> txp) {
        if (error) {
            callback(error);
            return;
        }
        collector_update_report(txp, report_id, entry, [=](Error error) {
            txp->close([=]() {
                callback(error);
            });
        }, settings, reactor, logger);
    }, reactor, logger);
}

template <MK_MOCK_AS(collector::post, collector_post)>
void close_report_impl(SharedPtr<Transport> transport, std::string report_id,
                       Callback<Error> callback, Settings settings,
                       SharedPtr<Reactor> reactor, SharedPtr<Logger> logger) {
    collector_post(transport, "/report/" + report_id + "/close", "",
                   [=](Error err, nlohmann::json) {
                       callback(err);
                   },
                   settings, reactor, logger);
}

template <MK_MOCK_AS(collector::connect, collector_connect),
          MK_MOCK_AS(collector::close_report, collector_close_report)>
void connect_and_close_report_impl(std::string report_id,
                                   Callback<Error> callback,
                                   Settings settings, SharedPtr<Reactor> reactor,
                                   SharedPtr<Logger> logger) {
    collector_connect(settings, [=](Error error, SharedPtr<Transport> txp) {
        if (error) {
            callback(error);
            return;
        }
        collector_close_report(txp, report_id, [=](Error error) {
            txp->close([=]() {
                callback(error);
            });
        }, settings, reactor, logger);
    }, reactor, logger);
}

ErrorOr<nlohmann::json> get_next_entry(SharedPtr<std::istream> file, SharedPtr<Logger> logger);

template <MK_MOCK_AS(collector::update_report, collector_update_report),
          MK_MOCK_AS(collector::get_next_entry, collector_get_next_entry)>
void update_and_fetch_next_impl(SharedPtr<std::istream> file, SharedPtr<Transport> txp,
                                std::string report_id, int line, nlohmann::json entry,
                                Callback<Error> callback, Settings settings,
                                SharedPtr<Reactor> reactor, SharedPtr<Logger> logger) {
    logger->info("adding entry report #%d...", line);
    collector_update_report(
        txp, report_id, entry,
        [=](Error err) {
            logger->info("adding entry report #%d... %d", line, err.code);
            if (err) {
                txp->close([=]() { callback(err); });
                return;
            }
            // After #644 bug and fix, I prefer to always break explicit
            // recursion by using the call_soon() pattern
            logger->debug("scheduling read of next entry...");
            reactor->call_soon([=]() {
                logger->debug("reading next entry");
                ErrorOr<nlohmann::json> entry = collector_get_next_entry(file, logger);
                if (!entry) {
                    if (entry.as_error() != FileEofError()) {
                        txp->close([=]() { callback(entry.as_error()); });
                        return;
                    }
                    close_report(
                        txp, report_id,
                        [=](Error err) {
                            txp->close([=]() { callback(err); });
                        },
                        settings, reactor, logger);
                    return;
                }
                update_and_fetch_next_impl<collector_update_report,
                                           collector_get_next_entry>(
                    file, txp, report_id, line + 1, *entry, callback, settings,
                    reactor, logger);
            });
        },
        settings, reactor, logger);
}

template <MK_MOCK_AS(collector::get_next_entry, collector_get_next_entry),
          MK_MOCK_AS(collector::connect, collector_connect),
          MK_MOCK_AS(collector::create_report, collector_create_report)>
void submit_report_impl(std::string filepath, std::string collector_base_url,
                        std::string collector_front_domain,
                        Callback<Error> callback, Settings settings,
                        SharedPtr<Reactor> reactor, SharedPtr<Logger> logger) {

    SharedPtr<std::istream> file(new std::ifstream(filepath));
    if (!file->good()) {
        callback(CannotOpenReportError());
        return;
    }
    ErrorOr<nlohmann::json> entry = collector_get_next_entry(file, logger);
    if (!entry) {
        callback(entry.as_error());
        return;
    }

    settings["collector_base_url"] = collector_base_url;
    if (collector_front_domain != "") {
        settings["collector_front_domain"] = collector_front_domain;
    }
    logger->info("connecting to collector %s...", collector_base_url.c_str());
    collector_connect(
        settings,
        [=](Error err, SharedPtr<Transport> txp) mutable {
            logger->info("connecting to collector %s... %d",
                         collector_base_url.c_str(), err.code);
            if (err) {
                callback(err);
                return;
            }
            logger->info("creating report...");
            collector_create_report(
                txp, *entry,
                [=](Error err, std::string report_id) mutable {
                    logger->info("creating report... %d", err.code);
                    if (err) {
                        txp->close([=]() { callback(err); });
                        return;
                    }
                    update_and_fetch_next_impl(file, txp, report_id, 1, *entry,
                                               callback, settings, reactor,
                                               logger);
                },
                settings, reactor, logger);
        },
        reactor, logger);
}

} // namespace collector
} // namespace mk
} // namespace ooni
#endif
