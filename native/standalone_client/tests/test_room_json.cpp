#include "../room/room_json.hpp"
#include <cassert>
#include <limits>

using namespace wot::room;
int main() {
    const auto config = parseJson(R"({"schema":1,"port":28792,"loopback":true,"name":"\u623f\u95f4\ud83d\ude80","lineup":[null,-1,1.25,1e-3],"large":18446744073709551615,"signed":-9223372036854775808})");
    assert(intField(config, "port") == 28792);
    assert(config.get("loopback").truth());
    assert(config.get("name").text == "房间🚀");
    assert(config.get("large").unsigned_integer);
    assert(config.get("signed").integer == std::numeric_limits<std::int64_t>::min());
    assert(parseJson(writeJson(config)) == config);
    for(double value:{0.,-0.,1.,-42.,1e20}) {
        const auto restored=parseJson(writeJson(number(value)));
        assert(restored.kind==Value::Kind::Number);
        assert(restored.number==value);
    }
    for (const char* bad : {"", "[1,]", "{\"x\":1,}", "01", "1.", "1e", "true null",
            "{\"x\":1,\"x\":2}", "\"\\ud800\"", "\"\\udc00\"", "1e9999",
            "18446744073709551616", "-9223372036854775809", "\"\\x00\""}) {
        bool failed = false;
        try { (void)parseJson(bad); } catch (const std::invalid_argument&) { failed = true; }
        assert(failed);
    }
    auto unsafe = number(std::numeric_limits<double>::infinity());
    bool failed = false;
    try { (void)writeJson(unsafe); } catch (const std::invalid_argument&) { failed = true; }
    assert(failed);
    auto control = text(std::string("\0\t\r\n\\\"", 6));
    assert(parseJson(writeJson(control)) == control);
}
