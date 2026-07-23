#ifndef TRAJECTORY_TRAJECTORY_HPP
#define TRAJECTORY_TRAJECTORY_HPP

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <functional>
#include <unordered_set>

#include "../config/config.hpp"
#include "../shared/value/value.hpp"
#include "Geode/cocos/cocoa/CCGeometry.h"

class TrajectoryDrawNode : public cocos2d::CCDrawNode {
   public:
    static TrajectoryDrawNode* create() {
        TrajectoryDrawNode* node = new TrajectoryDrawNode();

        if (node && node->init()) {
            node->autorelease();
            node->m_bUseArea = false;
        } else {
            CC_SAFE_DELETE(node);
        }

        return node;
    }
};

struct TrajectoryPlayerData {
    cocos2d::CCPoint position;
    cocos2d::CCRect hitbox;
    cocos2d::CCRect innerHitbox;
    float rotation;
    bool p1;
    bool holding;

    int score;
};

struct TrajectoryState {
    ConfigValuePtr<bool> m_enabled = ConfigValue<bool>::create(
        "trajectory.enabled", &GrapeSettings::get()->trajectory.enabled);
    ConfigValuePtr<double> m_width = ConfigValue<double>::create(
        "trajectory.width", &GrapeSettings::get()->trajectory.width);
    ConfigValuePtr<double> m_length = ConfigValue<double>::create(
        "trajectory.length", &GrapeSettings::get()->trajectory.length);
};

struct PredictionConfig {
    bool m_bypassConfig = false;
    int m_maxLength = 10'000'000;
    double m_overridenTPS = 0.0;
};

class Trajectory {
   public:
    using TrajectoryMode = GrapeSettings::TrajectorySettings::Mode;

    constexpr static int CLICK_MASK = 0b111;
    constexpr static int DIRECTION_MASK = 0b11000;

   private:
    cocos2d::CCRenderTexture* m_renderTex = nullptr;
    TrajectoryDrawNode* m_node = nullptr;

   public:
    PlayerObject* m_fakePlayer1 = nullptr;
    PlayerObject* m_fakePlayer2 = nullptr;

   private:
    std::unordered_set<uintptr_t> m_activatedObjectsP1;
    std::unordered_set<uintptr_t> m_activatedObjectsP2;

    struct TrajectoryAction {
        int m_delay;
        std::function<void()> m_func;
        bool m_executed = false;
    };
    std::vector<TrajectoryAction> m_actions;

    uint64_t m_lastFrame = 0;
    bool m_calculated = false;

    struct Signature {
        uint32_t frame = 0;
        float p1[7] = {};
        uint64_t p1flags = 0;
        float p2[7] = {};
        uint64_t p2flags = 0;
        float timeWarp = 0.0f;
        float cameraZoom = 0.0f;
        float width = 0.0f;
        float length = 0.0f;
        int maxSteps = 0;
        uint32_t boolPack = 0;
        size_t categoriesHash = 0;

        bool operator==(const Signature&) const = default;
    };
    Signature m_lastSignature;

    bool m_drawing = false;
    bool m_deadP1 = false;
    bool m_deadP2 = false;

    bool m_p1Holding = false;
    bool m_p2Holding = false;

    float m_delta = 0.0;

   public:
    Trajectory() = default;
    ~Trajectory() {
        m_actions.clear();
        m_activatedObjectsP1.clear();
        m_activatedObjectsP2.clear();

        auto releasePlayer = [](PlayerObject*& player) {
            if (!player) return;
            if (player->getParent()) player->removeFromParentAndCleanup(true);
            player->release();
            player = nullptr;
        };
        releasePlayer(m_fakePlayer1);
        releasePlayer(m_fakePlayer2);

        if (m_node) {
            if (m_node->getParent()) m_node->removeFromParentAndCleanup(true);
            m_node->release();
            m_node = nullptr;
        }
    }

    TrajectoryState* m_state = nullptr;

    bool drawing() const { return m_drawing; }
    bool enabled() const { return m_state->m_enabled->inner(); }
    void setEnabled(bool enabled) {
        m_state->m_enabled->inner() = enabled;
        m_state->m_enabled->notifyChange();
    }
    void setDelta(float delta) { m_delta = delta; }

    PlayerObject* getOtherPlayer(PlayerObject* player) {
        if (player == m_fakePlayer1) return m_fakePlayer2;
        return m_fakePlayer1;
    }

    void scheduleAction(std::function<void()> action, int delay) {
        m_actions.push_back(TrajectoryAction{
            .m_delay = delay,
            .m_func = action,
        });
    }

    bool hasDied(PlayerObject* player) {
        if (player == m_fakePlayer1) {
            m_deadP1 = true;
            return true;
        }

        if (player == m_fakePlayer2) {
            m_deadP2 = true;
            return true;
        }

        return false;
    }

    bool isFakePlayer(PlayerObject* player) {
        return player == m_fakePlayer1 || player == m_fakePlayer2;
    }

    void handleButton(bool p1, bool holding) {
        if (p1) {
            m_p1Holding = holding;
        } else {
            m_p2Holding = holding;
        }
    }

    PlayerObject* createFakePlayer(std::string&& id) {
        GJBaseGameLayer* pl = GJBaseGameLayer::get();

        PlayerObject* player = PlayerObject::create(1, 1, pl, pl, true);
        player->retain();
        player->setPosition({0, 105});
        player->setID(id);
        pl->m_objectLayer->addChild(player);

        return player;
    }

