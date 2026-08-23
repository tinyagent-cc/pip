#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "body.hpp"
#include "fake_body.hpp"
using namespace pip::brain;

TEST_CASE("HttpBody speaks protocol v0 to a fake Pip") {
    FakePip pip; pip.senses = {12.5, 28.0, false, true};
    HttpBody body(pip.url());
    CHECK(body.express("wink"));
    CHECK(body.chirp("rise"));
    CHECK(body.led(1, 2, 3));
    Senses s = body.senses();
    CHECK(s.ok); CHECK(s.light_lux == doctest::Approx(12.5)); CHECK(s.temp_c == doctest::Approx(28.0)); CHECK_FALSE(s.button_down);
    CHECK(pip.calls == std::vector<std::string>{"express:wink", "chirp:rise", "led:1,2,3", "senses"});
}
TEST_CASE("HttpBody reports 400 and unreachable as false") {
    FakePip pip;
    HttpBody body(pip.url());
    CHECK_FALSE(body.express("angry"));   // not a v0 emotion
    HttpBody dead("http://127.0.0.1:9", 200);
    CHECK_FALSE(dead.express("wink"));
    CHECK_FALSE(dead.senses().ok);
}
