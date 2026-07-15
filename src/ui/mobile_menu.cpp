#include "mobile_menu.hpp"

#include <Geode/UI.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fmt/format.h>

#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "bot/bot.hpp"
#include "bot/updater.hpp"
#include "checkpoint/fix.hpp"
#include "replay/system.hpp"
#include "settings/settings.hpp"
#include "trajectory/trajectory.hpp"

using namespace geode::prelude;

namespace {
constexpr float kTabWidth = 100.f;

ButtonSprite* makeButtonSprite(std::string const& text, float width) {
    return ButtonSprite::create(text.c_str(), static_cast<int>(width), true,
                                "bigFont.fnt", "GJ_button_01.png", 24.f,
                                .55f);
}
}  // namespace

bool MobileMenu::init() {
    const auto winSize = CCDirector::get()->getWinSize();
    const float width = std::min(470.f, winSize.width - 24.f);
    const float height = std::min(290.f, winSize.height - 18.f);
    if (!Popup::init(width, height, "GJ_square01.png")) return false;

    this->setTitle("Grape Mobile", "goldFont.fnt", .75f, 18.f);

    static constexpr const char* names[] = {"Record", "Assist", "Settings"};
    for (int i = 0; i < 3; ++i) {
        auto sprite = makeButtonSprite(names[i], kTabWidth);
        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(MobileMenu::onTab));
        button->setTag(i);
        button->setPosition({m_size.width / 2.f + (i - 1) * 108.f,
                             m_size.height - 48.f});
        m_buttonMenu->addChild(button);
    }

    m_pageNode = CCNode::create();
    m_pageNode->setAnchorPoint({0.f, 0.f});
    m_pageNode->setPosition({14.f, 28.f});
    m_pageNode->setContentSize({m_size.width - 28.f, m_size.height - 86.f});
    m_mainLayer->addChild(m_pageNode);

    rebuildPage();
    return true;
}

float MobileMenu::columnX(int column) const {
    return m_pageNode->getContentSize().width * (column == 0 ? .25f : .75f);
}

float MobileMenu::rowY(int row) const {
    return m_pageNode->getContentSize().height - 15.f - row * 25.f;
}

void MobileMenu::addLabel(std::string const& text, float x, float y,
                          float scale, ccColor3B color) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);
    label->setColor(color);
    label->setPosition({x, y});
    label->limitLabelWidth(185.f, scale, .15f);
    m_pageNode->addChild(label);
}

ButtonSprite* MobileMenu::addButton(std::string const& text, float x, float y,
                                    std::function<void()> callback,
                                    float width) {
    auto sprite = makeButtonSprite(text, width);
    auto button = geode::cocos::CCMenuItemExt::createSpriteExtra(
        sprite, [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
            callback();
        });
    button->setPosition({x, y});
    m_pageMenu->addChild(button);
    return sprite;
}

void MobileMenu::addToggle(std::string const& text, int row, int column,
                           bool value, std::function<void(bool)> callback) {
    const float x = columnX(column);
    const float y = rowY(row);
    addLabel(text, x - 22.f, y, .33f);

    auto toggle = geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(
        .42f, [callback = std::move(callback)](CCMenuItemToggler* item) {
            callback(!item->isToggled());
        });
    toggle->toggle(value);
    toggle->setPosition({x + 72.f, y});
    m_pageMenu->addChild(toggle);
}

void MobileMenu::addAdjuster(
    std::string const& text, int row, int column,
    std::function<double()> getter, std::function<void(double)> setter,
    double step, double minimum, double maximum,
    std::function<std::string(double)> formatter) {
    const float x = columnX(column);
    const float y = rowY(row);
    addLabel(text, x - 57.f, y, .32f);

    auto valueLabel = CCLabelBMFont::create(formatter(getter()).c_str(),
                                            "bigFont.fnt");
    valueLabel->setScale(.31f);
    valueLabel->setPosition({x + 38.f, y});
    valueLabel->limitLabelWidth(52.f, .31f, .15f);
    m_pageNode->addChild(valueLabel);

    const auto change = [getter, setter, formatter, valueLabel, minimum,
                         maximum](double delta) {
        const double value = std::clamp(getter() + delta, minimum, maximum);
        setter(value);
        valueLabel->setString(formatter(value).c_str());
    };
    addButton("-", x + 2.f, y, [change, step] { change(-step); }, 25.f);
    addButton("+", x + 76.f, y, [change, step] { change(step); }, 25.f);
}

void MobileMenu::rebuildPage() {
    m_pageNode->removeAllChildren();
    m_pageMenu = CCMenu::create();
    m_pageMenu->setPosition({0.f, 0.f});
    m_pageMenu->setContentSize(m_pageNode->getContentSize());
    m_pageNode->addChild(m_pageMenu, 2);

    switch (m_page) {
        case Page::Record: buildRecordPage(); break;
        case Page::Assist: buildAssistPage(); break;
        case Page::Settings: buildSettingsPage(); break;
    }
}

