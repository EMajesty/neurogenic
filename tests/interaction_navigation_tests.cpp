#include "neurogenic/application/controller_protocol.hpp"
#include "neurogenic/audio/engine.hpp"
#include "neurogenic/timeline/edit.hpp"
#include "neurogenic/ui/slint_adapter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
bool await_browser(neurogenic::SlintFrontendAdapter &adapter) {
    for (int i = 0; i < 200; ++i) {
        (void)adapter.refresh();
        if (!adapter.state().browser.loading) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
}

int main() try {
    using namespace neurogenic;
    AudioEngine engine;
    Timeline initial;
    initial.tracks = {{41, "Track", false, {ticks_per_quarter * 4, {}}}};
    for (PatternId id : {91U, 92U}) {
        PatternDefinition pattern;
        pattern.id = id;
        pattern.name = id == 91 ? "Delete target" : "Other pattern";
        pattern.length = ticks_per_quarter * 4;
        pattern.tracks = {{41, {pattern.length, {}}}};
        initial.arrangement.patterns.push_back(std::move(pattern));
    }
    TimelineService timeline([&](const Timeline &value, std::string &error) {
        return engine.queue_timeline(value, error);
    }, initial);
    SampleManager samples(engine);
    ApplicationService application(engine, timeline, samples);
    SlintFrontendAdapter frontend(application);
    ControllerHost host(application, 256, NG_CONTROLLER_V2_DEVICE_ID);
    ReferenceV2FirmwareSimulator firmware(host);
    require(firmware.negotiate(), "V2 negotiation");

    require(firmware.button("page.2", true), "Sequencer navigation press");
    require(application.snapshot().interaction.destination == "sequencer",
            "controller press did not publish shared destination: " +
                application.snapshot().interaction.destination + "/" +
                host.session_snapshot().destination);
    require(firmware.button("page.2", false), "Sequencer navigation release");
    (void)frontend.refresh();
    require(frontend.state().shell.page_id == "sequencer",
            "physical destination did not reach production frontend: " +
                frontend.state().shell.page_id + "/" +
                application.snapshot().interaction.destination + "/" +
                host.session_snapshot().destination);
    require(application.execute({"pattern.select.v1",
                {{"pattern_id", std::int64_t{91}}}, "test", 1}).accepted,
            "pattern selection");
    (void)frontend.refresh();

    require(frontend.manage_pattern("delete"), "open pattern modal");
    const auto opened = application.snapshot().interaction;
    require(opened.modal.identity == "sequencer.pattern-delete" &&
                opened.modal.target_identity == "91" &&
                opened.focus_identity == "modal.cancel",
            "destructive modal did not capture target with safe focus");
    require(application.execute({"pattern.select.v1",
                {{"pattern_id", std::int64_t{92}}}, "test", 2}).accepted,
            "selection change while modal open");
    host.publish(application.snapshot());
    require(host.session_snapshot().interaction.modal_context ==
                "sequencer.pattern-delete",
            "shared modal did not reach controller gesture ownership");
    const auto dropped_before = host.snapshot().dropped_input;
    require(firmware.button("page.1", true), "modal page input frame");
    require(host.session_snapshot().destination == "sequencer" &&
                host.snapshot().dropped_input > dropped_before,
            "page navigation bypassed modal ownership");
    require(firmware.button("page.1", false), "modal page release");
    require(firmware.button("navigation.enter", true) &&
                firmware.button("navigation.enter", false),
            "modal safe-focus ENTER");
    (void)application.dispatch_pending();
    (void)frontend.refresh();
    require(application.snapshot().patterns.size() == 2 &&
                application.snapshot().interaction.modal.identity.empty(),
            "safe initial ENTER did not cancel without deletion");

    require(application.execute({"pattern.select.v1",
                {{"pattern_id", std::int64_t{91}}}, "test", 3}).accepted,
            "restore delete selection");
    (void)frontend.refresh();
    require(frontend.manage_pattern("delete"), "reopen pattern modal");
    host.publish(application.snapshot());
    require(firmware.button("navigation.down", true) &&
                firmware.button("navigation.down", false),
            "focus modal confirmation");
    (void)application.dispatch_pending();
    require(firmware.button("navigation.enter", true) &&
                firmware.button("navigation.enter", false),
            "physical modal confirmation");
    (void)application.dispatch_pending();
    (void)frontend.refresh();
    const auto deleted = application.snapshot();
    require(deleted.patterns.size() == 1 && deleted.patterns.front().id == 92,
            "confirmation redirected from captured pattern: count=" +
                std::to_string(deleted.patterns.size()) +
                (deleted.patterns.empty() ? std::string{} :
                    " first=" + std::to_string(deleted.patterns.front().id)) +
                " error=" + frontend.last_error());
    require(application.execute({"edit.undo.v1", {}, "test", 4}).accepted,
            "undo confirmed deletion");
    host.publish(application.snapshot());

    const auto root = std::filesystem::temp_directory_path() /
                      "neurogenic-097a-browser";
    std::filesystem::create_directories(root);
    std::ofstream(root / "a.wav") << "RIFF";
    std::ofstream(root / "b.wav") << "RIFF";
    require(frontend.navigate_browser(root.string()) && await_browser(frontend),
            "populate browser");
    require(firmware.button("page.8", true) && firmware.button("page.8", false),
            "Browser navigation");
    (void)application.dispatch_pending();
    (void)frontend.refresh();
    const auto before = frontend.state().browser.selected;
    require(firmware.encoder("data", 1), "browser DATA input");
    (void)application.dispatch_pending();
    (void)frontend.refresh();
    require(frontend.state().browser.selected >= before &&
                frontend.state().shell.data_role == "Browser entry",
            "focus-aware DATA did not navigate Browser: " +
                std::to_string(before) + "->" +
                std::to_string(frontend.state().browser.selected) + "/" +
                frontend.state().shell.data_role + "/" +
                application.snapshot().interaction.destination);

    require(firmware.button("edit.shift", true) &&
                firmware.button("navigation.enter", true) &&
                firmware.button("navigation.enter", false) &&
                firmware.button("edit.shift", false), "physical Save request");
    (void)application.dispatch_pending();
    (void)frontend.refresh();
    require(frontend.state().text_entry.visible &&
                frontend.state().text_entry.operation == "project-save-as",
            "unnamed physical Save did not enter shared Save As workflow");
    require(frontend.text_entry_action("cancel") &&
                !frontend.state().text_entry.visible,
            "Save As cancellation did not restore presentation");
    require(firmware.button("edit.shift", true) &&
                firmware.button("navigation.enter", true) &&
                firmware.button("navigation.enter", false) &&
                firmware.button("edit.shift", false),
            "second physical Save request");
    (void)application.dispatch_pending();
    (void)frontend.refresh();
    require(frontend.text_entry_insert("hardware-first.ngp") &&
                frontend.text_entry_action("confirm"),
            "complete shared Save As workflow");
    require(std::filesystem::exists(root / "hardware-first.ngp"),
            "physical Save workflow did not persist the project");

    require(firmware.button("navigation.down", true), "held navigation");
    host.disconnected("test disconnect");
    require(firmware.negotiate(), "reconnect");
    require(host.session_snapshot().interaction.held_controls.empty(),
            "disconnect retained modal/navigation ownership");
    std::filesystem::remove_all(root);
    frontend.shutdown();
    samples.shutdown();
    std::cout << "neurogenic.interaction-navigation-tests.v1 passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
