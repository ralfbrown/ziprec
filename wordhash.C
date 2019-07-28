/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: wordhash.C - Hash table for words, with possible wildcards	*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-28						*/
/*									*/
/*  (c) Copyright 2011,2013,2019 Carnegie Mellon University		*/
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

#include "wordhash.h"

/************************************************************************/
/*	Methods for class WordString					*/
/************************************************************************/

WordString::WordString()
{
   initClear() ;
}

//----------------------------------------------------------------------

WordString::WordString(const uint8_t* word, unsigned length)
{
   initClear() ;
   if (word && length > 0)
      {
      m_chars.allocate(length) ;
      if (m_chars)
	 {
	 m_length = length ;
	 m_frequency = 1 ;
	 for (size_t i = 0 ; i < length ; i++)
	    {
	    m_chars[i].setByteValue(word[i]) ;
	    }
	 }
      }
   return ;
}

//----------------------------------------------------------------------

WordString::WordString(const WordCharacter* word, unsigned length)
{
   initClear() ;
   if (word && length > 0)
      {
      m_chars.allocate(length) ;
      if (m_chars)
	 {
	 m_length = length ;
	 m_frequency = 1 ;
	 std::copy_n(word,length,m_chars.begin()) ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

WordString::WordString(const WordString *orig)
{
   if (orig)
      {
      m_length = orig->length() ;
      m_frequency = orig->frequency() ;
      m_wildcards = orig->hasWildcards() ;
      m_userflag = orig->isFlagged() ;
      if (m_length > 0 && orig->m_chars)
	 {
	 m_chars.allocate(m_length) ;
	 if (m_chars)
	    {
	    std::copy_n(orig->m_chars.begin(),m_length,m_chars.begin()) ;
	    }
	 else
	    m_length = 0 ;
	 }
      else
	 {
	 m_chars = nullptr ;
	 m_length = 0 ;
	 }
      }
   else
      initClear() ;
   return ;
}

//----------------------------------------------------------------------

WordString::WordString(const WordString *first, WordCharacter separator, const WordString *second)
{
   if (first && second)
      {
      unsigned len1 = first->length() ;
      unsigned len2 = second->length() ;
      m_length = len1 + len2 + 1 ;
      m_frequency = (first->frequency()+second->frequency())/2 ;
      m_wildcards = first->hasWildcards() || second->hasWildcards() ;
      m_userflag = first->isFlagged() || second->isFlagged() ;
      m_chars.allocate(m_length) ;
      if (m_chars)
	 {
	 std::copy_n(first->m_chars.begin(),len1,m_chars.begin()) ;
	 m_chars[len1++] = separator ;
	 std::copy_n(second->m_chars.begin(),len2,m_chars.at(len1)) ;
	 }
      else
	 {
	 m_length = 0 ;
	 }
      }
   else
      initClear() ;
   return ;
}

//----------------------------------------------------------------------

WordString::WordString(const WordString *first, const WordString *second, const WordString *third)
{
   if (first && second && third)
      {
      unsigned len1 = first->length() ;
      unsigned len2 = second->length() ;
      unsigned len3 = third->length() ;
      m_length = len1 + len2 + len3 ;
      m_frequency = (first->frequency()+third->frequency())/2 ;
      m_wildcards = (first->hasWildcards() || second->hasWildcards() ||  third->hasWildcards()) ;
      m_userflag = (first->isFlagged() || second->isFlagged() || third->isFlagged()) ;
      m_chars.allocate(m_length) ;
      if (m_chars)
	 {
	 std::copy_n(first->m_chars.begin(),len1,m_chars.begin()) ;
	 std::copy_n(second->m_chars.begin(),len2,m_chars.at(len1)) ;
	 std::copy_n(third->m_chars.begin(),len3,m_chars.at(len1+len2)) ;
	 }
      else
	 {
	 m_length = 0 ;
	 }
      }
   else
      initClear() ;
   return ;
}

//----------------------------------------------------------------------

WordString::WordString(const WordString &orig)
{
   m_length = orig.length() ;
   m_frequency = orig.frequency() ;
   m_wildcards = orig.hasWildcards() ;
   m_userflag = orig.isFlagged() ;
   if (m_length > 0 && orig.m_chars)
      {
      m_chars.allocate(m_length) ;
      if (m_chars)
	 {
	 std::copy_n(orig.m_chars.begin(),m_length,m_chars.begin()) ;
	 }
      else
	 m_length = 0 ;
      }
   else
      {
      m_chars = nullptr ;
      m_length = 0 ;
      }
   return ;
}

//----------------------------------------------------------------------

WordString::WordString(const WordString &orig, bool add_sentinels)
{
   m_length = orig.length() ;
   size_t ofs = 0 ;
   if (add_sentinels)
      {
      ofs = 1 ;
      m_length += 2 ;
      }
   m_frequency = orig.frequency() ;
   m_wildcards = orig.hasWildcards() ;
   m_userflag = orig.isFlagged() ;
   if (m_length > 0 && orig.m_chars)
      {
      m_chars.allocate(m_length) ;
      if (m_chars)
	 {
	 for (size_t i = 0 ; i < m_length - 2 * ofs ; i++)
	    m_chars[i+ofs] = orig.character(i) ;
	 if (add_sentinels)
	    {
	    m_chars[0] = ' ' ;
	    m_chars[m_length-1] = ' ' ;
	    }
	 }
      else
	 m_length = 0 ;
      }
   else
      {
      m_length = 0 ;
      }
   return ;
}

//----------------------------------------------------------------------

void WordString::initClear()
{
   m_chars = nullptr ;
   m_frequency = 0 ;
   m_length = 0 ;
   m_wildcards = false ;
   m_userflag = false ;
   return ;
}

//----------------------------------------------------------------------

WordString::~WordString()
{
   m_length = 0 ;
   return ;
}

//----------------------------------------------------------------------

void WordString::setCharacter(unsigned N, uint8_t c)
{
   if (N < length())
      {
      m_chars[N].setByteValue(c) ;
      }
   return ;
}

//----------------------------------------------------------------------

bool WordString::applyReplacements(const DecodedByte *repl, size_t num_repl)
{
   bool changed = false ;
   if (repl && num_repl > 0)
      {
      for (size_t i = 0 ; i < m_length ; i++)
	 {
	 const WordCharacter &ch = character(i) ;
	 if (!ch.isLiteral())
	    {
	    size_t loc = ch.originalLocation() ;
	    if (loc <= num_repl)
	       m_chars[i] = repl[num_repl - loc] ;
	    }
	 }
      }
   return changed ;
}

//----------------------------------------------------------------------

bool WordString::trim(unsigned start_pos, unsigned end_pos)
{
   if (start_pos >= end_pos || start_pos >= length())
      return false ;
   for (size_t i = start_pos ; i < end_pos ; i++)
      {
      m_chars[i - start_pos] = m_chars[i] ;
      }
   m_length = (end_pos - start_pos) ;
   return true ;
}

//----------------------------------------------------------------------

int WordString::compareText(const WordString *other) const
{
   unsigned minlength = length() ;
   if (other->length() < minlength)
      minlength = other->length() ;
   for (size_t i = 0 ; i < minlength ; i++)
      {
      const WordCharacter c1 = character(i) ;
      const WordCharacter c2 = other->character(i) ;
      if (c1.isLiteral())
	 {
	 if (c2.isLiteral())
	    {
	    uint8_t b1 = c1.byteValue() ;
	    uint8_t b2 = c2.byteValue() ;
	    if (b1 < b2)
	       return -1 ;
	    else if (b1 > b2)
	       return +1 ;
	    }
	 else
	    return -1 ;		     // literals sort before wildcards
	 }
      else if (c2.isLiteral())
	 {
	 return +1 ;		     // literals sort before wildcards
	 }
      else
	 {
	 // both are wildcards, so sort by location
	 uint32_t loc1 = c1.originalLocation() ;
	 uint32_t loc2 = c2.originalLocation() ;
	 if (loc1 < loc2)
	    return -1 ;
	 else if (loc1 > loc2)
	    return +1 ;
	 }
      }
   // if we get here, the two strings have a common prefix, so the
   //   longer one sorts later
   if (length() < other->length())
      return -1 ;
   else if (length() > other->length())
      return +1 ;
   return 0 ;
}

//----------------------------------------------------------------------

bool WordString::operator == (const WordString &other) const
{
   return other.length() == length() && std::equal(m_chars.begin(),m_chars.begin()+length(),other.m_chars.begin()) ;
}

//----------------------------------------------------------------------

ostream &operator << (ostream &out, const WordString &ws)
{
   for (size_t i = 0 ; i < ws.length() ; i++)
      {
      const WordCharacter &wc = ws.character(i) ;
      if (wc.isLiteral())
	 {
	 unsigned char ch = (unsigned char)wc.byteValue() ;
	 switch (ch)
	    {
	    case '\0':	out << "\\0" ;	break ;
	    case '\t':  out << "\\t" ;	break ;
	    case '\n':  out << "\\n" ;	break ;
	    case '\r':	out << "\\r" ;	break ;
	    case '\\':  out << "\\\\" ;	break ;
	    default:    out << ch ;	break ;
	    }
	 }
      else
	 out << "{@" << wc.originalLocation() << '}' ;
      }
   return out ;
}

/************************************************************************/
/*	Methods for class WordList					*/
/************************************************************************/

WordList::~WordList()
{
   delete next() ;
   return ;
}

//----------------------------------------------------------------------

WordList *WordList::nconc(WordList *other)
{
   WordList *tail = this ;
   if (tail)
      {
      while (tail->next())
	 tail = tail->next() ;
      tail->m_next = other ;
      return this ;
      }
   else
      return other ;
}

//----------------------------------------------------------------------

WordList *WordList::reverse()
{
   WordList *list = this ;
   WordList *prev = nullptr ;
   while (list)
      {
      WordList *nxt = list->next() ;
      list->setNext(prev) ;
      prev = list ;
      list = nxt ;
      }
   return prev ;
}

//----------------------------------------------------------------------

unsigned WordList::listlength() const
{
   unsigned count = 0 ;
   for (const WordList *list = this ; list ; list = list->next())
      {
      count++ ;
      }
   return count ;
}

//----------------------------------------------------------------------

void WordList::setAllFlags() const
{
   for (const WordList *l = this ; l ; l = l->next())
      {
      if (l->string())
	 l->string()->setFlag() ;
      }
   return ;
}

//----------------------------------------------------------------------

void WordList::clearAllFlags() const
{
   for (const WordList *l = this ; l ; l = l->next())
      {
      if (l->string())
	 l->string()->clearFlag() ;
      }
   return ;
}

/************************************************************************/
/*	Methods for class WordHash					*/
/************************************************************************/

WordHash::WordHash()
{

   return ;
}

//----------------------------------------------------------------------

WordHash::~WordHash()
{

   return ;
}

// end of file wordhash.C //