void MobileMenu::buildRecordPage() {
    auto* bot = Bot::get();
    auto& updater = bot->updater();
    auto& replay = bot->replaySystem();

    addLabel(fmt::format("Frame {}  |  {} inputs", updater.getFrame(),
                         replay.m_actionAtom.length()),
             m_pageNode->getContentSize().width / 2.f, rowY(0), .36f,
             {255, 220, 90});

    addButton(
        bot->isRecording() ? "Stop recording" : "Start recording",
        columnX(0), rowY(1), [bot, &replay, &updater] {
            bot->setMode(bot->isRecording() ? Bot::Stopped : Bot::Recording);
            if (PlayLayer::get() && bot->isRecording() &&
                replay.getInputIndex() < replay.m_actionAtom.length()) {
                replay.createBackup();
                replay.m_actionAtom.clipActions(updater.getFrame());
            }
        }, 150.f);

    addButton(
        bot->isPlaying() ? "Stop playback" : "Start playback", columnX(1),
        rowY(1), [bot] {
            bot->setMode(bot->isPlaying() ? Bot::Stopped : Bot::Playing);
        }, 150.f);

    addAdjuster(
        "TPS", 2, 0, [&] { return updater.m_tps->inner(); },
        [&](double value) {
            updater.m_tps->inner() = value;
            updater.m_tps->notifyChange();
        },
        5.0, 1.0, 10000.0,
        [](double value) { return fmt::format("{:.0f}", value); });
    addAdjuster(
        "Speed", 2, 1, [&] { return updater.m_speedhack->inner(); },
        [&](double value) {
            updater.m_speedhack->inner() = value;
            updater.m_speedhack->notifyChange();
        },
        .05, .05, 100.0,
        [](double value) { return fmt::format("{:.2f}x", value); });

    addToggle("Audio speedhack", 3, 0, updater.m_speedhackAudio->inner(),
              [&](bool value) {
                  updater.m_speedhackAudio->inner() = value;
                  updater.m_speedhackAudio->notifyChange();
              });
    addToggle("Ignore play inputs", 3, 1, replay.m_ignoreInputs->inner(),
              [&](bool value) { replay.m_ignoreInputs->inner() = value; });

    auto input = geode::TextInput::create(185.f, "Replay name");
    input->setCommonFilter(geode::CommonFilter::Name);
    input->setMaxCharCount(64);
    input->setString(replay.m_replayName);
    input->setCallback([&replay](std::string const& value) {
        replay.m_replayName = value;
    });
    input->setPosition({m_pageNode->getContentSize().width * .30f, rowY(4)});
    m_pageNode->addChild(input, 1);

    addButton("Save", m_pageNode->getContentSize().width * .67f, rowY(4),
              [&replay] {
                  if (replay.m_replayName.empty())
                      replay.m_replayName = "mobile-replay";
                  auto path = replay.getCurrentPath();
                  replay.backupExisting(path);
                  replay.save(path);
              }, 70.f);
    addButton("Load", m_pageNode->getContentSize().width * .86f, rowY(4),
              [&replay] {
                  if (replay.m_replayName.empty()) return;
                  const auto path = replay.getCurrentPath();
                  if (std::filesystem::exists(path)) {
                      replay.load(path);
                  } else {
                      FLAlertLayer::create("Grape", "Replay not found.", "OK")
                          ->show();
                  }
              }, 70.f);

    addToggle("Frame advance", 5, 0, updater.m_paused->inner(),
              [&](bool value) { updater.setPaused(value); });
    addButton("Advance one frame", columnX(1), rowY(5),
              [&updater] { updater.stepOnce(); }, 150.f);
    addToggle("Intentional death", 6, 0, updater.m_canDie->inner(),
              [&](bool value) { updater.m_canDie->inner() = value; });
    addToggle("Frame extrapolation", 6, 1,
              updater.m_extrapolateFrames->inner(), [&](bool value) {
                  updater.m_extrapolateFrames->inner() = value;
              });
}

