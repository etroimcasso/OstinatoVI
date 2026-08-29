// Tests for the first-start decision: every way the "you need a cartridge" flow can end.
//
// The three things that need a machine — the presence check, the dialog, the install — are handed
// in, so every path runs without a window, a cartridge, or a filesystem.
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "assets/first_start.h"

namespace {

using namespace ostinato;
namespace fs = std::filesystem;

using assets::DialogEnd;
using assets::DialogResult;
using assets::InstallResult;

// Everything the flow did, so a test can assert on what the player was told and how many times
// they were asked.
struct FlowLog {
    std::string reported;
    int dialogsShown = 0;
    int installsRun = 0;
    fs::path installedFrom;
};

// Drive the flow with a scripted sequence of dialog outcomes and a fixed install answer.
bool runFlow(FlowLog& log, std::vector<DialogResult> dialogs, InstallResult install,
             bool installedBefore, bool installedAfter) {
    bool installDone = false;
    return assets::ensureCartridgeInstalled(
        [&log](const std::string& line) { log.reported += line; },
        [&] { return installDone ? installedAfter : installedBefore; },
        [&] {
            const std::size_t index = static_cast<std::size_t>(log.dialogsShown);
            ++log.dialogsShown;
            return index < dialogs.size() ? dialogs[index]
                                          : DialogResult{DialogEnd::Cancelled, {}};
        },
        [&](const fs::path& rom) {
            ++log.installsRun;
            log.installedFrom = rom;
            installDone = true;
            return install;
        });
}

const InstallResult kInstalled{true, GameVersion::US_1_1, "Installed Final Fantasy III 1.1 (U)."};
const InstallResult kRefused{false, std::nullopt, "That file is not a cartridge this port reads."};

TEST(FirstStart, AnInstalledCartridgeIsNotAskedAbout) {
    FlowLog log;
    EXPECT_TRUE(runFlow(log, {}, kInstalled, /*installedBefore=*/true, /*installedAfter=*/true));
    EXPECT_EQ(log.dialogsShown, 0);
    EXPECT_EQ(log.installsRun, 0);
    EXPECT_TRUE(log.reported.empty());
}

TEST(FirstStart, AChosenCartridgeIsInstalledAndStartupContinues) {
    FlowLog log;
    EXPECT_TRUE(runFlow(log, {{DialogEnd::Chosen, "/tmp/ff6.sfc"}}, kInstalled, false, true));
    EXPECT_EQ(log.dialogsShown, 1);
    EXPECT_EQ(log.installsRun, 1);
    EXPECT_EQ(log.installedFrom, fs::path{"/tmp/ff6.sfc"});
    EXPECT_NE(log.reported.find("Installed"), std::string::npos);
}

TEST(FirstStart, ADismissedDialogIsAskedOnceMore) {
    FlowLog log;
    EXPECT_TRUE(runFlow(log,
                        {{DialogEnd::Cancelled, {}}, {DialogEnd::Chosen, "/tmp/ff6.sfc"}},
                        kInstalled, false, true));
    EXPECT_EQ(log.dialogsShown, 2);
    EXPECT_EQ(log.installsRun, 1);
    EXPECT_NE(log.reported.find("ask once more"), std::string::npos);
}

TEST(FirstStart, TwoDismissalsAreAnAnswer) {
    FlowLog log;
    EXPECT_FALSE(runFlow(log, {{DialogEnd::Cancelled, {}}, {DialogEnd::Cancelled, {}}},
                         kInstalled, false, true));
    EXPECT_EQ(log.dialogsShown, 2);
    EXPECT_EQ(log.installsRun, 0);
    EXPECT_NE(log.reported.find("cannot start"), std::string::npos);
}

TEST(FirstStart, ABrokenDialogIsNotOpenedTwice) {
    // A dialog that failed will fail again, so the player's second attempt is not spent on it.
    FlowLog log;
    EXPECT_FALSE(runFlow(log, {{DialogEnd::Failed, {}}}, kInstalled, false, true));
    EXPECT_EQ(log.dialogsShown, 1);
    EXPECT_EQ(log.installsRun, 0);
    EXPECT_NE(log.reported.find("could not be opened"), std::string::npos);
}

TEST(FirstStart, ARefusedCartridgeEndsTheRunWithTheReason) {
    FlowLog log;
    EXPECT_FALSE(runFlow(log, {{DialogEnd::Chosen, "/tmp/not-a-rom.sfc"}}, kRefused, false, true));
    EXPECT_EQ(log.installsRun, 1);
    EXPECT_NE(log.reported.find("not a cartridge"), std::string::npos);
}

TEST(FirstStart, AnInstallThatDidNotLandIsCaught) {
    // The install said it worked and the file is not there. Taking its word for it would fail
    // later, somewhere less obvious.
    FlowLog log;
    EXPECT_FALSE(runFlow(log, {{DialogEnd::Chosen, "/tmp/ff6.sfc"}}, kInstalled, false,
                         /*installedAfter=*/false));
    EXPECT_EQ(log.installsRun, 1);
    EXPECT_NE(log.reported.find("cannot be found"), std::string::npos);
}

}  // namespace
