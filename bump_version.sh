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

sed -E -i '' "s/version=\"[0-9]+\.[0-9]+\.0\.0\"/version=\"$VERSION.0.0\"/" assets/neosu.manifest

sed -E -i '' "s/[0-9]+,[0-9]+,0,0/$VERSION_RC,0,0/g" assets/resource.rc
sed -E -i '' "s/(\"FileVersion\", \")[0-9]+\.[0-9]+\.0\.0/\1$VERSION.0.0/" assets/resource.rc
sed -E -i '' "s/(\"ProductVersion\", \")[0-9]+\.[0-9]+/\1$VERSION/" assets/resource.rc

sed -E -i '' "2s/[0-9]+\.[0-9]+/$VERSION/" cmake-win/src/CMakeLists.txt

sed -E -i '' "2s/[0-9]+\.[0-9]+/$VERSION/" configure.ac
autoconf

# update previous version
sed -E -i '' "s|(.*\.title = .*)(\" CHANGELOG_TIMESTAMP \")(.*)|\1$LASTDATE\3|" src/App/Osu/Changelog.cpp

sed -i '' "/std::vector<CHANGELOG> changelogs;/a\\
\\
    CHANGELOG v$VERSION_CL;\\
    v$VERSION_CL.title = \"$VERSION (\" CHANGELOG_TIMESTAMP \")\";\\
    v$VERSION_CL.changes = {\\
        R\"()\",\\
    };\\
    changelogs.push_back(v$VERSION_CL);" src/App/Neomod/Changelog.cpp
