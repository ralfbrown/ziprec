#!/bin/csh -f
set tmp="tmp$$"
set testflag=""
if ( -e ./ziprec ) then
   set ziprec=./ziprec
else if ( -e ../ziprec ) then
   set ziprec=../ziprec
else
   set ziprec=ziprec
endif

while (x$1 =~ x-* )
   switch ("$1")
      case "-old":
	 set ziprec=./ziprec-0.9
	 breaksw
      case "-new":
         # dummy for use by eval-same.sh / eval-all.sh
         breaksw
      case "-t":
         shift
	 set testflag="$1:s/-t//:s/ //"
	 breaksw
   endsw
   shift
end

if ($#argv < 5) then
   echo "Usage: $0:t [-old] [-t TST] testfile lngmodel lng outdir outspec [options]"
   exit 1
endif

set test="$1"
set model="$2"
set lang="$3"
set tracedir="$4"
set trace="${5:t:r}.trace"
shift
shift
shift
shift
shift

set log="${tracedir}/${lang}-${trace}"

echo "$test" "$model" "$log" $*

mkdir -p "$tracedir" >&/dev/null
echo $ziprec "-t$testflag" -s -v "-r=$model" -G "-d${tmp}" -o $* "$test" >&"$log"
$ziprec "-t$testflag" -s -v "-r=$model" -G "-d${tmp}" -o $* "$test" >>&"$log"
rm -rf "$tmp"

exit 0
