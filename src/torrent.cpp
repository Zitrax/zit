// -*- mode:c++; c-basic-offset : 2; -*-
#include "torrent.h"
#include "bencode.h"
#include "file_utils.h"
#include "peer.h"
#include "sha1.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <iostream>
#include <numeric>

using namespace bencode;
using namespace std;

namespace zit {

/**
 * To make transform calls more readable
 */
template <class In, class Out, class Op>
static auto transform_all(const In& in, Out& out, Op func) {
  return transform(begin(in), end(in), back_inserter(out), func);
}

/**
 * Convenience function for converting a known BeDict element for the multi
 * file "files" part.
 */
static auto beDictToFileInfo(const Element& element) {
  const auto& dict = element.template to<TypedElement<BeDict>>()->val();
  string md5;
  if (dict.find("md5sum") != dict.end()) {
    md5 = dict.at("md5sum")->template to<TypedElement<string>>()->val();
  }
  auto path_list = dict.at("path")->template to<TypedElement<BeList>>()->val();
  filesystem::path path;
  for (const auto& elm : path_list) {
    path /= elm->template to<TypedElement<string>>()->val();
  }
  return FileInfo(
      dict.at("length")->template to<TypedElement<int64_t>>()->val(), path,
      md5);
}

Torrent::Torrent(const filesystem::path& file) {
  m_logger = spdlog::get("console");
  // TODO: tmpnam is not fully safe - there is no replacement in std though so
  //       would have to be manually implemented.
  m_tmpfile = filesystem::path(tmpnam(nullptr));
  m_logger->debug("Using tmpfile {} for {}", m_tmpfile, file);
  auto root = bencode::decode(read_file(file));

  const auto& root_dict = root->to<TypedElement<BeDict>>()->val();

  // Required
  m_announce = root_dict.at("announce")->to<TypedElement<string>>()->val();
  const auto& info = root_dict.at("info")->to<TypedElement<BeDict>>()->val();

  m_name = info.at("name")->to<TypedElement<string>>()->val();
  m_pieces = info.at("pieces")->to<TypedElement<string>>()->val();
  m_piece_length = numeric_cast<uint32_t>(
      info.at("piece length")->to<TypedElement<int64_t>>()->val());

  // Either 'length' or 'files' is needed depending on mode
  if (info.find("length") != info.end()) {
    m_length = info.at("length")->to<TypedElement<int64_t>>()->val();
  }
  if (info.find("files") != info.end()) {
    if (m_length != 0) {
      throw runtime_error("Invalid torrent: dual mode");
    }
    transform_all(info.at("files")->to<TypedElement<BeList>>()->val(), m_files,
                  [](const auto& dict) { return beDictToFileInfo(*dict); });
  }
  if (m_length == 0 && m_files.empty()) {
    throw runtime_error("Invalid torrent: no mode");
  }

  auto has = [&root_dict](const auto& key) {
    return root_dict.find(key) != root_dict.end();
  };

  // Optional
  if (has("creation date")) {
    m_creation_date =
        root_dict.at("creation date")->to<TypedElement<int64_t>>()->val();
  }
  if (has("comment")) {
    m_comment = root_dict.at("comment")->to<TypedElement<string>>()->val();
  }
  if (has("created by")) {
    m_created_by =
        root_dict.at("created by")->to<TypedElement<string>>()->val();
  }
  if (has("encoding")) {
    m_encoding = root_dict.at("encoding")->to<TypedElement<string>>()->val();
  }
  if (has("md5sum")) {
    m_md5sum = root_dict.at("md5sum")->to<TypedElement<string>>()->val();
  }
  if (has("private")) {
    m_private =
        root_dict.at("private")->to<TypedElement<int64_t>>()->val() == 1;
  }
  if (has("announce-list")) {
    const auto& announce_list =
        root_dict.at("announce-list")->to<TypedElement<BeList>>()->val();

    transform_all(announce_list, m_announce_list, [](const auto& tier) {
      vector<string> tier_output_list;
      transform_all(tier->template to<TypedElement<BeList>>()->val(),
                    tier_output_list, [](const auto& elm) {
                      return elm->template to<TypedElement<string>>()->val();
                    });
      return tier_output_list;
    });
  }

  m_info_hash = Sha1::calculate(encode(info));
}

int64_t Torrent::length() const {
  return is_single_file()
             ? m_length
             : accumulate(
                   m_files.cbegin(), m_files.cend(), static_cast<int64_t>(0),
                   [](int64_t a, const FileInfo& b) { return a + b.length(); });
}

auto Torrent::left() const {
  // FIXME: Should not include pieces that are done

  if (is_single_file()) {
    return length();
  }

  return accumulate(
      m_files.begin(), m_files.end(), static_cast<int64_t>(0),
      [](int64_t a, const FileInfo& b) { return a + b.length(); });
}

vector<Peer> Torrent::start() {
  Url url(m_announce);
  url.add_param("info_hash=" + Net::urlEncode(m_info_hash));
  url.add_param("peer_id=abcdefghijklmnopqrst");  // FIXME: Use proper id
  url.add_param("port=20000");                    // FIXME: configure this
  url.add_param("uploaded=0");
  url.add_param("downloaded=0");
  url.add_param("left=" + to_string(left()));
  url.add_param("event=started");
  url.add_param("compact=1");  // TODO: Look up what this really means
  m_logger->info("\n{}", url);

  Net net;
  auto [headers, body] = net.http_get(url);
  auto reply = decode(body);

  m_logger->info("=====HEADER=====\n{}\n=====BODY=====\n{}", headers, reply);

  // The peers might be in binary or string form
  auto reply_dict = reply->to<TypedElement<BeDict>>()->val();
  if (reply_dict.find("peers") == reply_dict.end()) {
    throw runtime_error("Invalid tracker reply, no peer list");
  }
  auto peers_dict = reply_dict["peers"];
  // First try string form
  try {
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    auto string_peers = peers_dict->to<TypedElement<BeDict>>()->val();
    // FIXME: implement
    throw runtime_error("Dict peers not implemented");
  } catch (const BencodeConversionError&) {
    // This is fine - try the next format
  }

  auto binary_peers = peers_dict->to<TypedElement<string>>()->val();
  if (binary_peers.empty()) {
    throw runtime_error("Peer list is empty");
  }

  vector<Peer> peers;
  const int THREE_HEX_BYTES = 6;
  for (unsigned long i = 0; i < binary_peers.length(); i += THREE_HEX_BYTES) {
    peers.emplace_back(Url(binary_peers.substr(i, THREE_HEX_BYTES), true),
                       m_piece_length, *this);
  }

  return peers;
}

ostream& operator<<(ostream& os, const zit::FileInfo& file_info) {
  os << "(" << file_info.path() << ", " << file_info.length() << ", "
     << file_info.md5sum() << ")\n";
  return os;
}

ostream& operator<<(ostream& os, const zit::Torrent& torrent) {
  os << "--------------------\n";
  if (torrent.is_single_file()) {
    os << "Name:          " << torrent.name() << "\n";
    os << "Length:        " << torrent.length() << "\n";
    if (!torrent.md5sum().empty()) {
      os << "MD5Sum:        " << torrent.md5sum() << "\n";
    }
  } else {
    os << "Files:\n";
    for (const auto& fi : torrent.files()) {
      os << "  " << fi << "\n";
    }
  }
  os << "Creation date: " << torrent.creation_date() << "\n";
  os << "Comment:       " << torrent.comment() << "\n";
  if (!torrent.created_by().empty()) {
    os << "Created by:    " << torrent.created_by() << "\n";
  }
  if (!torrent.encoding().empty()) {
    os << "Encoding:      " << torrent.encoding() << "\n";
  }
  os << "Piece length:  " << torrent.piece_length() << "\n";
  os << "Private:       " << (torrent.is_private() ? "Yes" : "No") << "\n";
  os << "Announce List:\n";
  for (const auto& list : torrent.announce_list()) {
    for (const auto& url : list) {
      os << "  " << url;
    }
    os << "\n";
  }
  if (torrent.announce_list().empty()) {
    os << "  " << torrent.announce() << "\n";
  }
  os << "--------------------\n";
  return os;
}

}  // namespace zit
