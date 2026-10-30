// What the player sees of a battle over time: the battle's requests played out frame by frame.
//
// The battle asks for things to be shown (battle/battle_requests.h); the presentation plays them
// and tells the battle when it may take its next step. Each frame the host ticks the battle, hands
// its requests to the presentation, ticks the presentation, and gives the battle what the
// presentation reports:
//
//     BattlePresentationState shown;
//     const BattlePresentationContext context{.text = cartridge.text, .metrics = cartridge.dialogFont.metrics};
//     while (!battle.result()) {
//         BattleInput input = battleInput(shown);
//         input.playerActions = menu.finishedActions();
//         battle.tick(input);
//         tickBattlePresentation(shown, battle.state(), battle.takeRequests(), context, confirmPressed);
//     }
//
// The state holds what is on screen and nothing about how it is drawn: the message in the window
// at the top and how much of it has appeared, the damage numerals and how far each has bounced,
// the monsters being faded out, the monsters standing, the list of monster names, the pose each
// party member waits in, and each party member's pose counter. The rendering components read it.
//
// A battle script plays one command at a time, in order. A command that shows nothing takes no
// frames and the next one starts in the same frame:
//
//   - a name in the narrow window, or a battle message in the wide one, appears whole and stays up
//     for the message speed's time (a monster's special attack name for 32 frames);
//   - monster dialog appears one letter a frame, then stays up the same time;
//   - a line break scrolls the line shown up and out of the window, a pixel a frame for 16 frames;
//   - a wait in the text pauses it; a key wait holds the text until the player confirms, and the
//     window closes as soon as they do;
//   - damage numerals bounce for 64 frames each; one numeral at a time lets the script carry on
//     after two frames, and every target's numeral at once holds the script until all are gone;
//   - a ready stance, a step back or a pose reset changes a party member's waiting pose at once;
//   - attack animations, battle events and monster animations show nothing here.
//
// Between scripts, monsters that have fallen fade out together: one frame as they were, then 32
// frames in the death palette, darker each frame, added over what is behind them.
//
// The behavior, and the line in the original each rule comes from, is specified in
// docs/contracts/battle_presentation.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "battle/battle_requests.h"
#include "battle/battle_script.h"
#include "battle/battle_sim.h"
#include "battle/battle_state.h"
#include "battle/battler_set.h"
#include "data/text_corpus.h"
#include "ostinato/game_version.h"
#include "ostinato/monster_id.h"
#include "text/font_metrics.h"

namespace ostinato::battle {

// --- the player's message speed ---------------------------------------------------------------

// The message speed a new game starts with, and the slowest the config menu offers. The setting
// picks how long a message stays up (messageHoldFrames, data/battle_gfx_tables.h).
inline constexpr std::uint8_t kNewGameMessageSpeed = 2;
inline constexpr std::uint8_t kSlowestMessageSpeed = 5;

// How long a monster's special attack name stays up, whatever the message speed.
inline constexpr int kSpecialAttackNameFrames = 32;

// --- what the presentation reads --------------------------------------------------------------

// What the presentation needs beyond the battle: the game text, the dialog font's widths (to
// center a name), and the player's message speed.
struct BattlePresentationContext {
    const TextCorpus& text;
    const ostinato::text::FontMetrics& metrics;
    std::uint8_t messageSpeed = kNewGameMessageSpeed;  // 0 to kSlowestMessageSpeed
};

// --- the window at the top of the screen ------------------------------------------------------

// The two windows at the top of the screen: the narrow one for a name, the wide one for a message.
enum class MessageWindow : std::uint8_t {
    Narrow,
    Wide,
};

// One letter of the text shown, in the dialog font. `x` is where the pen put it, from the start of
// the line; `rise` is how far a line break has scrolled it up.
struct ShownGlyph {
    std::uint16_t glyph = 0;
    int x = 0;
    int rise = 0;
};

// The text in the window at the top of the screen, as it stands this frame.
struct ShownMessage {
    MessageWindow window = MessageWindow::Wide;
    // How far right the text is moved so it sits centered in the narrow window; 0 in the wide one.
    int centering = 0;
    std::vector<ShownGlyph> glyphs;
};

// One step of a message's text once its names and numbers are filled in.
struct MessageGlyph {
    std::uint16_t glyph = 0;
};
struct MessageLineBreak {};
struct MessagePause {
    int frames = 0;
};
struct MessageKeyWait {};
using MessageStep = std::variant<MessageGlyph, MessageLineBreak, MessagePause, MessageKeyWait>;

// How long a line break takes: the line shown scrolls up a pixel a frame until it is gone.
inline constexpr int kLineScrollFrames = 16;

// A message being played: its steps, how far it has got, and how it ends.
struct MessagePlayback {
    MessageWindow window = MessageWindow::Wide;
    std::vector<MessageStep> steps;
    std::size_t next = 0;           // the next step to play
    bool typesOut = false;          // one letter a frame, rather than the whole text at once
    int holdFrames = 0;             // how long it stays up once its text has all appeared
    int pauseLeft = 0;              // frames left of a pause in the text
    int scrollLeft = 0;             // frames left of a line break's scroll
    bool awaitingKey = false;       // held until the player confirms
    bool keyWaited = false;         // a key wait ended it: it closes without being held
    int holdLeft = 0;               // frames it has left to stay up
    bool textDone = false;          // every step has played
    int pen = 0;                    // where the next letter goes on the line
    ShownMessage shown;
};

// --- damage numerals --------------------------------------------------------------------------

// How many frames a numeral bounces before it goes, and how many numerals shown one at a time can
// be on the screen together (a fifth replaces the oldest).
inline constexpr int kDamageNumberLife = 64;
inline constexpr std::size_t kSingleNumeralThreads = 4;

// How long a numeral shown one at a time waits when one is already over the same battler.
inline constexpr int kSameTargetWaitFrames = 8;

// A damage numeral on the screen.
struct ShownNumeral {
    BattlerSlot target = BattlerSlot::character(0);
    std::uint16_t amount = 0;
    bool miss = false;
    bool heals = false;
    // How far left of the battler's point the numeral's first digit starts: half its width for a
    // number. "Miss" starts one cell further right than its offset says.
    int offset = 0;
    int frame = 0;  // how long it has been up: 0 to kDamageNumberLife - 1
};

// --- a monster's death ------------------------------------------------------------------------

// Fallen monsters fade out together over this many frames after one frame as they were.
inline constexpr int kDeathFadeFrames = 32;

struct DeathFade {
    BattlerSet dying;
    int frame = 0;  // 0: as they were; 1 to kDeathFadeFrames: in the death palette

