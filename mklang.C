/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: mklang.C - generate language data for reconstruction		*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-28						*/
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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "global.h"
#include "pstrie.h"
#include "sort.h"
#include "wildcard.h"
#include "wordhash.h"
#include "words.h"
#include "ziprec.h"
#include "whatlang2/langid.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest constants					        */
/************************************************************************/

#define BUFFER_SIZE (1024*1024)
#define BUFFER_HIGHWATER (15*BUFFER_SIZE/16)

// how many words to collect before sorting and merging a batch
#define SORT_INTERVAL 250000

// zero out any ngram counts of less than N
#define DEFAULT_FILTER_THRESHOLD  1

#define DEFAULT_MAX_NGRAM 6

/************************************************************************/
/*	Type declarations						*/
/************************************************************************/

typedef unsigned char uchar ;

/************************************************************************/
/*	Global variables						*/
/************************************************************************/

static NewPtr<size_t> ngram_counts ;
static NewPtr<uint16_t> trigram_counts ;
static bool store_unfiltered_counts = false ;

static WordList *frequencies = nullptr ;
static WordList *words = nullptr ;  // intermediate, gets merged into 'frequencies'
static size_t wordcount = 0 ;

/************************************************************************/
/************************************************************************/

static void usage(const char *argv0)
{
   fprintf(stderr,"MkLang v" ZIPREC_VERSION " -- make language data for ZipRecover -- GPLv3\n") ;
   fprintf(stderr,
	   "Usage: %s [options] langmodel trainfile [trainfile ...]\n"
	   "Options:\n"
	   "  -d   display the word frequency list to standard output\n"
	   "  -f   build ngram model in forward direction only\n"
	   "  -mN  filter out ngrams occurring fewer than N times\n"
	   "  -nN  count ngrams up to length N (default %u)\n",
	   argv0,DEFAULT_MAX_NGRAM) ;
   exit(1) ;
}

//----------------------------------------------------------------------

static size_t count_words(const WordList *words)
{
   return words ? words->listlength() : 0 ;
}

//----------------------------------------------------------------------

static int compare_words(const WordString *w1, const WordString *w2)
{
   return w1->compareText(w2) ;
}
 
//----------------------------------------------------------------------
 
static void merge_word_lists(WordList *&words1, WordList *&words2)
{
   //cout << '.' << flush ;
   words2 = sort_words(words2,compare_words) ;
   words2 = merge_duplicates(words2) ;
   words1 = merge_lists(words1,words2,compare_words) ;
   words1 = merge_duplicates(words1) ;
   words2 = nullptr ;
   return ;
}

//----------------------------------------------------------------------

static void make_word(uint8_t *word, unsigned wordlen, bool &had_text)
{
   if (wordlen && wordlen < MAX_WORD)
      {
      WordString *ws = new WordString(word,wordlen) ;
      WordList *newword = new WordList(ws) ;
      newword->setNext(words) ;
      words = newword ;
      wordcount++ ;
      if (wordcount >= SORT_INTERVAL)
	 {
	 merge_word_lists(frequencies,words) ;
	 wordcount = 0 ;
	 }
      had_text = true ;
      }
   return ;
}

//----------------------------------------------------------------------

static void incr_trigram(int c1, int c2, int c3)
{
   uint16_t *tg = &trigram_counts[(c1 << 16) | (c2 << 8) | c3] ;
   if (*tg < 0xFFFF)
      (*tg)++ ;
   return ;
}

//----------------------------------------------------------------------

static unsigned get_trigram(int c1, int c2, int c3)
{
   return trigram_counts[(c1 << 16) | (c2 << 8) | c3] ;
}

//----------------------------------------------------------------------

static bool count_trigrams(CFile& fp)
{
   int c1 = fp.getc() ;
   int c2 = fp.getc() ;
   if (c1 == EOF || c2 == EOF)
      return true ;
   c1 &= 0xFF ;
   c2 &= 0xFF ;
   int c3 ;
   while ((c3 = fp.getc()) != EOF)
      {
      c3 &= 0xFF ;
      incr_trigram(c1,c2,c3) ;
      c1 = c2 ;
      c2 = c3 ;
      }
   return true ;
}

//----------------------------------------------------------------------

static bool count_trigrams(const char *filename)
{
   if (!trigram_counts)
      {
      trigram_counts.allocate(256*256*256) ;
      if (!trigram_counts)
	 return false ;
      std::fill_n(trigram_counts.begin(),256*256*256,0) ;
      }
   if (filename && *filename)
      {
      CInputFile fp(filename) ;
      if (fp)
	 {
	 return count_trigrams(fp) ;
	 }
      else
	 {
	 fprintf(stderr,"Unable to open %s\n",filename) ;
	 }
      }
   return false ;
}

