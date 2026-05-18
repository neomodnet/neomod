#!/bin/bash
# To add a new locale, run "msginit -i neomod.pot -o xx.po -l xx" with "xx" being the target language
cd "$(dirname "$0")/.."
xgettext --keyword=_,tformat --add-comments=TRANSLATORS --sort-by-file -o po/neomod.pot --package-name=neomod $(find src/ -name '*.cpp' -o -name '*.h')
for po_file in po/*.po; do
    msgmerge -U "$po_file" po/neomod.pot
done
