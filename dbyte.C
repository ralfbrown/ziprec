/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: dbyte.C - representation of a byte or back-reference		*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-25						*/
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

#include <memory.h>
#include "dbyte.h"
#include "global.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest Constants for this module				*/
/************************************************************************/

#ifdef USE_PRE_TAG
#  define PRE_TAG_OPEN "<PRE>"
#  define PRE_TAG_CLOSE "</PRE>"
#else
#  define PRE_TAG_OPEN ""
#  define PRE_TAG_CLOSE ""
#endif

/************************************************************************/
/*	Globals for class DecodedByte					*/
/************************************************************************/

ByteType DecodedByte::s_prev_bytetype = BT_Literal ;
size_t DecodedByte::s_total_bytes = 0 ;
size_t DecodedByte::s_known_bytes = 0 ;
size_t DecodedByte::s_original_size = 0 ;
uint64_t DecodedByte::s_global_total_bytes = 0 ;
uint64_t DecodedByte::s_global_known_bytes = 0 ;
uint64_t DecodedByte::s_global_original_size = 0 ;

const ByteType DecodedByte::s_confidence_to_type[] =
   {
      BT_Unknown, BT_WildGuess, BT_WildGuess, BT_WildGuess,
      BT_WildGuess, BT_WildGuess, BT_WildGuess, BT_WildGuess,
      BT_WildGuess, BT_WildGuess, BT_WildGuess, BT_WildGuess,
      BT_WildGuess, BT_WildGuess, BT_WildGuess, BT_WildGuess,
      BT_WildGuess, BT_WildGuess, BT_WildGuess, BT_WildGuess,
      BT_Guessed, BT_Guessed, BT_Guessed, BT_Guessed,
      BT_Guessed, BT_Guessed, BT_Guessed, BT_Guessed,
      BT_Guessed, BT_Guessed, BT_Guessed, BT_Guessed,
      BT_Guessed, BT_Guessed, BT_Guessed, BT_Guessed,
      BT_Guessed, BT_Guessed, BT_Guessed, BT_Guessed,
      BT_Guessed, BT_Guessed, BT_Guessed, BT_Guessed,
      BT_Guessed, BT_Guessed, BT_Guessed, BT_Guessed,
      BT_Reconstructed, BT_Reconstructed, BT_Reconstructed, BT_Reconstructed,
      BT_Reconstructed, BT_Reconstructed, BT_Reconstructed, BT_Reconstructed,
      BT_Reconstructed, BT_Reconstructed, BT_Reconstructed, BT_Reconstructed,
      BT_Reconstructed, BT_Reconstructed, BT_Reconstructed, BT_UserSupplied,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_InferredLit, BT_InferredLit, BT_InferredLit, BT_InferredLit,
      BT_Literal, BT_Literal, BT_Literal, BT_Literal,
      BT_Literal, BT_Literal, BT_Literal, BT_Literal,
      BT_Literal, BT_Literal, BT_Literal, BT_Literal,
      BT_Literal, BT_Literal, BT_Literal, BT_Literal,
      BT_Literal, BT_Literal, BT_Literal, BT_Literal,
      BT_Literal, BT_Literal, BT_Literal, BT_Literal,
      BT_Literal, BT_Literal, BT_Literal, BT_Literal,
      BT_Literal, BT_Literal, BT_Literal, BT_Literal
   } ;

/************************************************************************/
/*	Methods for class DecodedByte					*/
/************************************************************************/

static bool open_tag(CFile& outfp, ByteType bt)
{
   switch (bt)
      {
      case BT_Unknown:
	 outfp.puts("<B>") ;
	 break ;
      case BT_WildGuess:
	 outfp.puts("<DFN>") ;
	 break ;
      case BT_Guessed:
	 outfp.puts("<U>") ;
	 break ;
      case BT_Reconstructed:
	 outfp.puts("<I>") ;
	 break ;
      case BT_UserSupplied:
	 outfp.puts("<EM>") ;
	 break ;
      case BT_InferredLit:
	 outfp.puts("<S>") ;
	 break ;
      case BT_Literal:
	 // not opening a tag, so nothing to do
	 break ;
      default:
	 // Uh oh, missed a case!
	 return false ;
      }
   return true ;
}

//----------------------------------------------------------------------

