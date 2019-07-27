/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: reconstruct.C - Lempel-Ziv stream reconstruction		*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-26						*/
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

#include <cfloat>
#include <cmath>
#include "index.h"
#include "models.h"
#include "reconstruct.h"
#include "wildcard.h"
#include "global.h"
#include "framepac/bitvector.h"
#include "framepac/config.h"
#include "framepac/memory.h"
#include "framepac/message.h"
#include "framepac/smartptr.h"
#include "framepac/texttransforms.h"
#include "framepac/timer.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest Constants for this module				*/
/************************************************************************/

#define MAX_LOCAL_NGRAM_LEN 6

// how close to the highest confidence ratio must a wildcard be to
//   be replaced in the current cycle?
#define WILDCARD_SCORE_CUTOFF 0.96

// the highest value to assign for ratio between highest and second
//   highest scores, e.g. when second highest score is zero
#define MAX_RATIO 10000

// how many good contexts do we want to see to declare that a wildcard
//   has good enough statistics even if not all occurrences had good
//   contexts?
//#define DESIRED_CONTEXT_COUNT 10
#define DESIRED_CONTEXT_COUNT 5

#define UNSUPPORTED_CUTOFF 0.2

// maximum ratio between highest and second-highest score to use in scoring
#define MAX_HIGH_RATIO 4.0

#define RATIO_WEIGHT 8.0
#define RATIO_ADJ 1.2
#define HIGHSCORE_ADJ 1.0

/************************************************************************/
/*	Types for this module						*/
/************************************************************************/

class Score
   {
   public:
      Score() { clear() ; }
      ~Score() {}

      // accessors
      bool dirty() const { return m_dirty ; }
      double score(uint8_t byte) const { return m_scores[byte] ; }
      ZRScore *scoreArray() { return m_scores ; }
      double highest()
	 { if (dirty()) findTopScores() ; return m_highest ; }
      double second() const { return m_second ; }
      unsigned indexOfHighest() const { return  m_highindex ; }

      // modifiers
      void markDirty() { m_dirty = true ; }
      void clear() ;
      void clear(uint8_t byte)
	 { m_scores[byte] = 0.0 ; markDirty() ; }
      void set(uint8_t byte, double val)
	 { m_scores[byte] = (ZRScore)val ; markDirty() ; }
      void incr(uint8_t byte, double inc)
	 { m_scores[byte] += (ZRScore)inc ; markDirty() ; }

   protected:
      void findTopScores() ;

   private:
      ZRScore m_scores[256] ;
      ZRScore m_highest ;
      ZRScore m_second ;
      unsigned m_highindex ;
      bool   m_dirty ;
   } ;

//----------------------------------------------------------------------

class ScoreCollection
   {
   public:
      ScoreCollection(unsigned max_ref) ;
      ~ScoreCollection() ;

      // accessors
      unsigned numScores() const { return m_numscores ; }
      Score *scoreArray(unsigned wild)
	 { return (wild < numScores()) ? &m_scores[wild] : &m_scores[0] ; }
      const Score *scoreArray(unsigned wild) const
	 { return (wild < numScores()) ? &m_scores[wild] : &m_scores[0] ; }
      double score(unsigned wild, uint8_t byte) const { return m_scores[wild].score(byte) ; }
      double highest(unsigned wild) { return m_scores[wild].highest() ; }
      double second(unsigned wild) const { return m_scores[wild].second() ; }
      unsigned indexOfHighest(unsigned wild) { return m_scores[wild].indexOfHighest() ; }

      // modifiers
      void incr(unsigned wild, uint8_t byte, double inc)
	 { if (wild < numScores()) m_scores[wild].incr(byte,inc) ; }
      void clear(unsigned wild)
	 { if (wild < numScores()) m_scores[wild].clear() ; }
      void clearAll() ;

   private:
      NewPtr<Score> m_scores ;
      unsigned      m_numscores ;
   } ;

//----------------------------------------------------------------------

class WildcardList
   {
   public:
      WildcardList() = default ;
      ~WildcardList() = default ;

      // accessors
      size_t size() const { return m_count ; }
      size_t allocated() const { return m_maxwild ; }
      unsigned wildcard(size_t index) const
	 { return (index < size()) ? m_wildcards[index] : 0 ; }

      // modifiers
      void clear() ;
      bool expand() ;
      void append(unsigned wildcard)
	 { 
	    if (size() >= allocated() && !expand())
	       return ;
	    m_wildcards[m_count++] = wildcard ;
	 }

   private:
      NewPtr<unsigned> m_wildcards ;
      unsigned         m_count { 0 } ;
      unsigned         m_maxwild { 0 } ;
   } ;

/************************************************************************/
/*	Forward declarations						*/
/************************************************************************/

/************************************************************************/
/*	Global variables for this module				*/
/************************************************************************/

static double score_ratio_factor = 10.0 ;
static double score_value_factor = 0.25 ;

static double mle_ratio_cutoff_incremental = 25.0 ;
static double mle_ratio_cutoff = 1.2 ;

bool use_local_models = false ;
bool update_local_models = false ;
bool do_remove_unsupported = false ;
bool aggressive_inference = true ;

/************************************************************************/
/*	Utility functions						*/
/************************************************************************/

ostream &operator << (ostream &out, const DecodedByte &wc)
{
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
   return out ;
}

