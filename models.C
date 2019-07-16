/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: models.C - language-model manipulation			*/
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

#include <cfloat>
#include "dbuffer.h"
#include "models.h"
#include "langident/wildcard.h"
#include "global.h"
#include "framepac/config.h"
#include "framepac/memory.h"
#include "framepac/texttransforms.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest constants						*/
/************************************************************************/

// how many bytes at a time to score for language identification
#define SAMPLE_SIZE 1024

// how many alternative matches to use for computeScore()
#define MAX_AMBIG 6

// how many alternative joint n-grams to take for computeCenterScore()
#define MAX_CENTER_AMBIG 8

// how many alternatives for the first byte will we allow before giving up?
#define MAX_FIRST_AMBIG 2

// max allowed wildcard combos when scoring
#define MAX_SCORE_AMBIG (12*MAX_AMBIG)
#define MAX_CENTER_SCORE_AMBIG (30*MAX_CENTER_AMBIG)

// how many successful ngram ranks do we include in the scoring?  Short-
//   circuit the evaluation once we reach this amount
#define MAX_RANKS 1

// what is the shortest history we'll accept for predicting an unknown byte?
#define MIN_NGRAM_LOCAL 2
#define MIN_NGRAM_GLOBAL 2

#define LENGTH_FACTOR_CACHESIZE 128
#define HISTORY_FACTOR_CACHESIZE 8192

#define RECONST_DISCOUNT 1.5

// fall-back global location to search for language models
#define APPDIR "/usr/share/ziprec/"

/************************************************************************/
/*	Types for this module						*/
/************************************************************************/

struct LocationSpec
   {
   const char *formatstring ;
   unsigned var1, var2, var3, var4 ;
   } ;

/************************************************************************/
/*	Global variables for this module				*/
/************************************************************************/

static Fr::CharPtr current_language_model ;
static size_t *global_ngram_counts = 0 ;
static size_t global_ngram_length = 0 ;
static double *global_ngram_avgfreq = 0 ;
NybbleTrie *global_word_frequencies = 0 ;
LangIDPackedTrie *global_ngrams_forward = 0 ;
LangIDPackedTrie *global_ngrams_reverse = 0 ;

static double center_match_factor_2 = 0.15 ;    // bidirectional models
static double center_match_factor_1 = 0.25 ;    // forward model only
static bool center_match_reverse = false ;

static double global_model_weight = 1.0 ;
static double local_model_weight = 0.05 ;

static double length_weight_factor = 0.4 ;

static bool cached_history_factors_initialized = false ;
static double cached_ratio_factor[HISTORY_FACTOR_CACHESIZE] ;
static double cached_length_factor[LENGTH_FACTOR_CACHESIZE] ;

static double ratio_weight_factor = 0.4 ;

unsigned max_score_ambig = MAX_SCORE_AMBIG ;
unsigned max_center_score_ambig = MAX_CENTER_SCORE_AMBIG ;

// specification of the locations in which to search for language models
static LocationSpec model_locations[] =
   {
      { "%s-%s.lang",		2, 3, 0, 0 },
      { "%s/%s-%s.lang", 	1, 2, 3, 0 },
      { "models/%s-%s.lang",	2, 3, 0, 0 },
      { APPDIR "%s-%s.lang",	2, 3, 0, 0 },
      { "%s-%s.lang",		2, 4, 0, 0 },
      { "%s/%s-%s.lang",	1, 2, 4, 0 },
      { "models/%s-%s.lang",	2, 4, 0, 0 },
      { APPDIR "%s-%s.lang",	2, 4, 0, 0 },
      { "%s.lang",		2, 0, 0, 0 },
      { "%s/%s.lang",		1, 2, 0, 0 },
      { "models/%s.lang",	2, 0, 0, 0 },
      { APPDIR "%s.lang",	2, 0, 0, 0 },
      { "null.lang",		0, 0, 0, 0 },
      { "%s/null.lang",		1, 0, 0, 0 },
      { "models/null.lang",	0, 0, 0, 0 },
      { APPDIR "null.lang",	0, 0, 0, 0 },
      { 0, 0, 0, 0, 0 } // sentinel to mark end of array
   } ;

/************************************************************************/
/*	Utility functions						*/
/************************************************************************/

