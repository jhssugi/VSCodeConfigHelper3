// Copyright (C) 2021 Guyutongxue
//
// This file is part of VS Code Config Helper.
//
// VS Code Config Helper is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// VS Code Config Helper is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with VS Code Config Helper.  If not, see <http://www.gnu.org/licenses/>.

#include "generator.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/assign.hpp>
#include <boost/process.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "cli.h"
#include "config.h"
#include "log.h"

namespace bp = boost::process;
namespace fs = boost::filesystem;
using namespace boost::assign;
using namespace std::literals;

std::string ExtensionManager::runScript(const std::initializer_list<std::string>& args) {
    bp::ipstream is;
    bp::child proc(scriptPath, std::vector(args), bp::std_out > is);
    proc.wait();
    std::ostringstream oss;
    std::copy(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>(),
              std::ostreambuf_iterator<char>(oss));
    return oss.str();
}

ExtensionManager::ExtensionManager(const boost::filesystem::path& vscodePath)
    : scriptPath{
#ifdef WINDOWS
          vscodePath.parent_path() / "bin\\code.cmd"
#else
          vscodePath
#endif
      } {
    installedExtensions = list();
}

std::unordered_set<std::string> ExtensionManager::list() {
    try {
        auto output{runScript({"--list-extensions"})};
        boost::trim(output), boost::to_lower(output);
        LOG_DBG(output);
        std::unordered_set<std::string> result;
        boost::split(result, output, boost::is_any_of("\r\n"));
        return result;
    } catch (...) {
        LOG_ERR("????????????????????????????????????????????????");
        throw;
    }
}
void ExtensionManager::install(const std::string& id_orig) {
    auto id{boost::to_lower_copy(id_orig)};
    if (installedExtensions.find(id) != installedExtensions.end()) {
        LOG_INF("?????? ", id, " ????????????");
        return;
    }
    try {
        LOG_INF("???????????? ", id, "...");
        LOG_DBG(runScript({"--install-extension", id}));
        installedExtensions.insert(id);
        LOG_INF("???????????????");
    } catch (...) {
        LOG_ERR("???????????? ", id, " ??????????????????");
        throw;
    }
}

void ExtensionManager::uninstall(const std::string& id_orig) {
    auto id{boost::to_lower_copy(id_orig)};
    if (auto pos{installedExtensions.find(id)}; pos != installedExtensions.end()) {
        try {
            LOG_INF("???????????? ", id, "...");
            LOG_DBG(runScript({"--uninstall-extension", id}));
            installedExtensions.erase(pos);
            LOG_INF("???????????????");
        } catch (...) {
            LOG_ERR("???????????? ", id, " ??????????????????");
            throw;
        }
    } else {
        LOG_INF("?????? ", id, " ????????????");
    }
}

void ExtensionManager::uninstallAll() {
    for (auto&& id : shouldUninstallExtensions) {
        uninstall(id);
    }
}

namespace {

const char offlineHost[]{"https://guyutongxue.oss-cn-beijing.aliyuncs.com"};
#ifdef WINDOWS
const char offlinePath[]{"/vscode-cpptools/cpptools-win32.vsix"};
#elif defined(LINUX)
const char offlinePath[]{"/vscode-cpptools/cpptools-linux.vsix"};
#else
const char offlinePath[]{"/vscode-cpptools/cpptools-osx.vsix"};
#endif
const char C_CPP_EXT_ID[]{"ms-vscode.cpptools"};

boost::regex splitPathRegex(";(?=(?:[^\"]*\"[^\"]*\")*(?![^\"]*\"))");

template <template <typename> typename ContainerT>
ContainerT<std::string> splitPath(std::optional<std::string> (*getter)(const std::string&)) {
    auto origin{getter("Path")};
    auto originalPath{origin ? *origin : ""};
    ContainerT<std::string> container;
    boost::split_regex(container, originalPath, splitPathRegex);
    return container;
}

}  // namespace

Generator::Generator(CurrentOptions options) : options{options} {}

const char* Generator::fileExt() {
    return options.Language == LanguageType::Cpp ? ".cpp" : ".c";
}

