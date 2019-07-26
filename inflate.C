/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: inflate.C - DEFLATE decompression				*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-16						*/
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

#include <cstring>
#include "dbuffer.h"
#include "inflate.h"
#include "loclist.h"
#include "models.h"
#include "partial.h"
#include "recover.h"
#include "reconstruct.h"
#include "symtab.h"
#include "utility.h"
#include "words.h"
#include "global.h"
#include "whatlang2/langid.h"
#include "framepac/config.h"
#include "framepac/texttransforms.h"
#include "framepac/timer.h"

#if defined(__WATCOMC__) || defined(__MSDOS__) || defined(_MSC_VER)
#  include <io.h> 	// for unlink()
#endif /* __WATCOMC__ || __MSDOS__ || _MSC_VER */
#ifdef __linux__
#  include <unistd.h>	// for unlink()
#endif /* __linux__ */

using namespace Fr;

/************************************************************************/
/*	Manifest constants						*/
/************************************************************************/

// minimum run of identical bytes in the DEFLATE stream to declare a
//   corrupt region
#define MIN_REPETITIONS 128

// minimum size of a fixed-Huffman packet to process to avoid excessive
//   false positives
#define MIN_FIXED_PACKET 3072

// number of bytes at a time to run through language identification to
//   try to determine a corruption-point
#define LANGIDENT_WINDOW 256
#define LANGIDENT_WINDOW_SLIDE 128

// number of bytes at a time to run through word-length model to try to
//   determine a corruption point
#define LENMODEL_WINDOW 512
#define LENMODEL_WINDOW_SLIDE 128

// number of bytes at a time to run through word-unigram model to try to
//   determine a corruption point
#define WORDMODEL_WINDOW 512
#define WORDMODEL_WINDOW_SLIDE 64

// how bad must the current block's best language score be relative to
//   the previous block's before we declare corruption?
#define LANGID_THRESHOLD 0.2

// what percentage of words in the current block need to be unknown before
//   we declare corruption?
#define WORDMODEL_THRESHOLD 0.4

//----------------------------------------------------------------------

#if (WORDMODEL_WINDOW / WORDMODEL_WINDOW_SLIDE) > (LENMODEL_WINDOW / LENMODEL_WINDOW_SLIDE)
#  define MAX_SLIDE_RATIO (WORDMODEL_WINDOW / WORDMODEL_WINDOW_SLIDE)
#elif (LANGIDENT_WINDOW / LANGIDENT_WINDOW_SLIDE) > (LENMODEL_WINDOW / LENMODEL_WINDOW_SLIDE)
#  define MAX_SLIDE_RATIO (LANGIDENT_WINDOW / LANGIDENT_WINDOW_SLIDE)
#else
#  define MAX_SLIDE_RATIO (LENMODEL_WINDOW / LENMODEL_WINDOW_SLIDE)
#endif

/************************************************************************/
/*	Types for this module						*/
/************************************************************************/

class CheckPoints
   {
   private:
      BitPointer m_checkpoints[MAX_SLIDE_RATIO] ;
      unsigned   m_active ;
      unsigned   m_next ;
   public:
      CheckPoints(const BitPointer checkpoint, unsigned count) ;
      ~CheckPoints() {}

      // modifiers
      void addCheckpoint(BitPointer checkpoint) ;

      // accessors
      BitPointer checkpoint() const { return m_checkpoints[m_active] ; }
   } ;

/************************************************************************/
/*	Global variables						*/
/************************************************************************/

bool detect_corruption_by_langmodel = false ;

static const char *packet_type_names[] = 
   { 
      // must be in same order as enum PacketType
      "uncompressed",
      "fixed Huffman",
      "dynamic Huffman",
      "invalid"
   } ;

//static DecodeBuffer *decode_buffer = 0 ;

size_t max_packet_size = 2 * 1024 * 1024 ;

const char *recovery_name_base = 0 ;

/************************************************************************/
/*	Helper functions						*/
/************************************************************************/

static CFile open_output_file(char *&filename, char *default_filename,
			      const char *filename_hint, bool using_stdin,
			      const ZipRecParameters &params)
{
   const char* outname = (params.write_format != WFMT_Listing) ? filename : NULL_DEVICE ;
   auto opts = CFile::binary | (params.force_overwrite ? CFile::fail_if_exists : CFile::default_options) ;
   COutputFile outfp(outname, opts, using_stdin ? nullptr : CFile::askOverwrite) ;
   // the given hinted filename may not be valid on this OS or the
   //   user may have refused to allow an overwrite, so try the
   //   default name if the open failed
   if (!outfp && filename_hint)
      {
      Free(filename) ;
      filename = default_filename ;
      return COutputFile(filename, opts, using_stdin ? nullptr : CFile::askOverwrite) ;
      }
   return outfp ;
}

//----------------------------------------------------------------------

static void dump_stream(BitPointer currpos, const BitPointer &str_end)
{
#if DEBUG
   currpos.advanceToByte() ;
   while (currpos < str_end)
      {
      uint32_t nextbyte = currpos.nextBits(8) ;
      if (isascii(nextbyte) && isprint(nextbyte))
	 cerr << ' ' << (char)nextbyte ;
      else if (nextbyte < ' ')
	 cerr << " 0x" << hex << nextbyte ;
      else
	 cerr << ' ' << hex << nextbyte ;
      }
   cerr << dec ;
#else
   (void)currpos; (void)str_end; // keep compiler happy
#endif
   return ;
}

/************************************************************************/
/*	Methods for class CheckPoints					*/
/************************************************************************/

CheckPoints::CheckPoints(const BitPointer checkpoint, unsigned count)
{
   for (size_t i = 0 ; i < lengthof(m_checkpoints) ; i++)
      {
      m_checkpoints[i] = checkpoint ;
      }
   m_next = count ;
   m_active = 0 ;
   return ;
}

//----------------------------------------------------------------------

void CheckPoints::addCheckpoint(BitPointer checkpoint)
{
   m_checkpoints[m_next] = checkpoint ;
   m_next++ ;
   m_active++ ;
   if (m_next >= lengthof(m_checkpoints))
      m_next = 0 ;
   if (m_active >= lengthof(m_checkpoints))
      m_active = 0;
   return ;
}

/************************************************************************/
/************************************************************************/

static bool valid_literal_packet(const BitPointer &pos)
{
   BitPointer start(pos) ;
   start.advance(PACKHDR_SIZE) ;	// skip the packet header
   start.advanceToByte() ;		// align to byte
   uint32_t size1 = start.nextBits(16) ;
   uint32_t size2 = start.nextBits(16) ;
   // size2 must be one's-complement of size1
   // a packet size of zero would normally be nonsensical, but is used
   //   by pigz for byte alignment and by zlib as a flush point
   if ((size1 ^ size2) != 0xFFFF /*|| size1 == 0*/)
      return false ;
   return true ;
}

//----------------------------------------------------------------------

static bool valid_literal_packet(const BitPointer &pos,
				 const BitPointer &str_end,
				 bool full_size = true)
{
   INCR_STAT(candidate_uncomp_packet) ;
   if (full_size && str_end - pos > MAX_LITERAL_PACKET_SIZE)
      return false ;
   INCR_STAT(considered_uncomp_packet) ;
   BitPointer start(pos) ;
   start.advance(PACKHDR_SIZE) ;	// skip the packet header
   start.advanceToByte() ;		// align to byte
   uint32_t size1 = start.nextBits(16) ;
   uint32_t size2 = start.nextBits(16) ;
   if ((size1 ^ size2) != 0xFFFF)
      return false ;  // size2 must be one's-complement of size1
#if 0
   if (size1 == 0)		  // zero-length packet is normally nonsensical
      return false ;		  //   but is used by pigz for byte alignment
   				  //   and by zlib at a flush point
#endif
   if (full_size && str_end - start != size1)
      return false ;  // literal data does not fill rest of packet
   INCR_STAT(valid_uncomp_packet) ;
   return true ;
}

//----------------------------------------------------------------------
//  check that the entire packet's worth of compressed bits is valid
//  returns 'true' if no errors were encountered and there is at least
//    one byte of uncompressed data represented by the stream, and updates
//    'currpos' to point just after the erroneous symbol.
 
static bool check_compressed_stream(const HuffSymbolTable *symtab,
				    BitPointer &currpos,
				    const BitPointer &str_end,
				    bool exact_end)
{
   bool nonempty = false ;
   BitPointer str_end_byte(str_end) ;
   if (!exact_end)
      str_end_byte.retreat(7) ;
   while (currpos < str_end)
      {
      HuffSymbol symbol ;
      if (!symtab->nextSymbol(currpos,str_end,symbol))
	 {
	 return false ;
	 }
      if (symbol == END_OF_DATA)
	 {
	 // verify that we are actually at the end of the packet -- an EOD
	 //   at any other time is an error
         if (!currpos.inBounds(str_end_byte,str_end))
	    {
	    dump_stream(currpos,str_end) ;
	    return false ;
	    }
	 break ;
	 }
      nonempty = true ;
      if (symbol > END_OF_DATA)
	 {
	 // we have a back-reference, so extract the length/distance pair
	 unsigned length = symtab->getLength(symbol,currpos) ;
	 unsigned distance = symtab->getDistance(currpos,str_end) ;
	 if (length == INVALID_LENGTH || distance == INVALID_DISTANCE)
	    {
	    return false ;
	    }
	 }
      }
   return nonempty ;
}