static uint64_t read_N(FILE *fp,unsigned bytes)
{
   uint64_t value = 0 ;
   for (unsigned i = 0 ; i < bytes && !feof(fp) ; i++)
      {
      int b = fgetc(fp) ;
      if (b == EOF)
	 break ;
      value = (value << 8) | b ;
      }
   return value ;
}

//----------------------------------------------------------------------

uint64_t read_64bits(FILE *fp)
{
   return read_N(fp,8) ;
}

//----------------------------------------------------------------------

uint32_t read_32bits(FILE *fp)
{
   return (uint32_t)read_N(fp,4) ;
}

//----------------------------------------------------------------------

uint16_t read_16bits(FILE *fp)
{
   return (uint16_t)read_N(fp,2) ;
}

/************************************************************************/
/*	Scoring functions						*/
/************************************************************************/

#define compute_ratio_factor(hist)				\
   (((1.0 + ::log(hist)) * ratio_weight_factor)/hist)

#define compute_length_factor(len)				\
   ::exp(len * length_weight_factor)

void precompute_history_factors()
{
   if (!cached_history_factors_initialized)
      {
      for (size_t i = 1 ; i < HISTORY_FACTOR_CACHESIZE ; i++)
	 {
	 cached_ratio_factor[i] = compute_ratio_factor(i) ;
	 }
      for (size_t i = 1 ; i < LENGTH_FACTOR_CACHESIZE ; i++)
	 {
	 cached_length_factor[i] = compute_length_factor(i) ;
	 }
      cached_history_factors_initialized = true ;
      }
   return ;
}

/************************************************************************/
/************************************************************************/

// force 0 to 256 while leaving 1-256 unaltered:
//    (x-1)&0xFF is [255,0,...,255] for [0...256]
//    add one to get [256,1,...,256]
#define adjusted_setsize(x) ((((x)->setSize() - 1) & 0xFF) + 1)

//----------------------------------------------------------------------

static void find_longest_ambiguities(unsigned *ambiguities, size_t num_bytes,
				     size_t min_length, size_t max_length,
				     const WildcardSet **allowed_wild,
				     unsigned max_ambig)
{
   //assert(min_length > 0) ;
   //assert(max_ambig < ULONG_MAX/256) ;
   for (size_t i = 0 ; i + min_length <= num_bytes ; i++)
      {
      size_t ambig = 1 ;
      if (allowed_wild[i])
	 {
	 // limit the amount of ambiguity we allow on the first byte
	 //   to reduce n-gram lookup costs
	 ambig = adjusted_setsize(allowed_wild[i]) ;
	 if (ambig > MAX_FIRST_AMBIG)
	    {
	    ambiguities[i] = 0 ;
	    continue ;
	    }
	 }
      size_t len = 1 ;
      for ( ; len <= max_length && i + len < num_bytes ; len++)
	 {
	 const WildcardSet *wild = allowed_wild[i+len] ;
	 if (wild)
	    {
	    unsigned setsize = adjusted_setsize(wild) ;
	    ambig *= setsize ;
	    if (ambig > (len+1)*max_ambig)
	       break ;
	    }
	 }
      ambiguities[i] = len ;
      }
}

//----------------------------------------------------------------------

static void count_ambiguities(unsigned *ambiguities, size_t num_bytes,
			      const WildcardSet **allowed_wild,
			      unsigned max_ambig)
{
   size_t ambig = 1 ;
   for (size_t i = num_bytes ; i > 0 ; i--)
      {
      size_t bytes = (num_bytes - i) + 1 ;
      if (allowed_wild[i-1])
	 {
	 unsigned setsize = adjusted_setsize(allowed_wild[i-1]) ;
	 ambig = (ambig >= ULONG_MAX / setsize) ? ULONG_MAX : ambig * setsize ;
	 // only start an n-gram at this position if the first byte is
	 //   relatively unambiguous
         ambiguities[i-1] = (setsize <= MAX_FIRST_AMBIG && ambig <= bytes * max_ambig)
	    ? ambig : 0 ; 
	 }
      else
	 {
	 // are there still few enough ambiguities to be worth a try?
	 ambiguities[i-1] = (ambig <= bytes * max_ambig) ? ambig : 0 ;
	 }
      }
   return ;
}

/************************************************************************/
/*	Methods for class BidirModel					*/
/************************************************************************/