std::string Generator::compilerPath() {
#ifdef WINDOWS
    const char* filename{options.Language == LanguageType::Cpp ? "g++.exe" : "gcc.exe"};
    return (fs::path(options.MingwPath) / filename).string();
#else
    return options.Compiler;
#endif
}

std::string Generator::debuggerPath() {
#ifdef WINDOWS
    return (fs::path(options.MingwPath) / "gdb.exe").string();
#else
    return "gdb";
#endif
}
std::string Generator::scriptPath(const std::string& filename) {
    return (scriptDirectory(options) / filename).string();
}

void Generator::saveFile(const fs::path& path, const char* content) {
    LOG_INF("???????????? ", path, " ???...");
    if (fs::exists(path)) {
        LOG_INF("?????? ", path, " ???????????????????????????");
    } else {
        fs::create_directories(path.parent_path());
        fs::save_string_file(path, content);
        LOG_INF("???????????????");
#ifndef WINDOWS
        LOG_INF("???????????? ", path, " ???????????????...");
        fs::permissions(path, fs::perms::owner_all);
#endif
    }
}

void Generator::addKeybinding(const std::string& key, const std::string& command,
                              const std::string& args) {
    using json = nlohmann::json;
    auto filepath(Native::getAppdata() / "Code" / "User" / "keybindings.json");
    LOG_INF("???????????? ", key, " (", command, " ", args, ") ????????? ", filepath, " ???...");
    auto result(json::array());
    if (fs::exists(filepath)) {
        LOG_INF("??????????????????????????????????????????...");
        try {
            std::string content;
            fs::load_string_file(filepath, content);
            auto exists(nlohmann::json::parse(content));
            for (auto&& i : exists) {
                if (i["key"].get<std::string>() == key) {
                    LOG_WRN("??????????????? ", key, " ?????????????????????????????????");
                    continue;
                }
                result.push_back(i);
            }
        } catch (...) {
            LOG_WRN("?????????????????????????????????????????????????????????");
        }
    }
    result += json::object({{"key", key}, {"command", command}, {"args", args}});

    auto resultStr{result.dump(2)};
    LOG_DBG(resultStr);
    fs::create_directories(filepath.parent_path());
    fs::save_string_file(filepath, resultStr);
}

#ifdef WINDOWS
void Generator::addToPath(const fs::path& path) {
    auto newPath{path.string()};

    auto sysPaths{splitPath<std::unordered_set>(Native::getLocalMachineEnv)};
    if (sysPaths.find(newPath) != sysPaths.end()) {
        LOG_WRN("?????????????????? Path ????????? ", newPath, "?????????????????????????????????????????? Path ??????");
        return;
    }

    LOG_INF("??? ", newPath, " ??????????????????????????? Path ???...");
    auto paths{splitPath<std::list>(Native::getCurrentUserEnv)};
    if (auto it{std::find(paths.begin(), paths.end(), newPath)}; it != paths.end()) {
        LOG_INF("?????????????????? Path ???????????? ", newPath, "???????????????????????????");
        paths.erase(it);
        paths.push_front(newPath);
    } else {
        LOG_INF("?????????????????? Path ???????????? ", newPath, "??????????????????");
        paths.push_front(newPath);
    }
    auto result{boost::join(paths, ";")};
    LOG_DBG("Final Path: ", result);
    Native::setCurrentUserEnv("Path", result);
}
#endif