//----------------------------------------------------------------------

static bool corrupted_words(DecodeBuffer &decode_buf,
			    const WordLengthModel *lenmodel,
			    WordLengthModel *&running_model)
{
   bool corrupted = false ;
   if (lenmodel)
      {
      if (!running_model)
	 {
	 running_model = new WordLengthModel(lenmodel->type()) ;
	 running_model->combine(lenmodel) ;
	 }
      WordLengthModel *curr_lengths = new WordLengthModel(lenmodel->type()) ;
      unsigned char text[LENMODEL_WINDOW] ;
      decode_buf.copyBufferTail(text,sizeof(text)) ;
      const unsigned char *text_start = lenmodel->skipToDelim(text,sizeof(text)) ;
      unsigned buflen = sizeof(text) - (text_start - text) ;
      unsigned minlen = (buflen > 20) ? buflen-20 : buflen ;
      curr_lengths->addWords(text_start,minlen,buflen) ;
      if (running_model->totalCount() > 4*running_model->maxLength() &&
	  curr_lengths->totalCount() > 0)
	 {
	 double similarity = running_model->similarity(curr_lengths) ;
//cerr<<"sim="<<similarity<<endl;//!!!
	 if (similarity < 0.8)
	    {
	    corrupted = true ;
//FIXME
	    }
	 }
      running_model->scale(0.75) ;
      running_model->combine(curr_lengths) ;
      delete curr_lengths ;
      }
   return corrupted ;
}

//----------------------------------------------------------------------

static bool corrupted_words(DecodeBuffer &decode_buf,
			    const NybbleTrie *wordmodel,
			    NybbleTrie *&local_words)
{
   bool corrupted = false ;
   local_words = 0 ; // no local model yet
   if (wordmodel)
      {
      unsigned char text[WORDMODEL_WINDOW+1] ;
      text[0] = 'a' ;
      decode_buf.copyBufferTail(text+1,sizeof(text)-1) ;
      // skip the potentially-partial first word
      unsigned pos = 1 ;
      while (pos < sizeof(text))
	 {
	 if (is_word_boundary(text,pos))
	    break ;
	 pos++ ;
	 }
      unsigned prev_word = pos ;
      unsigned known = 0 ;
      unsigned unknown = 0 ;
      while (++pos < sizeof(text))
	 {
	 // extract the next word from the buffer
	 if (is_word_boundary(text,pos))
	    {
	    if (!is_whitespace(text,prev_word,pos) &&
		!contains_unknown(text,prev_word,pos))
	       {
	       unsigned wordlen = pos - prev_word ;
	       if (wordlen > 1/* || text[prev_word] != '?'*/)
		  {
		  uint32_t freq = wordmodel->find(text+prev_word,wordlen) ;
		  if (freq != 0 && freq != (uint32_t)~0)
		     {
		     known++ ;
		     }
		  else
		     {
		     unknown++ ;
		     }
		  }
	       }
	    prev_word = pos ;
	    }
	 }
      double total = known + unknown ;
      double frac = total ? (unknown/total) : 0.0 ;
      if (total >= 8 && frac >= WORDMODEL_THRESHOLD)
	 {
cerr<<"corruption detected by word model!  frac="<<frac << endl;
	 corrupted = true ;
	 }
      }
   return corrupted ;
}

//----------------------------------------------------------------------

static bool corrupted_language(DecodeBuffer &decode_buf,
			       const LanguageIdentifier *langid,
			       LanguageScores *&prev_scores)
{
   bool corrupted = false ;
   if (langid)
      {
      LanguageScores *scores = new LanguageScores(langid->numLanguages()) ;
      unsigned char text[LANGIDENT_WINDOW] ;
      decode_buf.copyBufferTail(text,sizeof(text)) ;
      if (langid->identify(scores,(const char*)text,sizeof(text),(uint8_t*)0))
	 {
	 if (prev_scores &&
	     (scores->highestScore()
	      < LANGID_THRESHOLD * prev_scores->highestScore()))
	    {
	    corrupted = true ;
	    }
	 }
      delete prev_scores ;
      prev_scores = scores ;
      }
   return corrupted ;
}

//----------------------------------------------------------------------
//  check that the entire packet's worth of compressed bits is valid
//  returns 'true' if no errors were encountered and there is at least
//    one byte of uncompressed data represented by the stream, and updates
//    'currpos' to point just after the erroneous symbol.
 
static bool check_compressed_stream(const HuffSymbolTable *symtab,
				    DecodeBuffer &decode_buf,
				    const FileInformation *fileinfo,
				    BitPointer &currpos,
				    const BitPointer &str_end,
				    bool exact_end,
				    unsigned long uncompressed_offset,
				    unsigned long &uncomp_size,
				    bool previous_corruption,
				    unsigned long &corruption_size)
{
   const LanguageIdentifier *langid = fileinfo->langid() ;
   const WordLengthModel *lenmodel = fileinfo->lengthmodel() ;
   const NybbleTrie *wordmodel = fileinfo->wordmodel() ;
   if (!detect_corruption_by_langmodel)
      langid = 0 ;
   corruption_size = 0 ;
   bool correct = false ;
   BitPointer str_end_byte(str_end) ;
   if (!exact_end)
      str_end_byte.retreat(7) ;
   size_t offset = 0 ;
   unsigned num_checkpoints ;
   size_t highwater ;
   if (langid)
      {
      highwater = LANGIDENT_WINDOW ;
      num_checkpoints = (LANGIDENT_WINDOW / LANGIDENT_WINDOW_SLIDE) ;
      }
   else if (wordmodel)
      {
      // word model is too likely to give a false positive if there was
      //   corruption in a previous packet
      highwater = previous_corruption ? (size_t)~0 : WORDMODEL_WINDOW ;
      num_checkpoints = (WORDMODEL_WINDOW / WORDMODEL_WINDOW_SLIDE) ;
      }
   else //if (lenmodel)
      {
      highwater = LENMODEL_WINDOW ;
      num_checkpoints = (LENMODEL_WINDOW / LENMODEL_WINDOW_SLIDE) ;
      }
   LanguageScores *scores = 0 ;
   WordLengthModel *word_lengths = 0 ;
   CheckPoints checkpoints(currpos,num_checkpoints) ;
   BitPointer prevpos(currpos) ;
   while (currpos < str_end)
      {
      HuffSymbol symbol ;
      BitPointer activepos(currpos) ;
      if (!symtab->nextSymbol(currpos,str_end,symbol))
	 {
	 currpos = activepos ;
	 uncomp_size = offset ;
	 correct = false ;
	 break ;
	 }
      if (symbol == END_OF_DATA)
	 {
	 // verify that we are actually at the end of the packet -- an EOD
	 //   at any other time is an error
         if (!currpos.inBounds(str_end_byte,str_end))
	    {
	    dump_stream(currpos,str_end) ;
	    uncomp_size = offset ;
	    correct = false ;
	    }
	 break ;
	 }
      correct = true ;
      if (symbol > END_OF_DATA)
	 {
	 // we have a back-reference, so extract the length/distance pair
	 unsigned length = symtab->getLength(symbol,currpos) ;
	 unsigned distance = symtab->getDistance(currpos,str_end) ;
	 if (length == INVALID_LENGTH || distance == INVALID_DISTANCE ||
	     (distance > offset && (distance - offset) > uncompressed_offset))
	    {
	    uncomp_size = offset + 1 ;
	    prevpos = activepos ;
	    correct = false ;
	    break ;
	    }
	 decode_buf.copyString(length,distance) ;
	 offset += length ;
	 }
      else
	 {
	 decode_buf.addByte((unsigned char)symbol) ;
	 offset++ ;
	 }
      if (offset >= highwater)
	 {
	 if (langid)
	    {
	    highwater = offset + LANGIDENT_WINDOW_SLIDE ;
	    if (corrupted_language(decode_buf,langid,scores))
	       {
	       corruption_size = LANGIDENT_WINDOW + LANGIDENT_WINDOW_SLIDE ;
	       correct = false ;
	       break ;
	       }
	    }
	 else if (wordmodel)
	    {
	    highwater = offset + WORDMODEL_WINDOW_SLIDE ;
	    NybbleTrie *localwords = 0 ;
	    bool corr = corrupted_words(decode_buf,wordmodel,localwords) ;
	    delete localwords ;
	    if (corr)
	       {
	       corruption_size = WORDMODEL_WINDOW + WORDMODEL_WINDOW_SLIDE ;
	       correct = false ;
	       break ;
	       }
	    }
	 else if (lenmodel)
	    {
	    highwater = offset + LENMODEL_WINDOW_SLIDE ;
	    if (corrupted_words(decode_buf,lenmodel,word_lengths))
	       {
	       corruption_size = LENMODEL_WINDOW + LENMODEL_WINDOW_SLIDE ;
	       correct = false ;
	       break ;
	       }
	    }
	 checkpoints.addCheckpoint(currpos) ;
	 }
      }
   if (!correct)
      {
      cerr << "  corruption detected at uncompressed offset " << offset << " in packet" << endl ;
      currpos = checkpoints.checkpoint() ;
      correct = false ;
      }
   delete scores ;
   delete word_lengths ;
   uncomp_size = offset ;
   return correct ;
}

