#include "clock.hpp"

void ScheduledJob::update(const float dt) {
    m_lastExecuted += dt;

    if (m_lastExecuted < m_interval) return;

    m_lastExecuted = 0.0;
    m_executor();
    m_stale = !m_repeat;
}

GameScheduler::JobId GameScheduler::schedule(const JobExecutor& executor,
                                           const double interval,
                                           const bool repeat) {
    m_nextFreeId++;

    m_jobs.insert({m_nextFreeId,
                   {
                       .m_interval = interval,
                       .m_repeat = repeat,
                       .m_executor = executor,
                   }});

    return m_nextFreeId;
}

void GameScheduler::unschedule(const JobId id) {
    if (!m_jobs.contains(id)) {
        return;
    }

    m_jobs.erase(id);
}

void GameScheduler::reschedule(const JobId id, const double interval) {
    if (!m_jobs.contains(id)) {
        return;
    }

    m_jobs[id].m_interval = interval;
}

void GameScheduler::update(const float dt) {
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        it->second.update(dt);
        if (it->second.m_stale) {
            it = m_jobs.erase(it);
        } else {
            ++it;
        }
    }
}