    static Trajectory* create() {
        Trajectory* t = new Trajectory();

        t->m_node = TrajectoryDrawNode::create();
        t->m_node->retain();
        t->m_node->setID("trajectory-node"_spr);
        t->m_node->setBlendFunc(cocos2d::ccBlendFunc{770, 771});

        t->m_fakePlayer1 = t->createFakePlayer("trajectory-fake-player1"_spr);
        t->m_fakePlayer2 = t->createFakePlayer("trajectory-fake-player2"_spr);

        GJBaseGameLayer* pl = GJBaseGameLayer::get();
        pl->m_debugDrawNode->getParent()->addChild(
            t->m_node, pl->m_uiLayer->getZOrder() + 10000);



        return t;
    }

    int getPredictionLength();

    void invalidateCache() {
        m_lastFrame = UINT64_MAX;
        m_calculated = false;
    }
    Signature computeSignature(GJBaseGameLayer* pl);
    bool iterate(GJBaseGameLayer* pl, PlayerObject* player, int mode,
                 float* colors, bool& hasHeld, int& stepCount,
                 PredictionConfig config);

    TrajectoryPlayerData runPrediction(GJBaseGameLayer* pl,
                                       PlayerObject* player,
                                       PlayerObject* other, int mode,
                                       float* colors, bool both,
                                       PredictionConfig config);
    TrajectoryPlayerData simulate(GJBaseGameLayer* pl, bool p1, int mode,
                                  bool clickBothPlayers,
                                  PredictionConfig config = PredictionConfig());
    void update(GJBaseGameLayer* pl);
    void handlePortal(PlayerObject* player, GameObject* object);
    void drawHitbox(PlayerObject* player);

    bool playerHasActivated(PlayerObject* player, EnhancedGameObject* object);
    bool realPlayerHasActivated(PlayerObject* player,
                                EnhancedGameObject* object);

    void rememberActivatedObject(EnhancedGameObject* object,
                                 PlayerObject* player) {
        if (player == m_fakePlayer1) {
            m_activatedObjectsP1.insert((uintptr_t)object);
        } else if (player == m_fakePlayer2) {
            m_activatedObjectsP2.insert((uintptr_t)object);
        }
    }

    void deactivateAllRemembered() {
        m_activatedObjectsP1.clear();
        m_activatedObjectsP2.clear();
    }
};

class TrajectoryManager {
   private:
    Trajectory* m_trajectory;

   public:
    TrajectoryState m_state;
    TrajectoryManager() { m_trajectory = nullptr; }

    bool exists() const { return m_trajectory != nullptr; }

    Trajectory* unsafeInner() { return m_trajectory; }

    bool drawing() const {
        if (m_trajectory) {
            return m_trajectory->drawing();
        }

        return false;
    }

    bool enabled() const {
        if (m_trajectory) {
            return m_trajectory->enabled();
        }

        return false;
    }

    PlayerObject* getOtherPlayer(PlayerObject* player) {
        if (m_trajectory) {
            return m_trajectory->getOtherPlayer(player);
        }

        return 0;
    }

    void handleButton(bool p1, bool holding) {
        if (m_trajectory) {
            m_trajectory->handleButton(p1, holding);
        }
    }

    void update(GJBaseGameLayer* pl) {
        if (m_trajectory) m_trajectory->update(pl);
    }

    void toggle() {
        if (m_trajectory) {
            m_trajectory->setEnabled(!m_trajectory->enabled());
        }
    }

    void setEnabled(bool enabled) {
        if (m_trajectory) {
            m_trajectory->setEnabled(enabled);
        }
    }

    void init() {
        if (m_trajectory) uninit();
        m_trajectory = Trajectory::create();
        if (m_trajectory) m_trajectory->m_state = &m_state;
    }

    void uninit() {
        delete m_trajectory;
        m_trajectory = nullptr;
    }

    bool playerHasActivated(PlayerObject* player, EnhancedGameObject* object) {
        if (m_trajectory) {
            return m_trajectory->playerHasActivated(player, object);
        }

        return false;
    }

    bool realPlayerHasActivated(PlayerObject* player,
                                EnhancedGameObject* object) {
        if (m_trajectory) {
            return m_trajectory->realPlayerHasActivated(player, object);
        }

        return false;
    }

    bool hasDied(PlayerObject* player) {
        if (m_trajectory) {
            return m_trajectory->hasDied(player);
        }

        return false;
    }
    void setDelta(float delta) {
        if (m_trajectory) {
            m_trajectory->setDelta(delta);
        }
    }

    bool isFakePlayer(PlayerObject* player) {
        if (m_trajectory) {
            return m_trajectory->isFakePlayer(player);
        }

        return false;
    }

    void handlePortal(PlayerObject* player, GameObject* object) {
        if (m_trajectory) {
            m_trajectory->handlePortal(player, object);
        }
    }

    void rememberActivatedObject(EnhancedGameObject* object,
                                 PlayerObject* player) {
        if (m_trajectory) {
            m_trajectory->rememberActivatedObject(object, player);
        }
    }

    void scheduleAction(std::function<void()> action, int delay) {
        if (m_trajectory) {
            m_trajectory->scheduleAction(action, delay);
        }
    }
};

#endif