//----------------------------------------------------------------------

static bool check_compressed_packet(DeflatePacketDesc *packet,
				    DecodeBuffer &decode_buf,
				    const FileInformation *fileinfo,
				    unsigned long &uncomp_size,
				    bool previous_corruption)
{
   BitPointer header(packet->packetHeader()) ;
   const BitPointer &str_end = packet->packetEnd() ;
   uint32_t hdr = header.nextBits(PACKHDR_SIZE) ;
   HuffSymbolTable *symtab = 0 ;
   uncomp_size = 0 ;
   switch (PACKHDR_TYPE(hdr))
      {
      case PT_INVALID:
      default:
	 return false ;
      case PT_UNCOMP:
	 header.advanceToByte() ;
	 uncomp_size = header.getBits(16) ;
	 return true ;
      case PT_FIXEDHUFF:
	 symtab = build_default_symtable(packet->deflate64()) ;
	 break ;
      case PT_DYNAMIC:
	 symtab = build_symbol_table(header,str_end,packet->deflate64()) ;
	 break ;
      }
   bool success = true ;
   bool exact_end = !packet->last() ;
   unsigned long uncomp_offset = packet->uncompressedOffset() ;
   unsigned long corruption_size = 0 ;
   if (!check_compressed_stream(symtab,decode_buf,fileinfo,header,str_end,
				exact_end,uncomp_offset,uncomp_size,
				previous_corruption,corruption_size))
      {
      unsigned long corruption = header - packet->packetHeader() ;
//      if (verbosity >= VERBOSITY_PACKETS)
	 fprintf(stderr,"  found corruption at packet offset %lu\n",
		 corruption) ;
      if (corruption > 0)
	 corruption-- ;
      if (corruption_size == 0)
	 corruption_size = 1 ;
      unsigned long corruption_end = corruption + corruption_size ;
      packet->updateCorruption(corruption,corruption_end) ;
      success = false ;
      }
   return success ;
}

//----------------------------------------------------------------------

static bool valid_compressed_packet(const HuffSymbolTable *symtab,
				    BitPointer &pos,
				    const BitPointer &str_end,
				    bool exact_end, bool &valid_EOD)
{
   valid_EOD = false ;
   if (!symtab)
      return false ;
   VariableBits eod ;
   symtab->getEOD(eod) ;
   if (eod.length() == 0)
      return false ;
   // check whether the last symbol in the candidate packet is an end-of-data
   //   marker
   if (exact_end)
      {
      // only need to check the bits ending exactly with the bit pointed
      //   at by 'str_end'
      BitPointer tail(str_end) ;
      uint32_t tailbits = tail.prevBitsReversed(eod.length()) ;
      if (tailbits != eod.value())
	 return false ;
      }
   else
      {
      // need to check all positions in the byte pointed at by 'str_end'
      bool have_EOD = false ;
      for (size_t i = 0 ; i < 8 /*- str_end.bitNumber()*/ ; i++)
	 {
	 BitPointer tail(str_end) ;
	 tail.retreat(i) ;
	 uint32_t tailbits = tail.prevBitsReversed(eod.length()) ;
	 if (tailbits == eod.value())
	    {
	    have_EOD = true ;
	    break ;
	    }
	 }
      if (!have_EOD)
	 return false ;
      }
   valid_EOD = true ;
   // OK, we have a proper end-of-data marker at the end of the proposed
   //   packet, so now run the full decompression to verify
   BitPointer currpos(pos) ;
   return check_compressed_stream(symtab,currpos,str_end,exact_end) ;
}

//----------------------------------------------------------------------

bool valid_fixed_packet(BitPointer &pos, bool deflate64)
{
   const HuffSymbolTable *symtab = build_default_symtable(deflate64) ;
   BitPointer currpos(pos) ;
   BitPointer str_end(pos) ;
   str_end.advance(800) ;  // check up to 100 bytes
   size_t num_bytes = 0 ;
   while (currpos < str_end)
      {
      HuffSymbol symbol ;
      if (!symtab->nextSymbol(currpos,str_end,symbol))
	 break ;
      if (symbol == END_OF_DATA)
	 {
	 if (num_bytes == 0)
	    return false ;
	 break ;
	 }
      else if (symbol > END_OF_DATA)
	 {
	 // it's a back-reference, so extract length and distance
	 unsigned length = symtab->getLength(symbol,currpos) ;
	 unsigned distance = symtab->getDistance(currpos,str_end) ;
	 // empty back-references don't make sense, and we assume
	 //   this is the start of a stream, so we can't have a
	 //   back-reference prior to the start of the packet
	 if (length == INVALID_LENGTH || distance == INVALID_DISTANCE ||
	     distance > num_bytes)
	    {
	    return false ;
	    }
	 num_bytes += length ;
	 }
      else // if (symbol < END_OF_DATA)
	 num_bytes++ ;
      }
   // if we get here, we didn't find anything invalid
   return true ;
}

//----------------------------------------------------------------------

#if 0
static bool is_last_packet(const BitPointer &header)
{
   if (header.bytePointer() != 0)
      {
      uint32_t hdr = header.getBits(PACKHDR_SIZE) ;
      return (hdr & PACKHDR_LAST_MASK) != 0 ;
      }
   else
      return false ;
}
#endif

//----------------------------------------------------------------------

bool valid_packet_header(const char *buffer, bool deflate64,
			 bool allow_fixedHuff)
{
   BitPointer pos(buffer) ;
   uint32_t hdr = pos.getBits(PACKHDR_SIZE) ;
   switch (PACKHDR_TYPE(hdr))
      {
      case PT_UNCOMP:
	 return valid_literal_packet(pos) ;
      case PT_FIXEDHUFF:
	 // we don't have any fast way of ruling out fixed-Huffman packets,
	 //   so just say OK if they're allowed
	 return allow_fixedHuff ? valid_fixed_packet(pos,deflate64) : false ;
      case PT_DYNAMIC:
         {
	 pos.advance(PACKHDR_SIZE) ;	// skip the packet header
	 return valid_symbol_table_header(pos,deflate64) ;
	 }
      default:
	 return false ;
      }
}

//----------------------------------------------------------------------