void MobileMenu::buildAssistPage() {
    auto* bot = Bot::get();
    auto& updater = bot->updater();
    auto& hitboxes = bot->hitboxes();
    auto& trajectory = bot->trajectory();

    addToggle("Show hitboxes", 0, 0, hitboxes.m_enabled->inner(),
              [&](bool value) { hitboxes.m_enabled->inner() = value; });
    addToggle("Hitbox trail", 0, 1, hitboxes.m_trailEnabled->inner(),
              [&](bool value) { hitboxes.m_trailEnabled->inner() = value; });
    addToggle("Noclip", 1, 0, updater.m_noclip->inner(),
              [&](bool value) { updater.m_noclip->inner() = value; });
    addToggle("Prevent death", 1, 1, updater.m_preventDeath->inner(),
              [&](bool value) { updater.m_preventDeath->inner() = value; });
    addToggle("Trajectory", 2, 0, trajectory.m_state.m_enabled->inner(),
              [&](bool value) { trajectory.setEnabled(value); });
    addToggle("Layout mode", 2, 1, updater.m_layoutMode->inner(),
              [&](bool value) {
                  updater.m_layoutMode->inner() = value;
                  updater.m_layoutMode->notifyChange();
              });
    addToggle("Autoclicker", 3, 0,
              bot->autoclicker().m_enabled->inner(), [&](bool value) {
                  bot->autoclicker().m_enabled->inner() = value;
              });
    addToggle("Backwards stepping", 3, 1,
              updater.m_backwardsStepping->inner(), [&](bool value) {
                  updater.m_backwardsStepping->inner() = value;
              });
    addToggle("Regular background", 4, 0,
              updater.m_useRegularBg->inner(), [&](bool value) {
                  updater.m_useRegularBg->inner() = value;
              });
    addToggle("No mirror", 4, 1, updater.m_noMirror->inner(),
              [&](bool value) { updater.m_noMirror->inner() = value; });

    addAdjuster(
        "Hitbox width", 5, 0, [&] { return hitboxes.m_width->inner(); },
        [&](double value) { hitboxes.m_width->inner() = value; }, .05, 0.05,
        1.0, [](double value) { return fmt::format("{:.2f}", value); });
    addAdjuster(
        "Path length", 5, 1,
        [&] { return trajectory.m_state.m_length->inner(); },
        [&](double value) { trajectory.m_state.m_length->inner() = value; },
        .1, .1, 5.0,
        [](double value) { return fmt::format("{:.1f}s", value); });
    addToggle("SSB fix", 6, 0, updater.m_ssbFix->inner(),
              [&](bool value) { updater.m_ssbFix->inner() = value; });
    addToggle("Use regular updates", 6, 1,
              updater.m_useVisualUpdates->inner(), [&](bool value) {
                  updater.m_useVisualUpdates->inner() = value;
              });
}

void MobileMenu::buildSettingsPage() {
    auto* bot = Bot::get();
    auto& updater = bot->updater();
    auto& replay = bot->replaySystem();

    addToggle("Bot enabled", 0, 0, bot->m_enabled->inner(), [&](bool value) {
        bot->m_enabled->inner() = value;
        bot->m_enabled->notifyChange();
    });
    addToggle("Lock delta", 0, 1, updater.m_lockDelta->inner(),
              [&](bool value) { updater.m_lockDelta->inner() = value; });
    addToggle("Real time", 1, 0, updater.m_realTime->inner(),
              [&](bool value) { updater.m_realTime->inner() = value; });
    addToggle("Dynamic UPR", 1, 1, updater.m_dynamicUpr->inner(),
              [&](bool value) { updater.m_dynamicUpr->inner() = value; });
    addToggle("Alternate input hook", 2, 0,
              replay.m_useAlternateHook->inner(), [&](bool value) {
                  replay.m_useAlternateHook->inner() = value;
              });
    addToggle("Autosave at end", 2, 1,
              replay.m_autosaveAtLevelEnd->inner(), [&](bool value) {
                  replay.m_autosaveAtLevelEnd->inner() = value;
              });
    addToggle("Periodic backup", 3, 0,
              replay.m_autosaveAtInterval->inner(), [&](bool value) {
                  replay.m_autosaveAtInterval->inner() = value;
                  replay.m_autosaveAtInterval->notifyChange();
              });
    addToggle("Block playback input", 3, 1,
              replay.m_ignoreInputs->inner(),
              [&](bool value) { replay.m_ignoreInputs->inner() = value; });
    addToggle("Audio speedhack", 4, 0,
              updater.m_speedhackAudio->inner(), [&](bool value) {
                  updater.m_speedhackAudio->inner() = value;
                  updater.m_speedhackAudio->notifyChange();
              });
    addToggle("Visual updates", 4, 1,
              updater.m_useVisualUpdates->inner(), [&](bool value) {
                  updater.m_useVisualUpdates->inner() = value;
              });

    addAdjuster(
        "Backup seconds", 5, 0,
        [&] { return replay.m_autosaveInterval->inner(); },
        [&](double value) {
            replay.m_autosaveInterval->inner() = value;
            replay.m_autosaveInterval->notifyChange();
        },
        15.0, 15.0, 3600.0,
        [](double value) { return fmt::format("{:.0f}", value); });
    addAdjuster(
        "Target FPS", 5, 1, [&] { return updater.m_fpsTarget->inner(); },
        [&](double value) { updater.m_fpsTarget->inner() = value; }, 5.0, 15.0,
        480.0, [](double value) { return fmt::format("{:.0f}", value); });

    addLabel("Native Cocos UI - no ImGui/OpenGL overlay", columnX(0), rowY(6),
             .27f, {120, 255, 160});
    addLabel("iOS + Android32 + Android64", columnX(1), rowY(6), .27f,
             {120, 255, 160});
}

void MobileMenu::onTab(CCObject* sender) {
    m_page = static_cast<Page>(sender->getTag());
    rebuildPage();
}

MobileMenu* MobileMenu::create() {
    auto* ret = new MobileMenu();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void MobileMenu::open() {
    if (auto* popup = create()) popup->show();
}