/************************************************************************/
/*	Scoring functions						*/
/************************************************************************/

static double replacement_confidence(unsigned wildcard,
				     ScoreCollection *scores,
				     double context_ratio)
{
   double topscore = scores->highest(wildcard) ;
   if (topscore <= 0.0)
      return 0.0 ;
   double secondscore = scores->second(wildcard) ;
   double ratio ;
   if (secondscore > 0.0)
      {
      ratio = topscore / secondscore ;
      if (ratio > MAX_RATIO)
	 ratio = MAX_RATIO ;
      }
   else
      ratio = MAX_RATIO ;
   double conf = (score_ratio_factor * ::log(ratio)
		  + score_value_factor * ::log(1.0+(topscore-secondscore))) ;
   return ::sqrt(context_ratio) * conf ; 
}

/************************************************************************/
/*	Methods for class Score						*/
/************************************************************************/

void Score::clear()
{
   std::fill_n(m_scores,lengthof(m_scores),0.0) ;
   m_highest = 0.0 ;
   m_second = 0.0 ;
   m_highindex = 0 ;
   m_dirty = false ;
   return ;
}

//----------------------------------------------------------------------

void Score::findTopScores()
{
   double hi = m_scores[0] ;
   m_highindex = 0 ;
   double second = -DBL_MAX ;
   for (unsigned i = 1 ; i < lengthof(m_scores) ; i++)
      {
      double sc = m_scores[i] ;
      if (sc > hi)
	 {
	 second = hi ;
	 hi = sc ;
	 m_highindex = i ;
	 }
      else if (sc > second)
	 {
	 second = sc ;
	 }
      }
   m_highest = hi ;
   m_second = second ;
   m_dirty = false ;
   return ;
}

/************************************************************************/
/*	Methods for class ScoreCollection				*/
/************************************************************************/

ScoreCollection::ScoreCollection(unsigned max_ref)
   : m_scores(max_ref)
{
   if (m_scores)
      {
      m_numscores = max_ref ;
      clearAll() ;
      }
   else
      m_numscores = 0 ;
   return ;
}

//----------------------------------------------------------------------

ScoreCollection::~ScoreCollection()
{ 
   m_numscores = 0 ;
   return ;
}

//----------------------------------------------------------------------

void ScoreCollection::clearAll()
{
   for (size_t i = 0 ; i < numScores() ; i++)
      {
      m_scores[i].clear() ;
      }
   return ;
}
   
/************************************************************************/
/*	Member functions for class WildcardList				*/
/************************************************************************/

void WildcardList::clear()
{
   m_wildcards = nullptr ;
   m_maxwild = 0 ;
   m_count = 0 ;
   return ;
}

//----------------------------------------------------------------------

bool WildcardList::expand()
{
   size_t new_size = m_maxwild ? 2 * m_maxwild : 1024 ;
   if (m_wildcards.reallocate(m_maxwild,new_size))
      {
      m_maxwild = new_size ;
      return true ;
      }
   return false ;
}

/************************************************************************/
/*	Character-encoding support					*/
/************************************************************************/

static void eliminate_invalid_UTF8(WildcardCollection *wildcards,
				   DecodeBuffer &decode_buffer)
{
   DecodedByte *file_buffer = decode_buffer.fileBuffer() ;
   size_t num_bytes = decode_buffer.loadedBytes() ;
   // enforce valid multi-byte code points
   for (size_t i = 0 ; i + 1 < num_bytes ; i++)
      {
      if (file_buffer[i].isLiteral())
	 {
	 uint8_t byte = file_buffer[i].byteValue() ;
	 if ((byte & 0x80) == 0)
	    {
	    // following byte must NOT be a continuation byte
	    if (!file_buffer[i+1].isLiteral())
	       wildcards->removeRange(file_buffer[i+1].originalLocation(),
				      0x80,0xBF) ;
	    }
	 else if ((byte & 0xC0) == 0x80) // continuation byte?
	    {
	    // the previous byte must NOT be 7-bit
	    if (i > 0 && !file_buffer[i-1].isLiteral())
	       wildcards->removeRange(file_buffer[i-1].originalLocation(),
				      0x00,0x7F) ;
	    }
	 else
	    {
	    // first byte of a multi-byte code point, so enforce that the
	    //   appropriate number of following bytes are all continuation
	    //   bytes
	    for ( ; (byte & 0x40) && i+1 < num_bytes ; byte<<=1, i++)
	       {
	       if (!file_buffer[i+1].isLiteral())
		  {
		  unsigned wild = file_buffer[i+1].originalLocation() ;
		  wildcards->removeRange(wild,0x00,0x7F) ;
		  wildcards->removeRange(wild,0xC0,0xFF) ;
		  }
	       }
	    }
	 }
      }
   return ;
}

//----------------------------------------------------------------------