static bool valid_packet(const BitPointer &pos,
			 const BitPointer &str_start,
			 const BitPointer &str_end,
			 bool final_packet, bool exact_bit,
			 bool deflate64)
{
   uint32_t hdr = pos.getBits(PACKHDR_SIZE) ;
   uint32_t is_last = hdr & PACKHDR_LAST_MASK ;
   // first, check whether the current position could possibly be the
   //   start of the packet we want
   if (final_packet && !is_last)
      return false ;
   if (!final_packet && is_last)
      return false ;
   if (PACKHDR_TYPE(hdr) == PT_INVALID)
      return false ;
   // next, we need to build the symbol table (if not a literal packet)
   //   and check whether the end-of-data symbol appears at the end of
   //   the packet
   switch (PACKHDR_TYPE(hdr))
      {
      case PT_INVALID:
	 return false ;
      case PT_UNCOMP:
	 return valid_literal_packet(pos,str_end) ;
      case PT_FIXEDHUFF:
         {
	 // for now, ignore any fixed-tree packets unless they cover
	 //   the entire span or enough bytes to avoid excessive false
	 //   positives
	 INCR_STAT(candidate_fixed_packet) ;
	 if (str_end - pos < MIN_FIXED_PACKET &&
	     (!is_last || pos != str_start))
	    return false ;
	 INCR_STAT(considered_fixed_packet) ;
	 BitPointer position(pos) ;
	 position.advance(PACKHDR_SIZE) ;
	 HuffSymbolTable *symtab = build_default_symtable(deflate64) ;
#if !defined(NDEBUG)
	 if (verbosity > VERBOSITY_SEARCH)
	    {
	    unsigned long byte_offset = pos - str_start ;
	    unsigned bit_number = pos.bitNumber() ;
	    fprintf(stderr," checking for valid %s packet at %lu.%u\n",
		    final_packet?"final":"internal",
		    byte_offset,bit_number) ;
	    }
#endif /* !NDEBUG */
	 bool valid_EOD ;
	 bool valid = valid_compressed_packet(symtab,position,str_end,
					      exact_bit,valid_EOD);
	 INCR_STAT_IF(valid,valid_fixed_packet) ;
	 INCR_STAT_IF(valid_EOD,valid_fixed_EOD_marker) ;
	 free_symbol_table(symtab) ;
	 return valid ;
	 }
      case PT_DYNAMIC:
         {
	 INCR_STAT(candidate_dynhuff_packet) ;
	 BitPointer position(pos) ;
	 position.advance(PACKHDR_SIZE) ;	// skip the packet header
	 HuffSymbolTable *symtab = build_symbol_table(position,str_end,
						  deflate64) ;
	 bool valid = symtab != 0 ;
	 if (valid)
	    {
	    INCR_STAT(valid_huffman_tree) ;
#if !defined(NDEBUG)
	    if (verbosity > VERBOSITY_SEARCH)
	       {
	       unsigned long byte_offset = pos - str_start ;
	       unsigned bit_number = pos.bitNumber() ;
	       fprintf(stderr," checking for valid %s packet at %lu.%u\n",
		       final_packet?"final":"internal",
		       byte_offset,bit_number) ;
	       }
#endif /* !NDEBUG */
	    bool valid_EOD ;
	    valid = valid_compressed_packet(symtab,position,str_end,
					    exact_bit,valid_EOD) ;
	    INCR_STAT_IF(valid,valid_dynhuff_packet) ;
	    INCR_STAT_IF(valid_EOD,valid_EOD_marker) ;
	    }
	 free_symbol_table(symtab) ;
	 return valid ;
	 }
      }
   return false ;
}

//----------------------------------------------------------------------

static bool advance_over_literal_packet(BitPointer &pos,
					const BitPointer &str_end,
					off_t &offset)
{
   pos.advanceToByte() ;
   unsigned size1 = pos.nextBits(16) ;
   unsigned size2 = pos.nextBits(16) ;
   if (size1 != ~size2)
      return false ;
   if (str_end - pos < size1)
      return false ;
   pos.advanceBytes(size1) ;
   offset += size1 ;
   return true ;
}

//----------------------------------------------------------------------

static bool advance_over_packet(BitPointer &pos, const BitPointer &str_end,
				const HuffSymbolTable *symtab, off_t &offset)
{
   if (!symtab)
      return false ;
   VariableBits eod ;
   symtab->getEOD(eod) ;
   if (eod.length() == 0)
      return false ;
   while (pos < str_end)
      {
      HuffSymbol symbol ;
      if(!symtab->nextSymbol(pos,str_end,symbol))
	 return false ;
      if (symbol == END_OF_DATA)
	 return true ;
      else if (symbol > END_OF_DATA)
	 {
	 // we have a back-reference, so extract the length/distance pair
	 unsigned length = symtab->getLength(symbol,pos) ;
	 unsigned distance = symtab->getDistance(pos,str_end) ;
	 if (length == INVALID_LENGTH || distance == INVALID_DISTANCE ||
	     distance > offset)
	    {
	    return false ;
	    }
	 offset += length ;
	 }
      else
	 offset++ ;
      }
   return false ;
}

//----------------------------------------------------------------------

static bool skip_to_valid_packet(BitPointer &pos, const BitPointer &str_end,
				 DeflatePacketDesc *&stream, bool deflate64)
{
   // do a brute-force scan for a valid uncompressed or dynamic-Huffman
   //   packet header starting at the current position
   while (pos < str_end)
      {
      uint32_t hdr = pos.getBits(PACKHDR_SIZE) ;
      bool valid = false ;
      switch (PACKHDR_TYPE(hdr))
	 {
	 case PT_INVALID:
	    break ;
	 case PT_FIXEDHUFF:
	    break ;
	 case PT_UNCOMP:
	    {
	    valid = valid_literal_packet(pos,str_end,false) ;
	    break ;
	    }
	 case PT_DYNAMIC:
	    {
	    BitPointer position(pos) ;
	    position.advance(PACKHDR_SIZE) ;
	    HuffSymbolTable *symtab
	       = build_symbol_table(position,str_end,deflate64) ;
	    if (symtab)
	       {
	       free_symbol_table(symtab) ;
	       valid = true ;
	       }
	    break ;
	    }
	 }
      if (valid)
	 {
	 if (stream->split(pos,PACKHDR_TYPE(hdr)))
	    {
	    stream = stream->next() ;
	    return true ;
	    }
	 }
      else
	 pos.advance(1) ;
      }
   return true ;
}

//----------------------------------------------------------------------

static bool split_into_packets(DeflatePacketDesc *stream, bool deflate64)
{
   BitPointer str_start(stream->streamStart()) ;
   BitPointer str_end(stream->packetEnd()) ;
   BitPointer pos(str_start) ;
   off_t offset = 0 ;

   while (pos < str_end)
      {
      BitPointer str_pos(pos) ;
      uint32_t hdr = pos.getBits(PACKHDR_SIZE) ;
      uint32_t is_last = hdr & PACKHDR_LAST_MASK ;
      // split the stream at the current position, then make the tail the
      //   active packet
      if (!stream->split(str_pos,PACKHDR_TYPE(hdr)))
	 return false ;
      stream = stream->next() ;
      if (is_last)
	 stream->markAsLast() ;
      // process by packet type
      pos.advance(PACKHDR_SIZE) ;
      switch (PACKHDR_TYPE(hdr))
	 {
	 case PT_INVALID:
	    pos = str_end ;
	    break ;
	 case PT_UNCOMP:
	    if (!advance_over_literal_packet(pos,str_end,offset) &&
		!skip_to_valid_packet(pos,str_end,stream,deflate64))
	       {
	       return false ;
	       }
	    break ;
	 case PT_FIXEDHUFF:
	    {
	    HuffSymbolTable *symtab = build_default_symtable(deflate64) ;
	    if (!advance_over_packet(pos,str_end,symtab,offset) &&
		!skip_to_valid_packet(pos,str_end,stream,deflate64))
	       {
	       free_symbol_table(symtab) ;
	       return false ;
	       }
	    free_symbol_table(symtab) ;
	    break ;
	    }
	 case PT_DYNAMIC:
	    {
	    HuffSymbolTable *symtab
	       = build_symbol_table(pos,str_end,deflate64) ;
	    if (!advance_over_packet(pos,str_end,symtab,offset) &&
		!skip_to_valid_packet(pos,str_end,stream,deflate64))
	       {
	       free_symbol_table(symtab) ;
	       return false ;
	       }
	    free_symbol_table(symtab) ;
	    break ;
	    }
	 }
      }
   return false ;
}

//----------------------------------------------------------------------

static PacketType find_packet_start(BitPointer &str_pos,
				    const BitPointer &str_start,
				    const BitPointer &str_end,
				    size_t base_offset,
				    bool final,
				    bool exact_bit,
				    bool deflate64)
{
   BitPointer pos(str_pos) ;
   BitPointer start(str_start) ;
   if (str_end - str_start > max_packet_size)
      {
      start = str_end ;
      start.retreat(8*max_packet_size) ;
      }
   while (pos >= start)
      {
      if (valid_packet(pos,str_start,str_end,final,exact_bit,
		       deflate64))
	 {
	 str_pos = pos ;
	 PacketType ptype
	    = (PacketType)PACKHDR_TYPE(pos.getBits(PACKHDR_SIZE)) ;
	 // if we have an uncompressed packet starting in the very
	 //   first byte of the stream, the scan will show it as
	 //   starting on bit 5 instead of bit 0, so correct the pointer
	 if (ptype == PT_UNCOMP &&
	     str_start.bytePointer() == str_pos.bytePointer())
	    {
	    str_pos.retreatToByte(); 
	    }
	 if (verbosity >= VERBOSITY_PACKETS)
	    {
	    size_t offset = str_pos - str_start ;
	    offset += base_offset ;
	    unsigned bit_number = str_pos.bitNumber() ;
	    cerr << " *** found " << packet_type_names[ptype]
		 << " packet at <" << offset << "." << bit_number << ">" 
		 << endl ;
	    }
	 return ptype ;
	 }
      pos.retreat(1) ;
      }
   return PT_INVALID ;
}

//----------------------------------------------------------------------

