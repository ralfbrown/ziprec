/************************************************************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 27jun2019							*/
/*									*/
/*  (c) Copyright 2011,2012,2013,2019 Ralf Brown/CMU			*/
/*      This program is free software; you can redistribute it and/or   */
/*      modify it under the terms of the GNU General Public License as  */
/*      published by the Free Software Foundation, version 3.           */
/*                                                                      */
/*      This program is distributed in the hope that it will be         */
/*      useful, but WITHOUT ANY WARRANTY; without even the implied      */
/*      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR         */
/*      PURPOSE.  See the GNU General Public License for more details.  */
/*                                                                      */
/*      You should have received a copy of the GNU General Public       */
/*      License (file COPYING) along with this program.  If not, see    */
/*      http://www.gnu.org/licenses/                                    */
/*                                                                      */
/************************************************************************/

#include <errno.h>
#include <limits.h>
#include <cstdlib>
#include <cstring>
#include "framepac/file.h"

#ifdef __WATCOMC__
#  include <ctype.h>   // for toupper(), isdigit()
#  include <direct.h>  // for rmdir()
#endif

using namespace std ;

#include "global.h"
#include "ziprec.h"
#include "inflate.h"
#include "models.h"
#include "recover.h"
#include "reconstruct.h"

extern void print_partial_packet_statistics() ;

/************************************************************************/
/*	Global variables for this module				*/
/************************************************************************/

/************************************************************************/
/************************************************************************/

static void cleanup()
{
   clear_reconstruction_data() ;
   return ;
}

//----------------------------------------------------------------------

static void usage(const char *argv0)
{
   fprintf(stderr,"ZipRecover v" ZIPREC_VERSION ": recover data from corrupted ZIP archives\n") ;
   fprintf(stderr,"  Copyright 2010-2013 Ralf Brown/Carnegie Mellon University -- GNU GPLv3\n\n") ;
   fprintf(stderr,"Usage: %s [options] zipfile ...\n",argv0) ;
   fprintf(stderr,"options:\n") ;
   fprintf(stderr,"   -bSIZ   read from stdin in chunks of at most SIZ megabytes\n") ;
   fprintf(stderr,"   -dDIR   extract to directory DIR (def: current, '%%' replaced by zipname)\n");
   fprintf(stderr,"   -fFMT   output format is { Text, HTML, Decoded, Listing }\n") ;
   fprintf(stderr,"   -g      assume input is gzip file instead of zip archive\n") ;
   fprintf(stderr,"   -G      assume input is gzip if filename ends in 'gz'\n") ;
   fprintf(stderr,"   -j      junk (ignore) directory names in archive\n") ;
   fprintf(stderr,"   -o      overwrite existing files without prompting\n") ;
   fprintf(stderr,"   -OS,E   scan only offsets S through E\n") ;
   fprintf(stderr,"   -r[DB]  reconstruct with auto language ID using database DB\n") ;
   fprintf(stderr,"   -r=LNG  reconstruct missing bytes using data in file LNG\n") ;
   fprintf(stderr,"   -r++    also attempt recovery of partial first packet\n") ;
   fprintf(stderr,"   -r+N    perform N iterations of reconstruction\n") ;
   fprintf(stderr,"   -r:w    disable corruption detection using word model\n") ;
#ifdef STATISTICS
   fprintf(stderr,"   -s      print search statistics at end of run\n") ;
#endif
   fprintf(stderr,"   -t[N]   test mode -- simulate missing first (or N) bytes\n") ;
   fprintf(stderr,"   -v[N]   run verbosely, at verbosity level N\n") ;
   fprintf(stderr,"   -xp     exclude compressed streams inside PDF files\n") ;
   fprintf(stderr,"   -zl     assume input is in zlib format\n") ;
   fprintf(stderr,"   -zr     assume input is a raw DEFLATE stream\n");
   fprintf(stderr,"   -zz     assume input contains multiple zlib streams\n");
   fprintf(stderr,"   -zZ     allow multiple zlib streams, including fixed-Huffman compression\n") ;
   fprintf(stderr,"\n") ;
   exit(2) ;
}

//----------------------------------------------------------------------

static void parse_output_format(const char *arg, ZipRecParameters &params,
				const char *argv0)
{
   if (!arg || !*arg)
      return ;
   switch (toupper(*arg))
      {
      case '+':
	 show_plaintext_errors = true ;
	 /*FALLTHROUGH*/
      case 'P':
      case 'T':
	 params.write_format = WFMT_PlainText ;
	 break ;
      case 'H':
	 params.write_format = WFMT_HTML ;
	 break ;
      case 'D':
	 params.write_format = WFMT_DecodedByte ;
	 break ;
      case 'L':
	 params.write_format = WFMT_Listing ;
	 break ;
      default:
	 usage(argv0) ;
	 break ;
      }
   return ;
}

