/************************************************************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.00gamma				       			*/
/*  LastEdit: 29apr2013							*/
/*									*/
/*  (c) Copyright 2012,2013 Ralf Brown/CMU				*/
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
      uint64_t scan_range_start ;
      uint64_t scan_range_end ;

      size_t test_mode_skip ;
      size_t test_mode_offset ;
      size_t reconstruction_iterations ;

      WriteFormat write_format ;

      bool junk_paths ;
      bool force_overwrite ;
      bool exclude_PDFs ;
      bool test_mode ;
      bool perform_reconstruction ;
      bool reconstruct_partial_packet ;
      bool reconstruct_align_discontinuities ;
      bool use_word_model ;

   public: // methods
      ZipRecParameters()
	 {
	    scan_range_start = 0ULL ;
	    scan_range_end = ~0ULL ;

	    reconstruction_iterations = 1 ;
	    test_mode_skip = 1 ;
	    test_mode_offset = 0 ;

	    write_format = WFMT_PlainText ;

	    junk_paths = false ;
	    force_overwrite = false ;
	    exclude_PDFs = false ;
	    test_mode = false ;
	    perform_reconstruction = false ;
	    reconstruct_partial_packet = false ;
	    reconstruct_align_discontinuities = true ;
	    use_word_model = true ;
	 }
      ~ZipRecParameters() {}
   } ;

#endif /* !__ZIPREC_H_INCLUDED */

// end of file ziprec.h //
