// -*- mode:c++; c-basic-offset : 2; - * -
#include "torrent.h"
#include "bencode.h"
#include "file_utils.h"

#include <algorithm>

using namespace bencode;
using namespace std;

namespace zit {

// To make transform calls more readable
template <class In, class Out, class Op>
static auto transform_all(const In& in, Out& out, Op func) {
  return transform(begin(in), end(in), back_inserter(out), func);
}

Torrent::Torrent(const filesystem::path& file) {
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

    transform_all(announce_list, m_announce_list, [](const auto& tier) {
      vector<string> tier_output_list;
      transform_all(tier->template to<TypedElement<BeList>>()->val(),
                    tier_output_list, [](const auto& elm) {
                      return elm->template to<TypedElement<string>>()->val();
                    });
      return tier_output_list;
    });
  }
}

}  // namespace zit
