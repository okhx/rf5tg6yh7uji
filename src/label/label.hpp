#ifndef LABEL_LABEL_HPP
#define LABEL_LABEL_HPP

#include <Geode/Geode.hpp>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "shared/value/value.hpp"

using namespace cocos2d;

// Whether custom .ttf/.otf label fonts can be used on this platform.
//
// Custom fonts are drawn with CCLabelTTF, and the bindings only bind that class
// on Apple platforms -- Cocos2d.bro gives CCLabelTTF::create addresses for imac,
// m1 and ios and none for win or android, and the inline implementation is
// wrapped in `#if defined(GEODE_IS_IOS)`. Selecting a custom font elsewhere
// therefore called into nothing and took the game down. Bitmap fonts
// (CCLabelBMFont) are unaffected, so the rest falls back to those. Gates both
// the font picker and the label itself, so an unusable font can neither be
// selected nor loaded from a config saved before this fix.
constexpr bool labelFontsSupported() {
#if defined(GEODE_IS_IOS) || defined(GEODE_IS_MACOS)
    return true;
#else
    return false;
#endif
}

class Label;

class RawLabel {
   public:
    std::string m_id;
    std::function<std::string(Label&)> m_display;
    std::string m_font;
    int m_anchor = 0;

    RawLabel(std::string id, std::function<std::string(Label&)> display,
             std::string font, int anchor = 0) {
        m_id = id;
        m_display = display;
        m_font = font;
        m_anchor = anchor;
    }
};

class Label {
   public:
    enum class LabelAnchor {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    enum class LabelFont {
        BigFont,
        ChatFont,
    };

    struct LabelConfig {
        bool m_enabled = false;

        LabelAnchor m_anchor = LabelAnchor::TopLeft;
        LabelFont m_font = LabelFont::ChatFont;
        std::string m_customFont;
        float m_opacity = 1.0f;
        float m_scale = 0.7f;
    };

    LabelConfig m_config;

   private:
    std::string m_id;
    std::string m_friendly;
    std::function<std::string(Label&)> m_display;

    std::string m_font = "chatFont.fnt";
    std::string m_loadedFont;
    CCPoint m_position = CCPoint(0.0f, 0.0f);
    CCPoint m_cocosAnchor = CCPoint(0.0f, 0.0f);

   public:
    Label() = default;
    Label(std::string id, std::string friendly,
          std::function<std::string(Label&)> display, LabelConfig cfg) {
        m_id = id;
        m_friendly = friendly;
        m_display = display;
        m_config = cfg;
    }
    CCNode* get();

    void calculatePosition(float& currentHeight, CCNode* label);
    void setColor(ccColor3B color);

    void update(bool forceDisable, bool refresh, float& currentHeight);

    const std::string& getId() const { return m_id; }

    const std::string& getFriendlyName() const { return m_friendly; }
};

class LabelManager {
   public:
    std::vector<Label> m_labels;
    bool m_requiresRefresh = false;
    bool m_globalEnabled = true;
    ConfigValuePtr<bool> m_globalEnabledValue = ConfigValue<bool>::create(
        "labels.global_enabled", &m_globalEnabled);

    template <typename F>
        requires std::is_invocable_r_v<std::string, F, Label&>
    void addLabel(std::string id, std::string friendly, F display,
                  Label::LabelConfig cfg) {
        m_labels.push_back(Label(id, friendly, display, cfg));
    }

    void readFromConfig();
    void writeToConfig();

    void update(bool forceDisable = false);
    LabelManager();
};

#endif  // LABEL_LABEL_HPP
