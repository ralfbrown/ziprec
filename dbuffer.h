/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: dbuffer.h - file-wise buffer for bytes/back-references	*/
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

#ifndef __DBUFFER_H_INCLUDED
#define __DBUFFER_H_INCLUDED

#include <memory.h>
#include "dbyte.h"
#include "framepac/cstring.h"
#include "framepac/file.h"

//----------------------------------------------------------------------

class ContextFlags
   {
   private:
      uint8_t m_flags ;
   public:
      enum { left = 1, right = 2, center = 4 } ;
      ContextFlags() { clear() ; }

      // accessors
      bool goodLeft() const { return (m_flags & left) != 0 ; }
      bool goodRight() const { return (m_flags & right) != 0 ; }
      bool goodCenter() const { return (m_flags & center) != 0 ; }
      bool anyGood() const { return m_flags != 0 ; }

      // modifiers
      void clear() { m_flags = 0 ; }
      void clear(size_t N) { memset(this,'\0',N) ; }
      void setLeft() { m_flags |= left ; }
      void setRight() { m_flags |= right ; }
      void setSide(bool rght) { m_flags |= (rght ? right : left) ; }
      void setCenter() { m_flags |= center ; }
   } ;

//----------------------------------------------------------------------

class WildcardCounts
   {
   private:
      uint32_t *m_counts ;
      unsigned m_numcounts ;
      mutable unsigned m_prevhighest ;
      bool     m_known_highest ;
   public:
      WildcardCounts(unsigned size) ;
      ~WildcardCounts() ;

      // accessors
      unsigned numCounts() const { return  m_numcounts ; }
      uint32_t count(unsigned wild) const { return m_counts[wild] ; }
      unsigned highestUsed() const ;  //(updates m_prevhighest)

      // modifiers
      void clear() ;
      void clear(unsigned wild)
	 { if (wild < numCounts()) m_counts[wild] = 0 ; }
      void incr(unsigned wild, uint32_t inc = 1) { m_counts[wild] += inc ; }
      void decr(unsigned wild, uint32_t dec = 1) { m_counts[wild] -= dec ; }
      void setHighestUsed() ;
      bool expandTo(unsigned new_size) ;
      bool expand(unsigned extra) { return expandTo(numCounts()+extra) ; }
   } ;

//----------------------------------------------------------------------

