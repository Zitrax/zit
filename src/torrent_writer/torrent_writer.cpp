#include "arg_parser.hpp"

#include "bencode.hpp"
#include "logger.hpp"
#include "sha1.hpp"
#include "string_utils.hpp"
#include "strong_type.hpp"
#include "version.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <generator>
#include <iostream>
#include <iterator>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using TorrentFile = StrongType<std::string, struct TorrentFileTag>;
using DataPath = StrongType<std::string, struct DataPathTag>;
using Comment = StrongType<std::string, struct CommentTag>;
using AnnounceURL = StrongType<std::string, struct AnnounceURLTag>;

namespace rv = std::ranges::views;

/**
 * Get chunks for the pieces.
 *
 * @param is Input stream
 * @param n First chunk size
 * @param m Subsequent chunk size
 * @return std::generator<std::vector<char>>
 */
std::generator<zit::bytes> get_chunks(std::istream& is, size_t n, size_t m) {
  const auto read_chunk = [&](size_t size) {
    zit::bytes buf;
    char c;
    for (size_t i = 0; i < size && is.get(c); ++i) {
      buf.push_back(static_cast<std::byte>(c));
    }
    return buf;
  };

  // 1. Read the first chunk (size n)
  if (auto first = read_chunk(n); !first.empty()) {
    co_yield first;
  }

  // 2. Read subsequent chunks (size m)
  while (is) {
    if (auto next = read_chunk(m); !next.empty()) {
      co_yield next;
    } else {
      break;
    }
  }
}

void write_torrent(const TorrentFile& torrent_file,
                   const DataPath& data_path,
                   const Comment& comment,
                   const AnnounceURL& announce_url,
                   const uint32_t piece_length) {
  auto root = bencode::BeDict();
  root["announce"] = bencode::Element::build(announce_url.get());
  root["comment"] = bencode::Element::build(comment.get());
  root["creation date"] = bencode::Element::build(time(nullptr));
  root["created by"] = bencode::Element::build(
      fmt::format("Zit v{}.{}.{}", zit::MAJOR_VERSION, zit::MINOR_VERSION,
                  zit::PATCH_VERSION));
  root["encoding"] = bencode::Element::build("UTF-8");

  // info dict
  auto info = bencode::BeDict();
  std::stringstream pieces_ss;
  // single file
  if (std::filesystem::is_regular_file(data_path.get())) {
    const auto file_size = std::filesystem::file_size(data_path.get());
    zit::logger()->debug("Adding file: {} ({} bytes)", data_path.get(),
                         file_size);
    const auto name_str =
        std::filesystem::path(data_path.get()).filename().string();
    // Some clients include a UTF-8 variant of the filename in the info
    // dictionary (commonly `name.utf-8`). This is not required by the
    // core BitTorrent spec but is widely used by several clients to
    // preserve UTF-8 metadata. Include it here to improve compatibility
    // and to ensure generated torrents match existing ones that include
    // the same field.
    info["name"] = bencode::Element::build(name_str);
    info["name.utf-8"] = bencode::Element::build(name_str);
    info["length"] = bencode::Element::build(static_cast<int64_t>(file_size));
    // md5sum is optional - we skip it for now

    // Read file piecewise and add sha1 per piece
    std::ifstream file_stream(data_path.get(), std::ios::in | std::ios::binary);
    if (!file_stream) {
      throw std::runtime_error("Could not open data file for reading");
    }
    auto file_range =
        std::ranges::subrange(std::istreambuf_iterator<char>(file_stream),
                              std::istreambuf_iterator<char>());
    auto chunked_view = file_range | rv::chunk(piece_length);

    for (auto&& chunk : chunked_view) {
      auto data = std::ranges::to<std::string>(chunk);
      pieces_ss << zit::Sha1::calculateData(data).str();
    }

  } else if (std::filesystem::is_directory(data_path.get())) {
    // multi file
    const auto base_name =
        std::filesystem::path(data_path.get()).filename().string();
    info["name"] = bencode::Element::build(base_name);
    info["name.utf-8"] = bencode::Element::build(base_name);

    auto files = std::vector<bencode::ElmPtr>{};
    zit::bytes piece_data;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(data_path.get())) {
      if (std::filesystem::is_regular_file(entry.path())) {
        const auto file_size = std::filesystem::file_size(entry.path());
        zit::logger()->debug("Adding file: {} ({} bytes)",
                             entry.path().string(), file_size);

        // build path list relative to data_path
        std::vector<bencode::ElmPtr> path_list;
        for (const auto& part :
             std::filesystem::relative(entry.path(), data_path.get())) {
          path_list.emplace_back(bencode::Element::build(part.string()));
        }

        auto file_dict = bencode::BeDict();
        file_dict["length"] =
            bencode::Element::build(static_cast<int64_t>(file_size));
        file_dict["path"] = bencode::Element::build(path_list);
        file_dict["path.utf-8"] = bencode::Element::build(path_list);
        // md5sum is optional - we skip it for now

        files.emplace_back(bencode::Element::build(file_dict));

        // Now read data for sha1 pieces

        std::ifstream file_stream(entry.path(),
                                  std::ios::in | std::ios::binary);
        if (!file_stream) {
          throw std::runtime_error("Could not open data file for reading");
        }

        auto file_range =
            std::ranges::subrange(std::istreambuf_iterator<char>(file_stream),
                                  std::istreambuf_iterator<char>());

        auto chunk_generator = get_chunks(
            file_stream, piece_length - piece_data.size(), piece_length);
        for (auto&& chunk : chunk_generator) {
          piece_data.insert(piece_data.end(), chunk.begin(), chunk.end());
          if (piece_data.size() == piece_length) {
            pieces_ss << zit::Sha1::calculateData(piece_data).str();
            piece_data.clear();
          }
        }
      }
    }

    // Handle any remaining piece data
    if (!piece_data.empty()) {
      pieces_ss << zit::Sha1::calculateData(piece_data).str();
      piece_data.clear();
    }

    info["files"] = bencode::Element::build(files);

  } else {
    throw std::runtime_error(fmt::format(
        "Data path '{}' is neither an accessible file nor directory",
        data_path.get()));
  }

  // common info fields
  info["piece length"] = bencode::Element::build(piece_length);
  // the "private" field is optional - we skip it for now
  info["pieces"] = bencode::Element::build(pieces_ss.str());
  root["info"] = bencode::Element::build(info);

  const auto encoded = bencode::encode(root);
  std::ofstream torrent_stream(torrent_file.get(),
                               std::ios::out | std::ios::binary);
  if (!torrent_stream) {
    throw std::runtime_error("Could not open torrent file for writing");
  }
  torrent_stream << encoded;
}

}  // namespace