static void eliminate_invalid_ASCII16(WildcardCollection *wildcards,
				      DecodeBuffer &decode_buffer)
{
   DecodedByte *file_buffer = decode_buffer.fileBuffer() ;
   size_t num_bytes = decode_buffer.loadedBytes() ;
   // every other byte must be 0x00
   unsigned alignment = 2 ;
   // start by checking which set of bytes are zeros, and that we have
   //   a consistent alignment throughout the file
   for (size_t i = 0 ; i + 1 < num_bytes ; i += 2)
      {
      if (file_buffer[i].isLiteral() && file_buffer[i+1].isLiteral())
	 {
	 uint8_t val1 = file_buffer[i].byteValue() ;
	 uint8_t val2 = file_buffer[i+1].byteValue() ;
	 if (alignment == 2)
	    {
	    if (val1 == 0 && val2 != 0)
	       alignment = 0 ;
	    else if (val1 != 0 && val2 == 0)
	       alignment = 1 ;
	    }
	 else if (alignment == 1 && val1 == 0)
	    return ;
	 else if (alignment == 0 && val2 == 0)
	    return ;
	 }
      }
   if (alignment == 2)
      return ;
   for (size_t i = 0 ; i < num_bytes ; i++)
      {
      if (file_buffer[i].isLiteral())
	 continue ;
      unsigned loc = file_buffer[i].originalLocation() ;
      if ((i % 2) == alignment)
	 wildcards->remove(loc,0x00) ;
      else if (wildcards->setSize(loc) > 1)
	 wildcards->removeRange(loc,0x01,0xFF) ;
      }
   return ;
}

//----------------------------------------------------------------------