//----------------------------------------------------------------------

static bool process_file(CFile& fp, NybbleTrie *forward, unsigned max_ngram,
			 uint64_t &total_bytes, unsigned filter_thresh)
{
   uint8_t file_buffer[BUFFER_SIZE] ;
   // read the first block of the file
   size_t bufsize
      = fp.read(file_buffer,sizeof(uint8_t),lengthof(file_buffer)) ;
   total_bytes += bufsize ;
   size_t prev_word = 0 ;
   size_t bufpos = 0 ;
   bool had_text = (bufsize > 0) ;
   for ( ; bufpos < bufsize ; bufpos++)
      {
      if (bufpos >= BUFFER_HIGHWATER)
	 {
	 // shift the buffer and read the next block of the file
	 memcpy(file_buffer,file_buffer+BUFFER_HIGHWATER,bufsize-BUFFER_HIGHWATER) ;
	 bufpos -= BUFFER_HIGHWATER ;
	 bufsize -= BUFFER_HIGHWATER ;
	 prev_word -= BUFFER_HIGHWATER ;
	 auto count = fp.read(file_buffer+bufsize,sizeof(uint8_t),BUFFER_SIZE-bufsize) ;
	 bufsize += count ;
	 total_bytes += count ;
	 }
      // record the current ngram
      unsigned len = max_ngram ;
      if (bufpos + max_ngram > bufsize)
	 len = bufsize - bufpos ;	// handle tail end of data
      if (len >= 3)
	 {
	 // by definition, any n-gram containing a trigram which occurs
	 //   fewer than the minimum number of times in the entire training
	 //   data will itself occur too few times, so don't bother counting
	 uint8_t *bp = file_buffer + bufpos ;
	 for (size_t i = 0 ; i + 3 <= len ; bp++, i++)
	    {
	    if (get_trigram(bp[0],bp[1],bp[2]) < filter_thresh)
	       {
	       len = i + 2 ;
	       break ;
	       }
	    }
	 }
      if (forward && len > 0)
	 {
	 forward->incrementExtensions(file_buffer+bufpos,0,len,1) ;
	 forward->addTokenCount(); 
	 }
      // ensure that words don't get excessively long
      if (bufpos - prev_word > MAX_WORD)
	 prev_word++ ;
      // do we have a new word?
      if (is_word_boundary(file_buffer,bufpos))
	 {
	 if (!is_whitespace(file_buffer,prev_word,bufpos))
	    {
	    unsigned wordlen = bufpos - prev_word ;
	    if (wordlen > 1 || file_buffer[prev_word] != '?')
	       {
	       make_word(file_buffer+prev_word,bufpos-prev_word,had_text) ;
	       }
	    }
	 prev_word = bufpos ;
	 }
      }
   // process any leftover characters as a final word
   if (bufpos > prev_word && !is_whitespace(file_buffer,prev_word,bufpos))
      make_word(file_buffer+prev_word,bufpos-prev_word,had_text) ;
   return had_text ;
}

//----------------------------------------------------------------------

static bool process_file(const char *filename, NybbleTrie *forward,
			 unsigned max_ngram, uint64_t &total_bytes,
			 unsigned filter_thresh)
{
   CInputFile fp(filename) ;
   if (fp)
      {
      return process_file(fp,forward,max_ngram,total_bytes,filter_thresh) ;
      }
   else
      {
      fprintf(stderr,"Unable to open %s\n",filename) ;
      }
   return false ;
}

//----------------------------------------------------------------------

static bool count_ngrams(const NybbleTrie* trie, NybbleTrie::NodeIndex nodeindex, const uint8_t * /*key*/,
			 unsigned keylen, void *user_data)
{
   unsigned min_freq = *((unsigned*)user_data) ;
   auto node = trie->node(nodeindex) ;
   if (ngram_counts && node->frequency() >= min_freq)
      ngram_counts[keylen]++ ;
   return true ;
}

//----------------------------------------------------------------------

static bool count_ngrams(const uint8_t * /*key*/, unsigned keylen,
			 uint32_t /*freq*/, void * /*user_data*/)
{
   if (ngram_counts)
      ngram_counts[keylen]++ ;
   return true ;
}

//----------------------------------------------------------------------

static bool reverse_ngram(const uint8_t* key, unsigned keylen, uint32_t frequency, void* user_data)
{
   LocalAlloc<uint8_t> reversed_key(keylen) ;
   std::reverse_copy(key,key+keylen,&reversed_key) ;
   auto reverse = reinterpret_cast<NybbleTrie*>(user_data) ;
   reverse->insert(reversed_key,keylen,frequency,false) ;
   if (ngram_counts && !store_unfiltered_counts)
      ngram_counts[keylen]++ ;
   return true ;
}