int main(int argc, const char* argv[]) noexcept {
  try {
    zit::ArgParser parser("Zit - torrent client");
    parser.add_option<bool>("--help")
        .aliases({"-h"})
        .help("Print help")
        .help_arg();
    parser.add_option<std::string>("--torrent")
        .help("Torrent file to download or write")
        .required();
    parser.add_option<std::string>("--comment")
        .help("Torrent file comment")
        .default_value("");
    parser.add_option<std::string>("--data")
        .help("Directory or file with torrent content")
        .required();
    parser.add_option<std::string>("--announce")
        .help(
            "Announce URL for the torrent. Currently only single URL is "
            "supported")
        .required();
    constexpr uint32_t DEFAULT_PIECE_LENGTH{256 * 1024};
    parser.add_option<uint32_t>("--piece-length")
        .help("Piece length in bytes (default is " +
              zit::bytesToHumanReadable(DEFAULT_PIECE_LENGTH) + ")")
        .default_value(DEFAULT_PIECE_LENGTH);
    const auto args = std::vector<std::string>{argv, std::next(argv, argc)};
    parser.parse(args);
    if (parser.get<bool>("--help")) {
      std::cout << parser.usage();
      return 0;
    }

    const auto torrent_file = parser.get<std::string>("--torrent");
    const auto data_path = parser.get<std::string>("--data");
    const auto comment = parser.get<std::string>("--comment");
    const auto announce_url = parser.get<std::string>("--announce");
    const auto piece_length = parser.get<uint32_t>("--piece-length");

    write_torrent(TorrentFile(torrent_file), DataPath(data_path),
                  Comment(comment), AnnounceURL(announce_url), piece_length);

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