static void eliminate_invalid_EUC(WildcardCollection *wildcards,
				  DecodeBuffer &decode_buffer)
{
   DecodedByte *file_buffer = decode_buffer.fileBuffer() ;
   size_t num_bytes = decode_buffer.loadedBytes() ;
   // bytes with high bit set must occur in pairs, except when the first
   //   byte is 0x8E 0r 0x8F
   // first, eliminate any possibility of singleton bytes with the
   //   high bit set
   for (size_t i = 1 ; i+1 < num_bytes ; i++)
      {
      if (file_buffer[i].isReference() &&
	  file_buffer[i-1].isLiteral() && file_buffer[i+1].isLiteral() &&
	  (file_buffer[i-1].byteValue() & 0x80) == 0 &&
	  (file_buffer[i+1].byteValue() & 0x80) == 0)
	 {
	 wildcards->removeRange(file_buffer[i].originalLocation(),
				0x80,0xFF) ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

static void eliminate_invalid_encodings(WildcardCollection *wildcards,
					const char *encoding,
					DecodeBuffer &decode_buffer)
{
   START_TIME(timer) ;
   PROGRESS("   -> applying character-encoding constraints\n") ;
   if (strcasecmp(encoding,"utf-8") == 0 ||
       strcasecmp(encoding,"utf8") == 0)
      {
      eliminate_invalid_UTF8(wildcards,decode_buffer) ;
      }
   else if (strncasecmp(encoding,"ASCII-16",8) == 0)
      {
      eliminate_invalid_ASCII16(wildcards,decode_buffer) ;
      }
   else if (strncasecmp(encoding,"EUC",3) == 0)
      {
      eliminate_invalid_EUC(wildcards,decode_buffer) ;
      }
   // any wildcards which have no remaining possibilities are due to
   //   broken codepoints in the file, so remove restrictions on them
   wildcards->allowAllIfEmpty() ;
   ADD_TIME(timer,time_validating_encoding) ;
   return ;
}

//----------------------------------------------------------------------

static void enforce_CRLF(WildcardCollection *wildcards,
			 DecodeBuffer &decode_buffer)
{
   DecodedByte *file_buffer = decode_buffer.fileBuffer() ;
   size_t num_bytes = decode_buffer.loadedBytes() ;
   for (size_t i = 1 ; i+1 < num_bytes ; i++)
      {
      if (file_buffer[i].isReference())
	 {
	 if (file_buffer[i-1].isLiteral() &&
	     file_buffer[i-1].byteValue() == '\r')
	    {
	    wildcards->remove(file_buffer[i].originalLocation(),'\n') ;
	    }
	 else if (file_buffer[i+1].isLiteral() &&
		  file_buffer[i+1].byteValue() == '\n')
	    {
	    wildcards->remove(file_buffer[i].originalLocation(),'\r') ;
	    }
	 }
      }
   return ;
}

/************************************************************************/
/************************************************************************/

#ifdef STATISTICS
static void count_wildcards(unsigned iteration,
			    const DecodeBuffer &decode_buffer)
{
   if (iteration == 0)
      {
      const WildcardCounts *counts = decode_buffer.wildcardCounts() ;
      if (!counts)
	 return ;
      for (size_t i = 0 ; i < decode_buffer.numReplacements() ; i++)
	 {
	 INCR_STAT_IF(counts->count(i) > 0,replacements_needed) ;
	 }
      }
   return ;
}
#else
#  define count_wildcards(iter,buffer,len) 
#endif

//----------------------------------------------------------------------

static void clear_unused_wildcards(DecodeBuffer &decode_buffer,
				   WildcardCollection *wildcards)
{
   const WildcardCounts *wccounts = decode_buffer.wildcardCounts() ;
   if (wccounts)
      {
      unsigned highest_used = wccounts->highestUsed() ;
      for (size_t i = 0 ; i < decode_buffer.numReplacements() ; i++)
	 {
	 if (wccounts->count(i) == 0 && i > highest_used)
	    {
	    wildcards->removeAll(i) ;
	    }
	 }
      }
   return ;
}

//----------------------------------------------------------------------

static bool enough_contexts(uint32_t seen_contexts,
			    uint32_t occurrences)
{
   // if the wildcard doesn't occur in the recovered text, but we have
   //  a good context anyway due to adjacent wildcards in the pre-recovery
   //  portion of the file buffer, that's enough
   if (seen_contexts > occurrences)
      return true ;
   uint32_t desired = 3 + (occurrences / 2) ;
   return seen_contexts >= desired ;
}

//----------------------------------------------------------------------

static bool remove_unsupported_wildcards(DecodeBuffer &decode_buffer,
					 WildcardCollection *wildcards,
					 const WildcardCounts *contexts,
					 ScoreCollection *scores,
					 double cutoff = UNSUPPORTED_CUTOFF)
{
   if (!wildcards || !scores)
      return false ;
   START_TIME(timer) ;
   size_t removed = 0 ;
   size_t unambig = 0 ;
   const WildcardCounts *wccounts = decode_buffer.wildcardCounts() ;
   for (size_t i = 0 ; i < decode_buffer.numReplacements() ; i++)
      {
      if (contexts &&
	  !enough_contexts(contexts->count(i),wccounts->count(i)))
	 continue ;
      WildcardSet *wcset = wildcards->set(i) ;
      if (wcset->setSize() < 2)
	 continue ;
      double hiscore = scores->highest(i) ;
      double threshold = cutoff * hiscore ;
      for (size_t j = 0 ; j < 256 ; j++)
	 {
	 if (!wcset->contains(j))
	    continue ;
	 double sc = scores->score(i,j) ;
	 if (sc <= 0.0 || sc < threshold)
	    {
	    wcset->remove(j) ;
	    removed++ ;
	    }
	 }
      wcset->cacheSetSize() ;
      }
   if (verbosity > VERBOSITY_PROGRESS)
      {
      if (removed)
	 fprintf(stderr,"      removed %lu wildcard possibilities\n",
		 (unsigned long)removed) ;
      if (unambig)
	 fprintf(stderr,"      replaced %lu unambiguous wildcards\n",
		 (unsigned long)unambig) ;
      }
   ADD_TIME(timer,time_reconst_wildcards) ;
   return (removed > 0) ;
}

//----------------------------------------------------------------------

static void apply_unambiguous_wildcards(DecodeBuffer &decode_buffer,
					WildcardCollection *wildcards,
					WildcardList *active_wildcards)
{
   if (!wildcards)
      return ;
   START_TIME(timer) ;
   size_t unambig = 0 ;
   for (size_t i = 0 ; i < decode_buffer.numReplacements() ; i++)
      {
      if (wildcards->setSize(i) == 1)
	 {
	 unambig++ ;
	 uint8_t wc = wildcards->firstMember(i) ;
	 decode_buffer.setReplacement(i,wc,DBYTE_CONFIDENCE_LEVELS) ;
	 active_wildcards->append(i) ;
	 wildcards->removeAll(i) ;
	 }
      }
   if (unambig && verbosity > VERBOSITY_PACKETS)
      {
      fprintf(stderr,"      replaced %lu unambiguous wildcards\n",
	      (unsigned long)unambig) ;
      }
   ADD_TIME(timer,time_reconst_wildcards) ;
   return ;
}

//----------------------------------------------------------------------

static bool reverse_ngram(const uint8_t *key, unsigned keylen,
			  uint32_t frequency, void *user_data)
{
   NybbleTrie *reverse = (NybbleTrie*)user_data ;
   uint8_t reversed_key[keylen] ;
   for (size_t i = 0 ; i < keylen ; i++)
      {
      reversed_key[i] = key[keylen-i-1] ;
      }
   reverse->insert(reversed_key,keylen,frequency,false) ;
   return true ;
}

//----------------------------------------------------------------------

static void augment_file_models(DecodeBuffer &decode_buffer,
				unsigned max_ngram_len,
				unsigned min_confidence,
				LangIDPackedTrie *&ngrams_forward,
				LangIDPackedTrie *&ngrams_reverse,
				bool &file_uses_CRLF,
				bool &file_uses_CR)
{
   START_TIME(timer) ;
   delete ngrams_forward ; ngrams_forward = nullptr ;
   delete ngrams_reverse ; ngrams_reverse = nullptr ;
   size_t crlf_count = 0 ;
   size_t cr_count = 0 ;
   size_t nl_count = 0 ;
   DecodedByte *file_buffer = decode_buffer.fileBuffer() ;
   size_t num_bytes = decode_buffer.loadedBytes() ;
   if (use_local_models)
      {
      Owned<NybbleTrie> ngrams_left ;
      LocalAlloc <uint8_t> chars(max_ngram_len+1) ;
      for (size_t offset = 0 ; offset < num_bytes ; offset++)
	 {
	 // add ngrams starting at the current position, up to the end of
	 //   the buffer or the next non-literal
	 unsigned max_len = ((offset + max_ngram_len >= num_bytes)
			     ? num_bytes - offset : max_ngram_len) ;
	 unsigned len = 0 ;
	 for (size_t i = 0 ; i < max_len ; i++)
	    {
	    const DecodedByte &db = file_buffer[offset-i] ;
	    if (!db.isLiteral() ||
		(db.isReconstructed() && db.confidence() < min_confidence))
	       break ;
	    chars[max_len-1-(len++)] = db.byteValue() ;
	    }
	 if (len > 0)
	    {
	    ngrams_left->incrementExtensions(chars+max_len-len,0,len,1) ;
	    ngrams_left->addTokenCount() ;
	    }
	 }
      crlf_count = ngrams_left->find((uint8_t*)"\r\n",2) ;
      cr_count = ngrams_left->find((uint8_t*)"\r",1) ;
      nl_count = ngrams_left->find((uint8_t*)"\n",1) ;
      uint64_t total_tokens = ngrams_left->totalTokens() ;
      ngrams_forward = new LangIDPackedTrie(ngrams_left,1,false) ;
      ngrams_left = nullptr ;
      Owned<NybbleTrie> ngrams_right ;
      uint8_t keybuf[max_ngram_len+1] ;
      ngrams_forward->enumerate(keybuf,max_ngram_len,reverse_ngram,ngrams_right) ;
      ngrams_right->addTokenCount(total_tokens) ;
      ngrams_reverse = new LangIDPackedTrie(ngrams_right,1,false) ;
      }
   else
      {
      for (size_t offset = 1 ; offset < num_bytes ; offset++)
	 {
	 if (!file_buffer[offset].isLiteral())
	    continue ;
	 uint8_t byte = file_buffer[offset].byteValue() ;
	 if (byte == '\r')
	    cr_count++ ;
	 else if (byte == '\n')
	    {
	    nl_count++ ;
	    if (file_buffer[offset-1].isLiteral() &&
		file_buffer[offset-1].byteValue() == '\r')
	       crlf_count++ ;
	    }
	 }
      }
   file_uses_CRLF = (crlf_count > 0) ;
   file_uses_CR = !file_uses_CRLF && (cr_count > nl_count) ;
   ADD_TIME(timer,time_reconst_modeling) ;
   return ;
}

//----------------------------------------------------------------------

static bool collect_ngram_counts(DecodeBuffer &decode_buffer,
				 LangIDPackedTrie *&ngrams_left,
				 LangIDPackedTrie *&ngrams_right,
				 bool &file_uses_CRLF,
				 bool &file_uses_CR,
				 unsigned max_ngram_len,
				 bool first = false)
{
   PROGRESS("   -> generating language model for file\n") ;
   (void)first; // keep compiler happy if not collecting stats
   // reset the input file
   file_uses_CRLF = false ;
   DecodedByte *file_buffer = decode_buffer.fileBuffer() ;
   if (!file_buffer)
      return false ;
   const WildcardCounts *wccounts = decode_buffer.wildcardCounts() ;
   if (wccounts && wccounts->highestUsed() == 0)
      {
      decode_buffer.clearLoadedBytes() ;
      return false ;
      }
   size_t num_bytes = decode_buffer.loadedBytes() ;
   // we may have included the wildcards at the start of the loaded bytes,
   //   so only count unknowns in the actual file data
   size_t first_real_byte = decode_buffer.firstRealByte() ;
   // apply existing replacements
   for (size_t i = 0 ; i < num_bytes ; i++)
      {
      INCR_STAT_IF(first&&i>=first_real_byte&&file_buffer[i].isReference(),unknown_bytes) ;
      if (file_buffer[i].isReference())
	 decode_buffer.applyReplacement(file_buffer[i]) ;
      }
   // augment ngram statistics and scan for CR-LF
   augment_file_models(decode_buffer,max_ngram_len,0,
		       ngrams_left,ngrams_right,file_uses_CRLF,
		       file_uses_CR) ;
   if (verbosity >= VERBOSITY_PROGRESS)
      {
      fprintf(stderr,"     (file is using %s line terminators)\n",
	      file_uses_CRLF?"CR-LF":(file_uses_CR?"CR":"LF")) ;
      }
   if (!use_local_models)
      {
      delete ngrams_left ; ngrams_left = nullptr ;
      delete ngrams_right ; ngrams_right = nullptr ;
      }
   return true ;
}

//----------------------------------------------------------------------

static void update_ngram_score(const DecodeBuffer &decode_buffer,
			       size_t offset, const BidirModel &langmodel,
			       const WildcardCollection *context_wildcards,
			       ScoreCollection *scores,
			       WildcardCounts *context_counts,
			       int weight)
{
   DecodedByte *file_buffer = decode_buffer.fileBuffer() ;
   ContextFlags *context_flags = decode_buffer.contextFlags() ;
   size_t total_bytes = decode_buffer.loadedBytes() ;
   if (file_buffer[offset].isReference())
      {
      ContextFlags &cflags = context_flags[offset] ;
      if (weight > 0)
	 cflags.clear() ;
      unsigned wild = file_buffer[offset].originalLocation() ;
      Score *sc = scores->scoreArray(wild) ;
      size_t maxlen = langmodel.longestForwardNgram() ;
      size_t left_size =  maxlen ? maxlen - 1 : 0 ;
      ZRScore *ngram_scores = sc->scoreArray() ;
      if (left_size > offset)
	 left_size = offset ;
      bool good_left = false ;
      if (weight > 0 || cflags.goodLeft())
	 {
	 good_left = langmodel.computeScores(false,
					     file_buffer+offset-left_size,
					     left_size,context_wildcards,
					     ngram_scores,weight,cflags) ;
	 }
      size_t max_len_right = total_bytes - offset ;
      maxlen = langmodel.longestReverseNgram() ;
      size_t right_size = maxlen ? maxlen - 1 : 0 ;
      if (right_size > max_len_right)
	 right_size = max_len_right ;
      bool good_right = false ;
      if (weight > 0 || cflags.goodRight())
	 {
	 good_right = langmodel.computeScores(true,file_buffer+offset,
					      right_size,context_wildcards,
					      ngram_scores,weight,cflags) ;
	 }
      bool good_center = false ;
      if (langmodel.centerMatchFactor() > 0.0)
	 {
	 if (weight > 0 || cflags.goodCenter())
	    {
	    good_center = langmodel.computeCenterScores(file_buffer+offset,
							left_size,right_size,
							context_wildcards,
							ngram_scores,weight) ;
	    if (good_center)
	       cflags.setCenter() ;
	    }
	 }
      else
	 {
	 if (offset > 0 && file_buffer[offset-1].isLiteral())
	    good_left = true ;
	 if (offset + 1 < total_bytes && file_buffer[offset+1].isLiteral())
	    good_right = true ;
	 }
      if ((good_left && good_right) || good_center)
	 {
	 context_counts->incr(wild,weight) ;
	 }
      if (cflags.anyGood())
	 sc->markDirty() ;
      }
   return ;
}

//----------------------------------------------------------------------

static void collect_ngram_scores(const DecodeBuffer &decode_buffer,
				 const WildcardCollection *wildcards,
				 const WildcardCollection *context_wildcards,
				 const BidirModel &langmodel,
				 ScoreCollection *scores,
				 WildcardCounts *context_counts)
{
   START_TIME(timer) ;
   PROGRESS("   -> collecting ngram scores\n") ;
   size_t num_bytes = decode_buffer.loadedBytes() ;
   scores->clearAll() ;
   unsigned max_ambig = set_max_score_ambig(1) ;
//   set_max_score_ambig(2*max_ambig/3) ;
   set_max_score_ambig(max_ambig) ;
   for (size_t i = 0 ; i < num_bytes ; i++)
      {
      update_ngram_score(decode_buffer,i,langmodel,context_wildcards,
			 scores,context_counts,1) ;
      }
   set_max_score_ambig(max_ambig) ;
   if (wildcards)
      {
      // zap any scores which have been ruled out in the wildcard set
      for (size_t i = 0 ; i < decode_buffer.numReplacements() ; i++)
	 {
	 const WildcardSet *set = wildcards->set(i) ;
	 unsigned ss = set->setSize() ;
	 if (ss % 256 != 0)  // ss!=0 && ss!=256
	    {
	    Score *sc = scores->scoreArray(i) ;
	    for (size_t n = 0 ; n < 256 ; n++)
	       {
	       if (!set->contains(n))
		  sc->clear(n) ;
	       }
	    }
         }
      }
   ADD_TIME(timer,time_reconst_ngram) ;
   return ;
}

//----------------------------------------------------------------------

static void update_ngram_scores(const DecodeBuffer &decode_buffer,
				const WildcardCollection *wildcards,
				WildcardList *active_wildcards,
				const BidirModel &langmodel,
				ScoreCollection *scores,
				const WildcardIndex *wildcard_index,
				WildcardCounts *context_counts,
				int weight)
{
   size_t num_bytes = decode_buffer.loadedBytes() ;
   size_t left_range = langmodel.longestForwardNgram() ;
   size_t right_range = langmodel.longestReverseNgram() ;
   ScopedObject<BitVector> already_updated(num_bytes) ;
   for (size_t i = 0 ; i < active_wildcards->size() ; i++)
      {
      unsigned wild = active_wildcards->wildcard(i) ;
      unsigned count = wildcard_index->numLocations(wild) ;
      for (size_t j = 0 ; j < count ; j++)
	 {
	 uint32_t offset = wildcard_index->location(wild,j) ;
	 // because we want to update any wildcards which have
	 //   'offset' in their context window, we need to flip
	 //   left/right ranges
	 size_t startloc = offset > right_range ? (offset - right_range) : 0 ;
	 size_t endloc = ((offset + left_range < num_bytes)
			  ? offset + left_range : num_bytes - 1) ;
	 for (size_t i = startloc ; i <= endloc ; i++)
	    {
	    // we don't need to update the central wildcard if subtracting,
	    //   since we're about to zap its scores anyway, and on the second
	    //   pass it's no longer a wildcard
	    if (i != offset && !already_updated->getBit(i))
	       {
	       update_ngram_score(decode_buffer,i,langmodel,wildcards,
				  scores,context_counts,weight) ;
	       already_updated->setBit(i,true) ;
	       }
	    }
	 }
      }
   return ;
}

//----------------------------------------------------------------------

static void update_ngram_scores(const DecodeBuffer &decode_buffer,
				const WildcardCollection *wildcards,
				WildcardList *active_wildcards,
				const BidirModel &langmodel,
				ScoreCollection *scores,
				const WildcardIndex *wildcard_index,
				WildcardCounts *context_counts)
{
   PROGRESS1("     -> updating ngram scores\n") ;
   START_TIME(timer) ;
   // subtract out the scores for any wildcards in the contexts of the
   //   wildcards we're about to replace
   update_ngram_scores(decode_buffer,wildcards,active_wildcards,
		       langmodel,scores,wildcard_index,context_counts,-1) ;
   // apply the replacements
   for (size_t i = 0 ; i < active_wildcards->size() ; i++)
      {
      unsigned wild = active_wildcards->wildcard(i) ;
      unsigned count = wildcard_index->numLocations(wild) ;
      scores->clear(wild) ;
      for (size_t j = 0 ; j < count ; j++)
	 {
	 uint32_t loc = wildcard_index->location(wild,j) ;
	 decode_buffer.applyReplacement(loc) ;
	 }
      }
   // now add in the updated scores for any wildcards in the contexts of
   //   the just-replaced wildcards
   update_ngram_scores(decode_buffer,wildcards,active_wildcards,
		       langmodel,scores,wildcard_index,context_counts,+1) ;
   active_wildcards->clear() ;
   ADD_TIME(timer,time_reconst_ngram) ;
   return ;
}

//----------------------------------------------------------------------

static bool infer_replacement(DecodeBuffer &decode_buffer,
			      ScoreCollection *scores,
			      size_t wildcard,
			      WildcardList *active_wildcards,
			      unsigned iteration)
{
   double highscore = scores->highest(wildcard) ;
   if (highscore <= 0.0)
      return false ;
   double second = scores->second(wildcard) ;
   double ratio ;
   if (highscore > MAX_HIGH_RATIO * second)
      ratio = MAX_HIGH_RATIO ;
   else
      ratio = highscore / second ;
   ratio -= RATIO_ADJ ;
   highscore *= HIGHSCORE_ADJ ;
   uint32_t occur = decode_buffer.wildcardCounts()->count(wildcard) ;
   double conf = (RATIO_WEIGHT * ratio + (highscore / occur)) ;
   if (conf < 1)
      return false ;
   if (conf > DBYTE_CONFIDENCE_LEVELS)
      conf = DBYTE_CONFIDENCE_LEVELS ;
   if (conf > 8 * iteration)
      conf -= 8 * iteration ;
   else
      conf = 1 ;
   decode_buffer.setReplacement(wildcard,scores->indexOfHighest(wildcard),
				(unsigned)conf) ;
   active_wildcards->append(wildcard) ;
   return true ;
}

//----------------------------------------------------------------------

static double compute_context_ratio(double context_count,
				    uint32_t wc_count)
{
   if (wc_count == 0)
      wc_count = 1 ;
   double ratio1 = context_count / DESIRED_CONTEXT_COUNT ;
   double ratio2 = context_count / wc_count ;
   if (ratio1 > 1.0) ratio1 = 1.0 ;
   return (ratio1 > ratio2) ? ratio1 : ratio2 ;
}

//----------------------------------------------------------------------

static bool can_infer_replacements(DecodeBuffer &decode_buffer,
				   ScoreCollection *scores,
				   WildcardList *active_wildcards,
				   const WildcardCounts *context_counts,
				   unsigned iteration)
{
   PROGRESS2("     -> finding highest-scoring wildcards\n") ;
   START_TIME(timer) ;
   const WildcardCounts *wildcard_counts = decode_buffer.wildcardCounts() ;
   unsigned numrepl = decode_buffer.numReplacements() ;
   unsigned highest_wild = wildcard_counts->highestUsed() ;
   if (highest_wild < numrepl)
      numrepl = highest_wild ;
   size_t num_replaced = 0 ;
   Owned<LanguageScores> conf_scores(numrepl) ;
   if (conf_scores)
      {
      for (size_t i = 1 ; i < numrepl ; i++)
	 {
	 uint32_t context_count = context_counts->count(i) ;
	 if (context_count == 0)
	    continue ;  // would generate a conf of 0.0, which is default
	 uint32_t wc_count = wildcard_counts->count(i) ;
	 double context_ratio
	    = compute_context_ratio(context_count,wc_count) ;
	 conf_scores->setScore(i,replacement_confidence(i,scores,
							context_ratio)) ;
	 }
      // find the very top wildcards by how confident we are in the
      //   accuracy of the best replacement value for them
      conf_scores->sort(WILDCARD_SCORE_CUTOFF) ;
      if (conf_scores->score(0) > 0.0)
	 {
//cerr<<"best conf="<<conf_scores->score(0)<<endl;
	 for (size_t i = 0 ; i < conf_scores->numLanguages() ; i++)
	    {
	    unsigned wild = conf_scores->languageNumber(i) ;
	    if (infer_replacement(decode_buffer,scores,wild,
				  active_wildcards,iteration))
	       num_replaced++ ;
	    }
	 }
      }
   if (num_replaced && verbosity > VERBOSITY_PACKETS)
      {
      fprintf(stderr,"      replaced %lu wildcards\n",
	      (unsigned long)num_replaced) ;
      }
   ADD_TIME(timer,time_reconst_infer) ;
   return num_replaced > 0 ;
}

//----------------------------------------------------------------------

static void infer_most_likely(DecodeBuffer &decode_buffer,
			      ScoreCollection *scores,
			      WildcardList *active_wildcards,
			      double cutoff_ratio, unsigned iteration)
{
   PROGRESS("   -> selecting most likely remaining values as replacements\n") ;
   for (size_t i = 1 ; i < decode_buffer.numReplacements() ; i++)
      {
      if (decode_buffer.haveReplacement(i))
	 continue ;
      double topscore = scores->highest(i) ;
      double secondscore = scores->second(i) ;
      if (topscore > 0.0 &&
	  (secondscore <= 0.0 || topscore / secondscore >= cutoff_ratio))
	 {
	 infer_replacement(decode_buffer,scores,i,active_wildcards,iteration) ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

bool infer_replacements(DecodeBuffer &decode_buffer,
			const char *encoding, unsigned iteration,
			bool last_iteration)
{
   // load the file data and build local adaptive ngram model
   bool file_uses_CR = false ;
   bool file_uses_CRLF = false ;
   LangIDPackedTrie *ngram_counts_forward = nullptr ;
   LangIDPackedTrie *ngram_counts_reverse = nullptr ;
   if (!collect_ngram_counts(decode_buffer,ngram_counts_forward,
			     ngram_counts_reverse,
			     file_uses_CRLF,file_uses_CR,
			     MAX_LOCAL_NGRAM_LEN,(iteration==0)))
      {
      PROGRESS("     nothing to be reconstructed\n") ;
      return false ;
      }
   count_wildcards(iteration,decode_buffer) ;
   BidirModel langmodel(global_ngrams_forward, global_ngrams_reverse) ;
   langmodel.setFileModels(ngram_counts_forward, ngram_counts_reverse) ;
   // since we need to allocate a bunch of large structures, do all
   //   the allocations at once so that we can bail out if there is
   //   not enough memory
   size_t num_wildcards = decode_buffer.referenceWindow() ;
   if (decode_buffer.numReplacements() > num_wildcards)
      num_wildcards = decode_buffer.numReplacements() ;
   Owned<WildcardCollection> allowed_wildcards(num_wildcards,true) ;
   Owned<ScoreCollection> scores(num_wildcards) ;
   Owned<WildcardCounts> context_counts(num_wildcards) ;
   Owned<WildcardList> active_wildcards ;
   Owned<WildcardIndex> wildcard_index(decode_buffer.fileBuffer(),decode_buffer.loadedBytes(),num_wildcards) ;
   bool success = false ;
   if (!allowed_wildcards || !scores || !context_counts || !active_wildcards)
      {
      SystemMessage::no_memory("while allocating working space for inferring replacements") ;
      }
   else
      {
      precompute_history_factors() ;
      eliminate_invalid_encodings(allowed_wildcards,encoding,decode_buffer) ;
      if (file_uses_CRLF)
	 {
	 enforce_CRLF(allowed_wildcards,decode_buffer) ;
	 }
      else if (!strstr(encoding,"16"))
	 allowed_wildcards->removeFromAll(file_uses_CR ? '\n' : '\r') ;
      clear_unused_wildcards(decode_buffer,allowed_wildcards) ;
      apply_unambiguous_wildcards(decode_buffer,allowed_wildcards,active_wildcards) ;
      active_wildcards->clear() ;
      collect_ngram_scores(decode_buffer,allowed_wildcards,allowed_wildcards,langmodel,scores,context_counts) ;
      if (do_remove_unsupported)
	 {
	 WildcardCollection context_wildcards(allowed_wildcards) ;
//FIXME: can we get any traction from remove_unsupp_wc() ?
	 if (remove_unsupported_wildcards(decode_buffer,&context_wildcards,context_counts,scores))
	    {
	    collect_ngram_scores(decode_buffer,allowed_wildcards,&context_wildcards,langmodel,scores,context_counts) ;
	    }
	 }
      PROGRESS("   -> inferring replacements") ;
      PROGRESS1("\n") ;
      size_t steps = 0 ;
      while (can_infer_replacements(decode_buffer,scores,active_wildcards,context_counts,iteration))
	 {
	 success = true ;
	 if (update_local_models && (steps == 2 || steps == 5))
	    {
	    augment_file_models(decode_buffer,MAX_LOCAL_NGRAM_LEN, (3*DBYTE_CONFIDENCE_LEVELS/4),
				ngram_counts_forward, ngram_counts_reverse, file_uses_CRLF, file_uses_CR) ;
	    langmodel.setFileModels(ngram_counts_forward, ngram_counts_reverse) ;
	    }
	 update_ngram_scores(decode_buffer,allowed_wildcards, active_wildcards,langmodel,scores,
			     wildcard_index,context_counts) ;
	 if (aggressive_inference && (steps % 50) == 20)
	    {
	    infer_most_likely(decode_buffer,scores,active_wildcards, mle_ratio_cutoff_incremental,iteration) ;
	    }
	 if (++steps % 100 == 0 && verbosity >= VERBOSITY_PROGRESS && verbosity < VERBOSITY_PACKETS)
	    {
	    fputc('.',stderr) ;
	    fflush(stderr) ;
	    }
	 }
      if (verbosity >= VERBOSITY_PROGRESS && verbosity < VERBOSITY_PACKETS)
	 fputc('\n',stderr) ;

      // at this point, we may still have useable information in the scores,
      //   so just go ahead and replace unknowns with the most likely value
      //   if above our threshold
      if (last_iteration)
	 {
	 infer_most_likely(decode_buffer,scores,active_wildcards, mle_ratio_cutoff,iteration) ;
	 }
      }
   langmodel.deleteFileModels() ;
   return success ;
}

// end of file reconstruct.C //