static bool decompress(BitPointer &str_pos, const BitPointer &str_end,
		       const HuffSymbolTable *symtab,
		       DecodeBuffer *decode_buffer,
		       bool start_of_stream, bool exact_end)
{
   if (!decode_buffer || !symtab || str_pos >= str_end)
      return false ;
   HuffSymbol code = INVALID_SYMBOL ;
   while (str_pos < str_end)
      {
      if (!symtab->nextValue(str_pos,str_end,code))
	 {
	 return false ;
	 }
      if (code < END_OF_DATA)
	 {
	 // literal code, so add it to the output
	 decode_buffer->addByte((unsigned char)code) ;
	 }
      else if (code == END_OF_DATA)
	 break ;
      else
	 {
	 // we have a back-reference, so get the complete length and
	 //   distance values
	 unsigned length = symtab->getLength(code,str_pos) ;
	 unsigned distance = symtab->getDistance(str_pos,str_end) ;
	 if (length == INVALID_LENGTH || distance == INVALID_DISTANCE)
	    break ;
	 if (start_of_stream && distance > decode_buffer->totalBytes())
	    return false ; // reference prior to start of original file!
	 // now copy the referenced string to the output
	 decode_buffer->copyString(length,distance) ;
	 }
      }
   // decompression was successful if the last symbol before reaching the
   //   end of the packet is the end-of-data marker, and the end of data
   //   marker occurs at the very end of the packet.
   if (code != END_OF_DATA)
      return false ;
   if (!exact_end)
      {
      str_pos.advanceToByte() ;
      return str_pos == str_end ;
      }
   return true ;
}

//----------------------------------------------------------------------

static bool decompress(BitPointer &str_pos, const BitPointer str_start,
		       const BitPointer &str_end,
		       DecodeBuffer *decode_buffer, const char *type,
		       const char *outfile,
		       bool may_be_corrupt, bool start_of_stream,
		       bool *hit_final_packet = 0,
		       BitPointer *last_packet_header = 0)
{
   if (verbosity >= VERBOSITY_PROGRESS)
      {
      fflush(stdout) ;
      fprintf(stderr,"decompressing %s to '%s'\n",type,outfile) ;
      }
   bool success = true ;
   bool deflate64 = decode_buffer->deflate64() ;
   while (str_pos < str_end && success)
      {
      if (last_packet_header)
	 *last_packet_header = str_pos ;
      // get the packet's type
      uint32_t phdr = str_pos.nextBits(PACKHDR_SIZE) ;
      uint32_t is_last = (phdr & PACKHDR_LAST_MASK) != 0 ;
      bool exact_end = !is_last ;
      switch (PACKHDR_TYPE(phdr))
	 {
	 case PT_INVALID:
	    if (verbosity > VERBOSITY_PACKETS)
	       {
	       fprintf(stderr,"  encountered invalid packet type @ %lu.%u\n",
		       (unsigned long)(str_pos - str_start),
		       str_pos.bitNumber()) ;
	       }
	    success = false ;
	    break ;
	 case PT_FIXEDHUFF:
	    {
	    if (verbosity > VERBOSITY_PACKETS)
	       fprintf(stderr,"  decompressing fixed-Huff packet @ %lu.%u\n",
		       (unsigned long)(str_pos - str_start),
		       str_pos.bitNumber()) ;
	    HuffSymbolTable *symtab = build_default_symtable(deflate64) ;
	    if (!decompress(str_pos,str_end,symtab,decode_buffer,
			    start_of_stream,exact_end))
	       {
	       success = false ;
	       }
	    free_symbol_table(symtab) ;
	    break ;
	    }
	 case PT_DYNAMIC:
	    {
	    if (verbosity > VERBOSITY_PACKETS)
	       fprintf(stderr,"  decompressing dyn-Huff packet @ %lu.%u\n",
		       (unsigned long)(str_pos - str_start),
		       str_pos.bitNumber()) ;
	    HuffSymbolTable *symtab
	       = build_symbol_table(str_pos,str_end,
				    decode_buffer->deflate64()) ;
	    if (!decompress(str_pos,str_end,symtab,decode_buffer,
			    start_of_stream,exact_end))
	       {
	       success = false ;
	       }
	    free_symbol_table(symtab) ;
	    break ;
	    }
	 case PT_UNCOMP:
	    {
	    if (verbosity > VERBOSITY_PACKETS)
	       {
	       fflush(stdout) ;
	       fprintf(stderr,"  extracting uncompressed packet @ %lu.%u\n",
		       (unsigned long)(str_pos - str_start),
		       str_pos.bitNumber()) ;
	       }
	    str_pos.advanceToByte() ;
	    uint16_t size1 = (uint16_t)str_pos.nextBits(16) ;
	    uint16_t size2 = (uint16_t)str_pos.nextBits(16) ;
	    if (str_pos < str_end && size1 != 0)
	       {
	       if ((size1 ^ size2) == 0xFFFF)
		  {
		  if (size1 > (str_end - str_pos))
		     size1 = (str_end - str_pos) ;
		  for (unsigned i = 0 ; i < size1 ; i++)
		     {
		     decode_buffer->addByte((unsigned char)str_pos.nextBits(8)) ;
		     }
		  }
	       else
		  {
		  if (!may_be_corrupt)
		     fprintf(stderr,
			     "internal error: invalid uncompressed packet @ %lu, sizes=%4.04X/%4.04X\n",
			     (unsigned long)(str_pos - str_start),size1,size2) ;
		  success = false ;
		  }
	       }
	    else if (verbosity > VERBOSITY_PACKETS)
	       {
	       if (size1 == 0)
		  fprintf(stderr,"empty uncompressed packet\n") ;
	       else
		  fprintf(stderr,"uncompressed packet header extends beyond end of stream\n") ;
	       }
	    break ;
	    }
	 }
      if (is_last)
	 {
	 if (verbosity > VERBOSITY_PACKETS)
	    fprintf(stderr,"  ** last packet\n") ;
	 if (hit_final_packet)
	    *hit_final_packet = true ;
	 break ;
	 }
      }
   if (verbosity > VERBOSITY_PACKETS)
      fprintf(stderr,"  ** decoded up to %lu of %lu\n",
	      (unsigned long)(str_pos - str_start),
	      (unsigned long)(str_end - str_start)) ;
   delete decode_buffer ;
   decode_buffer = 0 ;
   return success ;
}

//----------------------------------------------------------------------

static char *decompress_reference(const char *stream_start,
				  const char *stream_end,
				  const ZipRecParameters &params,
				  const char *outfile_hint,
				  bool deflate64 = true)
{
   BitPointer::initBitReversal() ;
   auto outfile = aprintf("%s.ref",outfile_hint) ;
   if (!outfile)
      return nullptr ;
   COutputFile outfp(outfile,CFile::binary) ;
   bool success = false ;
   if (outfp)
      {
      BitPointer str_start(stream_start) ;
      BitPointer str_end(stream_end) ;
      CpuTimer timer ;
      ZipRecParameters sub_params(params) ;
      sub_params.write_format = WFMT_PlainText ;
      DecodeBuffer *decode_buffer = new DecodeBuffer(outfp,sub_params.write_format,DEFAULT_UNKNOWN,outfile,deflate64) ;
      success = decompress(str_start,str_start,str_end,decode_buffer,"reference",outfile,false,true) ;
      ADD_TIME(timer,time_reference) ;
      }
   if (!success)
      {
      outfile = nullptr ;
      }
   return outfile.move() ;
}

//----------------------------------------------------------------------

static bool extract_uncompressed(CFile& outfp, const char *outfile, WriteFormat fmt,
				 const char *stream_start, const char *stream_end)
{
   if (!outfp || !stream_start || !stream_end || stream_end <= stream_start)
      return false ;
   DecodeBuffer buffer(outfp,fmt,DEFAULT_UNKNOWN,outfile,false) ;
   for (const char *str = stream_start ; str < stream_end ; str++)
      {
      if (!buffer.addByte((unsigned char)(*str)))
	 return false ;
      }
   INCR_STAT(uncompressed_files_recovered) ;
   return true ;
}

//----------------------------------------------------------------------

static DeflatePacketDesc *locate_packets(BitPointer str_start,
					 BitPointer str_end,
					 size_t base_offset,
					 bool deflate64)
{
   DeflatePacketDesc *packets = 0 ;
   BitPointer str_pos(str_end) ;
   BitPointer curr_end(str_end) ;
   bool exact_bit = false ;

   while (str_pos > str_start)
      {
      str_pos.retreat(MINIMUM_PACKET_SIZE_BITS) ;
      PacketType ptype = find_packet_start(str_pos,str_start,curr_end,
					   base_offset,packets == 0,
					   exact_bit,deflate64) ;
      if (ptype == PT_INVALID)
	 break ;
      // the header for a non-final uncompressed packet is 000, and
      //   the padding to align the size field to a byte boundary is
      //   also zero bits, so the actual packet boundary is ambiguous
      //   and we thus need to allow the EOD check on the preceding
      //   packet to test multiple positions
      exact_bit = (ptype != PT_UNCOMP) || (packets == 0) ;
      // add the packet to the list of all packets found
      DeflatePacketDesc *p = new DeflatePacketDesc(&str_start,&str_pos,
						   &curr_end,packets==0,
						   deflate64) ;
      p->setNext(packets) ;
      p->setPacketType(ptype) ;
      packets = p ;
      // update boundary pointers
      curr_end = str_pos ;
      }
   return packets ;
}

