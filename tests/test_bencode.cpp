#include "gtest/gtest.h"
#include "bencode.h"

using namespace bencode;
using namespace std;

TEST(bencode, string)
{
    EXPECT_EQ(encode(std::string("spam")), "4:spam");
    EXPECT_EQ(encode("spam"), "4:spam");
    EXPECT_EQ(encode("spam"s), "4:spam");

    EXPECT_EQ(encode(std::string("egg")), "3:egg");
    EXPECT_EQ(encode("egg"), "3:egg");
    EXPECT_EQ(encode("egg"s), "3:egg");

    EXPECT_EQ(encode("0"), "1:0");
    EXPECT_EQ(encode("0"s), "1:0");

    EXPECT_EQ(encode(""), "0:");
    EXPECT_EQ(encode(""s), "0:");
}

TEST(bencode, integers)
{
    // short
    EXPECT_EQ(encode(static_cast<short>(3)), "i3e");
    EXPECT_EQ(encode(static_cast<short>(-3)), "i-3e");

    // unsigned short
    EXPECT_EQ(encode(static_cast<unsigned short>(3)), "i3e");

    // int
    EXPECT_EQ(encode(0), "i0e");
    EXPECT_EQ(encode(3), "i3e");
    EXPECT_EQ(encode(-3), "i-3e");

    // unsigned int
    EXPECT_EQ(encode(3U), "i3e");

    // long
    EXPECT_EQ(encode(3L), "i3e");
    EXPECT_EQ(encode(-3L), "i-3e");

    // unsigned long
    EXPECT_EQ(encode(3UL), "i3e");

    // long long
    EXPECT_EQ(encode(3LL), "i3e");
    EXPECT_EQ(encode(-3LL), "i-3e");

    // unsigned long long
    EXPECT_EQ(encode(3ULL), "i3e");
    EXPECT_EQ(encode(12345678901234567890ULL), "i12345678901234567890e");
}

TEST(bencode, lists)
{
    auto v = std::vector<ElmPtr>();
    EXPECT_EQ(encode(v), "le");
    v.push_back(Element::build("spam"));
    v.push_back(Element::build("egg"));
    EXPECT_EQ(encode(v), "l4:spam3:egge");
    v.push_back(Element::build(99));
    EXPECT_EQ(encode(v), "l4:spam3:eggi99ee");
}

TEST(bencode, map)
{
    auto m = BencodeMap();
    EXPECT_EQ(encode(m), "de");
    m["cow"] = Element::build("moo");
    m["spam"] = Element::build("eggs");
    EXPECT_EQ(encode(m), "d3:cow3:moo4:spam4:eggse");
    m.clear();
    auto v = std::vector<ElmPtr>();
    v.push_back(Element::build("a"));
    v.push_back(Element::build("b"));
    m["spam"] = Element::build(v);
    EXPECT_EQ(encode(m), "d4:spaml1:a1:bee");
}

TEST(bencode, decode_int)
{
    // No end marker
    EXPECT_THROW(decode("i3"), std::invalid_argument);
    // No number
    EXPECT_THROW(decode("ie"), std::invalid_argument);
    // Invalid number
    EXPECT_THROW(decode("iae"), std::invalid_argument);
    // Negative nothing
    EXPECT_THROW(decode("i-e"), std::invalid_argument);
    // OK
    // auto a = decode("i3e");
    // auto b = decode("i3e")->to<TypedElement<int64_t&>>();
    // std::cout << b;
    EXPECT_EQ(*decode("i3e")->to<TypedElement<int64_t>>(), 3);
    EXPECT_EQ(*decode("i-3e")->to<TypedElement<int64_t>>(), -3);
    EXPECT_EQ(*decode("i1234567890123456789e")->to<TypedElement<int64_t>>(), 1234567890123456789);
}

TEST(bencode, decode_string)
{
    // Longer than string
    EXPECT_THROW(decode("2:a"), std::invalid_argument);
    // Missing :
    EXPECT_THROW(decode("2aa"), std::invalid_argument);
    EXPECT_THROW(decode("2"), std::invalid_argument);
    // OK

    // FIXME: This is uglier than the int.
    //        Why can't operator T() be used like in the int case?
    EXPECT_EQ(decode("4:spam")->to<TypedElement<string>>()->val(), "spam"s);
    EXPECT_EQ(decode("3:egg")->to<TypedElement<string>>()->val(), "egg"s);
    EXPECT_EQ(decode("0:")->to<TypedElement<string>>()->val(), ""s);
}

TEST(bencode, decode_vector)
{
    // FIXME: Tets some invalid strings

    // []
    auto v = decode("le")->to<TypedElement<vector<ElmPtr>>>()->val();
    EXPECT_EQ(v.size(), 0);

    // [3]
    v = decode("li3ee")->to<TypedElement<vector<ElmPtr>>>()->val();
    EXPECT_EQ(v.size(), 1);
    auto i = *v[0]->to<TypedElement<int64_t>>();
    EXPECT_EQ(i, 3);

}
