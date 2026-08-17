#include "mcd/core/json.hpp"

#include <gtest/gtest.h>

using mcd::json::Object;
using mcd::json::ParseError;
using mcd::json::Value;

TEST(Json, ParsesFlatObjectOfAllSupportedTypes) {
    const auto obj = mcd::json::parse_object(
        R"({"a": 1, "b": "hello", "c": true, "d": false, "e": -3.5e2})");
    ASSERT_EQ(obj.size(), 5U);
    EXPECT_EQ(mcd::json::find(obj, "a")->as_number(), 1.0);
    EXPECT_EQ(mcd::json::find(obj, "b")->as_string(), "hello");
    EXPECT_EQ(mcd::json::find(obj, "c")->as_bool(), true);
    EXPECT_EQ(mcd::json::find(obj, "d")->as_bool(), false);
    EXPECT_EQ(mcd::json::find(obj, "e")->as_number(), -350.0);
}

TEST(Json, ParsesEmptyObject) {
    const auto obj = mcd::json::parse_object("{}");
    EXPECT_TRUE(obj.empty());
}

TEST(Json, IgnoresWhitespaceAroundTokens) {
    const auto obj = mcd::json::parse_object("  {  \"a\"  :  1  ,  \"b\" : 2 }  ");
    ASSERT_EQ(obj.size(), 2U);
    EXPECT_EQ(mcd::json::find(obj, "a")->as_number(), 1.0);
    EXPECT_EQ(mcd::json::find(obj, "b")->as_number(), 2.0);
}

TEST(Json, FindReturnsNullptrForMissingKey) {
    const auto obj = mcd::json::parse_object(R"({"a": 1})");
    EXPECT_EQ(mcd::json::find(obj, "missing"), nullptr);
}

TEST(Json, ParsesEscapeSequencesInStrings) {
    const auto obj = mcd::json::parse_object(R"({"a": "line1\nline2\ttab\\slash\"quote"})");
    EXPECT_EQ(mcd::json::find(obj, "a")->as_string(), "line1\nline2\ttab\\slash\"quote");
}

TEST(Json, RejectsNestedObjects) {
    EXPECT_THROW((void)mcd::json::parse_object(R"({"a": {"b": 1}})"), ParseError);
}

TEST(Json, RejectsArrays) {
    EXPECT_THROW((void)mcd::json::parse_object(R"({"a": [1, 2]})"), ParseError);
}

TEST(Json, RejectsUnicodeEscapesAsOutOfScope) {
    EXPECT_THROW((void)mcd::json::parse_object("{\"a\": \"\\u0041\"}"), ParseError);
}

TEST(Json, RejectsMalformedSyntax) {
    EXPECT_THROW((void)mcd::json::parse_object("{"), ParseError);
    EXPECT_THROW((void)mcd::json::parse_object(R"({"a" 1})"), ParseError);
    EXPECT_THROW((void)mcd::json::parse_object(R"({"a": 1,})"), ParseError);
    EXPECT_THROW((void)mcd::json::parse_object("not json"), ParseError);
    EXPECT_THROW((void)mcd::json::parse_object(R"({"a": 1} trailing)"), ParseError);
}

TEST(Json, SerializeRoundTripsThroughParse) {
    Object obj;
    obj.emplace_back("price", Value::from_number(10.4506));
    obj.emplace_back("label", Value::from_string("call \"ATM\""));
    obj.emplace_back("ok", Value::from_bool(true));

    const std::string text = mcd::json::serialize(obj);
    const auto reparsed = mcd::json::parse_object(text);

    ASSERT_EQ(reparsed.size(), 3U);
    EXPECT_NEAR(mcd::json::find(reparsed, "price")->as_number(), 10.4506, 1e-9);
    EXPECT_EQ(mcd::json::find(reparsed, "label")->as_string(), "call \"ATM\"");
    EXPECT_EQ(mcd::json::find(reparsed, "ok")->as_bool(), true);
}

TEST(Json, SerializePreservesInsertionOrder) {
    Object obj;
    obj.emplace_back("z", Value::from_number(1));
    obj.emplace_back("a", Value::from_number(2));
    EXPECT_EQ(mcd::json::serialize(obj), R"({"z":1,"a":2})");
}

TEST(Json, NumberAccessorThrowsOnTypeMismatch) {
    const auto obj = mcd::json::parse_object(R"({"a": "not a number"})");
    EXPECT_THROW((void)mcd::json::find(obj, "a")->as_number(), ParseError);
}
