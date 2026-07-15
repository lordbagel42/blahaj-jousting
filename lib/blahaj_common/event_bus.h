// lib/blahaj_common/event_bus.h
#pragma once
#include <functional>
#include <map>
#include <vector>

template <typename EventType, typename ContextType>
class EventBus {
public:
    using Handler = std::function<void(const ContextType&)>;

    void on(EventType event, Handler handler) {
        _handlers[static_cast<int>(event)].push_back(std::move(handler));
    }

    void emit(EventType event, const ContextType& ctx) const {
        auto it = _handlers.find(static_cast<int>(event));
        if (it == _handlers.end()) return;
        for (const auto& h : it->second) h(ctx);
    }

    int handlerCount(EventType event) const {
        auto it = _handlers.find(static_cast<int>(event));
        return it == _handlers.end() ? 0 : static_cast<int>(it->second.size());
    }

private:
    std::map<int, std::vector<Handler>> _handlers;
};