void Generator::generateTasksJson(const fs::path& path) {
    using json = nlohmann::json;
    LOG_INF("?????? ", path, " ...");
    auto args{options.CompileArgs};
    args += "-g", "${file}", "-o",
        "${fileDirname}" PATH_SLASH "${fileBasenameNoExtension}." EXE_EXT;
    // clang-format off
    auto sfbTask(json::object({
        {"type", "process"}, // "cppbuild" won't apply options
        {"label", "gcc single file build"},
        {"command", compilerPath()},
        {"args", args},
        {"group", json::object({
            {"kind", "build"},
            {"isDefault", true}
        })},
        {"presentation", json::object({
            {"reveal", "silent"},
            {"focus", false},
            {"echo", false},
            {"showReuseMessage", false},
            {"panel", "shared"},
            {"clear", true}
        })},
        {"problemMatcher", "$gcc"}
    }));
    auto pauseTask(json::object({
        {"type", "shell"},
        {"label", "run and pause"},
        {"command", 
#ifdef WINDOWS
            "START"
#elif defined(LINUX)
            "x-terminal-emulator",
#else // MACOS
            scriptPath("pause-console-launcher.sh"),
#endif
        },
        {"dependsOn", "gcc single file build"},
        {"args", json::array({
#ifndef MACOS
# ifdef WINDOWS
            "C:\\Windows\\system32\\WindowsPowerShell\\v1.0\\powershell.exe",
            "-ExecutionPolicy",
            "ByPass",
            "-NoProfile",
            "-File",
# else // LINUX
            "-e",
# endif
            scriptPath("pause-console." SCRIPT_EXT),
#endif
            "${fileDirname}" PATH_SLASH "${fileBasenameNoExtension}." EXE_EXT
        })},
        {"presentation", json::object({
            {"reveal", "never"},
            {"focus", false},
            {"echo", false},
            {"showReuseMessage", false},
            {"panel", "shared"},
            {"clear", true}
        })},
        {"problemMatcher", json::array()}
    }));
    auto asciiTask(json::object({
        {"type", "process"},
        {"label", "check ascii"},
        {"command", "C:\\Windows\\system32\\WindowsPowerShell\\v1.0\\powershell.exe"},
        {"dependsOn", "gcc single file build"},
        {"args", json::array({
            "-ExecutionPolicy",
            "ByPass",
            "-NoProfile",
            "-File",
            scriptPath("check-ascii.ps1"),
            "${fileDirname}\\${fileBasenameNoExtension}.exe"
        })},
        {"presentation", json::object({
            {"reveal", "never"},
            {"focus", false},
            {"echo", false},
            {"showReuseMessage", false},
            {"panel", "shared"},
            {"clear", true}
        })},
        {"problemMatcher", json::array()}
    }));
    auto allTasks(json::array({sfbTask}));
    if (options.UseExternalTerminal)
        allTasks += pauseTask;
#ifdef WINDOWS
    if (options.ApplyNonAsciiCheck)
        allTasks += asciiTask;
#endif
    auto result(json::object({
        {"version", "2.0.0"},
        {"tasks", allTasks},
#ifdef WINDOWS
        {"options", json::object(
            {
                {"shell", json::object({
                    {"executable", "C:\\Windows\\System32\\cmd.exe"},
                    {"args", json::array({
                        "/C"
                    })}
                })},
                {"env", json::object({
                    {"Path", options.MingwPath + ";${env:Path}"}
                })}
            }
        )}
#endif
    }));
    // clang-format on
    auto resultStr{result.dump(2)};
    LOG_DBG(resultStr);
    fs::save_string_file(path, resultStr);
}

void Generator::generateLaunchJson(const fs::path& path) {
    using json = nlohmann::json;
    LOG_INF("?????? ", path, " ...");
    // clang-format off
    auto result(json::object({
        {"version", "0.2.0"},
        {"configurations", json::array({
            json::object({
                {"name", "gcc single file debug"},
                {"type", "cppdbg"},
                {"request", "launch"},
                {"program", "${fileDirname}" PATH_SLASH "${fileBasenameNoExtension}." EXE_EXT},
                {"args", json::array({})},
                {"stopAtEntry", false},
                {"cwd", "${fileDirname}"},
                {"environment", json::array({})},
                {"externalConsole", 
#ifdef MACOS
                    true
#else
                options.UseExternalTerminal
#endif
                },
                {"MIMode", 
#ifdef MACOS
                "lldb"
#else
                "gdb"
#endif                
                },
#ifndef MACOS
                {"miDebuggerPath", debuggerPath()},
#endif
                {"setupCommands", json::array({
                    json::object({
                        {"text", "-enable-pretty-printing"},
                        {"ignoreFailures", true}
                    })
                })},
                {"preLaunchTask", 
#ifdef WINDOWS
                    options.ApplyNonAsciiCheck ? "check ascii" : 
#endif
                    "gcc single file build"},
                {"internalConsoleOptions", "neverOpen"}
            })
        })}
    }));
    // clang-format on
    auto resultStr{result.dump(2)};
    LOG_DBG(resultStr);
    fs::save_string_file(path, resultStr);
}
void Generator::generatePropertiesJson(const fs::path& path) {
    using json = nlohmann::json;
    LOG_INF("?????? ", path, " ...");
    // clang-format off
    auto result(json::object({
        {"version", 4},
        {"configurations", json::array({
            json::object({
                {"name", "gcc"},
                {"includePath", json::array({
                    "${workspaceFolder}/**"
                })},
                {"compilerPath", compilerPath()},
                {options.Language == LanguageType::Cpp ? 
                    "cppStandard" : 
                    "cStandard",
                options.LanguageStandard == "c++23" ?
                    "c++20" :
                    options.LanguageStandard},
                {"intelliSenseMode", "windows-gcc-x64"}
            })
        })}
    }));
    // clang-format on
    auto resultStr{result.dump(2)};
    LOG_DBG(resultStr);
    fs::save_string_file(path, resultStr);
}

