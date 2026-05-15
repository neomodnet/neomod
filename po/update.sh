#!/bin/bash
cd "$(dirname "$0")/.."
xgettext --keyword=_ --add-comments=TRANSLATORS --sort-by-file -o po/neomod.pot --package-name=neomod $(find src/ -name '*.cpp' -o -name '*.h')
for po_file in po/*.po; do
    msgmerge -U "$po_file" po/neomod.pot
done
