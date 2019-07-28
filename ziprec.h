/************************************************************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-28						*/
/*									*/
/*  (c) Copyright 2012,2013,2019 Carnegie Mellon University		*/
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

#ifndef __ZIPREC_H_INCLUDED
#define __ZIPREC_H_INCLUDED

#include "dbyte.h"

class ZipRecParameters
   {
   public: // members
      uint64_t scan_range_start { 0 } ;
      uint64_t scan_range_end ;

      size_t test_mode_skip { 1 } ;
      size_t test_mode_offset { 0 } ;
      size_t reconstruction_iterations { 1 } ;

      WriteFormat write_format { WFMT_PlainText } ;

      mutable const char* base_name { nullptr } ;

      bool junk_paths { false } ;
      bool force_overwrite { false } ;
      bool exclude_PDFs { false } ;
      bool test_mode { false } ;
      bool perform_reconstruction { false } ;
      bool reconstruct_partial_packet { false } ;
      bool reconstruct_align_discontinuities { true } ;
      bool use_word_model { true } ;

   public: // methods
      ZipRecParameters()
	 {
	    scan_range_end = ~0ULL ;
	 }
      ~ZipRecParameters() = default ;
   } ;

#endif /* !__ZIPREC_H_INCLUDED */

// end of file ziprec.h //
