#include <functional>
#include <string>

class MenuTab {
    std::string m_name;
    std::function<void()> m_bodyFn;

    MenuTab(std::string name) : m_name(name) {}

    MenuTab& body(std::function<void()> body) {
        m_bodyFn = body;
        return *this;
    }

    void draw() { m_bodyFn(); }
};
