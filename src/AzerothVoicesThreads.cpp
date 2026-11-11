#include "AzerothVoicesThreads.h"

#include <algorithm>
#include <sstream>

namespace AzerothVoices
{
    char const* ThreadMoveName(ThreadMove move)
    {
        switch (move)
        {
            case ThreadMove::ContinueTopic:   return "continue";
            case ThreadMove::DriftTopic:      return "drift";
            case ThreadMove::CallbackToPrior: return "callback";
            case ThreadMove::StartNewTopic:   return "new_topic";
            default:                          return "unknown";
        }
    }

    ConversationThread* ThreadManager::GetOrCreateThread(std::string const& key, std::string const& initialTopic)
    {
        if (key.empty())
            return nullptr;

        auto it = m_threads.find(key);
        if (it != m_threads.end())
            return &it->second;

        ConversationThread& thread = m_threads[key];
        thread.topic = initialTopic;
        thread.energy = 0.9f;
        thread.exchangeCount = 0;
        thread.lastTouch = std::chrono::steady_clock::now();
        thread.isInterrupted = false;
        return &thread;
    }

    ConversationThread* ThreadManager::FindThread(std::string const& key)
    {
        if (key.empty())
            return nullptr;
        auto it = m_threads.find(key);
        return it != m_threads.end() ? &it->second : nullptr;
    }

    ConversationThread const* ThreadManager::FindThread(std::string const& key) const
    {
        if (key.empty())
            return nullptr;
        auto it = m_threads.find(key);
        return it != m_threads.end() ? &it->second : nullptr;
    }

    void ThreadManager::RecordExchange(std::string const& key, std::string const& speaker,
                                       std::string const& /*message*/, std::string const& newOpenPoint)
    {
        ConversationThread* thread = FindThread(key);
        if (!thread)
            return;

        thread->lastTouch = std::chrono::steady_clock::now();
        ++thread->exchangeCount;

        if (!newOpenPoint.empty())
            thread->openPoint = newOpenPoint;

        if (!speaker.empty())
        {
            // Update speaker touch
            thread->speakerFeelings[speaker] = "engaged";
        }
    }

    void ThreadManager::DecayEnergy(std::string const& key, float decayRate)
    {
        ConversationThread* thread = FindThread(key);
        if (!thread)
            return;

        float const rate = std::clamp(decayRate, 0.10f, 0.99f);
        thread->energy = std::max(0.10f, thread->energy * rate);
    }

    ThreadMove ThreadManager::DecideThreadMove(std::string const& key, uint32_t highThreshold,
                                              uint32_t lowThreshold, uint32_t roll100)
    {
        ConversationThread* thread = FindThread(key);
        if (!thread || thread->topic.empty())
            return ThreadMove::StartNewTopic;

        float const highFrac = static_cast<float>(highThreshold) / 100.0f;
        float const lowFrac = static_cast<float>(lowThreshold) / 100.0f;
        uint32_t const roll = roll100 % 100;

        ThreadMove chosen = ThreadMove::ContinueTopic;

        if (thread->energy >= highFrac)
        {
            // High Energy: 70% continue, 20% drift, 5% callback, 5% new
            if (roll < 70)
                chosen = ThreadMove::ContinueTopic;
            else if (roll < 90)
                chosen = ThreadMove::DriftTopic;
            else if (roll < 95)
                chosen = ThreadMove::CallbackToPrior;
            else
                chosen = ThreadMove::StartNewTopic;
        }
        else if (thread->energy >= lowFrac)
        {
            // Mid Energy: 35% continue, 35% drift, 15% callback, 15% new
            if (roll < 35)
                chosen = ThreadMove::ContinueTopic;
            else if (roll < 70)
                chosen = ThreadMove::DriftTopic;
            else if (roll < 85)
                chosen = ThreadMove::CallbackToPrior;
            else
                chosen = ThreadMove::StartNewTopic;
        }
        else
        {
            // Low Energy: 10% continue, 25% drift, 25% callback, 40% new
            if (roll < 10)
                chosen = ThreadMove::ContinueTopic;
            else if (roll < 35)
                chosen = ThreadMove::DriftTopic;
            else if (roll < 60)
                chosen = ThreadMove::CallbackToPrior;
            else
                chosen = ThreadMove::StartNewTopic;
        }

        // If callback is chosen but we have no history, fallback to drift or continue
        if (chosen == ThreadMove::CallbackToPrior && thread->topicHistory.empty())
        {
            chosen = (thread->energy >= lowFrac) ? ThreadMove::ContinueTopic : ThreadMove::StartNewTopic;
        }

        // If new topic is chosen, record current topic into history before clearing
        if (chosen == ThreadMove::StartNewTopic && !thread->topic.empty())
        {
            if (std::find(thread->topicHistory.begin(), thread->topicHistory.end(), thread->topic) == thread->topicHistory.end())
            {
                thread->topicHistory.push_front(thread->topic);
                while (thread->topicHistory.size() > 5)
                    thread->topicHistory.pop_back();
            }
            thread->energy = 0.9f;
            thread->exchangeCount = 0;
            thread->openPoint.clear();
        }

        return chosen;
    }

    std::string ThreadManager::BuildThreadPromptGuidance(std::string const& key, ThreadMove move) const
    {
        ConversationThread const* thread = FindThread(key);
        if (!thread || thread->topic.empty())
            return "";

        std::ostringstream oss;
        oss << "CONVERSATION THREAD CONTEXT: Current topic is '" << thread->topic << "'.";

        if (!thread->openPoint.empty())
            oss << " Unresolved discussion point: '" << thread->openPoint << "'.";

        switch (move)
        {
            case ThreadMove::ContinueTopic:
                oss << " Directive: Continue developing this exact topic naturally. Deepen the banter.";
                break;
            case ThreadMove::DriftTopic:
                oss << " Directive: Naturally transition from the current topic to a related adjacent subject.";
                break;
            case ThreadMove::CallbackToPrior:
                if (!thread->topicHistory.empty())
                    oss << " Directive: Briefly make a natural callback or reference to earlier topic '"
                        << thread->topicHistory.front() << "'.";
                else
                    oss << " Directive: Continue the conversation with a subtle shift in tone.";
                break;
            case ThreadMove::StartNewTopic:
                oss << " Directive: Wrap up this topic with a quick quip and introduce a fresh observation or thought.";
                break;
        }

        return oss.str();
    }

    void ThreadManager::InterruptThread(std::string const& key, std::string const& reason)
    {
        ConversationThread* thread = FindThread(key);
        if (!thread)
            return;
        thread->isInterrupted = true;
        thread->interruptReason = reason;
        ++thread->majorEventRev;
    }

    void ThreadManager::ResumeThread(std::string const& key)
    {
        ConversationThread* thread = FindThread(key);
        if (!thread)
            return;
        thread->isInterrupted = false;
        thread->lastTouch = std::chrono::steady_clock::now();
    }

    void ThreadManager::PruneIdleThreads(std::chrono::steady_clock::time_point now, uint32_t maxTtlSeconds)
    {
        for (auto it = m_threads.begin(); it != m_threads.end();)
        {
            auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastTouch).count();
            if (elapsed > static_cast<int64_t>(maxTtlSeconds))
                it = m_threads.erase(it);
            else
                ++it;
        }
    }

    void ThreadManager::ClearThread(std::string const& key)
    {
        m_threads.erase(key);
    }

    void ThreadManager::ClearAll()
    {
        m_threads.clear();
    }

    size_t ThreadManager::Size() const
    {
        return m_threads.size();
    }
}
