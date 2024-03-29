#include "bencode.hpp"
#include "file_utils.hpp"
#include "gtest/gtest.h"

using namespace bencode;
using namespace std;
using namespace zit;
namespace fs = std::filesystem;

TEST(bencode, string) {
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

TEST(bencode, integers) {
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

TEST(bencode, lists) {
  auto v = std::vector<ElmPtr>();
  EXPECT_EQ(encode(v), "le");
  v.push_back(Element::build("spam"));
  v.push_back(Element::build("egg"));
  EXPECT_EQ(encode(v), "l4:spam3:egge");
  v.push_back(Element::build(99));
  EXPECT_EQ(encode(v), "l4:spam3:eggi99ee");
}

TEST(bencode, dict) {
  auto m = BeDict();
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

TEST(bencode, decode_int) {
  // No end marker
  EXPECT_THROW(std::ignore = decode("i3"), std::invalid_argument);
  // No number
  EXPECT_THROW(std::ignore = decode("ie"), std::invalid_argument);
  // Invalid number
  EXPECT_THROW(std::ignore = decode("iae"), std::invalid_argument);
  // Negative nothing
  EXPECT_THROW(std::ignore = decode("i-e"), std::invalid_argument);
  // OK
  // auto a = decode("i3e");
  // auto b = decode("i3e")->to<TypedElement<int64_t&>>();
  // std::cout << b;
  EXPECT_TRUE(decode("i3e")->is<TypedElement<int64_t>>());
  EXPECT_FALSE(decode("i3e")->is<TypedElement<int32_t>>());
  EXPECT_EQ(*decode("i3e")->to<TypedElement<int64_t>>(), 3);
  EXPECT_EQ(*decode("i-3e")->to<TypedElement<int64_t>>(), -3);
  EXPECT_EQ(*decode("i1234567890123456789e")->to<TypedElement<int64_t>>(),
            1234567890123456789);
  EXPECT_THROW(auto ret = *decode("i3e")->to<TypedElement<string>>(),
               BencodeConversionError);
}

TEST(bencode, decode_string) {
  // Longer than string
  EXPECT_THROW(std::ignore = decode("2:a"), std::invalid_argument);
  // Missing :
  EXPECT_THROW(std::ignore = decode("2aa"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("2"), std::invalid_argument);
  // OK
  EXPECT_EQ(*decode("4:spam")->to<TypedElement<string>>(), "spam"s);
  EXPECT_EQ(*decode("3:egg")->to<TypedElement<string>>(), "egg"s);
  EXPECT_EQ(*decode("0:")->to<TypedElement<string>>(), ""s);
}

TEST(bencode, decode_list) {
  // Invalid lists
  EXPECT_THROW(std::ignore = decode("l"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("lee"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("leee"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("leeee"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("lie"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("l4e"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("lle"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("lli3e"), std::invalid_argument);

  // []
  auto v = decode("le")->to<TypedElement<vector<ElmPtr>>>()->val();
  EXPECT_EQ(v.size(), 0);

  // [3]
  v = decode("li3ee")->to<TypedElement<vector<ElmPtr>>>()->val();
  EXPECT_EQ(v.size(), 1);
  auto i = *v[0]->to<TypedElement<int64_t>>();
  EXPECT_EQ(i, 3);

  // ["spam"]
  v = decode("l4:spame")->to<TypedElement<vector<ElmPtr>>>()->val();
  EXPECT_EQ(v.size(), 1);
  EXPECT_EQ(*v[0]->to<TypedElement<string>>(), "spam");

  // ["spam", "egg"]
  v = decode("l4:spam3:egge")->to<TypedElement<vector<ElmPtr>>>()->val();
  EXPECT_EQ(v.size(), 2);
  EXPECT_EQ(*v[0]->to<TypedElement<string>>(), "spam");
  EXPECT_EQ(*v[1]->to<TypedElement<string>>(), "egg");

  // ["spam", "egg", 99]
  v = decode("l4:spam3:eggi99ee")->to<TypedElement<vector<ElmPtr>>>()->val();
  EXPECT_EQ(v.size(), 3);
  EXPECT_EQ(*v[0]->to<TypedElement<string>>(), "spam");
  EXPECT_EQ(*v[1]->to<TypedElement<string>>(), "egg");
  EXPECT_EQ(*v[2]->to<TypedElement<int64_t>>(), 99);
}

TEST(bencode, decode_dict) {
  // Invalid dicts
  EXPECT_THROW(std::ignore = decode("d"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("dee"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("deee"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("deeee"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("die"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("d4e"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("dde"), std::invalid_argument);
  EXPECT_THROW(std::ignore = decode("ddi3e"), std::invalid_argument);

  // { "spam" => "egg" }
  auto m = decode("d4:spam3:egge")->to<TypedElement<BeDict>>()->val();
  EXPECT_EQ(m.size(), 1);
  auto v = m.at("spam");
  EXPECT_EQ(*v->to<TypedElement<std::string>>(), "egg");

  // { "cow" => "moo", "cows" => 7 }
  m = decode("d3:cow3:moo4:cowsi7ee")->to<TypedElement<BeDict>>()->val();
  EXPECT_EQ(m.size(), 2);
  v = m.at("cow");
  EXPECT_EQ(*v->to<TypedElement<std::string>>(), "moo");
  v = m.at("cows");
  EXPECT_EQ(*v->to<TypedElement<int64_t>>(), 7);
}

TEST(bencode, decode_recursion) {
  const auto data_dir = fs::path(DATA_DIR);
  auto s = read_file(data_dir / "bencode_recursion.txt");
  EXPECT_THROW(std::ignore = decode(s), std::invalid_argument);
}

TEST(bencode, decode_real) {
  const auto data_dir = fs::path(DATA_DIR);
  auto s = read_file(data_dir / "test.torrent");
  auto root = decode(s);

  // Torrent file specification expects a dict containing
  // * 'info' dict
  // * 'announce' (str) URL of tracker
  // * 'announce-list' (list<list<str>> optional)
  // * 'creation date' (int optional)
  // * 'comment' (str optional)
  // * 'created by' (str optional)
  // * 'encoding' (str optional)

  auto root_dict = root->to<TypedElement<BeDict>>()->val();

  // Verify required content
  EXPECT_TRUE(root_dict.find("info") != root_dict.end());
  EXPECT_TRUE(root_dict.find("announce") != root_dict.end());

  // Verify optional content for this specific torrent
  EXPECT_TRUE(root_dict.find("announce-list") != root_dict.end());
  EXPECT_TRUE(root_dict.find("creation date") != root_dict.end());
  EXPECT_TRUE(root_dict.find("comment") != root_dict.end());
  EXPECT_FALSE(root_dict.find("created by") != root_dict.end());
  EXPECT_FALSE(root_dict.find("encoding") != root_dict.end());

  // Verify info dict
  // - Common info
  // * piece length (int)
  // * pieces (str)
  // * private (int optional)
  // - Single File Mode ( Like this )
  // * name (str)
  // * length (int)
  // * md5sum (string optional)
  // - Multiple File Mode ( For multiple files )
  auto info = root_dict["info"]->to<TypedElement<BeDict>>()->val();
  EXPECT_TRUE(info.find("piece length") != info.end());
  EXPECT_TRUE(info.find("pieces") != info.end());
  EXPECT_FALSE(info.find("private") != info.end());
  EXPECT_TRUE(info.find("name") != info.end());
  EXPECT_TRUE(info.find("length") != info.end());
  EXPECT_FALSE(info.find("md5sum") != info.end());
  // Verify info content
  EXPECT_EQ(*info["piece length"]->to<TypedElement<int64_t>>(), 524288);
  auto pieces = info["pieces"]->to<TypedElement<string>>()->val();
  EXPECT_FALSE(pieces.empty());
  EXPECT_TRUE(pieces.size() % 20 == 0) << "Expected multiple of 20 bytes";
  EXPECT_EQ(*info["name"]->to<TypedElement<string>>(),
            "ubuntu-18.10-live-server-amd64.iso");
  EXPECT_EQ(*info["length"]->to<TypedElement<int64_t>>(), 923795456);

  // Verify content of the rest
  EXPECT_EQ(*root_dict["announce"]->to<TypedElement<string>>(),
            "http://torrent.ubuntu.com:6969/announce");
  EXPECT_EQ(*root_dict["creation date"]->to<TypedElement<int64_t>>(),
            1539860630);
  EXPECT_EQ(*root_dict["comment"]->to<TypedElement<string>>(),
            "Ubuntu CD releases.ubuntu.com");

  auto announce_list =
      root_dict["announce-list"]->to<TypedElement<BeList>>()->val();
  EXPECT_EQ(announce_list.size(), 2);
  auto announce_list_a = announce_list[0]->to<TypedElement<BeList>>()->val();
  auto announce_list_b = announce_list[1]->to<TypedElement<BeList>>()->val();
  EXPECT_EQ(announce_list_a.size(), 1);
  EXPECT_EQ(announce_list_b.size(), 1);
  EXPECT_EQ(*announce_list_a[0]->to<TypedElement<string>>(),
            "http://torrent.ubuntu.com:6969/announce");
  EXPECT_EQ(*announce_list_b[0]->to<TypedElement<string>>(),
            "http://ipv6.torrent.ubuntu.com:6969/announce");
}
