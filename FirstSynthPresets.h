#pragma once

#include <filesystem>
#include <fstream>

namespace firstsynth {

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
    if (entry->is_regular_file(ec) && entry->path().extension() == ".preset")
    {
      fs::copy_file(entry->path(), destination / entry->path().filename(),
                    fs::copy_options::skip_existing, ec);
      if (ec)
        return false;
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
