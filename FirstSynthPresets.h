#pragma once

#include <filesystem>
#include <fstream>

namespace firstsynth {

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
    return true;
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
