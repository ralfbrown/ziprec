/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: wordhash.h - Hash table for words, with possible wildcards	*/
/*  Version:  1.00gamma				       			*/
/*  LastEdit: 28apr2013							*/
/*									*/
/*  (c) Copyright 2011,2013 Ralf Brown/CMU				*/
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

#ifndef __WORDHASH_H_INCLUDED
#define __WORDHASH_H_INCLUDED

#include <iostream>

#ifndef __DBYTE_H_INCLUDED
#include "dbyte.h"
#endif

/************************************************************************/
/*	Types								*/
/************************************************************************/

// we abstract the character type that forms a string to allow a future
//   variant that is based on code points without modifying the rest of
//   the code
class WordCharacter : public DecodedByte
   {
   private:
      // no additional members
   public:
      WordCharacter() : DecodedByte() {}
      WordCharacter(const WordCharacter &orig) : DecodedByte(orig) {}
      WordCharacter(uint8_t byte) : DecodedByte(byte) {}
      ~WordCharacter() {}

      WordCharacter &operator = (const DecodedByte &orig)
	 { this->DecodedByte::operator = (orig) ; return *this ; }
      WordCharacter &operator = (uint8_t byte)
	 { this->DecodedByte::operator = (byte) ; return *this ; }

      bool operator == (const WordCharacter &other) const
	 { return originalLocation() == other.originalLocation() ; }
      bool operator != (const WordCharacter &other) const
	 { return originalLocation() != other.originalLocation() ; }
   } ;

//----------------------------------------------------------------------

class WordString
   {
   public:
      WordString() ;
      WordString(const WordString *) ;
      WordString(const WordString *, WordCharacter, const WordString *) ;
      WordString(const WordString *, const WordString *, const WordString *) ;
      WordString(const WordString &) ;
      WordString(const WordString &, bool add_sentinels) ;
      WordString(const uint8_t *word, unsigned length) ;
      WordString(const WordCharacter *word, unsigned length) ;
      ~WordString() ;

      // accessors
      bool hasWildcards() const { return m_wildcards ; }
      bool isFlagged() const { return m_userflag ; }
      size_t frequency() const { return m_frequency ; }
      unsigned length() const { return m_length ; }
      const WordCharacter *string() const { return m_chars ; }
      const WordCharacter &character(unsigned idx) const
	 { return m_chars[idx] ; }

      // modifiers
      void addOccurrence() { m_frequency++ ; }
      void setCharacter(unsigned N, uint8_t c) ;
      void setFrequency(size_t f) { m_frequency = f ; }
      void setFlag() { m_userflag = true ; }
      void clearFlag() { m_userflag = false ; }
      bool applyReplacements(const DecodedByte *repl, size_t num_repl) ;
      bool trim(unsigned start, unsigned end) ;

      // comparison
      int compareText(const WordString *other) const ;
      bool operator == (const WordString &other) const ;

   private:
      void initClear() ;

   private:
      Fr::NewPtr<WordCharacter> m_chars ;
      size_t	     m_frequency ;
      unsigned       m_length ;
      bool	     m_wildcards ;
      bool	     m_userflag ;
   } ;

//----------------------------------------------------------------------

class WordList
   {
   public:
      WordList() = default ;
      WordList(WordString *) ;
      WordList(const WordString *) ;
      ~WordList() ;

      // accessors
      WordList *next() const { return m_next ; }
      WordString *string() const { return m_string ; }
      unsigned listlength() const ;

      // manipulators
      void setNext(WordList *nxt) { m_next = nxt ; }
      WordList *nconc(WordList *other) ;
      WordList *reverse() ;
      void clearString() { m_string = nullptr ; }
      void eraseList() ; // applies delete to each member of list

      void setAllFlags() const ;
      void clearAllFlags() const ;

   private:
      WordList*   m_next { nullptr } ;
      WordString* m_string { nullptr } ;
   } ;

//----------------------------------------------------------------------

class WordHash
   {
   public:
      WordHash() ;
      ~WordHash() ;

   private:
      size_t        m_size ;		// allocated size
      size_t        m_members ;		// current size
   } ;

/************************************************************************/
/************************************************************************/

ostream &operator << (ostream &, const WordString &) ;

#endif /* !__WORDHASH_H_INCLUDED */

// end of file wordhash.h //
