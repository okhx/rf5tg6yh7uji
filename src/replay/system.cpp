#include "system.hpp"
#include "util/paths.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <span>
#include <slc/formats/v3/replay.hpp>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "bot/bot.hpp"
#include "bot/updater.hpp"

using namespace geode::prelude;

void ReplaySystem::onReset(uint32_t newFrame) {
    m_lastInputs.clear();
    m_forceNextInput = false;
    m_flipProcessingInputs = false;

    if (Bot::get()->isRecording()) {
        m_actionAtom.clipActions(newFrame);

        m_inputIndex = m_actionAtom.length();
    } else {
        if (m_actionAtom.m_actions.empty()) {
            m_inputIndex = 0;
            return;
        }

        m_inputIndex =
            std::distance(m_actionAtom.m_actions.begin(),
                          std::find_if(m_actionAtom.m_actions.begin(),
                                       m_actionAtom.m_actions.end(),
                                       [newFrame](const auto i) -> bool {
                                           return i.m_frame >= newFrame;
                                       }));
    }
}

void ReplaySystem::seekAfterFrame(uint32_t frame) {
    m_inputIndex = static_cast<size_t>(std::distance(
        m_actionAtom.m_actions.begin(),
        std::upper_bound(
            m_actionAtom.m_actions.begin(), m_actionAtom.m_actions.end(), frame,
            [](uint32_t target, const slc::Action& action) {
                return target < action.m_frame;
            })));
}

