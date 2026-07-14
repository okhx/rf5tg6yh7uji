#pragma once

namespace crash_log {
    // Call once at startup to install the unhandled exception filter
    // and std::terminate handler. Logs are written to Grape/logs/.
    void install();
}