//----------------------------------------------------------------------

static void parse_offset_range(const char *arg, ZipRecParameters &params,
			       const char *argv0)
{
   if (!arg || !*arg)
      return ;
   char *end = (char*)arg ;
   uint64_t startoffset = 0 ;
   uint64_t endoffset = ~0ULL ;
   if (*arg != ',')
      startoffset = strtoull(arg,&end,0) ;
   if (*end == ',')
      {
      arg = end+1 ;
      endoffset = strtoull(arg,&end,0) ;
      }
   if (endoffset <= startoffset)
      {
      Fr::FilePath path(argv0) ;
      fprintf(stderr,"%s: end offset must be greater than start offset\n",
	      path.basename()) ;
      }
   else
      {
      params.scan_range_start = startoffset ;
      params.scan_range_end = endoffset ;
      }
   return ;
}

//----------------------------------------------------------------------

static void parse_reconstruction_opts(const char *arg,
				      LanguageIdentifier *&langid,
				      WordLengthModel *&lenmodel,
				      ZipRecParameters &params)
{
   if (!arg || !*arg)
      return ;
   if (*arg == '=')
      {
      if (load_reconstruction_data(arg+1))
	 params.perform_reconstruction = true ;
      }
   else if (*arg == '+')
      {
      if (arg[1] == '+')
	 {
	 params.reconstruct_partial_packet = true ;
	 }
      else if (arg[1] >= '1' && arg[1] <= '9')
	 params.reconstruction_iterations = arg[1] - '0' ;
      else
	 thorough_search() ;
      }
   else if (*arg == '-')
      {
      params.reconstruct_align_discontinuities = false ;
      }
   else if (*arg == '^')
      {
      extern bool aggressive_inference ;
      aggressive_inference = false ;
      }
   else if (*arg == '@')
      {
      extern bool use_local_models, update_local_models ;
      use_local_models = true ;
      if (arg[1] == '@')
	 update_local_models = true ;
      }
   else if (*arg == '#')
      {
      extern bool do_remove_unsupported ;
      do_remove_unsupported = true ;
      }
   else if (*arg == ':')
      {
      if (arg[1] == 'l')
	 {
	 delete lenmodel ;
	 lenmodel = new WordLengthModel ;
	 }
      else if (arg[1] == 'w')
	 {
	 params.use_word_model = false ;
	 }
      else if (arg[1] == 'h')
	 {
	 count_history_bytes = false ;
	 }
      }
   else
      {
      const char *lang_db = arg ;
      if (!*lang_db)
	 lang_db = "languages.db" ;
      delete langid ;
      langid = LanguageIdentifier::load(lang_db,0) ;
      if (langid)
	 params.perform_reconstruction = true ;
      else
	 {
	 fprintf(stderr,"Unable to load language identification database '%s'\n",
	    lang_db) ;
	 params.perform_reconstruction = false ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

static void parse_test_mode(const char *arg,ZipRecParameters &params)
{
   params.test_mode = true ;
   if (isdigit(arg[0]))
      {
      char *end = (char*)arg ;
      params.test_mode_skip = strtol(arg,&end,0) ;
      if (end > arg && *end == '@')
	 {
	 params.test_mode_offset = atoi(end+1) ;
	 }
      if (params.test_mode_offset > 0 &&
	  (params.test_mode_skip < 1 || params.test_mode_skip > 4096))
	 {
	 params.test_mode_skip = 1 ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

static void write_listing_header(const ZipRecParameters &params)
{
   if (params.write_format == WFMT_Listing)
      {
      if (params.test_mode)
	 fprintf(stdout,"***** TEST MODE ***** TEST MODE ***** TEST MODE *****\n") ;
      fprintf(stdout,"  Original       Recoverable          File\n") ;
      fprintf(stdout," ========== ========== ========== ============\n") ;
      }
   return ;
}

//----------------------------------------------------------------------

static void write_listing_footer(const ZipRecParameters &params)
{
   if (params.write_format == WFMT_Listing)
      {
      fprintf(stdout," ========== ========== ========== ============\n") ;
      fprintf(stdout,"%11lu %10lu %10lu\n",
	      (unsigned long)DecodedByte::globalOriginalSize(),
	      (unsigned long)DecodedByte::globalKnownBytes(),
	      (unsigned long)DecodedByte::globalTotalBytes()) ;
      if (params.test_mode)
	 fprintf(stdout,"\n***** TEST MODE ***** TEST MODE ***** TEST MODE *****\n") ;
      }
   return ;
}

//----------------------------------------------------------------------

void print_statistics()
{
   if (!show_stats)
      return ;
   size_t headers = (STAT_COUNT(local_file_header)
		     + STAT_COUNT(central_dir_entry)
		     + STAT_COUNT(zlib_file_header)
		     + STAT_COUNT(gzip_file_header)
		     + STAT_COUNT(ALZip_file_header)
		     + STAT_COUNT(FlateDecode_file_header)
		     + STAT_COUNT(rar_file_header)
		     + STAT_COUNT(SevenZip_signature)) ;
   if (headers > 0 || STAT_COUNT(candidate_dynhuff_packet))
      {
      fflush(stderr) ;
      fflush(stdout) ;
      fprintf(stdout,"-------- Statistics --------\n") ;
      fprintf(stdout,"Found %lu zlib, %lu gzip, %lu ALZip, and %lu FlateDecode headers\n",
	      (unsigned long)STAT_COUNT(zlib_file_header),
	      (unsigned long)STAT_COUNT(gzip_file_header),
	      (unsigned long)STAT_COUNT(ALZip_file_header),
	      (unsigned long)STAT_COUNT(FlateDecode_file_header)) ;
      fprintf(stdout,"Found %lu RAR file headers (%lu RAR markers)\n",
	      (unsigned long)STAT_COUNT(rar_file_header),
	      (unsigned long)STAT_COUNT(rar_marker)) ;
      fprintf(stdout,"Found %lu 7zip and %lu xz signatures\n",
	      (unsigned long)STAT_COUNT(SevenZip_signature),
	      (unsigned long)STAT_COUNT(Xz_signature)) ;
      fprintf(stdout,
	      "Found %lu local and %lu central ZIP file headers\n"
	      "Found %lu end-of-central-directory records\n",
	      (unsigned long)STAT_COUNT(local_file_header),
	      (unsigned long)STAT_COUNT(central_dir_entry),
	      (unsigned long)STAT_COUNT(end_of_central_dir)) ;
      fprintf(stdout,
	      "Found %lu candidate Deflate SYNC markers\n",
	      (unsigned long)STAT_COUNT(Deflate_syncmarker)) ;
      fprintf(stdout,
	      "Recovered %lu uncompressed files, %lu complete compressed files,\n"
	      "  %lu truncated files, and %lu file ends\n",
	      (unsigned long)STAT_COUNT(uncompressed_files_recovered),
	      (unsigned long)STAT_COUNT(complete_comp_files_recovered),
	      (unsigned long)STAT_COUNT(truncated_files_recovered),
	      (unsigned long)STAT_COUNT(file_tails_recovered)) ;
      fprintf(stdout,"Packet counts:") ;
      for (size_t i = 0 ; i <= PACKET_HISTOGRAM_SIZE ; i++)
	 fprintf(stdout," %5lu",(unsigned long)STAT_COUNT(packet_count[i])) ;
      fprintf(stdout,"\n") ;
      fprintf(stdout,"Uncompressed packets:\n") ;
      fprintf(stdout,"  %lu candidates\n",
	      (unsigned long)STAT_COUNT(candidate_uncomp_packet))  ;
      fprintf(stdout,"  %lu considered\n",
	      (unsigned long)STAT_COUNT(considered_uncomp_packet)) ;
      fprintf(stdout,"  %lu valid\n",
	      (unsigned long)STAT_COUNT(valid_uncomp_packet))  ;
      fprintf(stdout,"Fixed-Huffman packets:\n") ;
      fprintf(stdout,"  %lu candidates\n",
	      (unsigned long)STAT_COUNT(candidate_fixed_packet)) ;
      fprintf(stdout,"  %lu considered\n",
	      (unsigned long)STAT_COUNT(considered_fixed_packet)) ;
      fprintf(stdout,"  %lu with valid EOD marker\n",
	      (unsigned long)STAT_COUNT(valid_fixed_EOD_marker)) ;
      fprintf(stdout,"  %lu valid\n",
	      (unsigned long)STAT_COUNT(valid_fixed_packet)) ;
      fprintf(stdout,"Dynamic-Huffman packets:\n") ;
      fprintf(stdout,"  %lu candidates\n",
	      (unsigned long)STAT_COUNT(candidate_dynhuff_packet)) ;
      fprintf(stdout,"  %lu with valid alphabet sizes\n",
	      (unsigned long)STAT_COUNT(sane_dynhuff_packet)) ;
      fprintf(stdout,"    %lu had invalid bit-length tree\n",
	      (unsigned long)STAT_COUNT(invalid_bitlength_tree)) ;
      fprintf(stdout,"    %lu had invalid bit lengths\n",
	      (unsigned long)STAT_COUNT(invalid_bit_lengths)) ;
      fprintf(stdout,"  %lu with valid Huffman tree\n",
	      (unsigned long)STAT_COUNT(valid_huffman_tree)) ;
      fprintf(stdout,"  %lu with valid EOD marker\n",
	      (unsigned long)STAT_COUNT(valid_EOD_marker)) ;
      fprintf(stdout,"  %lu valid\n",
	      (unsigned long)STAT_COUNT(valid_dynhuff_packet)) ;
      if (STAT_COUNT(replacements_needed))
	 {
	 fprintf(stdout,"Reconstruction:\n") ;
	 fprintf(stdout,"  %lu total unknown bytes (%lu in corrupted segments)\n",
		 (unsigned long)STAT_COUNT(unknown_bytes),
		 (unsigned long)STAT_COUNT(corrupted_bytes)) ;
	 fprintf(stdout,"  %lu replacements needed\n",
		 (unsigned long)STAT_COUNT(replacements_needed)) ;
	 fprintf(stdout,"  %lu replacements found, %lu matched across corruption\n",
		 (unsigned long)STAT_COUNT(replacements_found),
		 (unsigned long)STAT_COUNT(replacements_matched)) ;
//	 unsigned long totalbytes = (unsigned long)(STAT_COUNT(bytes_replaced)+STAT_COUNT(reconst_unaltered)) ;
	 unsigned long totalbytes = (unsigned long)(STAT_COUNT(unknown_bytes));
	 fprintf(stdout,"  %lu of %lu bytes replaced (%4.2f%%)\n",
		 (unsigned long)STAT_COUNT(bytes_replaced), totalbytes,
		 100.0*STAT_COUNT(bytes_replaced)/totalbytes) ;
	 if (STAT_COUNT(reconst_bytes))
	    {
	    fprintf(stdout,
		    "  %lu of %lu reconstructed bytes correct (%4.2f%%)\n",
		    (unsigned long)STAT_COUNT(reconst_correct),
		    (unsigned long)STAT_COUNT(reconst_bytes),
		    100.0*STAT_COUNT(reconst_correct)/STAT_COUNT(reconst_bytes));
	    if (STAT_COUNT(reconst_correct_casefolded))
	       {
	       unsigned long total
		  = (unsigned long)(STAT_COUNT(reconst_correct)
				    + STAT_COUNT(reconst_correct_casefolded)) ;
	       fprintf(stdout,
		       "     %lu correct, ignoring case (%4.2f%%)\n",
		       total,(100.0*total/STAT_COUNT(reconst_bytes))) ;
	       }
	    }
	 fprintf(stdout,"  %lu unknown bytes not reconstructed\n",
		 (unsigned long)STAT_COUNT(reconst_unaltered)) ;
	 if (STAT_COUNT(total_bytes) > 0)
	    {
	    double ident_percent = STAT_COUNT(identical_bytes)*100.0/STAT_COUNT(total_bytes) ;
	    fprintf(stdout,"  %lu of %lu bytes (%4.2f%%) were identical to reference\n",
		    (unsigned long)STAT_COUNT(identical_bytes),
		    (unsigned long)STAT_COUNT(total_bytes),ident_percent) ;
	    }
	 }
      }
   print_partial_packet_statistics() ;
   if (time_total > 0.0)
      {
      fprintf(stdout,"Timing:\n") ;
      fprintf(stdout," %8.3fs scanning for members\n",time_scanning) ;
      fprintf(stdout," %8.3fs searching for packets\n",time_searching) ;
      fprintf(stdout," %8.3fs inflating\n",time_inflating) ;
      if (time_reference > 0.0)
	 fprintf(stdout," %8.3fs extracting reference file\n",time_reference) ;
      if (time_corrupt_check > 0.0)
	 fprintf(stdout," %8.3fs checking for corruption\n",time_corrupt_check) ;
      fprintf(stdout," %8.3fs reconstructing\n",time_reconstructing) ;
      if (time_reconstructing > 0.0)
	 {
	 fprintf(stdout,"    %8.3fs building file-specific language models\n",
		 time_reconst_modeling) ;
	 fprintf(stdout,"    %8.3fs applying char-encoding constraints\n",
		 time_validating_encoding) ;
	 fprintf(stdout,"    %8.3fs collecting ngram scores\n",
		 time_reconst_ngram) ;
	 fprintf(stdout,"    %8.3fs collecting wildcard constraints\n",
		 time_reconst_wildcards) ;
	 fprintf(stdout,"    %8.3fs selecting replacements based on scores\n",
		 time_reconst_infer) ;
	 if (time_adj_discont > 0)
	    fprintf(stdout,"    %8.3fs inferring alignment across corrupt areas\n",
		 time_adj_discont) ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

int main(int argc, char **argv)
{
   const char *argv0 = argv[0] ;
   const char *output_directory = "." ;
   FileFormat file_format = FF_Default ;
   bool gzip_by_extension = false ;
   LanguageIdentifier *langid = 0 ;
   WordLengthModel *lenmodel = 0 ;
   ZipRecParameters params ;

   while (argc > 1 && argv[1][0] == '-' && argv[1][1] != '\0')
      {
      switch (argv[1][1])
	 {
	 case 'b':
	    blocking_size = atoi(argv[1]+2) ;
	    break ;
	 case 'd':
	    output_directory = argv[1]+2 ;
	    break ;
	 case 'f':
	    parse_output_format(argv[1]+2,params,argv0) ;
	    break ;
	 case 'g':
	    file_format = FF_gzip ;
	    gzip_by_extension = false ;
	    break ;
	 case 'G':
	    file_format = FF_Default ;
	    gzip_by_extension = true ;
	    break ;
	 case 'j':
	    params.junk_paths = true ;
	    break ;
	 case 'o':
	    params.force_overwrite = true ;
	    break ;
	 case 'O':
	    parse_offset_range(argv[1]+2,params,argv0) ;
	    break ;
	 case 'r':
	    parse_reconstruction_opts(argv[1]+2,langid,lenmodel,params) ;
	    break ;
	 case 's':
	    show_stats = true ;
	    break ;
	 case 't':
	    parse_test_mode(argv[1]+2,params) ;
	    break ;
	 case 'v':
	    if (isdigit(argv[1][2]))
	       verbosity = atoi(argv[1]+2) ;
	    else
	       verbosity++ ;
	    break ;
	 case 'x':
	    if (argv[1][2] == 'p')
	       params.exclude_PDFs = true ;
	    break ;
	 case 'z':
	    if (argv[1][2] == 'l')
	       file_format = FF_Zlib ;
	    else if (argv[1][2] == 'r')
	       file_format = FF_RawDeflate ;
	    else if (argv[1][2] == 'z') // allow multiple streams if -zz
	       file_format = FF_ZlibMulti ;
	    else if (argv[1][2] == 'Z')
	       file_format = FF_ZlibAll ;
	    else
	       file_format = FF_Zlib ;
	    gzip_by_extension = false ;
	    break ;
	 default:
	    usage(argv0) ;
	 }
      argc-- ;
      argv++ ;
      }
   if (argc < 2)
      usage(argv0) ;
   int total_args = argc ;
   int status = 0 ;
   if (total_args > 1)
      write_listing_header(params) ;
   while (argc > 1)
      {
      const char *input_file = argv[1] ;
      if (!*input_file)
	 continue ;
      if (verbosity && total_args > 2)
	 fprintf(stdout,"== %s\n",input_file) ;
      FileFormat format = file_format ;
      if (gzip_by_extension && strlen(input_file) > 2 &&
	  strcasecmp(input_file+strlen(input_file)-2,"gz") == 0)
	 {
	 format = FF_gzip ;
	 }
      NybbleTrie *wordmodel = 0 ;
      if (params.use_word_model)
	 wordmodel = global_word_frequencies ;
      FileInformation fileinfo(input_file,langid,lenmodel,wordmodel,
			       output_directory,format) ;
      if (!recover_file(params,&fileinfo))
	 {
	 fprintf(stderr,"Unable to recover file %s\n",input_file) ;
	 status = 1 ;
	 }
      argv++ ;
      argc-- ;
      }
   delete langid ;
   if (total_args > 1)
      write_listing_footer(params) ;
   print_statistics() ;
   cleanup() ;
   return status ;
}

// end of file ziprec.C //