[[nodiscard]] const std::optional<slc::Action> ReplaySystem::getNextInput(
    uint32_t frame) {
    if (m_inputIndex >= m_actionAtom.length()) {
        return std::nullopt;
    }

    auto& input = m_actionAtom.m_actions.at(m_inputIndex);
#ifdef GEODE_IS_MOBILE
    if (input.m_frame <= frame) {
#else
    if (input.m_frame == frame) {
#endif
        m_inputIndex++;
        return input;
    }

    return std::nullopt;
}

uint64_t& ReplaySystem::getCurrentRandomState() {
#ifndef GEODE_IS_WINDOWS
    return m_portableRandomState;
#else
    return *reinterpret_cast<uint64_t*>(geode::base::get() + 0x6c2e90);
#endif
}

uint64_t& ReplaySystem::getCurrentShakeState() {
    return this->m_shakeRandomState;
}

void ReplaySystem::onExit() {
    m_inputIndex = 0;
    m_lastInputs.clear();
    m_forceNextInput = false;
    m_flipProcessingInputs = false;
}

using Replay = slc::v3::Replay<>;

namespace {
struct ImportedReplay {
    slc::ActionAtom actions;
    double tps = 240.0;
};

void addImportedAction(ImportedReplay& replay, uint64_t frame,
                       slc::Action::ActionType type, bool holding,
                       bool player2) {
    replay.actions.m_actions.emplace_back(0, frame, type, holding, player2);
}

class MsgPack {
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;

    uint8_t byte() {
        if (m_pos >= m_data.size()) throw std::runtime_error("Unexpected EOF");
        return m_data[m_pos++];
    }

    template <class T>
    T big() {
        if (m_data.size() - m_pos < sizeof(T))
            throw std::runtime_error("Unexpected EOF");
        T value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value = static_cast<T>((value << 8) | byte());
        return value;
    }

    size_t length(uint8_t tag, uint8_t fixMask, uint8_t fixValue,
                  uint8_t tag16, uint8_t tag32) {
        if ((tag & fixMask) == fixValue) return tag & ~fixMask;
        if (tag == tag16) return big<uint16_t>();
        if (tag == tag32) return big<uint32_t>();
        throw std::runtime_error("Unexpected MessagePack container");
    }

    void skipBytes(size_t count) {
        if (count > m_data.size() - m_pos)
            throw std::runtime_error("Unexpected EOF");
        m_pos += count;
    }

   public:
    explicit MsgPack(std::span<const uint8_t> data) : m_data(data) {}

    size_t mapLength() {
        return length(byte(), 0xf0, 0x80, 0xde, 0xdf);
    }

    size_t arrayLength() {
        return length(byte(), 0xf0, 0x90, 0xdc, 0xdd);
    }

    std::string_view string() {
        const uint8_t tag = byte();
        size_t size = 0;
        if ((tag & 0xe0) == 0xa0) size = tag & 0x1f;
        else if (tag == 0xd9) size = byte();
        else if (tag == 0xda) size = big<uint16_t>();
        else if (tag == 0xdb) size = big<uint32_t>();
        else throw std::runtime_error("Expected MessagePack string");
        if (size > m_data.size() - m_pos)
            throw std::runtime_error("Unexpected EOF");
        const char* data =
            reinterpret_cast<const char*>(m_data.data() + m_pos);
        m_pos += size;
        return {data, size};
    }

    double number() {
        const uint8_t tag = byte();
        if (tag <= 0x7f) return tag;
        if (tag >= 0xe0) return static_cast<int8_t>(tag);
        switch (tag) {
            case 0xca:
                return std::bit_cast<float>(big<uint32_t>());
            case 0xcb:
                return std::bit_cast<double>(big<uint64_t>());
            case 0xcc: return byte();
            case 0xcd: return big<uint16_t>();
            case 0xce: return big<uint32_t>();
            case 0xcf: return static_cast<double>(big<uint64_t>());
            case 0xd0: return static_cast<int8_t>(byte());
            case 0xd1: return static_cast<int16_t>(big<uint16_t>());
            case 0xd2: return static_cast<int32_t>(big<uint32_t>());
            case 0xd3: return static_cast<double>(
                static_cast<int64_t>(big<uint64_t>()));
            default: throw std::runtime_error("Expected MessagePack number");
        }
    }

    bool boolean() {
        const uint8_t tag = byte();
        if (tag == 0xc2) return false;
        if (tag == 0xc3) return true;
        throw std::runtime_error("Expected MessagePack bool");
    }

    void skip() {
        const uint8_t tag = byte();
        if (tag <= 0x7f || tag >= 0xe0 || tag == 0xc0 || tag == 0xc2 ||
            tag == 0xc3)
            return;
        if ((tag & 0xe0) == 0xa0) return skipBytes(tag & 0x1f);
        if ((tag & 0xf0) == 0x90) {
            for (size_t i = 0; i < (tag & 0x0f); ++i) skip();
            return;
        }
        if ((tag & 0xf0) == 0x80) {
            for (size_t i = 0; i < (tag & 0x0f); ++i) {
                skip();
                skip();
            }
            return;
        }
        switch (tag) {
            case 0xc4: return skipBytes(byte());
            case 0xc5: return skipBytes(big<uint16_t>());
            case 0xc6: return skipBytes(big<uint32_t>());
            case 0xca: return skipBytes(4);
            case 0xcb: return skipBytes(8);
            case 0xcc:
            case 0xd0: return skipBytes(1);
            case 0xcd:
            case 0xd1: return skipBytes(2);
            case 0xce:
            case 0xd2: return skipBytes(4);
            case 0xcf:
            case 0xd3: return skipBytes(8);
            case 0xd9: return skipBytes(byte());
            case 0xda: return skipBytes(big<uint16_t>());
            case 0xdb: return skipBytes(big<uint32_t>());
            case 0xdc: {
                const size_t count = big<uint16_t>();
                for (size_t i = 0; i < count; ++i) skip();
                return;
            }
            case 0xdd: {
                const size_t count = big<uint32_t>();
                for (size_t i = 0; i < count; ++i) skip();
                return;
            }
            case 0xde: {
                const size_t count = big<uint16_t>();
                for (size_t i = 0; i < count; ++i) {
                    skip();
                    skip();
                }
                return;
            }
            case 0xdf: {
                const size_t count = big<uint32_t>();
                for (size_t i = 0; i < count; ++i) {
                    skip();
                    skip();
                }
                return;
            }
            default: throw std::runtime_error("Unsupported MessagePack value");
        }
    }
};

slc::Action::ActionType actionType(int button) {
    using Type = slc::Action::ActionType;
    if (button == 2) return Type::Left;
    if (button == 3) return Type::Right;
    return Type::Jump;
}

ImportedReplay parseGdrJson(const std::filesystem::path& path) {
    auto result = geode::utils::file::readJson(path);
    if (result.isErr()) throw std::runtime_error("Invalid GDR JSON");
    auto json = std::move(result).unwrap();
    ImportedReplay replay;
    replay.tps = json["framerate"].asDouble().unwrapOr(240.0);
    auto inputs = json["inputs"].asArray();
    if (inputs.isErr()) throw std::runtime_error("GDR has no inputs");
    for (const auto& input : inputs.unwrap()) {
        const auto frame = input["frame"].asInt();
        const auto down = input["down"].asBool();
        if (frame.isErr() || down.isErr())
            throw std::runtime_error("Invalid GDR input");
        addImportedAction(
            replay, static_cast<uint64_t>(frame.unwrap()),
            actionType(static_cast<int>(input["btn"].asInt().unwrapOr(1))),
            down.unwrap(), input["2p"].asBool().unwrapOr(false));
    }
    return replay;
}

ImportedReplay parseGdr(const std::filesystem::path& path) {
    auto bytes = geode::utils::file::readBinary(path);
    if (bytes.isErr()) throw std::runtime_error("Could not read GDR");
    MsgPack reader(bytes.unwrap());
    ImportedReplay replay;
    const size_t fields = reader.mapLength();
    for (size_t i = 0; i < fields; ++i) {
        const auto key = reader.string();
        if (key == "framerate") {
            replay.tps = reader.number();
        } else if (key == "inputs") {
            const size_t count = reader.arrayLength();
            for (size_t j = 0; j < count; ++j) {
                uint64_t frame = 0;
                int button = 1;
                bool player2 = false;
                bool down = false;
                const size_t inputFields = reader.mapLength();
                for (size_t k = 0; k < inputFields; ++k) {
                    const auto inputKey = reader.string();
                    if (inputKey == "frame")
                        frame = static_cast<uint64_t>(reader.number());
                    else if (inputKey == "btn")
                        button = static_cast<int>(reader.number());
                    else if (inputKey == "2p")
                        player2 = reader.boolean();
                    else if (inputKey == "down")
                        down = reader.boolean();
                    else
                        reader.skip();
                }
                addImportedAction(replay, frame, actionType(button), down,
                                  player2);
            }
        } else {
            reader.skip();
        }
    }
    return replay;
}

ImportedReplay parsePlainText(const std::filesystem::path& path) {
    std::ifstream input(path);
    ImportedReplay replay;
    if (!(input >> replay.tps)) throw std::runtime_error("Invalid text macro");
    uint64_t frame = 0;
    int holding = 0;
    int player2 = 0;
    while (input >> frame >> holding >> player2)
        addImportedAction(replay, frame, slc::Action::ActionType::Jump,
                          holding != 0, player2 != 0);
    if (!input.eof()) throw std::runtime_error("Invalid text macro input");
    return replay;
}

uint32_t little32(std::span<const uint8_t> bytes, size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        throw std::runtime_error("Unexpected YBot EOF");
    return static_cast<uint32_t>(bytes[offset]) |
           static_cast<uint32_t>(bytes[offset + 1]) << 8 |
           static_cast<uint32_t>(bytes[offset + 2]) << 16 |
           static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

ImportedReplay parseYbot(const std::filesystem::path& path) {
    auto result = geode::utils::file::readBinary(path);
    if (result.isErr()) throw std::runtime_error("Could not read YBot macro");
    const auto& bytes = result.unwrap();
    if (bytes.size() < 16 ||
        std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) !=
            "ybot")
        throw std::runtime_error("Invalid YBot header");

    const uint32_t metaLength = little32(bytes, 8);
    const uint32_t blobCount = little32(bytes, 12);
    size_t pos = 16;
    if (metaLength > bytes.size() - pos)
        throw std::runtime_error("Invalid YBot metadata");

    ImportedReplay replay;
    if (metaLength >= 28) {
        const uint32_t rawFps = little32(bytes, pos + 24);
        replay.tps = std::bit_cast<float>(rawFps);
    }
    pos += metaLength;
    for (uint32_t i = 0; i < blobCount; ++i) {
        const uint32_t size = little32(bytes, pos);
        pos += 4;
        if (size > bytes.size() - pos)
            throw std::runtime_error("Invalid YBot blob");
        pos += size;
    }

    uint64_t frame = 0;
    while (pos < bytes.size()) {
        uint64_t packed = 0;
        int shift = 0;
        uint8_t part = 0;
        do {
            if (pos >= bytes.size() || shift >= 64)
                throw std::runtime_error("Invalid YBot action");
            part = bytes[pos++];
            packed |= static_cast<uint64_t>(part & 0x7f) << shift;
            shift += 7;
        } while (part & 0x80);

        const uint8_t flags = packed & 0x0f;
        frame += packed >> 4;
        if (flags == 0x0f) {
            if (pos + 4 > bytes.size())
                throw std::runtime_error("Invalid YBot FPS action");
            pos += 4;
            continue;
        }
        const int button = flags >> 2;
        if (button < 1 || button > 3)
            throw std::runtime_error("Invalid YBot button");
        addImportedAction(replay, frame, actionType(button),
                          (flags & 2) != 0, (flags & 1) == 0);
    }
    return replay;
}
}

void ReplaySystem::save(std::filesystem::path path, bool noOverwrite) {
    m_lastOperationSucceeded = false;
    if (noOverwrite && std::filesystem::exists(path)) {
        geode::log::info("Not overwriting replay at {}", path);
        return;
    }

    Replay replay;
    std::stable_sort(
        m_actionAtom.m_actions.begin(), m_actionAtom.m_actions.end(),
        [](const auto& a, const auto& b) { return a.m_frame < b.m_frame; });

    uint64_t previousFrame = 0;
    for (auto& action : m_actionAtom.m_actions) {
        action.recalculateDelta(previousFrame);
        previousFrame = action.m_frame;
    }

    replay.m_atoms.add(m_actionAtom);

    replay.m_meta.m_build = 81;
    replay.m_meta.m_seed = m_startingSeed;
    replay.m_meta.m_tps = Bot::get()->updater().m_tps->inner();

    std::ofstream fd(path, std::ios::binary);
    if (!fd) {
        geode::log::error("Failed to open replay for writing: {}", path);
        return;
    }
    auto result = replay.write(fd);
    if (!result.has_value()) {
        geode::log::error("Failed to save replay: {}",
                          result.error().m_message);
        return;
    }

    geode::log::info("Successfully saved replay to {}", path);
    m_lastOperationSucceeded = true;
}

bool ReplaySystem::processSlc3(Replay& replay) {
    auto& atoms = replay.m_atoms.m_atoms;
    auto it = std::find_if(atoms.begin(), atoms.end(), [](auto& v) {
        return std::visit(
            [](auto& at) { return at.id == slc::v3::AtomId::Action; }, v);
    });

    if (it == atoms.end()) {
        geode::log::error("Replay contains no action data");
        return false;
    }

    auto atom = *it;
    auto& updater = Bot::get()->updater();
    m_actionAtom = std::get<slc::ActionAtom>(atom);
    std::stable_sort(
        m_actionAtom.m_actions.begin(), m_actionAtom.m_actions.end(),
        [](const auto& left, const auto& right) {
            return left.m_frame < right.m_frame;
        });
    m_inputIndex = 0;
    m_startingSeed = replay.m_meta.m_seed;
    m_startingSeedThisAttempt = m_startingSeed;
#ifndef GEODE_IS_WINDOWS
    m_portableRandomState = m_startingSeed;
#endif
    m_lastInputs.clear();
    updater.resetFrame();
    updater.m_frameOnLastAttempt = 0;
    updater.m_expectsDeath = false;
    updater.m_fullReset = false;
    updater.m_tpsOverflow = 0.0;
    updater.m_tps->inner() =
        std::isfinite(replay.m_meta.m_tps) && replay.m_meta.m_tps > 0.0
            ? replay.m_meta.m_tps
            : 240.0;
    updater.m_tps->notifyChange();
    Bot::get()->setMode(Bot::Mode::Stopped);
    return true;
}

bool ReplaySystem::processSlc2(slc::v2::Replay<ReplayMeta>& replay) {
    uint64_t currentFrame = 0;
    auto& a = m_actionAtom;
    a.clear();
    for (const auto& input : replay.getInputs()) {
        if (input.m_button == slc::v2::Input::InputType::Skip) {
            currentFrame = input.m_frame;
            continue;
        }

        if (static_cast<int>(input.m_button) <
            static_cast<int>(slc::v2::Input::InputType::Restart)) {
            a.m_actions.push_back(slc::v3::Action(
                currentFrame, input.m_frame - currentFrame,
                static_cast<slc::Action::ActionType>(input.m_button),
                input.m_holding, input.m_player2));
            currentFrame = input.m_frame;
            continue;
        }

        if (static_cast<int>(input.m_button) <
            static_cast<int>(slc::v2::Input::InputType::TPS)) {
            a.m_actions.push_back(slc::v3::Action(
                currentFrame, input.m_frame - currentFrame,
                static_cast<slc::v3::Action::ActionType>(input.m_button),
                replay.m_meta.seed));
            currentFrame = input.m_frame;
            continue;
        }

        a.m_actions.push_back(slc::v3::Action(
            currentFrame, input.m_frame - currentFrame, input.m_tps));
        currentFrame = input.m_frame;
    }

    auto& updater = Bot::get()->updater();
    std::stable_sort(
        m_actionAtom.m_actions.begin(), m_actionAtom.m_actions.end(),
        [](const auto& left, const auto& right) {
            return left.m_frame < right.m_frame;
        });
    m_inputIndex = 0;
    m_startingSeed = replay.m_meta.seed;
    m_startingSeedThisAttempt = m_startingSeed;
#ifndef GEODE_IS_WINDOWS
    m_portableRandomState = m_startingSeed;
#endif
    m_lastInputs.clear();
    updater.resetFrame();
    updater.m_frameOnLastAttempt = 0;
    updater.m_expectsDeath = false;
    updater.m_fullReset = false;
    updater.m_tpsOverflow = 0.0;
    updater.m_tps->inner() =
        std::isfinite(replay.m_tps) && replay.m_tps > 0.0 ? replay.m_tps
                                                          : 240.0;
    updater.m_tps->notifyChange();
    Bot::get()->setMode(Bot::Mode::Stopped);
    return true;
}

void ReplaySystem::load(std::filesystem::path path) {
    m_lastOperationSucceeded = false;
    if (!std::filesystem::exists(path) && path.extension() == ".grape") {
        auto legacyPath = path;
        legacyPath.replace_extension(".slc");
        if (std::filesystem::exists(legacyPath)) path = legacyPath;
    }
    if (!std::filesystem::exists(path)) {
        geode::log::error(
            "Failed to load slc3 replay from {}; file does not exist", path);
        return;
    }

    std::ifstream fd(path, std::ios::binary);

    auto replay = Replay::read(fd);
    if (replay.has_value()) {
        geode::log::info("Loading slc3 replay from {}", path);
        m_lastOperationSucceeded = this->processSlc3(replay.value());
    } else {
        fd.seekg(0, std::ios::beg);
        auto v2Replay = slc::v2::Replay<ReplayMeta>::read(fd);
        if (v2Replay.has_value()) {
            geode::log::info("Loading slc2 (legacy) replay from {}", path);
            m_lastOperationSucceeded = this->processSlc2(v2Replay.value());
        } else {
            geode::log::error("Failed to load slc3 replay from {}", path);
        }
    }
}

geode::utils::file::FilePickOptions ReplaySystem::converterFileOptions() {
    return {
        .defaultPath = std::nullopt,
        .filters = {{
            .description = "Macro files",
            .files = {"*.grape", "*.slc", "*.gdr", "*.json", "*.ybot",
                      "*.txt"},
        }},
    };
}

geode::Result<size_t> ReplaySystem::convertAndPlay(
    std::filesystem::path path) {
    if (!std::filesystem::is_regular_file(path))
        return geode::Err("Macro file does not exist");

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (extension == ".grape" || extension == ".slc") {
        load(path);
        if (!m_lastOperationSucceeded)
            return geode::Err("Could not parse SLC macro");
    } else {
        ImportedReplay imported;
        try {
            if (extension == ".txt")
                imported = parsePlainText(path);
            else if (extension == ".ybot")
                imported = parseYbot(path);
            else if (extension == ".json")
                imported = parseGdrJson(path);
            else if (extension == ".gdr")
                imported = parseGdr(path);
            else
                return geode::Err("Unsupported macro extension: {}",
                                  extension);
        } catch (const std::exception& error) {
            return geode::Err("Macro conversion failed: {}", error.what());
        }

        if (imported.actions.m_actions.empty())
            return geode::Err("Converted macro contains no inputs");
        if (!std::isfinite(imported.tps) || imported.tps <= 0.0)
            imported.tps = 240.0;

        std::stable_sort(
            imported.actions.m_actions.begin(),
            imported.actions.m_actions.end(),
            [](const auto& left, const auto& right) {
                return left.m_frame < right.m_frame;
            });
        uint64_t previousFrame = 0;
        for (auto& action : imported.actions.m_actions) {
            action.recalculateDelta(previousFrame);
            previousFrame = action.m_frame;
        }

        Replay converted;
        converted.m_atoms.add(imported.actions);
        converted.m_meta.m_tps = imported.tps;
        converted.m_meta.m_seed = m_startingSeed;
        if (!processSlc3(converted))
            return geode::Err("Could not activate converted macro");
    }

    auto name = path.stem();
    if (extension == ".json" && name.extension() == ".gdr")
        name = name.stem();
    m_replayName = name.string();
    const auto output = getCurrentPath();
    backupExisting(output);
    save(output);
    if (!m_lastOperationSucceeded)
        return geode::Err("Converted macro could not be saved");

    m_inputIndex = 0;
    Bot::get()->setMode(Bot::Playing);
    if (auto* playLayer = PlayLayer::get();
        playLayer && !playLayer->m_hasCompletedLevel &&
        !playLayer->m_levelEndAnimationStarted)
        playLayer->resetLevel();
    return geode::Ok(m_actionAtom.length());
}

void ReplaySystem::merge(std::filesystem::path path, MergeMode mode) {
    if (!std::filesystem::exists(path)) {
        geode::log::error("Merge failed: file does not exist at {}", path);
        return;
    }

    std::ifstream fd(path, std::ios::binary);
    slc::ActionAtom otherAtom;

    auto replay = slc::v3::Replay<>::read(fd);
    if (replay.has_value()) {
        auto& atoms = replay.value().m_atoms.m_atoms;
        auto it = std::find_if(atoms.begin(), atoms.end(), [](auto& v) {
            return std::visit(
                [](auto& at) { return at.id == slc::v3::AtomId::Action; }, v);
        });
        if (it == atoms.end()) {
            geode::log::error("Merge failed: no action atom in other replay");
            return;
        }
        otherAtom = std::get<slc::ActionAtom>(*it);
    } else {
        fd.seekg(0, std::ios::beg);
        auto v2Replay = slc::v2::Replay<ReplayMeta>::read(fd);
        if (!v2Replay.has_value()) {
            geode::log::error("Merge failed: could not parse other replay");
            return;
        }
        uint64_t currentFrame = 0;
        for (const auto& input : v2Replay.value().getInputs()) {
            if (input.m_button == slc::v2::Input::InputType::Skip) {
                currentFrame = input.m_frame;
                continue;
            }
            if (static_cast<int>(input.m_button) <
                static_cast<int>(slc::v2::Input::InputType::Restart)) {
                otherAtom.m_actions.push_back(slc::v3::Action(
                    currentFrame, input.m_frame - currentFrame,
                    static_cast<slc::Action::ActionType>(input.m_button),
                    input.m_holding, input.m_player2));
                currentFrame = input.m_frame;
            }
        }
    }

    bool keepCurrentP1 = true;
    bool keepCurrentP2 = true;
    bool keepOtherP1   = true;
    bool keepOtherP2   = true;
    bool swapOther     = false;

    switch (mode) {
        case MergeMode::P1FromOther:
            keepCurrentP1 = false;
            keepOtherP2   = false;
            break;
        case MergeMode::P2FromOther:
            keepCurrentP2 = false;
            keepOtherP1   = false;
            break;
        case MergeMode::SwapPlayers:
            swapOther = true;
            break;
    }

    std::vector<slc::Action> merged;

    for (auto& action : m_actionAtom.m_actions) {
        bool isP2 = action.m_player2;
        if (isP2 && !keepCurrentP2) continue;
        if (!isP2 && !keepCurrentP1) continue;
        merged.push_back(action);
    }

    for (auto action : otherAtom.m_actions) {
        bool isP2 = action.m_player2;
        if (!swapOther) {
            if (isP2 && !keepOtherP2) continue;
            if (!isP2 && !keepOtherP1) continue;
        } else {
            action.m_player2 = !isP2;
        }
        merged.push_back(action);
    }

    std::stable_sort(merged.begin(), merged.end(),
                     [](const auto& a, const auto& b) {
                         return a.m_frame < b.m_frame;
                     });

    uint64_t prevFrame = 0;
    for (auto& action : merged) {
        action.recalculateDelta(prevFrame);
        prevFrame = action.m_frame;
    }

    m_actionAtom.m_actions = std::move(merged);
	
    geode::log::info("Macro merge complete. Total inputs after merge: {}",
                     m_actionAtom.m_actions.size());
}

std::filesystem::path ReplaySystem::getCurrentPath() {
    return silicate::paths::directory("replays") /
           (m_replayName + ".grape");
}

static std::filesystem::path createBackupPath(const std::string& name) {
    namespace time = std::chrono;

    const time::time_point timestamp =
        time::floor<time::milliseconds>(time::system_clock::now());

    const std::filesystem::path path =
        silicate::paths::directory("backups") /
        fmt::format("_backup_{:%Y%m%d_%H%M%S}_{}.grape", timestamp, name);

    return path;
}

void ReplaySystem::backupExisting(std::filesystem::path path) {
    if (!std::filesystem::exists(path)) {
        return;
    }

    std::string name = path.stem().string();
    auto newPath = createBackupPath(name);
    std::filesystem::copy(path, newPath,
                          std::filesystem::copy_options::skip_existing);
}

void ReplaySystem::createBackup() {
    auto path = createBackupPath(m_replayName);
    this->save(path, true);
}

$execute {
    auto bot = Bot::get();
    auto& rs = bot->replaySystem();

    rs.m_autosaveId = bot->scheduler().schedule(
        [&rs]() {
            auto pl = PlayLayer::get();
            if (!pl) return;
            if (!rs.m_autosaveAtInterval->inner()) return;

            if (Bot::get()->isRecording()) {
                rs.createBackup();
            }
        },
        rs.m_autosaveInterval->inner(), true);

    rs.m_autosaveInterval->handle([bot, &rs](double& interval) {
        bot->scheduler().reschedule(rs.m_autosaveId, interval);
    });
}
