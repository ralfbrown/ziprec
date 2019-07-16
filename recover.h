/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: recover.h - find and recover archive members			*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-16						*/
/*									*/
/*  (c) Copyright 2011,2012,2013,2019 Carnegie Mellon University	*/
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

#ifndef __RECOVER_H_INCLUDED
#define __RECOVER_H_INCLUDED

#include "lenmodel.h"
#include "ziprec.h"
#include "whatlang2/trie.h"

/************************************************************************/
/************************************************************************/

extern size_t blocking_size ;

/************************************************************************/
/************************************************************************/

enum FileFormat
   {
      FF_Default,
      FF_ZIP,
      FF_gzip,
      FF_RawDeflate,
      FF_Zlib,
      FF_ZlibMulti,	// file may contain multiple Zlib-format streams
      FF_ZlibAll	// may contain multiple stream, incl fixed-Huffman
   } ;

//----------------------------------------------------------------------

class LanguageIdentifier ;

//----------------------------------------------------------------------

class FileInformation
   {
   private:
      LanguageIdentifier *m_langid ;
      WordLengthModel	 *m_lengthmodel ;
      NybbleTrie	 *m_wordmodel ;
      const char	 *m_filename ;
      const char	 *m_output_dir ;
      const char	 *m_orig_output_dir ;
      FileFormat	  m_format ;
      const char	 *m_bufferstart ;
      const char 	 *m_bufferend ;
      bool		  m_stdin ;
   public:
      FileInformation(const char *infile, LanguageIdentifier *id,
		      WordLengthModel *len, NybbleTrie *wordmodel,
		      const char *outdir, FileFormat fmt)
	 {
	 m_filename = infile ; m_langid = id ; m_lengthmodel = len ;
	 m_wordmodel = wordmodel ;
	 m_orig_output_dir = m_output_dir = outdir ; m_format = fmt ;
	 }
      ~FileInformation() {}

      // manipulators
      void setBuffer(const char *s, const char *e)
	 { m_bufferstart = s ; m_bufferend = e ; }
      void usingStdin(bool std) { m_stdin = std ; }
      void replaceOutputDirectory(const char *dir) { m_output_dir = dir ; }
      void restoreOutputDirectory() { m_output_dir = m_orig_output_dir ; }

      // accessors
      LanguageIdentifier *langid() const { return m_langid ; }
      WordLengthModel *lengthmodel() const { return m_lengthmodel ; }
      NybbleTrie *wordmodel() const { return m_wordmodel ; }
      const char *inputFile() const { return m_filename ; }
      const char *outputDirectory() const { return m_output_dir ; }
      FileFormat format() const { return m_format ; }
      const char *bufferStart() const { return m_bufferstart ; }
      const char *bufferEnd() const { return m_bufferend ; }
      bool usingStdin() const { return m_stdin ; }
   } ;
      
/************************************************************************/
/************************************************************************/

bool process_file_data(FileInformation *fileinfo, unsigned &seqnum) ;
bool recover_file(const ZipRecParameters &, FileInformation *fileinfo) ;

#endif /* !__RECOVER_H_INCLUDED */

// end of file recover.h //
