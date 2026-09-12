# Regression for mem_1CexjjDtpj5FvQKP7dqeqq.
# Compile the actual path-producing statements with WDL_String. INIPath is
# supplied by a fixture so this test never touches the user's Application Support.
require 'tmpdir'
require 'open3'

root = File.expand_path('..', __dir__)
iplug = ENV.fetch('IPLUG2_DIR', File.expand_path('../iPlug2', root))
source = File.read(File.join(root, 'FirstSynth.cpp'))
methods = %w[GetPresetsDir GetAutoStatePath].map do |name|
  source.match(/void FirstSynth::#{name}\(WDL_String& path\)\n\{.*?\n\}/m)&.to_s or abort "Missing #{name}"
end
file_paths = %w[SavePresetAs LoadPresetByName DeletePresetByName].map do |name|
  body = source.match(/void FirstSynth::#{name}\(.*?\n\{(.*?)\n\}/m)&.captures&.first or abort "Missing #{name}"
  statement = body.match(/  WDL_String path;\n(.*?)\n\n/m)&.captures&.first or abort "Missing #{name} path"
  <<~CPP
    #{statement}
    check(fs::path(path.Get()) == fs::path(fixture) / "Presets" / "日本語 Pad.preset", "#{name} must use the listed preset directory");
  CPP
end

Dir.mktmpdir('firstsynth-paths-') do |tmp|
  code = <<~CPP
    #include "wdlstring.h"
    #include <filesystem>
    #include <iostream>
    #define MAX_WIN32_PATH_LEN 4096
    static const char* fixture;
    void INIPath(WDL_String& path, const char*) { path.Set(fixture); }
    class FirstSynth { public:
      void GetPresetsDir(WDL_String&);
      void GetAutoStatePath(WDL_String&);
    };
    #{methods.join("\n")}
    int main(int argc, char** argv) {
      fixture = argv[1];
      FirstSynth synth;
      WDL_String dir, autosave, path, safeName("日本語 Pad");
      synth.GetAutoStatePath(autosave);
      synth.GetPresetsDir(dir);
      namespace fs = std::filesystem;
      bool ok = true;
      auto check = [&](bool passed, const char* message) {
        if (!passed) { std::cerr << "FAIL: " << message << std::endl; ok = false; }
      };
      check(fs::path(dir.Get()) == fs::path(fixture) / "Presets", "preset directory must be inside the app data directory");
      check(fs::is_directory(fs::path(fixture) / "Presets"), "preset directory must exist");
      check(fs::path(autosave.Get()) == fs::path(fixture) / "autosave.state", "autosave must be inside the app data directory");
      #{file_paths.join("\n")}
      return ok ? 0 : 1;
    }
  CPP
  cpp = File.join(tmp, 'test.cpp')
  exe = File.join(tmp, 'test')
  File.write(cpp, code)
  abort 'Compilation failed' unless system('c++', '-std=c++17', '-I', File.join(iplug, 'WDL'), cpp, '-o', exe)
  output, status = Open3.capture2e(exe, File.join(tmp, 'Application Support', 'FirstSynth'))
  puts output
  abort 'Storage path regression failed' unless status.success?
  puts 'Storage paths: PASS'
end
