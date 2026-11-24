#include "neurogenic/midi/input.hpp"
#include "neurogenic/midi/mapping.hpp"
#include "neurogenic/midi/output.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        neurogenic::MidiMapper mapper;
        neurogenic::MidiInput input(mapper);
        neurogenic::MidiOutput output;
        require(!input.backend().empty(), "MIDI input backend has no status");
        require(!output.backend().empty(), "MIDI output backend has no status");

        std::string error;
        require(!input.connect_exact(1, "definitely-missing-neurogenic-input",
                                     error),
                "missing exact MIDI input was silently substituted");
        require(!error.empty(), "missing MIDI input has no diagnostic");
        error.clear();
        require(!output.connect_exact(1, "definitely-missing-neurogenic-output",
                                      error),
                "missing exact MIDI output was silently substituted");
        require(!error.empty(), "missing MIDI output has no diagnostic");
        std::cout << "neurogenic.midi-runtime-tests.v1 passed input=\""
                  << input.backend() << "\" output=\"" << output.backend()
                  << "\"\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "midi runtime tests failed: " << exception.what() << '\n';
        return 1;
    }
}
