// mem_1CexjjDtpj5FvQKP7dqeqq: first-run presets must not overwrite user sounds.
#include "FirstSynthPresets.h"
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
  namespace fs = std::filesystem;
  const fs::path root(argv[1]), src = root / "bundle", dst = root / "user";
  fs::create_directories(src);
  fs::create_directories(dst / "Presets");
  std::ofstream(src / "日本語.preset") << "factory";
  std::ofstream(src / "Pad.preset") << "factory pad";
  std::ofstream(src / "ignore.txt") << "not a preset";
  std::ofstream(dst / "Presets" / "Pad.preset") << "user pad";
  auto check = [](bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
  };
  check(firstsynth::SeedFactoryPresets(src, dst), "first seed failed");
  check(fs::exists(dst / "Presets" / "日本語.preset"), "missing factory preset");
  std::string text;
  std::getline(std::ifstream(dst / "Presets" / "Pad.preset"), text);
  check(text == "user pad", "user preset overwritten");
  check(!fs::exists(dst / "Presets" / "ignore.txt"), "copied unrelated file");
  fs::remove(dst / "Presets" / "日本語.preset");
  check(firstsynth::SeedFactoryPresets(src, dst), "second seed failed");
  check(!fs::exists(dst / "Presets" / "日本語.preset"), "deleted preset restored");
  check(!firstsynth::SeedFactoryPresets(root / "missing", root / "retry"), "missing source treated as success");
  check(firstsynth::SeedFactoryPresets(src, root / "retry"), "retry failed");
  std::ofstream(root / "blocked") << "not a directory";
  check(!firstsynth::SeedFactoryPresets(src, root / "blocked"), "failed destination treated as success");
  fs::remove(root / "blocked");
  check(firstsynth::SeedFactoryPresets(src, root / "blocked"), "failed destination was not retried");
  fs::create_directories(root / "empty");
  check(!firstsynth::SeedFactoryPresets(root / "empty", root / "unseeded"), "empty bundle marked as seeded");
  check(!fs::exists(root / "unseeded" / ".factory_seeded_v1"), "premature marker");
  std::cout << "Factory preset seeding: PASS\n";
}
