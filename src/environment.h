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

#pragma once

#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>

#include <string>
#include <vector>
#include <unordered_set>
#include <optional>

struct CompilerInfo {
    std::string Path;
    std::string VersionText;

    std::string VersionNumber;
    std::string PackageString;
    CompilerInfo(const std::string& path, const std::string& versionText);

};

class Environment {
    std::optional<std::string> vscodePath;
    std::vector<CompilerInfo> compilers;

    static std::optional<std::string> getVscodePath();
    static std::unordered_set<std::string> getPaths();

public:
    Environment();
    const std::optional<std::string>& VscodePath() const;
    const std::vector<CompilerInfo>& Compilers() const;

    static std::optional<std::string> testCompiler(const boost::filesystem::path& path);
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CompilerInfo, Path, VersionText, VersionNumber, PackageString)

inline void to_json(nlohmann::json& j, const Environment& e) {
    j = nlohmann::json::object();
    auto&& VscodePath{e.VscodePath()};
    j["VscodePath"] = VscodePath ? *VscodePath : nullptr;
    j["Compilers"] = e.Compilers();
}