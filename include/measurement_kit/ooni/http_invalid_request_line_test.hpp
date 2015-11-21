// Part of measurement-kit <https://measurement-kit.github.io/>.
// Measurement-kit is free software. See AUTHORS and LICENSE for more
// information on the copying conditions.

#ifndef MEASUREMENT_KIT_OONI_HTTP_INVALID_REQUEST_LINE_TEST_HPP
#define MEASUREMENT_KIT_OONI_HTTP_INVALID_REQUEST_LINE_TEST_HPP

#include <measurement_kit/common/net_test.hpp>
#include <measurement_kit/common/settings.hpp>
#include <measurement_kit/common/var.hpp>
#include <string>

namespace measurement_kit {
namespace ooni {

/// Parameters of http-invalid-request-line test
class HttpInvalidRequestLineTest {
  public:
    /// Default constructor
    HttpInvalidRequestLineTest() {}

    /// Set backend used to perform the test
    HttpInvalidRequestLineTest &set_backend(std::string backend) {
        settings["backend"] = backend;
        return *this;
    }

    /// Set verbose
    HttpInvalidRequestLineTest &set_verbose() {
        is_verbose = true;
        return *this;
    }

    /// Set log-message handler
    HttpInvalidRequestLineTest &on_log(std::function<void(const char *)> func) {
        log_handler = func;
        return *this;
    }

    /// Create instance of the test
    common::Var<common::NetTest> create_test();

    common::Settings settings;
    bool is_verbose = false;
    std::function<void(const char *)> log_handler;
};

} // namespace ooni
} // namespace measurement_kit
#endif
