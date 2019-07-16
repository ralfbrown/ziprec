#!/bin/csh -f
onintr cleanup

if ( -e ./mklang ) then
   set mklang=./mklang
else if ( -e ../mklang) then
   set mklang=../mklang
else
   set mklang=mklang
endif

## find out how many cores we can use
set online = `cat /sys/devices/system/cpu/online| sed -e 's/-/ - -/'`
@ cpus = 1 - $online

set enc=utf8
set len8=7
set len16=8

rm -f /tmp/mkmod$$

foreach file (*-train.txt *-train.txt.[gx]z train/*-train.txt train/*-train.txt.[gx]z)
  set lang=`echo $file:t|sed -e 's@-.*@@'`
  set len=$len8
  if ( $lang == bg || $lang == el) set len=$len16
  echo "-n$len" >>/tmp/mkmod$$
  echo "${lang}-${enc}.lang" >>/tmp/mkmod$$
  echo "$file" >>/tmp/mkmod$$
end

xargs </tmp/mkmod$$ -P$cpus -n3 '-d\n' $mklang

cleanup:

rm -f /tmp/mkmod$$
exit 0
