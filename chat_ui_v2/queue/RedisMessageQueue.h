#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <hiredis/hiredis.h>
#include <muduo/base/Logging.h>
#include <sw/redis++/redis++.h>

#include "dao/MessageDao.h"

namespace chatui_v2 {
namespace queue {

struct MessageWriteEvent {
    std::string eventId;
    int64_t conversationId = 0;
    std::string role;
    std::string content;
};

class RedisMessagePublisher {
public:
    RedisMessagePublisher(const std::string& redisUri, std::string streamKey)
        : redis_(redisUri), streamKey_(std::move(streamKey)) {}

    std::string publish(const MessageWriteEvent& e) {
        std::vector<std::pair<std::string, std::string>> fields{
            {"event_id", e.eventId},
            {"conversation_id", std::to_string(e.conversationId)},
            {"role", e.role},
            {"content", e.content},
            {"ts", std::to_string(nowMs())}
        };
        return redis_.xadd(streamKey_, "*", fields.begin(), fields.end());
    }

private:
    static int64_t nowMs() {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    }

private:
    sw::redis::Redis redis_;
    std::string streamKey_;
};

class RedisMessageConsumer {
public:
    RedisMessageConsumer(const std::string& redisUri,
                         std::string streamKey,
                         std::string group,
                         std::string consumer,
                         int blockMs = 2000,
                         int batchCount = 64)
        : redis_(redisUri)
        , streamKey_(std::move(streamKey))
        , group_(std::move(group))
        , consumer_(std::move(consumer))
        , blockMs_(blockMs)
        , batchCount_(batchCount) {}

    ~RedisMessageConsumer() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        ensureGroup();
        worker_ = std::thread([this]() { run(); });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
    }

private:
    void ensureGroup() {
        try {
            redis_.xgroup_create(streamKey_, group_, "0", true);
            LOG_INFO << "[RedisMessageConsumer] created group: " << group_;
        } catch (const std::exception& e) {
            const std::string msg = e.what();
            if (msg.find("BUSYGROUP") == std::string::npos) {
                throw;
            }
        }
    }

    void run() {
        while (running_) {
            try {
                // Drain pending entries owned by this consumer first.
                consumeWithId("0", std::chrono::milliseconds(1));
                // Then process new messages.
                consumeWithId(">", std::chrono::milliseconds(blockMs_));
            } catch (const std::exception& e) {
                LOG_ERROR << "[RedisMessageConsumer] loop error: " << e.what();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
    }

    void consumeWithId(const std::string& id, const std::chrono::milliseconds& timeout) {
        auto reply = redis_.command(
            "XREADGROUP",
            "GROUP", group_, consumer_,
            "COUNT", std::to_string(batchCount_),
            "BLOCK", std::to_string(timeout.count()),
            "STREAMS", streamKey_, id);
        if (!reply) return;
        auto* root = reply.get();
        if (!root || root->type == REDIS_REPLY_NIL || root->elements == 0) return;
        parseAndProcess(root);
    }

    void parseAndProcess(redisReply* root) {
        // XREADGROUP reply:
        // [ [ stream, [ [id, [field, value, ...]], ... ] ] ]
        if (root->type != REDIS_REPLY_ARRAY) return;
        for (size_t i = 0; i < root->elements; ++i) {
            redisReply* streamArr = root->element[i];
            if (!streamArr || streamArr->type != REDIS_REPLY_ARRAY || streamArr->elements < 2) continue;
            redisReply* messagesArr = streamArr->element[1];
            if (!messagesArr || messagesArr->type != REDIS_REPLY_ARRAY) continue;

            for (size_t j = 0; j < messagesArr->elements; ++j) {
                redisReply* msgArr = messagesArr->element[j];
                if (!msgArr || msgArr->type != REDIS_REPLY_ARRAY || msgArr->elements < 2) continue;
                redisReply* idReply = msgArr->element[0];
                redisReply* fieldsReply = msgArr->element[1];
                if (!idReply || idReply->type != REDIS_REPLY_STRING) continue;
                if (!fieldsReply || fieldsReply->type != REDIS_REPLY_ARRAY) continue;

                MessageWriteEvent ev;
                for (size_t k = 0; k + 1 < fieldsReply->elements; k += 2) {
                    auto* f = fieldsReply->element[k];
                    auto* v = fieldsReply->element[k + 1];
                    if (!f || !v || f->type != REDIS_REPLY_STRING || v->type != REDIS_REPLY_STRING) continue;
                    const std::string field(f->str, f->len);
                    const std::string value(v->str, v->len);
                    if (field == "event_id") ev.eventId = value;
                    else if (field == "conversation_id") {
                        try { ev.conversationId = std::stoll(value); } catch (...) { ev.conversationId = 0; }
                    } else if (field == "role") ev.role = value;
                    else if (field == "content") ev.content = value;
                }

                bool ok = false;
                if (!ev.eventId.empty() && ev.conversationId > 0 && !ev.role.empty()) {
                    ok = chatui_v2::dao::MessageDao::insertWithEventId(
                        ev.conversationId, ev.role, ev.content, ev.eventId) >= 0;
                }

                if (ok) {
                    const std::string msgId(idReply->str, idReply->len);
                    redis_.xack(streamKey_, group_, msgId);
                }
            }
        }
    }

private:
    sw::redis::Redis redis_;
    std::string streamKey_;
    std::string group_;
    std::string consumer_;
    int blockMs_ = 2000;
    int batchCount_ = 64;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace queue
}  // namespace chatui_v2