std::string Generator::generateTestFile() {
    auto filepath{fs::path(options.WorkspacePath) / ("helloworld"s + fileExt())};
    for (int i{1}; fs::exists(filepath); i++) {
        filepath =
            fs::path(options.WorkspacePath) / ("helloworld(" + std::to_string(i) + ")" + fileExt());
    }
    LOG_INF("???????????????????????? ", filepath, "...");
    const std::string compileHotkeyComment{
        "?????? "s + (options.UseExternalTerminal ? "F6" : 
#ifdef MACOS
        "??? F5"
#else
        "Ctrl + F5"
#endif       
        ) + " ???????????????"};
    const std::string compileResultComment{"?????? "s +
                                           (options.UseExternalTerminal
                                                ? "F6 ????????????????????????"
                                                : "F5 ???????????????????????????????????????Terminal???") +
                                           "??????????????????????????????"};
    bool isCpp{options.Language == LanguageType::Cpp};
    std::string (*c)(const std::string&){nullptr};
    if (isCpp)  // I'm wonder why MSVC IntelliSense doesn't support Lambda in ?:.
        c = [](const std::string& s) { return "// " + s; };
    else
        c = [](const std::string& s) { return "/* " + s + " */"; };
    std::ostringstream oss;
    oss << c("VS Code C/C++ ???????????? \"Hello World\"") << '\n';
    oss << c("??? VSCodeConfigHelper v" PROJECT_VERSION " ??????") << '\n';
    oss << '\n';
    oss << c("?????????????????????????????????????????????????????????????????????") << '\n';
    oss << '\n';
    oss << c(compileHotkeyComment) << '\n';
    oss << c("?????? F5 ???????????????") << '\n';
    oss << c("?????? "
#ifdef MACOS
    "??? ??? B"
#else
    "Ctrl + Shift + B"
#endif
    " ????????????????????????") << '\n';
    if (isCpp) {
        oss << R"(
#include <iostream>

/**
 * ???????????????????????????
 */
int main() {
    // ???????????????????????? "Hello, world!"
    std::cout << "Hello, world!" << std::endl;
}
)";
    } else {
        oss << R"(
#include <stdio.h>
#include <stdlib.h>

/**
 * ???????????????????????????
 */
int main(void) {
    /* ???????????????????????? "Hello, world!" */
    printf("Hello, world!\n");
    return EXIT_SUCCESS;
}
)";
    }
    oss << '\n';
    oss << c("?????????????????????????????? \"Hello, world!\"???") << '\n';
    oss << c(compileResultComment) << '\n';
#ifdef WINDOWS
    oss << c("??????????????????????????????????????????????????????????????????????????????????????????????????????????????????")
        << '\n';
#endif
    oss << c("????????????????????????????????????") << '\n';
    oss << c("https://github.com/Guyutongxue/VSCodeConfigHelper/blob/v2.x/TroubleShooting.md")
        << '\n';
    oss << c("?????????????????????????????????????????????????????????????????? guyutongxue@163.com???") << '\n';

    fs::save_string_file(filepath, oss.str());
    LOG_INF("???????????????????????????");
    return filepath.string();
}

