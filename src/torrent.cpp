// -*- mode:c++; c-basic-offset : 2; - * -
#include "torrent.h"
#include "bencode.h"
#include "file_utils.h"

using namespace bencode;
using namespace std;

namespace zit {

Torrent::Torrent(const std::filesystem::path& file) {
  auto root = bencode::decode(read_file(file));

  auto root_dict = root->to<TypedElement<BeDict>>()->val();

  // Required
  m_announce = root_dict["announce"]->to<TypedElement<string>>()->val();
  auto info = root_dict["info"]->to<TypedElement<BeDict>>()->val();

  m_name = info["name"]->to<TypedElement<string>>()->val();
  m_pieces = info["pieces"]->to<TypedElement<string>>()->val();
  m_piece_length = info["piece length"]->to<TypedElement<int64_t>>()->val();

  // Either 'length' or 'files' is needed depending on mode
  if (info.find("length") != info.end()) {
    m_length = info["length"]->to<TypedElement<int64_t>>()->val();
  }
  if (info.find("files") != info.end()) {
    if (m_length != 0) {
      throw runtime_error("Invalid torrent: dual mode");
    }
    auto files = info["files"]->to<TypedElement<BeList>>()->val();
    // FIXME: Fill in files
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
        root_dict["creation date"]->to<TypedElement<int64_t>>()->val();
  }
  if (has("comment")) {
    m_comment = root_dict["comment"]->to<TypedElement<string>>()->val();
  }
  if (has("created by")) {
    m_created_by = root_dict["created by"]->to<TypedElement<string>>()->val();
  }
  if (has("encoding")) {
    m_encoding = root_dict["encoding"]->to<TypedElement<string>>()->val();
  }
  if (has("md5sum")) {
    m_md5sum = root_dict["md5sum"]->to<TypedElement<string>>()->val();
  }
  if (has("private")) {
    m_private = root_dict["private"]->to<TypedElement<int64_t>>()->val() == 1;
  }
  if (has("announce-list")) {
    // TODO: Check performance, copied ?
    auto announce_list =
        root_dict["announce-list"]->to<TypedElement<BeList>>()->val();
    for (const auto& tier : announce_list) {
      std::vector<std::string> tier_list;
      for (const auto& tier_url : tier->to<TypedElement<BeList>>()->val()) {
        tier_list.push_back(tier_url->to<TypedElement<string>>()->val());
      }
      m_announce_list.push_back(tier_list);
    }
  }
}

}  // namespace zit
