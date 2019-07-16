/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: lenmodel.C - length model					*/
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

#include <cmath>
#include "lenmodel.h"

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

#define DELIM_WEIGHT 0.1

/************************************************************************/
/*	Helper functions						*/
/************************************************************************/

#define is_delim8(ch) (ch <= ' ' || ch == '<' || ch == '>')
#define is_delim16(ch) (ch <= ' ' || ch == '<' || ch == '>' || ch == 0x2000)

inline unsigned short get16(const unsigned char *buf, bool big_endian)
{
   if (big_endian)
      return (buf[0] << 8) | buf[1] ;
   else
      return buf[0] | (buf[1] << 8) ;
}

/************************************************************************/
/************************************************************************/

WordLengthModel::WordLengthModel(WordLengthModelType t)
{
   for (size_t i = 0 ; i <= MAX_WORD_LENGTH ; i++)
      {
      m_counts[i] = 0 ;
      m_delims[i] = 0 ;
      }
   m_totalcount = 0 ;
   m_totaldelims = 0 ;
   m_sum_of_lengths = 0 ;
   m_sum_of_delims = 0 ;
   m_type = t ;
   return ;
}

//----------------------------------------------------------------------

WordLengthModel::~WordLengthModel()
{
   return ;
}

//----------------------------------------------------------------------

const unsigned char *WordLengthModel::skipToDelim(const unsigned char *buf,
						  size_t buflen) const
{
   if (m_type == WLMT_8bit)
      return skipToDelim8(buf,buflen) ;
   else
      return skipToDelim16(buf,buflen,m_type==WLMT_BE16) ;
}

//----------------------------------------------------------------------

const unsigned char *WordLengthModel::skipToDelim8(const unsigned char *buf,
						   size_t buflen)
{
   if (buf)
      {
      while (buflen)
	 {
	 unsigned char ch = *buf ;
	 if (is_delim8(ch))
	    return buf ;
	 buf++ ;
	 buflen-- ;
	 }
      }
   return 0 ;
}

//----------------------------------------------------------------------

const unsigned char *WordLengthModel::skipToDelim16(const unsigned char *buf,
						    size_t buflen,
						    bool big_endian)
{
   if (buf)
      {
      while (buflen)
	 {
	 unsigned short ch = get16(buf,big_endian) ;
	 if (is_delim16(ch))
	    return buf ;
	 buf += 2 ;
	 buflen -= 2 ;
	 }
      }
   return 0 ;
}

//----------------------------------------------------------------------

bool WordLengthModel::load(const char *filename)
{
   if (filename && *filename)
      {
//FIXME
      }
   return false ;
}

//----------------------------------------------------------------------

bool WordLengthModel::save(const char *filename) const
{
   if (filename && *filename)
      {
//FIXME
      }
   return false ;
}

//----------------------------------------------------------------------

void WordLengthModel::combine(const WordLengthModel *other)
{
   if (other)
      {
      for (size_t i = 0 ; i <= MAX_WORD_LENGTH ; i++)
	 {
	 m_counts[i] += other->m_counts[i] ;
	 m_delims[i] += other->m_delims[i] ;
	 }
      m_totalcount += other->m_totalcount ;
      m_totaldelims += other->m_totaldelims ;
      m_sum_of_lengths += other->m_sum_of_lengths ;
      m_sum_of_delims += other->m_sum_of_delims ;
      }
   return ;
}

//----------------------------------------------------------------------

void WordLengthModel::scale(double scale_factor)
{
   if (scale_factor >= 1.0 || scale_factor <= 0.0)
      return ;
   if (m_totalcount > 0)
      {
      m_totalcount = 0 ;
      for (size_t i = 0 ; i <= MAX_WORD_LENGTH ; i++)
	 {
	 if (m_counts[i] > 0)
	    {
	    m_counts[i] = ::ceil(scale_factor * m_counts[i]) ;
	    m_totalcount += m_counts[i] ;
	    }
	 }
      m_sum_of_lengths *= scale_factor ;
      }
   if (m_totaldelims > 0)
      {
      m_totaldelims = 0 ;
      for (size_t i = 0 ; i <= MAX_WORD_LENGTH ; i++)
	 {
	 if (m_delims[i] > 0)
	    {
	    m_delims[i] = ::ceil(scale_factor * m_delims[i]) ;
	    m_totaldelims += m_delims[i] ;
	    }
	 }
      m_sum_of_delims *= scale_factor ;
      }
   return ;
}

//----------------------------------------------------------------------

void WordLengthModel::addDelim(size_t len)
{
   if (len == 0)
      return ;
   m_sum_of_delims += len ;
   if (len > MAX_WORD_LENGTH)
      len = MAX_WORD_LENGTH ;
   m_delims[len]++ ;
   m_totaldelims++ ;
   return ;
}

//----------------------------------------------------------------------

void WordLengthModel::addWord(size_t len)
{
   if (len == 0)
      return ;
   m_sum_of_lengths += len ;
   if (len > MAX_WORD_LENGTH)
      len = MAX_WORD_LENGTH ;
   m_counts[len]++ ;
   m_totalcount++ ;
   return ;
}

//----------------------------------------------------------------------

void WordLengthModel::addWords(const unsigned char *buf, size_t buflen)
{
   if (m_type == WLMT_8bit)
      (void)addWords8(buf,buflen,buflen) ;
   else
      (void)addWords16(buf,buflen,buflen,m_type==WLMT_BE16) ;
}