//----------------------------------------------------------------------

static bool contains_corruption(DeflatePacketDesc *packet,
				const DeflatePacketDesc *prev,
				DecodeBuffer &decode_buf,
				const FileInformation *fileinfo,
				bool previous_corruption)
{
   if (!fileinfo || !packet || packet->isUncompressed())
      return false ;
   packet->setUncompOffset(prev) ;
   const uint8_t *p = packet->packetHeader().bytePointer() ;
   const uint8_t *packet_start = p ;
   const uint8_t *packet_end = packet->packetEnd().bytePointer() ;
   // scan for long sequences of repeated bytes; those will normally
   //   be due to an unreadable sector
   while (p + MIN_REPETITIONS < packet_end)
      {
      if (p[0] != p[1])
	 {
	 p++ ;
	 continue ;
	 }
      unsigned count = 2 ;
      for (size_t i = 2 ; p + i < packet_end ; i++)
	 {
	 if (p[0] != p[i])
	    break ;
	 count++ ;
	 }
      if (count >= MIN_REPETITIONS)
	 {
	 unsigned long start = (unsigned long)(p - packet_start) ;
	 unsigned long endpt = (unsigned long)(p - packet_start + count) ;
	 packet->updateCorruption(start,endpt) ;
	 }
      p += count ;
      }
   // check whether we can correctly decompress the packet; if an
   //   error occurs, we call the point at which it is detected the
   //   start of the corruption
   unsigned long uncomp_size
      = packet->deflate64() ? REFERENCE_WINDOW_DEFLATE64 : REFERENCE_WINDOW_DEFLATE ;
   if (packet->corruptionStart() > 0 &&
       !check_compressed_packet(packet,decode_buf,fileinfo,uncomp_size,
				previous_corruption))
      {
      if (uncomp_size < decode_buf.referenceWindow())
	 uncomp_size = decode_buf.referenceWindow() ;
      decode_buf.clearReferenceWindow() ;
      }
   packet->setUncompSize(uncomp_size) ;
   return false ;
}

//----------------------------------------------------------------------

static bool locate_corrupt_segments(DeflatePacketDesc *packet_list,
				    const FileInformation *fileinfo)
{
   START_TIME(timer) ;
   bool corruption_found = false ;
   DeflatePacketDesc *prev = 0 ;
   CFile dummyfile ;
   DecodeBuffer decode_buf(dummyfile,WFMT_PlainText,'\x7F') ;
   for ( ; packet_list ; packet_list = packet_list->next())
      {
      if (contains_corruption(packet_list,prev,decode_buf,fileinfo,
			      corruption_found) ||
	  packet_list->containsCorruption())
	 {
	 corruption_found = true ;
	 }
      prev = packet_list ;
      }
   ADD_TIME(timer,time_corrupt_check) ;
   return corruption_found ;
}

//----------------------------------------------------------------------

static bool decompress_packet(DecodeBuffer *decode_buffer,
			      const DeflatePacketDesc *packet,
			      const BitPointer &packet_end,
			      HuffSymbolTable **symtab,
			      BitPointer &corruption_loc)
{
   BitPointer packet_start(packet->packetHeader()) ;
   uint32_t phdr = packet_start.nextBits(PACKHDR_SIZE) ;
   HuffSymbolTable *symbol_table = 0 ;
   const char *ptype = "" ;
   bool uncompressed = false ;
   switch (PACKHDR_TYPE(phdr))
      {
      case PT_INVALID:
	 return false ;
      case PT_UNCOMP:
	 ptype = "uncompressed" ;
	 uncompressed = true ;
	 packet_start.advanceToByte() ;
	 break ;
      case PT_FIXEDHUFF:
	 ptype = "fixed-Huff" ;
	 symbol_table = build_default_symtable(packet->deflate64()) ;
	 break ;
      case PT_DYNAMIC:
	 ptype = "dyn-Huff" ;
	 symbol_table = build_symbol_table(packet_start,packet_end,
					   packet->deflate64()) ;
	 break ;
      }
   if (verbosity > VERBOSITY_PACKETS)
      {
      fprintf(stderr,"  decompressing %s packet @ %lu.%u\n",ptype,
	      (unsigned long)(packet->packetHeader() - packet->streamStart()),
	      packet->packetHeader().bitNumber()) ;
      }
   if (symtab)
      *symtab = symbol_table ;
   bool success = true ;
   if (symbol_table)
      {
      success = decompress(packet_start,packet_end,symbol_table,
			   decode_buffer,
			   packet->packetHeader() == packet->streamStart(),
			   packet->next()) ;
      if (success)
	 {
	 if (symtab)
	    *symtab = 0 ;
	 }
      else
	 corruption_loc = packet_start ;
      }
   else if (uncompressed)
      {
      unsigned size1 = packet_start.nextBits(16) ;
      unsigned size2 = packet_start.nextBits(16) ;
      if (size1 == ~size2)
	 {
	 unsigned plen = (unsigned)(packet_end - packet_start) ;
	 if (size1 < plen)
	    size1 = plen ;
	 for (size_t i = 0 ; i < size1 ; i++)
	    {
	    decode_buffer->addByte((unsigned char)packet_start.nextBits(8)) ;
	    }
	 }
      }
   else
      success = false ;
   if (!symtab || !*symtab)
      free_symbol_table(symbol_table) ;
   return success ;
}

//----------------------------------------------------------------------

static BitPointer resynchronize(BitPointer &str_pos,
				const BitPointer &packet_end,
				const HuffSymbolTable *symtab,
				bool deflate64)
{
//symtab->dump();
   // the maximum possible length of a code is twice the maximum
   //   bit length of a Huffman symbol plus the maximum extra bits
   //   for a length code plus the maximum extra bits for a
   //   distance code
   unsigned num_positions = 2 * MAX_BITLENGTH ;
   num_positions += deflate64 ? (16+14) : (5+13) ;
   BitPointer positions[num_positions+1] ;
   // initialize the positions to be every possible bit
   for (size_t i = 0 ; i < num_positions ; i++)
      {
      positions[i] = str_pos ;
      positions[i].advance(i) ;
      }
   positions[num_positions] = packet_end ; // sentinel
   while (num_positions > 1)
      {
      // pick off the earliest boundary, advance by one symbol, and
      //   re-insert if the new boundary is not yet in the array and
      //   had not reached the end of the packet
      bool inserted = false ;
      BitPointer new_pos = positions[0] ;
      if (symtab->advance(new_pos,packet_end) && new_pos < packet_end)
	 {
	 // shift the new boundary into the correct location
	 unsigned new_loc = 1 ;
	 for ( ; new_loc < num_positions ; new_loc++)
	    {
	    if (new_pos <= positions[new_loc])
	       break ;
	    }
	 if (new_pos < positions[new_loc])
	    {
	    for (size_t i = 1 ; i < new_loc ; i++)
	       {
	       positions[i-1] = positions[i] ;
	       }
	    new_loc-- ;
	    positions[new_loc] = new_pos ;
	    inserted = true ;
	    }
	 }
      if (!inserted)
	 {
	 // move all remaining candidates down
	 for (size_t i = 1 ; i <= num_positions ; i++)
	    {
	    positions[i-1] = positions[i] ;
	    }
	 num_positions-- ;
	 }
      }
   if (verbosity >= VERBOSITY_PACKETS)
      {
      unsigned bytes = positions[0].bytePointer() - str_pos.bytePointer() ;
      unsigned bits = positions[0].bitNumber() ;
      fprintf(stderr,"DEFLATE stream re-converges after %u.%u bytes\n",
	      bytes,bits) ;
      }
   return positions[0] ;
}

//----------------------------------------------------------------------

