#include "../room/room_channel.hpp"
#include <cassert>
#include <future>
#include <thread>

using namespace wot::room;
Value message(const char* type, std::int64_t sequence) {
    auto v = object(); v.object["type"] = text(type); v.object["seq"] = integer(sequence); return v;
}
int main() {
    Channel c(4, 100000);
    auto first = freeze(message("bot_state", 1));
    assert(c.offer(first, "state:edge1") == Channel::Admission::admitted);
    assert(c.offer(freeze(message("bot_state", 2)), "state:edge1") == Channel::Admission::replaced);
    assert(c.offer(freeze(message("projectile_launch", 1))) == Channel::Admission::admitted);
    assert(c.offer(freeze(message("bot_state", 3)), "state:edge1") == Channel::Admission::admitted);
    assert(c.offer(freeze(message("bot_state", 4)), "state:edge2") == Channel::Admission::admitted);
    const auto before = c.metrics();
    assert(before.pending == 4 && before.bytes > 0 && before.replaced == 1);
    assert(c.offer(freeze(message("projectile_resolve", 1))) == Channel::Admission::refused);
    auto out = c.take(3);
    assert(out.size() == 3 && intField(*out[0].message, "seq") == 2);
    assert(out[1].message->get("type").text == "projectile_launch");
    assert(intField(*out[2].message, "seq") == 3);
    assert(intField(*first, "seq") == 1);
    assert(c.offer(freeze(message("projectile_resolve", 1))) == Channel::Admission::admitted);
    c.close();
    assert(c.offer(freeze(message("bot_state", 5))) == Channel::Admission::refused);
    out = c.take(8);
    assert(out.size() == 2 && out[1].message->get("type").text == "projectile_resolve");
    assert(!c.metrics().bytes && c.metrics().closed);

    Channel wake(16, 100000);
    std::promise<void> waiting;
    auto consumed = std::async(std::launch::async, [&] {
        waiting.set_value();
        assert(wake.waitUntil(Channel::Clock::now() + std::chrono::seconds(5)));
        auto rows = wake.take(1); assert(rows.size() == 1); return rows[0].message;
    });
    waiting.get_future().wait();
    assert(wake.offer(freeze(message("battle_result", 7))) == Channel::Admission::admitted);
    assert(intField(*consumed.get(), "seq") == 7);

    // A superseding payload keeps backlog history, but has its own receipt.
    Channel timing(2, 100000);
    timing.offer(first, "snapshot");
    const auto after_first = Channel::Clock::now();
    timing.offer(freeze(message("bot_state", 99)), "snapshot");
    const auto latest = timing.take(1);
    assert(latest[0].queued <= after_first && latest[0].received >= after_first);
    assert(latest[0].received >= latest[0].queued);

    // A shared static tree is charged once; cached charges retain the same
    // conservative byte bound without keeping an expired payload alive.
    Channel sized(4, 1000000);
    auto section = array(); for (int i = 0; i < 1000; ++i) section.array.push_back(integer(i));
    const auto immutable = freeze(std::move(section));
    auto envelope = message("snapshot", 1); Value ref; ref.kind = Value::Kind::Shared; ref.reference = immutable;
    envelope.object["manifest"] = ref;
    sized.offer(freeze(envelope), "snapshot"); const auto cold = sized.metrics();
    envelope.object["seq"] = integer(2); sized.offer(freeze(envelope), "snapshot"); const auto warm = sized.metrics();
    assert(warm.bytes == cold.bytes && warm.sizing_cache_hits > cold.sizing_cache_hits);
    assert(warm.sizing_visits - cold.sizing_visits < 10);

    auto view_source = object(); view_source.object["counter"] = integer(7); view_source.object["section"] = ref;
    auto view_owner = freeze(std::move(view_source)); std::weak_ptr<const Value> old_frame = view_owner;
    const auto view = objectView(view_owner); view_owner.reset();
    assert(old_frame.expired());
    assert(view.get("counter").kind == Value::Kind::Integer && view.get("section").reference == immutable);

    // Sharing is representation, not another logical nesting level. A cached
    // valid subtree must still be checked when it is moved deeper in a tree.
    auto deep = integer(0);
    for (unsigned i = 0; i < 120; ++i) { auto outer = array(); outer.array.push_back(std::move(deep)); deep = std::move(outer); }
    const auto deep_owner = freeze(std::move(deep)); Channel depth(4, 1000000);
    assert(depth.offer(deep_owner) == Channel::Admission::admitted);
    Value nested; nested.kind = Value::Kind::Shared; nested.reference = deep_owner;
    for (unsigned i = 0; i < 10; ++i) { auto outer = array(); outer.array.push_back(std::move(nested)); nested = std::move(outer); }
    assert(depth.offer(freeze(std::move(nested))) == Channel::Admission::refused);

    Channel small(2, 1);
    Channel::Refusal reason;
    assert(small.offer(first, {}, &reason) == Channel::Admission::refused);
    assert(reason == Channel::Refusal::message_size);
    assert(small.metrics().pending == 0);
    Channel capacity(1, 100000);
    assert(capacity.offer(first) == Channel::Admission::admitted);
    assert(capacity.offer(first, {}, &reason) == Channel::Admission::refused && reason == Channel::Refusal::capacity);
    capacity.close();
    assert(capacity.offer(first, {}, &reason) == Channel::Admission::refused && reason == Channel::Refusal::closed);
}
