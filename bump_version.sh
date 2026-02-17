#!/usr/bin/env bash

if [ -z "$1" ]; then
  echo "Usage: $0 <new-version>"
  exit 1
fi

VERSION="$1"                 # 39.03
VERSION_RC="${VERSION//./,}" # 39,03
VERSION_RC="${VERSION_RC//,0/,}" # 39,3 (don't start a group 0)
VERSION_CL="${VERSION//./_}" # 39_03
LASTDATE="$(TZ=UTC date +%Y-%m-%d)"

if [[ $(sed --version | head -n 1) = *GNU* ]]; then
SEDCMD=("sed" "-Ei")
else
SEDCMD=("sed" "-E" "-i" "''")
fi

"${SEDCMD[@]}" "s/version=\"[0-9]+\.[0-9]+\.0\.0\"/version=\"$VERSION.0.0\"/" assets/neomod.manifest

"${SEDCMD[@]}" "s/[0-9]+,[0-9]+,0,0/$VERSION_RC,0,0/g" assets/resource.rc
"${SEDCMD[@]}" "s/(\"FileVersion\", \")[0-9]+\.[0-9]+\.0\.0/\1$VERSION.0.0/" assets/resource.rc
"${SEDCMD[@]}" "s/(\"ProductVersion\", \")[0-9]+\.[0-9]+/\1$VERSION/" assets/resource.rc

"${SEDCMD[@]}" "2s/[0-9]+\.[0-9]+/$VERSION/" cmake-win/src/CMakeLists.txt

"${SEDCMD[@]}" "2s/[0-9]+\.[0-9]+/$VERSION/" configure.ac
autoconf

# update previous version
"${SEDCMD[@]}" "s|(.*\.title = .*)(\" CHANGELOG_TIMESTAMP \")(.*)|\1$LASTDATE\3|" src/App/Neomod/Changelog.cpp

"${SEDCMD[@]}" "/std::vector<CHANGELOG> changelogs;/a\\
\\
    CHANGELOG v$VERSION_CL;\\
    v$VERSION_CL.title = \"$VERSION (\" CHANGELOG_TIMESTAMP \")\";\\
    v$VERSION_CL.changes = {\\
        R\"()\",\\
    };\\
    changelogs.push_back(v$VERSION_CL);" src/App/Neomod/Changelog.cpp