static bool decompress_packet(DecodeBuffer *decode_buffer,
			      const ZipRecParameters &params,
			      const DeflatePacketDesc *packet)
{
   BitPointer packet_end(packet->packetEnd()) ;
   if (packet->containsCorruption())
      {
      packet_end = packet->packetHeader() ;
      packet_end.advanceToByte() ;
      packet_end.advanceBytes(packet->corruptionStart()) ;
      }
   HuffSymbolTable *symtab = 0 ;
   BitPointer corruption_loc((char*)0) ;
   bool success = decompress_packet(decode_buffer,packet,packet_end,
				    &symtab,corruption_loc) ;
   if (!success || packet->containsCorruption())
      {
      unsigned max_backref = decode_buffer->referenceWindow() ;
      bool clear = packet->uncompressedOffset() > 0 ;
      decode_buffer->addDiscontinuityMarker(max_backref,clear) ;
      }
   if (!success && !corruption_loc.bytePointer())
      {
      free_symbol_table(symtab) ;
      symtab = 0 ;
      }
   // if we have corruption in the middle of a packet, decompress the
   //   remainder using the symbol table built at the start of the packet
   if ((success || corruption_loc.bytePointer()) && symtab)
      {
      BitPointer str_pos(packet->packetHeader()) ;
      const BitPointer &packet_end = packet->packetEnd() ;
      str_pos.advanceBytes(packet->corruptionEnd()) ;
      if (corruption_loc > str_pos)
	 str_pos = corruption_loc ;
      if (params.reconstruct_partial_packet)
	 {
	 // search for a synchronization point following the end of
	 //   the known corruption, using the Huffman trees from the
	 //   packet's header
	 HuffmanHypothesis *longest = search(&str_pos,&packet_end,
					     symtab) ;
	 if (longest)
	    {
	    str_pos = longest->startPosition() ;
	    free_hypotheses(longest) ;
	    }
	 }
      else
	 {
	 // find the point at which the symbol streams for all possible 
	 //   starting bit offsets resynchronize
	 str_pos = resynchronize(str_pos,packet_end,symtab,
				 decode_buffer->deflate64()) ;
	 }
      if (!decompress(str_pos,packet_end,symtab,decode_buffer,
		      false,packet->next()))
	 success = false ;
      free_symbol_table(symtab) ;
      }
   return true ;
}

//----------------------------------------------------------------------

static bool decompress_packets(const ZipRecParameters &params,
			       DecodeBuffer *decode_buffer,
			       const DeflatePacketDesc *packet_list,
			       const char *outfile, bool known_start,
			       bool known_end)
{
   if (verbosity >= VERBOSITY_PROGRESS)
      {
      bool have_corruption = false ;
      for (const DeflatePacketDesc *pl = packet_list ; pl ; pl = pl->next())
	 {
	 if (pl->containsCorruption())
	    {
	    have_corruption = true ;
	    break ;
	    }
	 }
      fflush(stdout) ;
      const char *type ;
      if (have_corruption)
	 type = "recovered packets" ;
      else if (known_start && known_end)
	 type=  "entire file" ;
      else if (!known_start)
	 type = "final segment" ;
      else //if (!known_end)
	 type = "initial segment" ;
      fprintf(stderr,"decompressing %s to '%s'\n",type,outfile) ;
      }
   bool success = true ;
   bool hit_last = false ;
   if (packet_list && packet_list->uncompressedOffset() > 0)
      {
      // insert a discontinuity marker at the start of the output
      unsigned max_backref = decode_buffer->referenceWindow() ;
      decode_buffer->addDiscontinuityMarker(max_backref,false) ;
      }
   for ( ; packet_list ; packet_list = packet_list->next())
      {
      if (!decompress_packet(decode_buffer,params,packet_list))
	 success = false ;
      hit_last = packet_list->last() ;
      }
   if (!hit_last)
      {
      decode_buffer->addString(
	 "\n\n"
	 "*******************************************\n"
	 "***                                     ***\n"
	 "***      End of Compressed Stream       ***\n"
	 "***                                     ***\n"
	 "*******************************************\n\n",
	 DBYTE_CONFIDENCE_UNKNOWN) ;
      INCR_STAT(truncated_files_recovered) ;
      }
   return success ;
}

//----------------------------------------------------------------------

static bool recover_stream(const ZipRecParameters &params, const FileInformation *fileinfo,
			   CFile& outfp, const char *outfile,
			   const char *stream_start, const char *stream_end,
			   size_t base_offset, bool known_start, bool deflate64, bool known_end)
{
   BitPointer::initBitReversal() ;
   if (!outfp || !stream_start || !stream_end || stream_end <= stream_start)
      return false ;
   if (params.test_mode && params.test_mode_offset == 0 && known_start &&
       stream_start + params.test_mode_skip >= stream_end)
      {
      return false ;
      }
   CpuTimer timer ;
   BitPointer packet_start(stream_end) ;
   BitPointer last_packet_header(known_start ? stream_start : 0) ;
   DeflatePacketDesc *packet_list = 0 ;
   // if we have a fragment with a known start (due to a header found
   //   via its signature), but without a known end (because there is
   //   no end signature and we are processing a disk image), skip the
   //   scan for DEFLATE packets and just decompress from the start 
   //   until an error occurs
   if (known_end)
      {
      packet_list = locate_packets(stream_start,stream_end,base_offset,
				   deflate64) ;
      if (packet_list)
	 packet_start = packet_list->packetHeader() ;
      }
   bool success = (packet_list != 0) ;
   BitPointer str_start(stream_start) ;
   if (known_start && packet_start != stream_start)
      {
      DeflatePacketDesc *prefix
	 = new DeflatePacketDesc(&str_start,&str_start,&packet_start,
				 known_end,deflate64) ;
      prefix->setNext(packet_list) ;
      if (split_into_packets(prefix,deflate64))
	 {
	 packet_list = prefix ;
	 success = true ; // we got something valid out of the stream
	 INCR_STAT(truncated_files_recovered) ;
	 }
      else
	 {
	 prefix->setNext(0) ;
	 delete prefix ;
	 }
      }
   unsigned num_packets = packet_list->length() ;
   if (num_packets == 0 && known_start)
      num_packets = 1 ;
   if (num_packets)
      num_packets = (num_packets > PACKET_HISTOGRAM_SIZE
		      ? PACKET_HISTOGRAM_SIZE : num_packets-1) ;
   INCR_STAT(packet_count[num_packets]) ;
   bool have_corruption = locate_corrupt_segments(packet_list,fileinfo) ;
   if (known_start && packet_list && params.test_mode && !have_corruption)
      {
      // insert a deliberate corruption in the first packet
      if (params.test_mode_offset)
	 {
	 packet_list->updateCorruption(params.test_mode_offset,
				       params.test_mode_offset + params.test_mode_skip) ;
	 }
      else if (!packet_list->containsCorruption())
	 {
	 packet_list->clipStart(params.test_mode_skip) ;
	 }
      }
   ADD_TIME(timer,time_searching) ;
   timer.restart() ;
   WriteFormat wf = params.write_format ;
   WriteFormat fmt = (wf == WFMT_Listing) ? WFMT_None : wf ;
   DecodeBuffer decode_buffer(outfp,fmt,DEFAULT_UNKNOWN,outfile,deflate64) ;
   decompress_packets(params,&decode_buffer,packet_list,outfile,
		      known_start,known_end) ;
   ADD_TIME(timer,time_inflating) ;
   delete packet_list ;
   return success ;
}

//----------------------------------------------------------------------

static bool reconstruct_stream(const char *reconst_filename,
			       const char *output_filename,
			       const char *reference_filename,
			       const ZipRecParameters &params,
			       const FileInformation *fileinfo,
			       size_t start_offset,
			       size_t end_offset,
			       bool known_start,
			       bool deflate64,
			       bool known_end)
{
   bool using_stdin = fileinfo->usingStdin() ;
   auto opts = CFile::binary | (params.force_overwrite ? CFile::fail_if_exists : CFile::default_options) ;
   COutputFile recfp(reconst_filename, opts, using_stdin ? nullptr : CFile::askOverwrite) ;
   if (!recfp)
      {
      fprintf(stderr,"Unable to open temporary file '%s'\n",reconst_filename);
      return false ;
      }
   const char* outname = (params.write_format != WFMT_Listing) ? output_filename : NULL_DEVICE ;
   COutputFile outfp(outname,params.force_overwrite?CFile::default_options:CFile::fail_if_exists) ;
   if (!outfp)
      {
      fprintf(stderr,"Unable to open '%s' for writing\n",output_filename) ;
      return false ;
      }
   bool success = false ;
   // first, recover the stream to a DecodedByte file
   ZipRecParameters sub_params(params) ;
   sub_params.write_format = WFMT_DecodedByte ;
   const char *buffer_start = fileinfo->bufferStart() ;
   success = recover_stream(sub_params,fileinfo,recfp,reconst_filename,
			    buffer_start + start_offset,
			    buffer_start + end_offset, start_offset,
			    known_start, deflate64, known_end) ;
   recfp.flush() ;
   recfp.close() ;
   CInputFile recfile(reconst_filename,CFile::binary) ;
   CFile dummy ;
   DecodeBuffer decode_buffer(dummy) ;
   decode_buffer.openInputFile(recfile,reconst_filename) ;
   // apply language identification to the recovered text if applicable,
   //   and load the appropriate language model
   bool reconstruct = true ;
   const char *encoding = "ASCII" ;
   const char *detected_encoding = 0 ;
   const LanguageIdentifier *langid = fileinfo->langid() ;
   if (langid)
      {
      if (load_reconstruction_data_by_lang(decode_buffer,langid,encoding))
	 detected_encoding = encoding ;
      else
	 reconstruct = false ;
      decode_buffer.rewindInput() ;
      }
   // then, apply reconstruction to the DecodedByte file
   CpuTimer timer ;
   decode_buffer.setOutputFile(outfp,params.write_format,DEFAULT_UNKNOWN,
			       output_filename,detected_encoding,
			       params.test_mode) ;
   if (verbosity >= VERBOSITY_PROGRESS)
      fprintf(stderr," -> computing reconstruction for '%s'\n",
	      output_filename) ;
   if (reconstruct)
      {
      size_t num_iter = 0 ;
      if (decode_buffer.loadBytes(false,true))
	 {
	 if (params.reconstruct_align_discontinuities)
	    num_iter = decode_buffer.discontinuities() + 1 ;
	 if (num_iter < params.reconstruction_iterations)
	    num_iter = params.reconstruction_iterations  ;
	 }
      for (size_t iter = 0 ; iter < num_iter ; iter++)
	 {
	 bool last = (iter+1 == num_iter) ;
	 if (!infer_replacements(decode_buffer,encoding,iter,last))
	    break ;
	 if (!last && params.reconstruct_align_discontinuities &&
	     !decode_buffer.alignDiscontinuities())
	    break ;
	 decode_buffer.clearLoadedBytes() ;
	 if (!last)
	    decode_buffer.loadBytes(false,true) ;
	 }
      }
   // and finally, apply the inferred replacements, converting format as
   //   needed
   if (verbosity >= VERBOSITY_PROGRESS)
      fprintf(stderr," -> applying reconstruction to '%s'\n",
	      output_filename) ;
   bool have_replacements = false ;
   for (size_t i = 1 ; i <= decode_buffer.numReplacements() ; i++)
      {
      if (decode_buffer.haveReplacement(i))
	 {
	 if (decode_buffer.inferredLiteral(i))
	    {
	    INCR_STAT(replacements_matched) ;
	    }
	 else
	    {
	    INCR_STAT(replacements_found) ;
	    }
	 have_replacements = true ;
	 }
      }
   decode_buffer.applyReplacements(reference_filename,have_replacements) ;
   //fclose(outfp) ; // closed by decode_buffer dtor
   ADD_TIME(timer,time_reconstructing) ;
   return success ;
}