static void close_tag(CFile& outfp, ByteType bt)
{
   switch (bt)
      {
      case BT_Unknown:
	 outfp.puts("</B>") ;
	 break ;
      case BT_WildGuess:
	 outfp.puts("</DFN>") ;
	 break ;
      case BT_Guessed:
	 outfp.puts("</U>") ;
	 break ;
      case BT_Reconstructed:
	 outfp.puts("</I>") ;
	 break ;
      case BT_UserSupplied:
	 outfp.puts("</EM>") ;
	 break ;
      case BT_InferredLit:
	 outfp.puts("</S>") ;
	 break ;
      case BT_Literal:
	 // no open tag, so nothing to do
	 break ;
      default:
	 // Uh oh, missed a case!
	 break ;
      }
   return ;
}

//----------------------------------------------------------------------

static bool write_HTML_char(unsigned char c, bool show_newlines, CFile& outfp, ByteType bt)
{
   static unsigned char prev_char = '\0' ;
   bool success = false ;
   switch (c)
      {
      case '<':
	 outfp.puts("&lt;") ;
	 break ;
      case '&':
	 outfp.puts("&amp;") ;
	 break ;
#ifndef USE_PRE_TAG
      case '\t':
	 outfp.puts(" &nbsp; ") ;
	 break ;
      case '\n':
	 if (show_newlines)
	    outfp.puts("&#x21A9;") ;
	 close_tag(outfp,bt) ;
	 if (prev_char == '\n' && !show_newlines)
	    outfp.puts("<p/>\n") ;
	 else
	    outfp.puts("<br/>\n") ;
	 if (success)
	    success = open_tag(outfp,bt) ;
	 break ;
      case ' ':
	 if (prev_char == ' ')
	    outfp.puts("&nbsp;") ;
	 else
	    outfp.putc('\n') ;
	 break ;
#else
      case '\n':
	 success = true ;
	 if (show_newlines)
	    outfp.puts("&#x21A9;") ;
	 close_tag(outfp,bt) ;
	 if (prev_char == '\n' && !show_newlines)
	    outfp.puts("</PRE>&nbsp;\n<PRE>") ;
	 else
	    outfp.puts("</PRE>\n<PRE>") ;
	 if (success)
	    success = open_tag(outfp,bt) ;
	 break ;
#endif /* !USE_PRE_TAG */
      case '\r':
	 if (show_newlines)
	    outfp.puts("&#x21B3;") ;
	 else
	    outfp.putc(c) ;
	 break ;
      default:
	 outfp.putc(c) ;
	 break ;
      }
   prev_char = c ;
   return success ;
}

//----------------------------------------------------------------------

