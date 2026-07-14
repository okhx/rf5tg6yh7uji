#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>

using namespace geode::prelude;

#include "bot/bot.hpp"
#include "util/crash_log.hpp"

$on_mod(Loaded) {
    crash_log::install();
    Bot::get()->initialize();
}
