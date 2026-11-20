#pragma once

#include <filesystem>
#include <fstream>

namespace firstsynth {

// The marks sidecar, for an install that was seeded before it shipped: on macOS
// every build up to 2026-09-12 copied the presets but not _factory.txt (it was
// not even in the bundle), and the one-time marker then blocks a reseed, so
// those installs would show no factory sounds forever. Only ever added when it
// is absent, which is unambiguous: WriteFactoryMarks leaves an empty file
// rather than removing it, so "missing" cannot mean "the user unmarked
// everything". Presets are left alone - a deletion must stick.
inline bool deliver_missing_marks(const std::filesystem::path& source,
                                 const std::filesystem::path& destination)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto name = "_factory.txt";
  if (fs::exists(destination / name, ec) || !fs::is_regular_file(source / name, ec))
    return true;
  ec.clear();
  fs::create_directories(destination, ec);
  if (ec)
    return false;
  fs::copy_file(source / name, destination / name, fs::copy_options::skip_existing, ec);
  return !ec;
}

// Copies the bundled factory set (*.preset plus the _factory.txt marks sidecar)
// into <appData>/Presets, never over a file that is already there.
// A completed seed is permanent: user edits and deletions survive later launches.
// Failed or missing bundles leave no marker so the next launch can retry.
inline bool SeedFactoryPresets(const std::filesystem::path& source,
                               const std::filesystem::path& appData)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto marker = appData / ".factory_seeded_v1";
  if (fs::is_regular_file(marker, ec))
    return deliver_missing_marks(source, appData / "Presets");
  ec.clear();
  if (!fs::is_directory(source, ec) || ec)
    return false;
  const auto destination = appData / "Presets";
  fs::create_directories(destination, ec);
  if (ec)
    return false;

  int count = 0;
  fs::directory_iterator entry(source, ec), end;
  while (!ec && entry != end)
  {
    // The factory "star" marks ride along with the presets: the UI reads them
    // from this sidecar inside the user's own Presets folder, so a bundle that
    // seeded its presets but not its marks would show no factory sounds.
    // Only presets count towards a completed seed - marks alone are not one.
    const bool isPreset = entry->path().extension() == ".preset";
    const bool isMarks = entry->path().filename() == "_factory.txt";
    if ((isPreset || isMarks) && entry->is_regular_file(ec))
    {
      fs::copy_file(entry->path(), destination / entry->path().filename(),
                    fs::copy_options::skip_existing, ec);
      if (ec)
        return false;
      if (isPreset)
        ++count;
    }
    if (ec)
      return false;
    entry.increment(ec);
  }
  if (ec || count == 0)
    return false;
  std::ofstream completed(marker);
  completed << '1';
  completed.close();
  return !completed.fail();
}

} // namespace firstsynth
