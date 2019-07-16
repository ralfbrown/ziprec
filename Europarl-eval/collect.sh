#!/bin/csh -f
set ext1=/tmp/collect$$.sed
set base=.
set lines=2500

if ("x$1" != "x") set base="$1"

onintr cleanup

cat >$ext1 <<EOF
/[(]The session /d
/[(]The sitting /d
/^[(]Die Sitzung /d
/^[(]La s/d
/^[(]Se cierra /d
/^[(]Se leva/d
/^[(]Se abre /d
/^[(]Posiedzenie /d
/^[(]Das Protokoll /d
/^[(]A sess/d
/^[(]Après le vote/d
/^[<]/d   
/^[(]La riunione/d
/^[(]" συνεδρίαση/d
/^[(]Ръкопляскания[)] *\$/d
/^[(]Председателят /d
/^The next item is the vote/d
/^- Rapport [A-Z][a-z]*\$/d
/^- Relazione: /d
/^- Bericht: /d
/details on the vote/d
/declare adjourned/d
s/^[(][A-Z][A-Z][)] *//
s@http://[-_:/A-Za-z0-9?&=.,#%]*@\n@g
EOF

set ext2=${ext1}2
cp -p $ext1 $ext2
echo '/^[	 -}]*\$/d' >>$ext2

foreach lng (??)
   set dir=${base}/${lng}
   set ext=$ext1
   if ($lng == bg || $lng == el) set ext=$ext2
   sed -f $ext ${lng}/ep-{9*,00-0?,0[1-9],10-0?,10-1[01],1[123]}*.txt >${dir}-train.txt
   sed -f $ext ${lng}/ep-{00-1?,10-12}*.txt >${dir}-testall.txt
   rm -f ${dir}-test?? ${dir}-test??.txt >&/dev/null
   split -d -l$lines ${dir}-testall.txt ${dir}-test
   foreach i (${dir}-test??)
      mv -i "$i" "${i}.txt"
      zip -mo9q "${i}.zip" "${i}.txt"
   end
   zip -mo9q "${dir}-testall.zip" "${dir}-testall.txt"
end


cleanup:

rm -f $ext1 $ext2
exit 0
