/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: global.C - global variables shared among modules		*/
/*  Version:  1.00gamma				       			*/
/*  LastEdit: 07may2013							*/
/*									*/
/*  (c) Copyright 2011,2012,2013 Ralf Brown/CMU				*/
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

#include "global.h"

/************************************************************************/
/*	Global variables						*/
/************************************************************************/

// control of informational output

unsigned verbosity = 0 ;
bool show_stats = false ;
bool count_history_bytes = true ;

// should erroneous reconstruction be marked with {} in plaintext
//   output in test mode?  Note: does not work properly for multibyte
//   characters when only part of the codepoint is incorrect.
bool show_plaintext_errors = false ;

//----------------------------------------------------------------------

// timing information
double time_total = 0.0 ;
double time_scanning = 0.0 ;
double time_searching = 0.0 ;
double time_inflating = 0.0 ;
double time_reference = 0.0 ;
double time_validating_encoding = 0.0 ;
double time_reconstructing = 0.0 ;
double time_reconst_modeling = 0.0 ;
double time_reconst_ngram = 0.0 ;
double time_reconst_infer = 0.0 ;
double time_reconst_wildcards = 0.0 ;
double time_adj_discont = 0.0 ;
double time_corrupt_check = 0.0 ;

//----------------------------------------------------------------------

// optional statistics
STATISTIC(gzip_file_header)
STATISTIC(zlib_file_header)
STATISTIC(ALZip_file_header)
STATISTIC(FlateDecode_file_header)
STATISTIC(rar_marker)
STATISTIC(rar_file_header)
STATISTIC(lzip_marker)
STATISTIC(cabinet_marker)
STATISTIC(SevenZip_signature)
STATISTIC(Xz_signature)
STATISTIC(Deflate_syncmarker)
STATISTIC(local_file_header)
STATISTIC(central_dir_entry)
STATISTIC(end_of_central_dir)
STATISTIC(uncompressed_files_recovered)
STATISTIC(complete_comp_files_recovered)
STATISTIC(truncated_files_recovered)
STATISTIC(file_tails_recovered)
STATISTIC(candidate_dynhuff_packet)
STATISTIC(candidate_fixed_packet)
STATISTIC(considered_fixed_packet)
STATISTIC(candidate_uncomp_packet)
STATISTIC(considered_uncomp_packet)
STATISTIC(valid_huffman_tree)
STATISTIC(valid_EOD_marker)
STATISTIC(sane_dynhuff_packet)
STATISTIC(valid_dynhuff_packet)
STATISTIC(valid_fixed_packet)
STATISTIC(valid_fixed_EOD_marker)
STATISTIC(valid_uncomp_packet)
STATISTIC(invalid_bitlength_tree)
STATISTIC(invalid_bit_lengths)
STATISTIC(total_bytes)
STATISTIC(identical_bytes)
STATISTIC(unknown_bytes)
STATISTIC(corrupted_bytes)
STATISTIC(bytes_replaced)
STATISTIC(replacements_needed)
STATISTIC(replacements_found)
STATISTIC(replacements_matched)
STATISTIC(packet_count[PACKET_HISTOGRAM_SIZE+1])
STATISTIC(reconst_bytes)
STATISTIC(reconst_correct)
STATISTIC(reconst_correct_casefolded)
STATISTIC(reconst_unaltered)

// end of file global.C //
