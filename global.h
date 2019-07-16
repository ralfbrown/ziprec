/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: global.h - global constants and variables			*/
/*  Version:  1.10bega				       			*/
/*  LastEdit: 28jun2019							*/
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

#ifndef __GLOBAL_H_INCLUDED
#define __GLOBAL_H_INCLUDED

#include "dbyte.h"

/************************************************************************/
/*	Configuration Options						*/
/************************************************************************/

// uncomment the following for EXCRUCIATING detail in the trace output at
//   high (>= 4) verbosity levels
//#define DEBUG 1

// uncommment the following to disable assertions and some trace statements
//   for a marginal boost in speed
//#define NDEBUG

// uncomment the following to enable statistics collection during a run
#ifndef NSTATISTICS
#define STATISTICS
#endif

// uncomment the following to use <PRE> in HTML output (preserves tabs and
//   other whitespace, uses fixed-width font instead of proportional)
#define USE_PRE_TAG

// maximum length of a word, in bytes
#define MAX_WORD 500

#include <assert.h>

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

#define ZIPREC_VERSION "1.00rc1"

#define LANGMODEL_SIGNATURE "ZipRec Language Model Data\n"
#define LANGMODEL_FORMAT_VERSION 2

// set the verbosity levels at which various types of data are dumped
#define VERBOSITY_PROGRESS 1
#define VERBOSITY_SCAN     2
#define VERBOSITY_PACKETS  3
// some trace statements use VERBOSITY_PACKETS+1
#define VERBOSITY_TREE	   5
// some trace statements use VERBOSITY_TREE+1
#define VERBOSITY_SEARCH   7

// permission bits for directory creation
//    use 0777 for very permissive, 0755 for somewhat permissive, and
//    0500 for strict.  The user's umask will of course tighten permissions.
#define MKDIR_MODE		0755

// how many bins do we use for the histogram of number of packets per member?
#define PACKET_HISTOGRAM_SIZE	10

/************************************************************************/
/*	Operating-System Dependencies					*/
/************************************************************************/

#if defined(unix) || defined(__LINUX__) || defined(__linux__)
#  define NULL_DEVICE "/dev/null"
#elif defined(__MSDOS__) || defined(__WINDOWS__) || defined(__NT__)
#  define NULL_DEVICE "nul"
#else
#  error Must define NULL_DEVICE
#endif

/************************************************************************/
/*	Statistics-collection macros					*/
/************************************************************************/

#ifdef STATISTICS
#  define STATISTIC_DECL(x) extern size_t stat__##x ;
#  define STATISTIC(x) size_t stat__##x ;
#  define INCR_STAT(x) ((stat__##x)++)
#  define INCR_STAT_IF(cond,x) if (cond) { ((stat__##x)++) ; }
#  define ADD_TO_STAT(x,amount) ((stat__##x) += amount)
#  define SET_STAT(x,value) ((stat__##x) = value)
#  define CLEAR_STAT(x) ((stat__##x) = 0)
#  define STAT_COUNT(x) (stat__##x)
#define START_TIME(timer) \
   Fr::CpuTimer timer ;
#define ADD_TIME(timer,var) \
   { \
     double t = timer.seconds() ; \
     time_total += t ; \
     var += t ; \
   }
#else
#  define STATISTIC_DECL(x)
#  define STATISTIC(x)
#  define INCR_STAT(x)
#  define INCR_STAT_IF(cond,x)
#  define ADD_TO_STAT(x,amount)
#  define SET_STAT(x,value)
#  define CLEAR_STAT(x)
#  define STAT_COUNT(x) 0
#  define START_TIME(x)
#  define ADD_TIME(x)
#endif

/************************************************************************/
/*	Utility macros							*/
/************************************************************************/

#define PROGRESS(msg) \
	if (verbosity >= VERBOSITY_PROGRESS) fprintf(stderr,msg)

#define PROGRESS1(msg) \
	if (verbosity >= VERBOSITY_PACKETS) fprintf(stderr,msg)

#define PROGRESS2(msg) \
        if (verbosity > VERBOSITY_PACKETS) fprintf(stderr,msg)

/************************************************************************/
/*	Global variables						*/
/************************************************************************/

extern unsigned verbosity ;
extern bool show_stats ;
extern bool count_history_bytes ;
extern bool show_plaintext_errors ;

extern double time_total ;
extern double time_scanning ;
extern double time_searching ;
extern double time_inflating ;
extern double time_reference ;
extern double time_validating_encoding ;
extern double time_reconstructing ;
extern double time_reconst_modeling ;
extern double time_reconst_ngram ;
extern double time_reconst_infer ;
extern double time_reconst_wildcards ;
extern double time_adj_discont ;
extern double time_corrupt_check ;

// optional statistics
STATISTIC_DECL(gzip_file_header)
STATISTIC_DECL(zlib_file_header)
STATISTIC_DECL(ALZip_file_header)
STATISTIC_DECL(FlateDecode_file_header)
STATISTIC_DECL(rar_marker)
STATISTIC_DECL(rar_file_header)
STATISTIC_DECL(lzip_marker)
STATISTIC_DECL(cabinet_marker)
STATISTIC_DECL(SevenZip_signature)
STATISTIC_DECL(Xz_signature)
STATISTIC_DECL(Deflate_syncmarker)
STATISTIC_DECL(local_file_header)
STATISTIC_DECL(central_dir_entry)
STATISTIC_DECL(end_of_central_dir)
STATISTIC_DECL(uncompressed_files_recovered)
STATISTIC_DECL(complete_comp_files_recovered)
STATISTIC_DECL(truncated_files_recovered)
STATISTIC_DECL(file_tails_recovered)
STATISTIC_DECL(candidate_dynhuff_packet)
STATISTIC_DECL(candidate_fixed_packet)
STATISTIC_DECL(considered_fixed_packet)
STATISTIC_DECL(candidate_uncomp_packet)
STATISTIC_DECL(considered_uncomp_packet)
STATISTIC_DECL(valid_huffman_tree)
STATISTIC_DECL(valid_EOD_marker)
STATISTIC_DECL(sane_dynhuff_packet)
STATISTIC_DECL(valid_dynhuff_packet)
STATISTIC_DECL(valid_fixed_packet)
STATISTIC_DECL(valid_fixed_EOD_marker)
STATISTIC_DECL(valid_uncomp_packet)
STATISTIC_DECL(invalid_bitlength_tree)
STATISTIC_DECL(invalid_bit_lengths)
STATISTIC_DECL(total_bytes)
STATISTIC_DECL(identical_bytes)
STATISTIC_DECL(unknown_bytes)
STATISTIC_DECL(corrupted_bytes)
STATISTIC_DECL(bytes_replaced)
STATISTIC_DECL(replacements_needed)
STATISTIC_DECL(replacements_found)
STATISTIC_DECL(replacements_matched)
STATISTIC_DECL(packet_count[PACKET_HISTOGRAM_SIZE+1])
STATISTIC_DECL(reconst_bytes)
STATISTIC_DECL(reconst_correct)
STATISTIC_DECL(reconst_correct_casefolded)
STATISTIC_DECL(reconst_unaltered)

#endif /* !__GLOBAL_H_INCLUDED */

// end of file global.h //