bool DecodedByte::read(CFile& infp)
{
   bool success = false ;
   if (infp)
      {
      uint32_t value ;
      if (infp.read32LE(value))
	 {
	 m_byte_or_pointer = value ;
	 success = true ;
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

bool DecodedByte::write(CFile& outfp, WriteFormat fmt, unsigned char unknown_char, DecodeBuffer *dbuf) const
{
   bool success = true ;
   switch (fmt)
      {
      case WFMT_PlainText:
	 outfp.putc(isLiteral()? byteValue() : unknown_char) ;
	 break ;
      case WFMT_DecodedByte:
	 success &= outfp.write32LE(m_byte_or_pointer) ;
	 break ;
      case WFMT_HTML:
         {
	 ByteType bt = byteType() ;
	 if (bt != prevByteType())
	    {
	    close_tag(outfp,prevByteType()) ;
	    success = open_tag(outfp,bt) ;
	    prevByteType(bt) ;
	    }
	 if (success)
#ifdef DEBUG_OUTPUT
	    {
	    if (isLiteral())
	       success = write_HTML_char(byteValue(),bt < BT_InferredLit, outfp, bt) ;
	    else
	       {
	       // show the co-index for the unknown instead of a question mark
	       static char hex[] = "0123456789ABCDEF" ;
	       unsigned loc = originalLocation() ;
	       write_HTML_char('[',false, outfp, bt) ;
	       write_HTML_char(hex[(loc>>12)&0xF],false, outfp, bt) ;
	       write_HTML_char(hex[(loc>>8)&0xF],false, outfp, bt) ;
	       write_HTML_char(hex[(loc>>4)&0xF],false, outfp, bt) ;
	       write_HTML_char(hex[loc&0xF],false, outfp, bt) ;
	       write_HTML_char(']',false, outfp, bt) ;
	       }
	    }
#else
	    success = write_HTML_char(isLiteral() ? byteValue() : unknown_char,
				      bt < BT_InferredLit, outfp, bt) ;
#endif
	 break ;
	 }
      case WFMT_Listing:
         {
	 s_total_bytes++ ;
	 s_global_total_bytes++ ;
	 if (isLiteral())
	    {
	    s_known_bytes++ ;
	    s_global_known_bytes++ ;
	    }
	 break ;
	 }
      case WFMT_Buffered:
	 //FIXME
	 (void)dbuf;
	 break ;
      case WFMT_None:
	 success = true ;
	 break ;
      default:
	 success = false ;
	 break ;
      }
   return success ;
}

//----------------------------------------------------------------------

bool DecodedByte::writeBuffer(const DecodedByte *buf, size_t n_elem, CFile& outfp, WriteFormat fmt,
			      unsigned char unknown_char)
{
   if (!outfp || !buf)
      return false ;
   bool success = true ;
   for (size_t i = 0 ; i < n_elem ; i++)
      {
      if (!buf[i].write(outfp,fmt,unknown_char))
	 {
	 success = false ;
	 break ;
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

bool DecodedByte::writeHTMLHeader(CFile& outfp, const char *encoding, bool test_mode)
{
   outfp.puts("<HTML><HEAD>\n"
		 "<STYLE>\n"
		 "/* compressed file recovered/reconstructed by ZipRec */\n"
		 "BODY {\n"
		 "  font-family : arial, verdana, sans-serif;\n"
		 "  color : black; background : white; font-weight: bold;\n"
		 "  }\n"
#ifdef USE_PRE_TAG
		 "PRE {\n"
		 " margin: 0 0 0 0 ;\n"
		 " padding: 0 0 0 0 ;\n"
		 " white-space: pre-wrap;  /* css-3 */\n"
		 " white-space: -moz-pre-wrap !important; /* Mozilla */\n"
		 " white-space: -pre-wrap; /* Opera 4-6 */\n"
		 " white-space: -o-pre-wrap; /* Opera 7+ */\n"
		 " word-wrap: break-word; /* IE 5.5+ */\n"
		 "}\n"
#endif /* USE_PRE_TAG */
		 "B { text-decoration: none !important ; font-style: normal !important ; font-weight: normal !important ; color : red ; } /* unknown */\n"
		 "DFN { text-decoration: none !important ; font-style: normal !important ; font-weight: normal !important ; color : orange ; background: #FFFF30 ; } /* low confidence */\n"
		 "U { text-decoration: none !important ; font-style: normal !important ; color : #FF0000 ; background: #FFFF80 ; } /* medium confidence */\n"
		 "I { text-decoration: none !important ; font-style: normal !important ; color : #00D000 ; background: #FFFFA0 ; } /* high confidence */\n"
		 "EM { text-decoration: none !important ; font-style: normal !important ; color : #0040F0 ; background: #FFFFD0 ; } /* user-supplied */\n"
		 "S { text-decoration: none !important ; font-style: normal ; font-weight: normal !important ; color : black ; background: #FFFFF0 ; } /* literal copied across a discontinuity */\n"
	         "</STYLE>\n") ;
   if (!(!encoding || !*encoding ||
	 outfp.printf("<META http-equiv=\"content-type\" content=\"text/html; charset=%s\"\n",encoding) > 0))
      return false ;
   outfp.puts("</HEAD><BODY>" PRE_TAG_OPEN "\n") ;
   if (verbosity)
       outfp.puts("<HR>\n<PRE>Key:\n"
	     "   Recovered from file\n"
	     "  <S> Matched across corrupt region </S>\n"
	     "  <EM> User-supplied </EM>\n"
	     "  <I> high-confidence reconstruction </I>\n"
	     "  <U> medium-confidence reconstruction </U>\n"
	     "  <DFN> low-confidence reconstruction </DFN>\n"
	     "  <B> Unknown </B>\n"
	     "</PRE><HR>\n") ;
   if (test_mode)
      outfp.puts("********* TEST MODE ************** TEST MODE **********\n") ;
   return true ;
}

//----------------------------------------------------------------------

bool DecodedByte::writeDBHeader(CFile& outfp, size_t reference_window) 
{
   bool success = outfp.writeSignature(DECODEDBYTE_SIGNATURE,DECODEDBYTE_VERSION) ;
   if (success)
      {
      // write a dummy count and offset for the data bytes
      success = (outfp.write64LE(0) && outfp.write64LE(0)) ;
      if (success)
	 {
	 // store the size of the reference window, the bytes per
	 //   DecodedByte, and a dummy number of discontinuities
	 success = (outfp.write32LE(reference_window) &&
		    outfp.write16LE(BYTES_PER_DBYTE) && outfp.write16LE(0)) ;
	 }
      if (success)
	 {
	 // write a dummy offset and count for the replacement values,
	 //   as well as a dummy for the highest replaced value
	 success = (outfp.write64LE(140) && outfp.write32LE(0) && outfp.write32LE(0)) ;
	 }
      if (success)
	 {
	 // write a dummy offset and count for the DEFLATE packet
	 //   descriptors
	 success = (outfp.write64LE(0) && outfp.write32LE(0)) ;
	 }
      if (success)
	 {
	 // some padding for possible future additions
	 success = (outfp.write32LE(0) && outfp.write64LE(0) &&
	    outfp.write64LE(0) && outfp.write64LE(0) &&
	    outfp.write64LE(0) && outfp.write64LE(0) &&
	    outfp.write64LE(0) && outfp.write64LE(0) &&
	    outfp.write64LE(0)) ;
	 }
      // get and store the offset of the DecodedBytes which are about
      //   to be appended
      if (success)
	 {
	 off_t db_offset = outfp.tell() ;
	 outfp.seek(sizeof(DECODEDBYTE_SIGNATURE)+6) ;
	 success = outfp.write64LE(db_offset) ;
	 // return to end of file
	 outfp.seek(db_offset) ;
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

bool DecodedByte::writeHeader(WriteFormat fmt, CFile& outfp, const char *encoding,
			      size_t reference_window, bool test_mode, DecodeBuffer *dbuf)
{
   prevByteType(BT_Literal) ;
   switch (fmt)
      {
      case WFMT_HTML:
	 return writeHTMLHeader(outfp,encoding,test_mode) ;
      case WFMT_DecodedByte:
	 return writeDBHeader(outfp,reference_window) ;
      case WFMT_Listing:
         {
	 s_total_bytes = 0 ;
	 s_known_bytes = 0 ;
	 return true ;
	 }
      case WFMT_Buffered:
         {
	 //FIXME
	 (void)dbuf;
	 return true ;
	 }
      default:
	 return true ;
      }
}

//----------------------------------------------------------------------

bool DecodedByte::writeMessage(WriteFormat fmt, CFile& outfp, const char *msg)
{
   if (!msg)
      return false ;
   DecodedByte dbyte ;
   for ( ; *msg ; msg++)
      {
      dbyte.setByteValue(*msg) ;
      dbyte.write(outfp,fmt,DEFAULT_UNKNOWN) ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool DecodedByte::writeFooter(WriteFormat fmt, CFile& outfp, const char *filename, bool test_mode,
			      DecodeBuffer *dbuf)
{
   if (fmt == WFMT_HTML)
      {
      if (test_mode)
	 outfp.puts("\n\n\n************** TEST MODE ***************\n") ;
      outfp.puts(PRE_TAG_CLOSE "</BODY></HTML>\n") ;
      return true ;
      }
   else if (fmt == WFMT_Listing)
      {
      if (s_original_size)
	 fprintf(stdout,"%c%10lu ",
		 (s_original_size == s_known_bytes) ? '+' : '-',
		 (unsigned long)s_original_size) ;
      else
	 fprintf(stdout,"        ??? ") ;
      fprintf(stdout,"%10lu %10lu %s\n",s_known_bytes,s_total_bytes,
	      filename) ;
      fflush(stdout) ;
      return true ;
      }
   else if (fmt == WFMT_Buffered)
      {
      //FIXME
      (void)dbuf;
      return true ;
      }
   else
      return true ;
}

//----------------------------------------------------------------------

void DecodedByte::addCounts(size_t known, size_t total, size_t original)
{
   s_known_bytes += known ;
   s_global_known_bytes += known ;
   s_total_bytes += total ;
   s_global_total_bytes += total ;
   s_original_size += original ;
   s_global_original_size += original ;
   return ;
}

//----------------------------------------------------------------------

void DecodedByte::clearCounts()
{
   s_total_bytes = 0 ;
   s_known_bytes = 0 ;
   s_original_size = 0 ;
   return ;
}

// end of file dbyte.C //