//----------------------------------------------------------------------

static void generate_output_filenames(const ZipRecParameters &params,
				      const char *output_directory,
				      const char *filename_hint,
				      off_t start_offset,
				      char *&filename,
				      CharPtr& default_filename,
				      CharPtr& reconst_filename)
{
   if (!output_directory || !*output_directory)
      output_directory = "" ;
   const char *extension = "dat" ;
   if (params.write_format == WFMT_HTML)
      extension = "htm" ;
   const char *name_base
      = recovery_name_base ? recovery_name_base : "recovered" ;
   default_filename = aprintf("%s/%s-%8.08lX.%s%c",
				 output_directory,name_base,
				 (unsigned long)start_offset,extension,'\0') ;
   reconst_filename = 0 ;
   if (params.perform_reconstruction)
      {
      reconst_filename = aprintf("%s/reconstruct-%8.08lX.dat%c",
				    output_directory,(unsigned long)start_offset,
				    '\0') ;
      }
   if (filename_hint && *filename_hint)
      {
      FilePath path(filename_hint) ;
      auto basename = path.basename() ;
      if (params.junk_paths)
	 {
	 if (basename)
	    filename_hint = basename ;
	 }
      else if (basename != filename_hint)
	 {
	 auto hint_path = path.directory() ;
	 auto path = aprintf("%s/%s%c",output_directory,hint_path,'\0') ;
	 if (path && params.write_format != WFMT_Listing)
	    {
	    if (!Fr::create_path(path))
	       {
	       fprintf(stderr,"Unable to create directory '%s'\n",*path) ;
	       }
	    }
	 }
      unsigned dir_len = strlen(output_directory) ;
      filename = New<char>(dir_len + strlen(filename_hint) + 8) ;
      if (filename)
	 {
	 if (strcmp(output_directory,".") != 0 &&
	     strcmp(output_directory,"") != 0)
	    {
	    strcpy(filename,output_directory) ;
	    strcat(filename,"/") ;
	    }
	 else
	    filename[0] = '\0' ;
	 strcat(filename,filename_hint) ;
	 if (params.write_format == WFMT_HTML)
	    strcat(filename,".htm") ;
	 }
      }
   else
      filename = default_filename.move() ;
   return ;
}

//----------------------------------------------------------------------

bool recover_stream(const LocationList *start_sig,
		    const LocationList *end_sig,
		    const ZipRecParameters &params,
		    const FileInformation *fileinfo,
		    const char *filename_hint,
		    uint32_t original_size_hint,
		    bool known_start, bool deflate64, 
		    bool known_end)
{
   const char *buffer_start = fileinfo->bufferStart() ;
   DecodedByte::setOriginalSize(original_size_hint) ;
   off_t end_offset = end_sig->offset() ;
   off_t start_offset ;
   if (start_sig)
      start_offset = start_sig->headerEndOffset(buffer_start) ;
   else if (original_size_hint > 0 && end_offset > (off_t)original_size_hint)
      start_offset = end_offset - original_size_hint ;
   else if (!deflate64 && end_offset > (off_t)MAX_DEFLATE_SIZE)
      start_offset = end_offset - MAX_DEFLATE_SIZE ;
   else
      start_offset = 0 ;
   if (start_sig && start_sig->signatureType() == ST_gzipHeader &&
       end_sig->signatureType() != ST_gzipEOF)
      end_offset -= 8 ; // account for gzip trailer record
   if (start_offset >= end_offset)
      return false ;
   if (verbosity >= VERBOSITY_PROGRESS)
      {
      fprintf(stdout,"attempting recovery on span %lu to %lu",
	      (unsigned long)start_offset,(unsigned long)end_offset) ;
      if (filename_hint && *filename_hint)
	 fprintf(stdout," (filename '%s')",filename_hint) ;
      fprintf(stdout,"\n") ;
      }
   char *filename ;
   CharPtr default_filename ;
   CharPtr reconst_filename ;
   const char *output_directory = fileinfo->outputDirectory() ;
   generate_output_filenames(params,output_directory,filename_hint,
			     start_offset,filename,default_filename,
			     reconst_filename) ;
   bool success = false ;
   bool is_uncompressed
      = (start_sig && start_sig->signatureType() == ST_LocalFileHeader &&
	 buffer_start[start_sig->offset() + 8] == 0) ;
   bool using_stdin = fileinfo->usingStdin() ;
   if (is_uncompressed && original_size_hint == (end_offset - start_offset))
      {
      if (params.test_mode)
	 {
	 if (params.write_format == WFMT_Listing)
	    {
	    // ensure that we get a line for this file in the scan listing
	    COutputFile outfp(NULL_DEVICE,CFile::binary) ;
	    DecodeBuffer decode_buffer(outfp,params.write_format,DEFAULT_UNKNOWN,filename,deflate64) ;
	    }
	 }
      else
	 {
	 if (verbosity >= VERBOSITY_PROGRESS)
	    {
	    fflush(stdout) ;
	    fprintf(stderr," -> extracting intact uncompressed data\n") ;
	    }
	 CFile outfp { open_output_file(filename,*default_filename, filename_hint,using_stdin,params) } ;
	 success = extract_uncompressed(outfp,filename,params.write_format,
					buffer_start + start_offset, buffer_start + end_offset) ;
	 }
      }
   else if (reconst_filename)
      {
      char *reference_filename = 0 ;
      if (/*known_start &&*/ params.test_mode)
	 {
	 reference_filename
	    = decompress_reference(buffer_start + start_offset,
				   buffer_start + end_offset,
				   params, filename, deflate64) ;
	 }
      success = reconstruct_stream(reconst_filename, filename,
				   reference_filename, params, fileinfo,
				   start_offset, end_offset, known_start,
				   deflate64, known_end) ;
      if (reference_filename)
	 {
	 unlink(reference_filename); 
	 }
      unlink(reconst_filename) ;
      }
   else if (filename)
      {
      char *reference_filename = 0 ;
      if (known_start && params.test_mode && 0)
	 {
	 reference_filename = decompress_reference(buffer_start + start_offset,
						   buffer_start + end_offset,
						   params,filename,deflate64) ;
	 }
      CFile outfp { open_output_file(filename,*default_filename, filename_hint,using_stdin,params) } ;
      if (outfp)
	 {
	 success = recover_stream(params,fileinfo,outfp,filename,
				  buffer_start + start_offset,
				  buffer_start + end_offset,
				  start_offset, known_start,
				  deflate64, known_end) ;
	 }
      else
	 fprintf(stderr,"unable to open '%s' for writing\n",filename) ;
      if (reference_filename)
	 {
	 unlink(reference_filename) ;
	 }
      }
   if (filename != default_filename)
      Free(filename) ;
   clear_default_symbol_table() ;
   return success ;
}

// end of file inflate.C //