void Generator::openVscode(const std::optional<std::string>& filename) {
    auto folderPath{fs::absolute(options.WorkspacePath).string()};

    std::vector args{folderPath};
    if (filename) {
        args += "--goto", *filename;
    }
    LOG_INF("?????? VS Code...");
    LOG_DBG(options.VscodePath, boost::join(args, " "));
    try {
        bp::spawn(options.VscodePath, bp::args = args, bp::std_out > bp::null);
    } catch (std::exception& e) {
        LOG_WRN("?????? VS Code ?????????", e.what());
    }
}

#ifdef WINDOWS
void Generator::generateShortcut() {
    fs::path shortcutPath{fs::path(Native::getDesktop()) / "Visual Studio Code.lnk"};
    if (fs::exists(shortcutPath)) {
        LOG_WRN("???????????? ", shortcutPath, " ???????????????????????????");
        fs::remove(shortcutPath);
    }
    auto targetPath{fs::absolute(options.WorkspacePath).string()};
    auto result{Native::createLink(shortcutPath.string(), options.VscodePath,
                                   "Open VS Code at " + targetPath, "\"" + targetPath + "\"")};
    if (result) {
        LOG_INF("???????????? ", shortcutPath, " ????????????");
    } else {
        LOG_WRN("???????????? ", shortcutPath, " ???????????????");
    }
}
#endif

boost::filesystem::path Generator::scriptDirectory(const CurrentOptions& opt) {
#ifdef WINDOWS
    return fs::path(opt.MingwPath);
#elif defined(LINUX)
    // ~/.config
    return Native::getAppdata().parent_path() / ".local/bin";
#else
    // ~/Library/Application Support
    return Native::getAppdata().parent_path().parent_path() / ".local/bin";
#endif
}

void Generator::generate() {
    try {
        fs::path dotVscode(fs::path(options.WorkspacePath) / ".vscode");

        ExtensionManager extensions(options.VscodePath);
        if (options.ShouldUninstallExtensions) {
            extensions.uninstallAll();
        }
        if (options.OfflineInstallCCpp) {
            extensions.installOffline(C_CPP_EXT_ID, offlineHost, offlinePath);
        } else {
            extensions.install(C_CPP_EXT_ID);
        }
        if (options.ShouldInstallL10n) {
            extensions.install("ms-ceintl.vscode-language-pack-zh-hans");
        }
        if (options.UseExternalTerminal) {
            saveFile(scriptDirectory(options) / "pause-console." SCRIPT_EXT, Embed::PAUSE_CONSOLE);
#ifdef MACOS
            saveFile(scriptDirectory(options) / "pause-console-launcher.sh",
                     Embed::PAUSE_CONSOLE_LAUNCHER);
#endif
            addKeybinding("f6", "workbench.action.tasks.runTask", "run and pause");
        }
#ifdef WINDOWS
        if (options.ApplyNonAsciiCheck) {
            saveFile(scriptDirectory(options) / "check-ascii.ps1", Embed::CHECK_ASCII);
        }
        if (!options.NoSetEnv) {
            addToPath(options.MingwPath);
        }
#endif

        if (fs::exists(dotVscode)) {
            fs::remove_all(dotVscode);
            LOG_INF("????????????????????? .vscode ????????????");
        }
        fs::create_directories(dotVscode);
        generateTasksJson(dotVscode / "tasks.json");
        generateLaunchJson(dotVscode / "launch.json");
        generatePropertiesJson(dotVscode / "c_cpp_properties.json");
        if (options.GenerateTestFile == BaseOptions::GenTestType::Auto) {
            if (fs::exists(fs::path(options.WorkspacePath) / ("helloworld"s + fileExt()))) {
                options.GenerateTestFile = BaseOptions::GenTestType::Never;
            } else {
                options.GenerateTestFile = BaseOptions::GenTestType::Always;
            }
        }
        std::optional<std::string> testFilename;
        if (options.GenerateTestFile == BaseOptions::GenTestType::Always) {
            testFilename = generateTestFile();
        }
        if (options.OpenVscodeAfterConfig) {
            openVscode(testFilename);
        }
#ifdef WINDOWS
        if (options.GenerateDesktopShortcut) {
            generateShortcut();
        }
#endif
        if (!options.NoSendAnalytics) {
            sendAnalytics();
        }
        LOG_INF("???????????????");
    } catch (std::exception& e) {
        LOG_ERR(e.what());
        throw;
    }
}
