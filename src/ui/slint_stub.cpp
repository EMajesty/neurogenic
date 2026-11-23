#include "neurogenic/ui/slint_adapter.hpp"

namespace neurogenic {
bool slint_available() noexcept { return false; }
int run_slint_frontend(ApplicationService &, const SlintOptions &,
                       std::string &error) {
    error = "Slint frontend was not enabled in this build";
    return 1;
}
} // namespace neurogenic
