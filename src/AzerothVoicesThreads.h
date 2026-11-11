#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace AzerothVoices
{
    // The next conversational move decided by the thread state machine
    enum class ThreadMove : uint8_t
    {
        ContinueTopic,
        DriftTopic,
        CallbackToPrior,
        StartNewTopic
    };

    char const* ThreadMoveName(ThreadMove move);

    // Persistent state of a conversation thread across multiple turns
    struct ConversationThread
    {
        std::string topic;
        std::string openPoint;
        float energy = 0.9f;
        uint32_t exchangeCount = 0;
        std::deque<std::string> topicHistory; // Capped queue of previously finished topics for callbacks
        std::unordered_map<std::string, std::string> speakerFeelings; // Speaker name -> lingering emotional reaction
        std::chrono::steady_clock::time_point lastTouch{};
        bool isInterrupted = false;
        std::string interruptReason;
        uint32_t majorEventRev = 0;
    };

    class ThreadManager
    {
    public:
        ConversationThread* GetOrCreateThread(std::string const& key, std::string const& initialTopic = "");
        ConversationThread* FindThread(std::string const& key);
        ConversationThread const* FindThread(std::string const& key) const;

        void RecordExchange(std::string const& key, std::string const& speaker, std::string const& message,
                            std::string const& newOpenPoint = "");
        void DecayEnergy(std::string const& key, float decayRate = 0.70f);
        ThreadMove DecideThreadMove(std::string const& key, uint32_t highThreshold = 60,
                                   uint32_t lowThreshold = 30, uint32_t roll100 = 0);
        std::string BuildThreadPromptGuidance(std::string const& key, ThreadMove move) const;

        void InterruptThread(std::string const& key, std::string const& reason = "combat");
        void ResumeThread(std::string const& key);
        void PruneIdleThreads(std::chrono::steady_clock::time_point now, uint32_t maxTtlSeconds = 1800);
        void ClearThread(std::string const& key);
        void ClearAll();
        size_t Size() const;

    private:
        std::unordered_map<std::string, ConversationThread> m_threads;
    };
}