BidirModel::BidirModel(const LangIDPackedTrie *gleft, const LangIDPackedTrie *gright)
{ 
   m_file_left = m_file_right = 0 ;
   m_global_left = gleft ; m_global_right = gright ;
   setLengths() ;
   m_center_factor = ((m_global_right && center_match_reverse)
		      ? center_match_factor_2
		      : center_match_factor_1) ; 
   return ;
}

//----------------------------------------------------------------------

void BidirModel::setLengths()
{
   size_t longest = 0 ;
   if (m_file_left)
      longest = m_file_left->longestKey() ;
   if (m_global_left && m_global_left->longestKey() > longest)
      longest = m_global_left->longestKey() ;
   m_forward_len = longest ;
   longest = 0 ;
   if (m_file_right)
      longest = m_file_right->longestKey() ;
   if (m_global_right && m_global_right->longestKey() > longest)
      longest = m_global_right->longestKey() ;
   m_reverse_len = longest ;
   return ;
}

//----------------------------------------------------------------------

bool BidirModel::computeScore(const LangIDPackedTrie *trie, uint8_t *key,
			      size_t num_bytes,
			      const WildcardSet **context_wildcards,
			      ZRScore *scores, double weight)
{
   PackedTrieMatch matches[MAX_AMBIG] ;
   unsigned matchcount
      = trie->enumerate(key,num_bytes,context_wildcards,matches,MAX_AMBIG,true) ;
   if (matchcount == 0 || matchcount > MAX_AMBIG)
      return false ;
   size_t len = (num_bytes<LENGTH_FACTOR_CACHESIZE) ? num_bytes : LENGTH_FACTOR_CACHESIZE ;
   weight = weight * cached_length_factor[len] / matchcount ;
   for (size_t i = 0 ; i < matchcount ; i++)
      {
      const PackedSimpleTrieNode *node = matches[i].node() ;
      uint32_t history_frequency = node->frequency() ;
      double ratio_factor ;
      if (history_frequency < HISTORY_FACTOR_CACHESIZE)
	 {
	 ratio_factor = cached_ratio_factor[history_frequency] ;
	 }
      else
	 {
	 ratio_factor = compute_ratio_factor(history_frequency) ;
	 }
      node->addToScores(trie,scores,ratio_factor * weight) ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool BidirModel::computeCenterScore(const LangIDPackedTrie *trie, uint8_t *key,
				    size_t num_bytes, size_t center_byte,
				    const WildcardSet **context_wildcards,
				    ZRScore *scores, double weight)
{
   PackedTrieMatch matches[MAX_CENTER_AMBIG] ;
   LocalAlloc<uint8_t,8192> keybuf(num_bytes * MAX_CENTER_AMBIG) ;
   if (!keybuf)
      return false ;
   for (size_t i = 0 ; i < lengthof(matches) ; i++)
      {
      matches[i].setKeyBuffer(keybuf + i*num_bytes, num_bytes) ;
      }
   unsigned matchcount
      = trie->enumerate(key,num_bytes,context_wildcards,matches,MAX_CENTER_AMBIG,false) ;
   if (matchcount == 0 || matchcount > MAX_CENTER_AMBIG)
      {
      return false ;
      }
   weight /= (matchcount * global_ngram_avgfreq[num_bytes]) ;
   weight *= num_bytes * num_bytes ;
   for (size_t i = 0 ; i < matchcount ; i++)
      {
      const uint8_t *key = matches[i].key() ;
      // extract the matched byte at the 'center' location and the frequency
      //   of the matched ngram
      uint32_t freq = matches[i].node()->frequency() ;
      scores[key[center_byte]] += (freq * weight) ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool BidirModel::computeScores(bool reverse,
			       const DecodedByte *bytes, size_t max_bytes,
			       const WildcardCollection *context_wildcards,
			       ZRScore *scores, double weight,
			       ContextFlags &context_flags) const
{
   if (max_bytes < MIN_NGRAM_LOCAL)
      return false ;
   LocalAlloc<uint8_t,512> key(max_bytes) ;
   LocalAlloc<const WildcardSet*,512> contexts(max_bytes) ;
   LocalAlloc<unsigned,512> ambiguities(max_bytes) ;
   double discount_factor = (DBYTE_CONFIDENCE_LEVELS + 2) * RECONST_DISCOUNT ;
   const LangIDPackedTrie *file_model ;
   if (reverse)
      {
      for (size_t i = 0 ; i < max_bytes ; i++)
	 {
	 size_t pos = max_bytes - i ;
	 if (bytes[pos].isDiscontinuity())
	    {
	    max_bytes = i ;
	    break ;
	    }
	 key[i] = bytes[pos].byteValue() ;
	 if (bytes[pos].isReconstructed())
	    weight *= (bytes[pos].confidence() / discount_factor) ;
	 const WildcardSet *context = 0 ;
	 if (!bytes[pos].isLiteral())
	    context = context_wildcards->set(bytes[pos].originalLocation()) ;
	 contexts[i] = context ;
	 }
      file_model = fileReverseModel() ;
      }
   else
      {
      for (size_t i = 0 ; i < max_bytes ; i++)
	 {
	 if (bytes[i].isDiscontinuity())
	    {
	    max_bytes = i ;
	    break ;
	    }
	 key[i] = bytes[i].byteValue() ;
	 if (bytes[i].isReconstructed())
	    weight *= (bytes[i].confidence() / discount_factor) ;
	 const WildcardSet *context = 0 ;
	 if (!bytes[i].isLiteral())
	    context = context_wildcards->set(bytes[i].originalLocation()) ;
	 contexts[i] = context ;
	 }
      file_model = fileForwardModel() ;
      }
   count_ambiguities(ambiguities,max_bytes,contexts,max_score_ambig) ;
   unsigned good_contexts = 0 ;
   if (file_model)
      {
      size_t max = max_bytes+1 ;
      if (file_model->longestKey() < max)
	 max = file_model->longestKey() ;
      unsigned ranks = 0 ;
      for (size_t i = max ; i > MIN_NGRAM_LOCAL ; i--)
	 {
	 size_t ofs = max_bytes - (i - 1) ;
	 if (ambiguities[ofs] &&
	     computeScore(file_model,key + ofs,i-1, contexts + ofs,scores,
			  i*weight*local_model_weight))
	    {
	    context_flags.setSide(reverse) ;
	    if (++ranks >= MAX_RANKS)
	       {
	       good_contexts++ ;
	       break ;
	       }
	    }
	 }
      }
   auto global_model = reverse ? globalReverseModel() : globalForwardModel() ;
   if (global_model)
      {
      size_t max = max_bytes+1 ;
      if (global_model->longestKey() < max)
	 max = global_model->longestKey() ;
      unsigned ranks = 0 ;
      for (size_t i = max ; i > MIN_NGRAM_GLOBAL ; i--)
	 {
	 size_t ofs = max_bytes - (i - 1) ;
	 if (ambiguities[ofs] &&
	     computeScore(global_model,key + ofs,i-1, contexts + ofs, scores,
			  i*weight*global_model_weight))
	    {
	    context_flags.setSide(reverse) ;
	    if (++ranks >= MAX_RANKS)
	       {
	       good_contexts++ ;
	       break ;
	       }
	    }
	 }
      }
   return (good_contexts > 0) ;
}

//----------------------------------------------------------------------

bool BidirModel::computeCenterScores(const DecodedByte *bytes,
				     size_t left_size, size_t right_size,
				     const WildcardCollection *context_wildcards,
				     ZRScore *scores, double weight) const
{
   size_t max_len = longestForwardNgram() ;
//   if (max_len > global_ngram_length)
//      max_len = global_ngram_length ;
   if (max_len < 2)
      return false ;
   // collect the wildcard contexts
   int start_offset = -left_size ;
   if (start_offset < (int)(-max_len) + 2)
      start_offset = -max_len + 2 ;
   int end_offset = right_size ;
   if (end_offset > (int)max_len - 2)
      end_offset = max_len - 2 ;
   int byte_count = end_offset - start_offset + 1 ;
   LocalAlloc<uint8_t> key(2*byte_count) ;
   LocalAlloc<const WildcardSet*> contexts(2*byte_count) ;
   LocalAlloc<unsigned> ambiguities(2*byte_count) ;
   uint8_t *key_rev = key + byte_count ;
   const WildcardSet **contexts_rev = contexts + byte_count ;
   unsigned *ambiguities_rev = ambiguities + byte_count ;
   for (int i = start_offset ; i <= end_offset ; i++)
      {
      const WildcardSet *context = 0 ;
      if (bytes[i].isReference())
	 context = context_wildcards->set(bytes[i].originalLocation()) ;
      contexts_rev[end_offset - i] = contexts[i-start_offset] = context ;
      key_rev[end_offset - i] = key[i-start_offset] = bytes[i].byteValue() ;
      }
   find_longest_ambiguities(ambiguities,byte_count,3,max_len,contexts,
			    max_center_score_ambig) ;
   find_longest_ambiguities(ambiguities_rev,byte_count,3,max_len,contexts_rev,
			    max_center_score_ambig) ;
   size_t good_contexts = 0 ;
   // enumerate the possible spans, from maximal n-grams down to
   //   trigrams, which contain the wildcard we're scoring
   weight *= centerMatchFactor() ;
   int shift = end_offset + start_offset ;
   for (int i = max_len ; i >= 3 ; i--)
      {
      int first_shift = 2 - i ;
      if (first_shift < start_offset)
	 first_shift = start_offset ;
      int last_shift = -1 ;
      if (last_shift + i - 1 > end_offset)
	 last_shift = end_offset - i + 1 ;
      for (int start = first_shift ; start <= last_shift ; start++)
	 {
	 // compute the score for the current span
	 int ofs = start - start_offset ;
	 if (ambiguities[ofs] >= (unsigned)i)
	    {
#if 0 // we don't currently have file-model ngram counts
	    if (fileForwardModel() &&
		computeCenterScore(fileForwardModel(),key+ofs,i,-start,
				   contexts+ofs,scores,weight))
	       {
	       good_contexts++ ;
	       }
#endif
	    if (computeCenterScore(globalForwardModel(),key+ofs,i,-start,
				   contexts+ofs,scores,weight))
	       good_contexts++ ;
	    }
	 if (center_match_reverse &&
	     ambiguities_rev[ofs+shift] >= (unsigned)i &&
	     ofs >= -shift)
	    {
	    if (globalReverseModel() &&
		computeCenterScore(globalReverseModel(), key_rev+ofs+shift,
				   i,shift-start,contexts_rev+ofs+shift,
				   scores,weight))
	       {
	       good_contexts++ ;
	       }
#if 0 // we don't currently have file-model ngram counts
	    if (fileReverseModel() &&
		computeCenterScore(fileReverseModel(), key_rev+ofs+shift,
				   i,shift-start,contexts_rev+ofs+shift,
				   scores,weight))
	       {
	       good_contexts++ ;
	       }
#endif
	    }
	 }
      if (good_contexts > 0)
	 break ;
      }
   return (good_contexts > 0) ;
}

/************************************************************************/
/************************************************************************/

static size_t *load_ngram_counts(FILE *fp, size_t &max_length)
{
   max_length = read_32bits(fp) ;
   size_t *counts = new size_t[max_length+1] ;
   if (max_length > 0 && counts)
      {
      for (size_t i = 0 ; i <= max_length ; i++)
	 {
	 counts[i] = (size_t)read_64bits(fp) ;
	 }
      }
   return counts ;
}

//----------------------------------------------------------------------

static void compute_ngram_frequencies(size_t maxlen, const size_t *counts,
				      double *&avgfreq)
{
   avgfreq = new double[maxlen+1] ;
   if (!avgfreq)
      return ;
   avgfreq[0] = DBL_MAX ;
   for (size_t i = 1 ; i <= maxlen ; i++)
      {
      if (counts[i] > 0)
	 avgfreq[i] = counts[0] / (double)counts[i] ;
      else
	 avgfreq[i] = counts[0] ;
      }
   return ;
}

//----------------------------------------------------------------------

static unsigned most_frequent_language(DecodeBuffer &decode_buffer,
				       const LanguageIdentifier *langid,
				       unsigned samples)
{
   // ensure that we have enough data for a proper determination
   size_t filesize = decode_buffer.totalBytes() ;
   if (filesize >= 10 * SAMPLE_SIZE)
      {
      size_t start_offset = 7 * filesize / 8 ;
      size_t step = (filesize - start_offset - SAMPLE_SIZE) / samples ;
      if (step == 0)
	 step = 1 ;
      LanguageScores *top_scores = new LanguageScores(langid->numLanguages());
      char decoded[SAMPLE_SIZE] ;
      bool literals[SAMPLE_SIZE] ;
      for (size_t i = 0 ; i < samples ; i++)
	 {
	 size_t offset = start_offset + (i * step) ; 
	 decode_buffer.convert(offset,SAMPLE_SIZE,'\0',decoded,literals) ;
	 // because UTF-16 encoding of ASCII-only or Latin1-only data
	 //   tends to leave all the null bytes as unknown, which breaks
	 //   the scoring of known bytes only, also identify based on
	 //   the recovered data with all unknown bytes changed to NULs
	 LanguageScores *scores = langid->identify(decoded,SAMPLE_SIZE) ;
	 if (scores)
	    scores->scaleScores(0.5) ; // give lower weight to replaced unknowns
	 else
	    scores = new LanguageScores(langid->numLanguages()) ;
	 size_t start = 0 ;
	 for ( ; ; )
	    {
	    while (start < SAMPLE_SIZE && !literals[start])
	       start++ ;
	    if (start >= SAMPLE_SIZE)
	       break ;
	    size_t end = start + 1 ;
	    while (end < SAMPLE_SIZE && literals[end])
	       end++ ;
	    if (end - start > 2)
	       langid->identify(scores,decoded+start,end-start,
				langid->alignments(),false,true,SAMPLE_SIZE) ;
	    start = end ;
	    }
	 top_scores->addThresholded(scores,scores->highestScore()*0.8) ;
	 delete scores ;
	 }
      unsigned top_lang = top_scores->highestLangID() ;
      delete top_scores ;
      return top_lang ;
      }
   return ~0 ;
}

//----------------------------------------------------------------------

static bool load_reconstruction_data(FILE *fp, const char *filename)
{
   PROGRESS("loading language model\n") ;
   // check for the proper file signature
   char sigbuffer[sizeof(LANGMODEL_SIGNATURE)] ;
   if (fread(sigbuffer,sizeof(char),sizeof(sigbuffer),fp) < sizeof(sigbuffer))
      return false ;
   if (memcmp(LANGMODEL_SIGNATURE,sigbuffer,sizeof(sigbuffer)) != 0)
      return false ;
   // check for the proper format version
   if (fgetc(fp) != (char)LANGMODEL_FORMAT_VERSION)
      return false ;
   // skip the alignment padding
   if (fgetc(fp) == EOF || fgetc(fp) == EOF || fgetc(fp) == EOF)
      return false ;
   // read the offsets of the embedded models
   uint64_t offset_forward = read_64bits(fp) ;
   uint64_t offset_reverse = read_64bits(fp) ;
   uint64_t offset_counts = read_64bits(fp) ;
   uint64_t offset_words = read_64bits(fp) ;
   // load in the language models
   bool success = true ;
   if (offset_forward != 0)
      {
      fseek(fp,offset_forward,SEEK_SET) ;
      global_ngrams_forward = LangIDPackedTrie::load(fp,filename) ;
      if (!global_ngrams_forward)
	 success = false ;
      }
   if (offset_reverse != 0)
      {
      fseek(fp,offset_reverse,SEEK_SET) ;
      global_ngrams_reverse = LangIDPackedTrie::load(fp,filename) ;
      if (!global_ngrams_reverse)
	 success = false ;
      }
   if (offset_counts != 0)
      {
      fseek(fp,offset_counts,SEEK_SET) ;
      global_ngram_counts = load_ngram_counts(fp,global_ngram_length) ;
      if (global_ngram_counts)
	 compute_ngram_frequencies(global_ngram_length,global_ngram_counts,
				   global_ngram_avgfreq) ;
      else
	 success = false ;
      }
   if (offset_words != 0)
      {
      fseek(fp,offset_words,SEEK_SET) ;
      delete global_word_frequencies ;
      NybbleTrie *frequencies = new NybbleTrie ;
      global_word_frequencies = frequencies ;
      if (frequencies)
	 {
	 // read the number of words to expect
	 uint32_t count = read_32bits(fp) ;
	 uint32_t total_tokens = 0 ;
	 for (size_t i = 0 ; i < count && !feof(fp) ; i++)
	    {
	    // read a word record: 64-bit frequency, 16-bit length, and then the word
	    size_t freq = read_64bits(fp) ;
	    total_tokens += freq ;
	    unsigned wordlen = read_16bits(fp) ;
	    if (wordlen > MAX_WORD)
	       {
	       fprintf(stderr,"Invalid data in language file: word length = %u\n",
		       wordlen) ;
	       success = false ;
	       break ;
	       }
	    uint8_t wordbuffer[MAX_WORD] ;
	    if (fread(wordbuffer,sizeof(char),wordlen,fp) < wordlen)
	       {
	       success = false ;
	       break ;
	       }
	    frequencies->insert(wordbuffer,wordlen,(uint32_t)freq,false) ;
	    }
	 frequencies->addTokenCount(total_tokens) ;
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

bool load_reconstruction_data(const char *data_file)
{
   bool success = false ;
   if (data_file && *data_file)
      {
      if (current_language_model &&
	  global_ngrams_forward && global_ngrams_reverse &&
	  strcmp(current_language_model,data_file) == 0)
	 {
	 // no need to re-load the model, we already have it
	 return true ;
	 }
      FILE *fp = fopen(data_file,"rb") ;
      if (fp)
	 {
	 clear_reconstruction_data() ;
	 success = load_reconstruction_data(fp,data_file) ;
	 fclose(fp) ;
	 if (success)
	    {
	    current_language_model = dup_string(data_file) ;
	    }
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

static const char *select_var(unsigned which,
			      const char *arg1, const char *arg2,
			      const char *arg3, const char *arg4)
{
   switch (which)
      {
      case 1:
	 return arg1 ;
      case 2:
	 return arg2 ;
      case 3:
	 return arg3 ;
      case 4:
	 return arg4 ;
      default:
	 return 0 ;
      }
}

//----------------------------------------------------------------------

static char *try_loading(const LocationSpec *locations,
			 const char *dblocation,
			 const char *langname,
			 const char *encoding,
			 const char *alt_encoding)
{
   CharPtr filename ;
   while (locations && locations->formatstring)
      {
      const char *arg1 = select_var(locations->var1,dblocation,langname,
				    encoding,alt_encoding) ;
      const char *arg2 = select_var(locations->var2,dblocation,langname,
				    encoding,alt_encoding) ;
      const char *arg3 = select_var(locations->var3,dblocation,langname,
				    encoding,alt_encoding) ;
      const char *arg4 = select_var(locations->var4,dblocation,langname,
				    encoding,alt_encoding) ;
      filename = aprintf(locations->formatstring,arg1,arg2,arg3,arg4) ;
      if (load_reconstruction_data(filename))
	 break ;
      else
	 {
	 filename = nullptr ;
	 }
      }
   return filename.move() ;
}

//----------------------------------------------------------------------

bool load_reconstruction_data_by_lang(DecodeBuffer &decode_buffer,
				      const LanguageIdentifier *langid,
				      const char *&encoding)
{
   // score N sections of the file, and report on the most frequent one
   unsigned langnum = most_frequent_language(decode_buffer,langid,10) ;
   const char *langname = langid->languageName(langnum) ;
   if (langname)
      {
      encoding = langid->languageEncoding(langnum) ;
      if (verbosity > 0)
	 {
	 cerr << "detected language '" << langname << "' in " << encoding
	      << endl ;
	 }
      const char *alt_enc = strcmp(encoding,"ASCII") == 0 ? "utf8" : encoding ;
      char *langdata = try_loading(model_locations,
				   langid->databaseLocation(),
				   langname, encoding, alt_enc) ;
      bool success = false ;
      if (langdata)
	 {
	 success = true ;
	 cerr << "; loaded language data from " << langdata << endl ;
	 }
      Free(langdata) ;
      return success ;
      }
   return true ;
}

//----------------------------------------------------------------------

void clear_reconstruction_data()
{
   delete global_word_frequencies ;	global_word_frequencies = nullptr ;
   delete global_ngrams_forward ;	global_ngrams_forward = nullptr ;
   delete global_ngrams_reverse ;	global_ngrams_reverse = nullptr ;
   delete global_ngram_counts ;		global_ngram_counts = nullptr ;
   delete global_ngram_avgfreq ;	global_ngram_avgfreq = nullptr ;
   global_ngram_length = 0 ;
   current_language_model = nullptr ;
   return ;
}

//----------------------------------------------------------------------

unsigned set_max_score_ambig(unsigned new_max)
{
   unsigned old_max = max_score_ambig ;
   max_score_ambig = new_max ;
   return old_max ;
}

//----------------------------------------------------------------------

void thorough_search(bool thoro)
{
   if (thoro)
      {
      max_score_ambig = MAX_SCORE_AMBIG * 40 ;
      max_center_score_ambig = MAX_CENTER_SCORE_AMBIG * 40 ;
      }
   else
      {
      max_score_ambig = MAX_SCORE_AMBIG ;
      max_center_score_ambig = MAX_CENTER_SCORE_AMBIG ;
      }
   return ;
}

// end of file models.C //