    // How many 31sts have been taken off every channel of the death palette this frame, once it
    // is in it.
    constexpr int darkening() const { return frame - 1; }
    constexpr bool inDeathPalette() const { return frame >= 1; }
};

// --- the whole state --------------------------------------------------------------------------

// How many lines the list of monster names has.
inline constexpr std::size_t kMonsterNameLines = 4;
using MonsterNameList = std::array<std::optional<MonsterId>, kMonsterNameLines>;

struct BattlePresentationState {
    bool sceneOpen = false;    // the battle's scene is on screen
    bool sceneClosed = false;  // the battle is over and its scene has closed

    // The scripts waiting to play, oldest first, and how far the first has got.
    std::deque<std::vector<BattleScriptCommand>> scripts;
    std::size_t command = 0;   // the command of the first script playing now
    bool commandStarted = false;

    // The command playing now, where it takes frames.
    std::optional<MessagePlayback> message;
    int waitLeft = 0;          // frames left of a numeral command's wait
    bool waitingForNumerals = false;  // held until every numeral has gone
    std::deque<DamageNumber> numeralsToShow;  // numerals still to start, one at a time

    std::array<std::optional<ShownNumeral>, kSingleNumeralThreads> singleNumerals{};
    std::size_t nextThread = 0;
    std::array<std::optional<ShownNumeral>, kBattlerSlots> numeralsTogether{};

    std::optional<DeathFade> deathFade;
    BattlerSet monstersShown;  // the monsters drawn on the field
    MonsterNameList monsterNames{};

    // The pose each party member waits in before acting, set by its ready stance.
    std::array<std::optional<std::uint8_t>, kPartySlots> waitingPoses{};
    // Each party member's pose counter: a pose takes its next step every eight counts.
    std::array<std::uint8_t, kPartySlots> poseCounters{0, 2, 4, 6};

    // Whether battle time runs this frame, and whether nothing is playing.
    bool timeRunning = true;
    bool idle = true;
};

// --- running it -------------------------------------------------------------------------------

// Play one frame: take in `requests` (the battle's, in the order it made them), then advance what
// is playing. `confirmPressed` is the player confirming a message that waits for a key.
void tickBattlePresentation(BattlePresentationState& shown, const BattleState& battle,
                            std::span<const BattleRequest> requests, const BattlePresentationContext& context,
                            bool confirmPressed);

// What the presentation tells the battle for its next frame. The player's actions are the menu's
// to add.
BattleInput battleInput(const BattlePresentationState& shown);

// Whether the message shown is waiting for the player to confirm it.
bool awaitingKey(const BattlePresentationState& shown);

// --- what the presentation works out ----------------------------------------------------------

// The list of monster names: each monster standing and shown in the list gets its name group's
// line, in the order the monsters stand in their slots, and a name group takes one line however
// many share it. At most kMonsterNameLines lines.
MonsterNameList monsterNameList(const BattleState& battle);

// The pose a party member stands in: fallen, asleep, confused, chanting, poisoned or near death
// show their own; otherwise the pose it waits in before acting, or the idle pose.
std::uint8_t partyMemberPose(const Battler& member, std::optional<std::uint8_t> waitingPose);

// The pose step a party member's counter shows: 0 to 3.
constexpr int poseStep(std::uint8_t counter) { return (counter >> 3) & 3; }

// The text of a message as the presentation plays it: its letters in the dialog font with every
// name and number filled in, and its pauses, key waits and line breaks.
std::vector<MessageStep> battleMessageSteps(const BattleMessage& message, const BattleState& battle,
                                            const TextCorpus& text);
std::vector<MessageStep> monsterDialogSteps(const MonsterDialogShown& dialog, const BattleState& battle,
                                            const TextCorpus& text);

}  // namespace ostinato::battle