//----------------------------------------------------------------------

const unsigned char *WordLengthModel::addWords(const unsigned char *buf,
					       size_t buflen,
					       size_t maxlen)
{
   if (m_type == WLMT_8bit)
      return addWords8(buf,buflen,maxlen) ;
   else
      return addWords16(buf,buflen,maxlen,m_type==WLMT_BE16) ;
}

//----------------------------------------------------------------------

void WordLengthModel::addWords8(const unsigned char *buf, size_t buflen)
{
   (void)addWords8(buf,buflen,buflen) ;
   return ;
}

//----------------------------------------------------------------------

const unsigned char *WordLengthModel::addWords8(const unsigned char *buf,
						size_t buflen,
						size_t maxlen)
{
   if (maxlen < buflen)
      buflen = maxlen ;
   if (!buf || buflen == 0)
      return 0 ;
   bool in_word = false ;
   size_t wordlen = 0 ;
   while (buflen)
      {
      unsigned char ch = *buf++ ;
      buflen-- ;
      maxlen-- ;
      bool delim = is_delim8(ch) ;
      if (in_word)
	 {
	 if (delim)
	    {
	    addWord(wordlen) ;
	    in_word = false ;
	    wordlen = 1 ;
	    }
	 else
	    {
	    wordlen++ ;
	    }
	 }
      else // if (!in_word)
	 {
	 if (!delim)
	    {
	    addDelim(wordlen) ;
	    in_word = true ;
	    wordlen = 1 ;
	    }
	 else
	    {
	    wordlen++ ;
	    }
	 }
      }
   while (in_word && maxlen > 0)
      {
      unsigned char ch = *buf++ ;
      maxlen-- ;
      bool delim = is_delim8(ch) ;
      if (delim)
	 {
	 addWord(wordlen) ;
	 in_word = false ;
	 }
      else
	 {
	 wordlen++ ;
	 }
      }
   return buf ;
}

//----------------------------------------------------------------------

void WordLengthModel::addWords16(const unsigned char *buf, size_t buflen,
				 bool big_endian)
{
   (void)addWords16(buf,buflen,buflen,big_endian) ;
   return ;
}

//----------------------------------------------------------------------

const unsigned char *WordLengthModel::addWords16(const unsigned char *buf,
						 size_t buflen,
						 size_t maxlen, bool big_endian)
{
   buflen &= ~1 ; // drop any partial final codepoint
   maxlen &= ~1 ; // drop any partial final codepoint
   if (maxlen < buflen)
      buflen = maxlen ;
   if (!buf || buflen == 0)
      return 0 ;
   bool in_word = false ;
   size_t wordlen = 0 ;
   while (buflen)
      {
      unsigned short ch = get16(buf,big_endian) ;
      buf += 2 ;
      buflen -= 2 ;
      maxlen -= 2 ;
      bool delim = is_delim16(ch) ;
      if (in_word)
	 {
	 if (delim)
	    {
	    addWord(wordlen) ;
	    in_word = false ;
	    wordlen = 1 ;
	    }
	 else
	    {
	    wordlen++ ;
	    }
	 }
      else // if (!in_word)
	 {
	 if (!delim)
	    {
	    addDelim(wordlen) ;
	    in_word = true ;
	    wordlen = 1 ;
	    }
	 else
	    {
	    wordlen++ ;
	    }
	 }
      }
   while (in_word && maxlen > 0)
      {
      unsigned short ch = get16(buf,big_endian) ;
      buf += 2 ;
      maxlen -= 2 ;
      bool delim = is_delim16(ch) ;
      if (delim)
	 {
	 addWord(wordlen) ;
	 in_word = false ;
	 }
      else
	 {
	 wordlen++ ;
	 }
      }
   return buf ;
}

//----------------------------------------------------------------------

double WordLengthModel::weight(size_t len) const
{
   if (len <= 1)
      return 0.1 ;
   return 1.0 ;
}

//----------------------------------------------------------------------

double WordLengthModel::similarity(const WordLengthModel *other) const
{
   if (!other)
      return 0.0 ;
   double inner = 0.0 ;
   double len1 = 0.0 ;
   double len2 = 0.0 ;

   if (totalCount() && other->totalCount())
      {
      for (size_t i = 1 ; i <= MAX_WORD_LENGTH ; i++)
	 {
	 double prob1 = probability(i) * weight(i) ;
	 double prob2 = other->probability(i) * weight(i) ;
	 inner += (prob1 * prob2) ;
	 len1 += (prob1 * prob1) ;
	 len2 += (prob2 * prob2) ;
	 }
      }
   if (totalDelims() && other->totalDelims())
      {
      for (size_t i = 1 ; i <= MAX_WORD_LENGTH ; i++)
	 {
	 double prob1 = delimProbability(i) * weight(i) * DELIM_WEIGHT ;
	 double prob2 = other->delimProbability(i) * weight(i) * DELIM_WEIGHT ;
	 inner += (prob1 * prob2) ;
	 len1 += (prob1 * prob1) ;
	 len2 += (prob2 * prob2) ;
	 }
      }
   if (len1 > 0.0 && len2 > 0.0)
      inner /= (::sqrt(len1) * ::sqrt(len2)) ;
   return inner ;
}

// end of file lenmodel.C //
