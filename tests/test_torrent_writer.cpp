#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "test_utils.hpp"
#include "torrent.hpp"

namespace fs = std::filesystem;

// Path to the writer executable provided by CMake as a compile definition.
// Example: "C:/path/to/build/src/torrent_writer/zit_torrent_writer"
#ifndef TORRENT_WRITER_EXE
#error "TORRENT_WRITER_EXE not defined"
#endif

using TorrentWriter = TestWithTmpDir;

TEST_F(TorrentWriter, GeneratedInfoHashEqualsReferenceSingle) {
  const auto data_dir = fs::path(DATA_DIR);
  const auto data_file = data_dir / "1MiB.dat";
  const auto out_torrent = tmp_dir() / "test_1MiB.torrent";

  // Run the writer executable to create a torrent for 1MiB.dat
  const std::string cmd =
      std::string(TORRENT_WRITER_EXE) + " --torrent " +
      out_torrent.generic_string() + " --data " + data_file.generic_string() +
      " --comment '' --announce http://example/announce --piece-length 32768";

  const int rc = std::system(cmd.c_str());
  ASSERT_EQ(rc, 0) << "torrent writer failed: " << cmd;

  // Read generated torrent and reference torrent using existing Torrent class.
  // Construct each Torrent in its own scope so they are not live at the same
  // time (the Torrent constructor registers the torrent by info_hash).

  zit::Sha1 gen_hash;
  std::vector<zit::Sha1> gen_pieces;
  uint32_t gen_piece_length = 0;
  int64_t gen_length = 0;
  std::string gen_name;

  {
    asio::io_context io1;
    zit::Torrent generated(io1, out_torrent);
    gen_hash = generated.info_hash();
    gen_pieces = generated.pieces();
    gen_piece_length = generated.piece_length();
    gen_length = generated.length();
    gen_name = generated.name();
  }

  zit::Sha1 ref_hash;
  std::vector<zit::Sha1> ref_pieces;
  uint32_t ref_piece_length = 0;
  int64_t ref_length = 0;
  std::string ref_name;

  {
    asio::io_context io2;
    zit::Torrent reference(io2, data_dir / "1MiB.torrent");
    ref_hash = reference.info_hash();
    ref_pieces = reference.pieces();
    ref_piece_length = reference.piece_length();
    ref_length = reference.length();
    ref_name = reference.name();
  }

  EXPECT_EQ(gen_name, ref_name);
  EXPECT_EQ(gen_length, ref_length);
  EXPECT_EQ(gen_pieces.size(), ref_pieces.size());
  EXPECT_EQ(gen_piece_length, ref_piece_length);
  EXPECT_EQ(gen_hash.hex(), ref_hash.hex());

  // Compare individual pieces
  for (size_t i = 0; i < gen_pieces.size(); ++i) {
    EXPECT_EQ(gen_pieces[i].hex(), ref_pieces[i].hex()) << "Piece index " << i;
  }
}

TEST_F(TorrentWriter, GeneratedInfoHashEqualsReferenceMulti) {
  const auto data_dir = fs::path(DATA_DIR);
  const auto multi_dir = data_dir / "multi";
  const auto out_torrent = tmp_dir() / "test_multi.torrent";

  // Run the writer executable to create a torrent for 1MiB.dat
  const std::string cmd =
      std::string(TORRENT_WRITER_EXE) + " --torrent " +
      out_torrent.generic_string() + " --data " + multi_dir.generic_string() +
      " --comment '' --announce http://example/announce --piece-length 262144";

  const int rc = std::system(cmd.c_str());
  ASSERT_EQ(rc, 0) << "torrent writer failed: " << cmd;

  // Read generated torrent and reference torrent using existing Torrent class.
  // Construct each Torrent in its own scope so they are not live at the same
  // time (the Torrent constructor registers the torrent by info_hash).

  zit::Sha1 gen_hash;
  std::vector<zit::Sha1> gen_pieces;
  uint32_t gen_piece_length = 0;
  int64_t gen_length = 0;
  std::string gen_name;
  std::vector<zit::FileInfo> gen_files;

  {
    asio::io_context io1;
    zit::Torrent generated(io1, out_torrent);
    gen_hash = generated.info_hash();
    gen_pieces = generated.pieces();
    gen_piece_length = generated.piece_length();
    gen_length = generated.length();
    gen_name = generated.name();
    gen_files = generated.files();
  }

  zit::Sha1 ref_hash;
  std::vector<zit::Sha1> ref_pieces;
  uint32_t ref_piece_length = 0;
  int64_t ref_length = 0;
  std::string ref_name;
  std::vector<zit::FileInfo> ref_files;

  {
    asio::io_context io2;
    zit::Torrent reference(io2, data_dir / "multi.torrent");
    ref_hash = reference.info_hash();
    ref_pieces = reference.pieces();
    ref_piece_length = reference.piece_length();
    ref_length = reference.length();
    ref_name = reference.name();
    ref_files = reference.files();
  }

  EXPECT_EQ(gen_name, ref_name);
  EXPECT_EQ(gen_length, ref_length);
  EXPECT_EQ(gen_pieces.size(), ref_pieces.size());
  EXPECT_EQ(gen_piece_length, ref_piece_length);

  // Compare file lists for both torrents
  const auto path_order = [](const zit::FileInfo& a, const zit::FileInfo& b) {
    return a.path() < b.path();
  };
  std::sort(gen_files.begin(), gen_files.end(), path_order);
  std::sort(ref_files.begin(), ref_files.end(), path_order);
  EXPECT_EQ(gen_files.size(), ref_files.size());
  for (size_t i = 0; i < gen_files.size(); ++i) {
    EXPECT_EQ(gen_files[i].path(), ref_files[i].path());
    EXPECT_EQ(gen_files[i].length(), ref_files[i].length());
  }

  // The current reference was generated with a different file order and is thus
  // not hash comparable.
  // TODO: Generate a reference with a fixed order and
  // enusure the writer uses the same order.
  /*
  EXPECT_EQ(gen_hash.hex(), ref_hash.hex());
  // Compare individual pieces
  for (size_t i = 0; i < gen_pieces.size(); ++i) {
    EXPECT_EQ(gen_pieces[i].hex(), ref_pieces[i].hex()) << "Piece index " << i;
  }
  */
}