class DecodeBuffer
   {
   public: // methods
      DecodeBuffer(Fr::CFile& fp, WriteFormat = WFMT_PlainText,
		   unsigned char unk = DEFAULT_UNKNOWN,
		   const char *friendly_filename = 0,
		   bool deflate64 = true,
		   bool test_mode = false) ;
      ~DecodeBuffer() ;

      // accessors
      unsigned referenceWindow() const { return m_refwindow ; }
      bool deflate64() const { return m_deflate64 ; }
      Fr::CFile& inputFile() { return m_infp ; }
      Fr::CFile& outputFile() { return m_outfp ; }
      unsigned offset() const { return m_bufptr ; }
      WriteFormat writeFormat() const { return m_format ; }
      unsigned char unknownChar() const { return m_unknown ; }
      DecodedByte *fileBuffer() const { return m_filebuffer ; }
      const DecodedByte *replacements() const { return m_replacements ; }
      bool haveReplacement(size_t which) const
	 { return replacements() && which <= numReplacements()
	       ? replacements()[which].isLiteral() : false ; }
      bool inferredLiteral(size_t which) const
	 { return replacements() && which <= numReplacements() &&
	       replacements()[which].isInferredLiteral() ; }
      ContextFlags *contextFlags() const { return m_context_flags ; }
      ContextFlags contextFlags(size_t which) const
         { return m_context_flags[which] ; }
      size_t numReplacements() const { return m_numreplacements ; }
      size_t highestReplacement() const { return m_highest_replaced ; }
      size_t highestReplacement(unsigned num_discont,
				unsigned max_backref) const ;
      unsigned countReplacements(unsigned num_discont,
				 unsigned max_backref = 0) const ;
      size_t totalBytes() const { return m_numbytes ; }
      size_t loadedBytes() const { return m_loadedbytes ; }
      size_t firstRealByte() const { return loadedBytes() - totalBytes() ; }
      unsigned discontinuities() const ;
      DecodedByte *copyReplacements() const ;
      const char *friendlyFilename() const { return m_filename ; }
      const WildcardCounts *wildcardCounts() const
	 { return m_wildcardcounts ; }
      unsigned copyBufferTail(unsigned char *result,
			      unsigned num_bytes) const ;

      // manipulators
      void rewind() { m_bufptr = 0 ; }
      void rewindInput() ;
      unsigned char setUnknownChar(unsigned char unk) ;
      bool openInputFile(Fr::CFile& fp, const char *filename) ;
      bool setOutputFile(Fr::CFile& fp, WriteFormat fmt, unsigned char unk = '?',
			 const char *friendlyfile = 0,
			 const char *encoding_name = 0, bool test_mode = false) ;
      DecodedByte *loadBytes(bool sentinel = false, bool include_wild = true) ;
      void clearLoadedBytes() ;
      void clearContextFlags() ;
      bool setReplacements(const DecodedByte *repl, size_t num_repl,
			   bool init = true) ;
      bool expandReplacements(size_t added_repl) ;
      bool setReplacement(size_t which, const DecodedByte &repl) ;
      bool setReplacement(size_t which, uint8_t c, unsigned confidence) ;
      bool clearReplacements(unsigned which_discontinuity) ;
      bool setInferredLiterals(unsigned which_discontinuity,
			       const DecodedByte *bytes,
			       size_t num_bytes, unsigned offset) ;
      bool addByte(DecodedByte b) ;
      bool addByte(unsigned char b) ;
      bool addByte(unsigned char b, unsigned confidence) ;
      bool addDiscontinuityMarker(unsigned max_backref, bool clear) ;
      bool addString(const char *s) ;
      bool addString(const char *s, unsigned confidence) ;
      bool outputString(const char *s, unsigned confidence) ;
      bool copyString(unsigned length, unsigned offset) ;
      bool applyReplacements(const char *reference_filename,
			     bool include_predecessors = true) ;
      bool applyReplacement(DecodedByte &db) const ;
      bool applyReplacement(uint32_t loc) const ;
      bool writeUpdatedByte(size_t which) ;
      void clearReferenceWindow(bool init = false) ;
      void rewindReferenceWindow() ;
      bool alignDiscontinuity(unsigned which, unsigned corruptionsize,
			      double compression_ratio) ;
      bool alignDiscontinuities() ;

      // I/O
      bool finalize() ;
      bool convert(size_t offset, size_t length, unsigned char unk,
		   char *result, bool *literals = 0) ;
      bool writeReplacements(size_t num_discontinuities,
			     unsigned max_backref, FILE *reffp = 0) ;
      void compareToReference(DecodedByte db, FILE *reffp, bool replaced) ;

   private: // methods
      bool finalizeDB() ;

   private:
      DecodedByte    *m_buffer ;
      DecodedByte    *m_filebuffer ;
      ContextFlags   *m_context_flags ;
      DecodedByte    *m_replacements ;
      Fr::CFile       m_infp ;
      Fr::CFile       m_outfp ;
      const char     *m_filename ;	// filename for WFMT_Listing
      Fr::CharPtr     m_backingfile ;
      WildcardCounts *m_wildcardcounts ;
      unsigned        m_bufptr ;
      unsigned	      m_refwindow ;
      size_t          m_numreplacements ;
      size_t          m_numbytes ;
      size_t	      m_loadedbytes ;
      off_t	      m_datastart ;
      unsigned	      m_highest_replaced ;
      unsigned	      m_discontinuities ;
      WriteFormat     m_format ;
      unsigned char   m_unknown ;
      bool	      m_deflate64 ;
      bool	      m_prev_correct ;
      bool	      m_show_errors ;
   } ;

#endif /* !__DBUFFER_H_INCLUDED */

// end of file dbuffer.h //