//----------------------------------------------------------------------

static bool write_ngram_counts(CFile& fp, const LangIDPackedTrie *ngrams,
			       const size_t *counts_by_len,
			       uint64_t total_bytes)
{
   if (fp && ngrams && counts_by_len)
      {
      unsigned max_len = ngrams->longestKey() ;
      if (!fp.write32LE(max_len) || !fp.write64LE(total_bytes))
	 return false ;
      fprintf(stdout,"N-gram frequencies:") ;
      for (size_t i = 1 ; i <= max_len ; i++)
	 {
	 fprintf(stdout," %lu",(unsigned long)counts_by_len[i]) ;
	 if (!fp.write64LE(counts_by_len[i]))
	    return false ;
	 }
      fprintf(stdout,"\n") ;
      return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

static bool write_words(CFile& fp, const WordList *frequencies, bool display_words)
{
   if (!fp || !frequencies)
      return false ;
   // store the count of words as a 32-bit big-endian number
   uint32_t count = count_words(frequencies) ;
   if (!fp.write32LE(count))
      return false ;
   for (const auto word : *frequencies)
      {
      size_t freq = word->frequency() ;
      if (display_words)
	 {
	 cout << freq << '\t' << *word << endl ;
	 }
      // write frequency as 64-bit big-endian number
      if (!fp.write64LE(freq))
	 return false ;
      unsigned len = word->length() ;
      // write string length as 16-bit big-endian number
      if (!fp.putc((len >> 8) & 0xFF) || !fp.putc(len & 0xFF))
	 return false ;
      // write the string itself
      for (size_t i = 0 ; i < len ; i++)
	 {
	 const WordCharacter &c = word->character(i) ;
	 if (!fp.putc(c.byteValue()))
	    return false ;
	 }
      }
   return true ;
}

//----------------------------------------------------------------------

static bool write_frequencies(CFile& fp, const LangIDPackedTrie* forward_ngrams,
			      const LangIDPackedTrie* reverse_ngrams, const size_t* counts_by_len,
			      const WordList* word_model, uint64_t total_bytes, bool display_words)
{
   // write format signature and version number
   if (!fp.writeSignature(LANGMODEL_SIGNATURE,LANGMODEL_FORMAT_VERSION))
      return false ;
   // some padding bytes for alignment and possible future use
   if (!fp.putNulls(3))
      return false ;
   // write dummy offsets
   fp.flush() ;
   off_t offsets_offset = fp.tell() ;
   if (!fp.write64LE(0) ||	// offset of forward ngram model
       !fp.write64LE(0) ||	// offset of reverse ngram model
       !fp.write64LE(0) ||	// offset of forward ngram counts
       !fp.write64LE(0) ||	// offset of word unigram model
       !fp.write64LE(0) ||	// some dummies for future expansion
       !fp.write64LE(0))
      return false ;
   // write the forward ngram model, if present
   off_t forward_offset = fp.tell() ;
   if (!forward_ngrams || !forward_ngrams->write(fp))
      forward_offset = 0 ;
   // write the reverse ngram model, if present
   off_t reverse_offset = fp.tell() ;
   if (!reverse_ngrams || !reverse_ngrams->write(fp))
      reverse_offset = 0 ;
   // write the forward ngram counts, if forward model is present
   off_t counts_offset = fp.tell() ;
   if (!write_ngram_counts(fp,forward_ngrams,counts_by_len,
			   total_bytes))
      counts_offset = 0 ;
   // write the word unigram model, if present
   off_t word_offset = fp.tell() ;
   if (!write_words(fp,word_model,display_words))
      word_offset = 0 ;
   // finally, go back and update the offsets of the embedded models
   fp.flush() ;
   fp.seek(offsets_offset) ;
   if (!fp.write64LE(forward_offset) ||
       !fp.write64LE(reverse_offset) ||
       !fp.write64LE(counts_offset) ||
       !fp.write64LE(word_offset))
      return false ;
   fp.flush() ;
   return true ;
}

//----------------------------------------------------------------------

static bool write_frequencies(const char *outfile,
			      const LangIDPackedTrie *forward_ngrams,
			      const LangIDPackedTrie *reverse_ngrams,
			      const size_t *counts_by_len,
			      const WordList *word_model,
			      uint64_t total_bytes,
			      bool display_words)
{
   if (forward_ngrams || reverse_ngrams)
      {
      COutputFile fp(outfile,CFile::binary) ;
      if (fp)
	 {
	 return write_frequencies(fp,forward_ngrams,reverse_ngrams,counts_by_len,
	                          word_model,total_bytes,display_words) ;
	 }
      }
   return false ;
}

//----------------------------------------------------------------------

int main(int argc, char **argv)
{
   const char *argv0 = argv[0] ;
   int filter_thresh = DEFAULT_FILTER_THRESHOLD ;
   int filter_factor = 1 ;
   unsigned max_ngram = DEFAULT_MAX_NGRAM ;
   bool display_words = false ;
   bool forward_only = false ;
   while (argc > 1 && argv[1][0] == '-')
      {
      switch (argv[1][1])
	 {
	 case 'd':	display_words = true ; 			break ;
	 case 'f':	forward_only = true ;			break ;
	 case 'F':	filter_factor = atoi(argv[1]+2) ;	break ;
	 case 'm':	filter_thresh = atoi(argv[1]+2) ;	break ;
	 case 'n':	max_ngram = atoi(argv[1]+2) ;		break ;
	 case 'u':	store_unfiltered_counts = true ; 	break ;
	 default:
	    usage(argv0) ;
	    return 1 ;
	 }
      argc-- ;
      argv++ ;
      }
   if (argc < 3)
      usage(argv0) ;
   if (filter_thresh > 0xFFFF)
      filter_thresh = 0xFFFF ;
   if (filter_factor < 1)
      filter_factor = 1 ;
   const char *outfile = argv[1] ;
   // skip program name and output file
   argc -= 2 ;
   argv += 2 ;
   uint64_t total_bytes = 0 ;
   Owned<NybbleTrie> forward ;
   for (int arg = 0 ; arg < argc ; arg++)
      {
      const char *trainfile = argv[arg] ;
      fprintf(stdout,"Scanning file '%s'\n",trainfile) ;
      if (!count_trigrams(trainfile))
	 {
	 fprintf(stderr,"  Error processing file '%s'\n",trainfile) ;
	 }
      }
   while (argc > 0)
      {
      const char *trainfile = argv[0] ;
      if (process_file(trainfile,forward,max_ngram,total_bytes, filter_factor*filter_thresh))
	 {
	 fprintf(stdout,"Processed file '%s'\n",trainfile) ;
	 }
      argc-- ;
      argv++ ;
      }
   if (words)
      merge_word_lists(frequencies,words) ;
   frequencies = sort_words(frequencies,compare_frequencies) ;
   if (filter_thresh < 1)
      filter_thresh = DEFAULT_FILTER_THRESHOLD ;
   ngram_counts.allocate(max_ngram+1) ;
   std::fill_n(ngram_counts.begin(),max_ngram+1,0) ;
   if (forward && store_unfiltered_counts)
      {
      LocalAlloc<uint8_t> keybuf(max_ngram+1) ;
      unsigned min_freq = filter_thresh ;
      forward->enumerate(keybuf,max_ngram,count_ngrams,&min_freq) ;
      }
   Owned<LangIDPackedTrie> forward_ngrams(forward.get(),filter_thresh) ;
   Owned<LangIDPackedTrie> reverse_ngrams { nullptr } ;
   if (forward_only)
      {
      if (!store_unfiltered_counts)
	 {
	 LocalAlloc<uint8_t> keybuf(max_ngram+1) ;
	 forward_ngrams->enumerate(keybuf,max_ngram,count_ngrams,nullptr) ;
	 }
      }
   else
      {
      gc() ;
      Owned<NybbleTrie> reverse ;
      LocalAlloc<uint8_t> keybuf(max_ngram+1) ;
      forward_ngrams->enumerate(keybuf,max_ngram,reverse_ngram,reverse) ;
      reverse->addTokenCount(forward->totalTokens()) ;
      reverse_ngrams.reinit(reverse,filter_thresh) ;
      }
   forward = nullptr ;
   if (!write_frequencies(outfile,forward_ngrams,reverse_ngrams,
			  ngram_counts,frequencies,total_bytes,display_words))
      {
      fprintf(stderr,"Error writing language data to file '%s'\n",outfile) ;
      return 1 ;
      }
   fprintf(stdout,"Built language model from %ld bytes of text\n", total_bytes) ;
   uint32_t count = count_words(frequencies) ;
   fprintf(stdout,"Processed %lu unique words\n",(unsigned long)count) ;
   delete frequencies ;
   frequencies = nullptr ;
   delete words ;
   words = nullptr ;
   ngram_counts = nullptr ;
   trigram_counts = nullptr ;
   return 0 ;
}

// end of file mklang.C //
