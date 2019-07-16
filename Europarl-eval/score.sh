#!/bin/csh -f
if ($#argv < 1) then
   echo "Usage: $0:t tracefile [tracefile ...] [> result}"
   exit 1
endif

echo "TstLng	MdlLng	Corr	UnkB	RecB	Rec%	CorrB	Corr%	TotalB	IdentB	Ident%	Time"
while ($#argv > 0)
   set tr="$1"
   shift
   set langs="`echo $tr:t | sed -e 's/-test.*//'`"
   set src=`echo $langs | sed -e 's/-[^-]*$//' -e 's/-/+/'`
   set tst="`echo $langs | sed -e 's/^.*-//'`"

   if ( -e "$tr" ) then
      fgrep -e 'total unknown bytes' \
	    -e 'bytes replaced' -e 'identical to ref' \
   	    -e 'bytes correct' -e 's reconstructing' "$tr" |\
         sed -e 's/^.*total unknown bytes //' \
	     -e 's/[(]\([0-9]*\) in corrupted segments[)] */ \1 /' \
	     -e 's/\( [0-9]*\) of \([0-9]*\) bytes replaced / \2\1 /' \
	     -e 's/bytes replaced //' \
    	     -e 's/of [0-9]* reconstructed bytes correct //' \
	     -e 's/^ *\([0-9]*\) of \([0-9]*\) bytes [(]\([0-9.]*\)%.*identical to ref.*/ \2 \1 \3/' \
	     -e 's/s reconstructing.*//' |\
         tr -d '\n' | \
         sed -e 's@  *@	@g' -e 's@[()]@@g' \
	     -e "s@^@$tst	$src@" -e 's@$@\n@' -e 's@	0.00%$@	0	0.00	0	0.00%@' | fgrep '%' | \
	 tr -d '%'
   endif
end

