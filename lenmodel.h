/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: lenmodel.h - length model					*/
/*  Version:  1.00gamma				       			*/
/*  LastEdit: 26apr2013							*/
/*									*/
/*  (c) Copyright 2013 Ralf Brown/CMU					*/
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

#include <cstdlib>

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

#define MAX_WORD_LENGTH 24

/************************************************************************/
/************************************************************************/

enum WordLengthModelType
   {
   WLMT_8bit,
   WLMT_BE16,
   WLMT_LE16
   } ;

//----------------------------------------------------------------------

class WordLengthModel
   {
   public:
      WordLengthModel(WordLengthModelType t = WLMT_8bit) ;
      ~WordLengthModel() ;

      // modifiers
      void combine(const WordLengthModel *othermodel) ;
      void scale(double scale_factor) ;
      void addDelim(size_t len) ;
      void addWord(size_t len) ;
      void addWords(const unsigned char *buf, size_t buflen) ;
      const unsigned char *addWords(const unsigned char *buf, size_t buflen,
				    size_t maxlen) ;
      void addWords8(const unsigned char *buf, size_t buflen) ;
      const unsigned char *addWords8(const unsigned char *buf, size_t buflen,
				     size_t maxlen) ;
      void addWords16(const unsigned char *buf, size_t buflen,
		      bool big_endian) ;
      const unsigned char *addWords16(const unsigned char *buf, size_t buflen,
				      size_t maxlen, bool big_endian) ;

      // accessors
      WordLengthModelType type() const { return m_type ; }
      double weight(size_t len) const ;
      size_t maxLength() const { return MAX_WORD_LENGTH ; }
      size_t totalCount() const { return  m_totalcount ; }
      size_t totalDelims() const { return  m_totaldelims ; }
      size_t totalLength() const { return m_sum_of_lengths ; }
      size_t totalDelimLength() const { return m_sum_of_delims ; }
      size_t frequency(size_t len) const { return m_counts[len] ; }
      size_t delimFrequency(size_t len) const { return m_delims[len] ; }
      double probability(size_t len) const
	 { return frequency(len) / (double)m_totalcount ; }
      double delimProbability(size_t len) const
	 { return delimFrequency(len) / (double)totalDelims() ; }
      double averageLength() const
	 { return totalLength() / (double)totalCount() ; }
      double averageDelim() const
	 { return totalDelimLength() / (double)totalDelims() ; }
      double similarity(const WordLengthModel *other) const ;

      // I/O
      const unsigned char *skipToDelim(const unsigned char *buf, size_t buflen) const ;
      static const unsigned char *skipToDelim8(const unsigned char *buf, size_t buflen) ;
      static const unsigned char *skipToDelim16(const unsigned char *buf, size_t buflen,
				       bool big_endian) ;
      bool load(const char *filename) ;
      bool save(const char *filename) const ;

   private:
      size_t	m_counts[MAX_WORD_LENGTH+1] ;
      size_t	m_delims[MAX_WORD_LENGTH+1] ;
      size_t	m_totalcount { 0 } ;
      size_t	m_totaldelims { 0 } ;
      size_t    m_sum_of_lengths { 0 };
      size_t	m_sum_of_delims { 0 } ;
      WordLengthModelType m_type ;
   } ;

// end of file lenmodel.h //
