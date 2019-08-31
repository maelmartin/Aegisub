#!/bin/bash
FTP_USER="$1"
FTP_PASS="$2"

dest="ftp://shelter.mahoro-net.org/aegisub-japan7"
tag=$(git describe --exact-match)
[ "$tag" ] || exit

curl -T 'packages\win_installer\output\Aegisub-Japan7-x64.exe' --user $FTP_USER:$FTP_PASS "$dest/Aegisub-Japan7-x64-${tag#v}.exe"
curl -T <(printf "${tag#v}\n$(git tag -l --format='%contents' $tag)") --user "$FTP_USER:$FTP_PASS" "$dest/"
