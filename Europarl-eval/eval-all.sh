#!/bin/csh -f

## find out how many cores we can use
set online = `cat /sys/devices/system/cpu/online| sed -e 's/-/ - -/'`
@ cpus = 1 - $online
set dir=.
set modeldir=./models
set old=""
set extraargs=""
set testflag=""
unset noself

while ("x$1" =~ x-* )
   switch ("$1")
       case "-c":
	    shift
	    set cpus = "$1"
	    breaksw
       case "-d":
            shift
	    set dir = "$1"
	    breaksw
       case "-old":
	    set old = "-old"
	    set extraargs = "-r+5 -r+"
	    breaksw
       case "-m":
            shift
	    set modeldir = "$1"
	    breaksw
       case "-noself":
	    set noself
	    breaksw
       case "-t":
	    shift
	    set testflag = "-t $1"
	    breaksw
       default:
            echo "Unrecognized option $1"
	    echo "Supported arguments:"
	    echo "  -c NUMCPUS"
	    echo "  -d OUTPUTDIR"
	    echo "  -old"
	    echo "  -m MODELDIR"
	    echo "  -noself"
	    echo "  -t TESTFLAG"
	    exit 1
	    breaksw
   endsw
   shift
end

set enc=utf8

foreach model ($modeldir/*-utf8*.lang)
   set lang=`echo ${model:t}|sed -e 's@-.*@@'`
   set count=`find . test -maxdepth 1 -name "*-test*.zip" |wc -l`
   if ($count == 0) continue
   foreach test (*-test*.zip test/*-test*.zip)
      echo "$old" >>$cmd
      echo "-t" >>$cmd
      echo "$testflag" >>$cmd
      echo "$test" >>$cmd
      echo "$model" >>$cmd
      echo "$lang" >>$cmd
      echo "$dir" >>$cmd
      echo "$test" >>$cmd
      echo "$extraargs" >>$cmd
      if (x$old == x-new && ! $?noself ) then
	 echo "$old" >>$cmd
	 echo "-t" >>$cmd
	 echo "$testflag" >>$cmd
	 echo "$test" >>$cmd
	 echo "$model" >>$cmd
	 echo "${lang}+self" >>$cmd
	 echo "$dir" >>$cmd
	 echo "$test" >>$cmd
	 echo "-r@" >>$cmd
      endif
   end
end

xargs <$cmd -P${cpus} '-d\n' -n9 csh -f ./eval.sh

cleanup:
rm -f $cmd

exit 0
