/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: dbyte.C - representation of a byte or back-reference		*/
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

#include <memory.h>
#include "byteio.h"
#include "dbyte.h"
#include "global.h"

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

static bool open_tag(FILE *outfp, ByteType bt)
{
   bool success = false ;
   switch (bt)
      {
      case BT_Unknown:
	 success = fputs("<B>", outfp) ;
	 break ;
      case BT_WildGuess:
	 success = fputs("<DFN>", outfp) ;
	 break ;
      case BT_Guessed:
	 success = fputs("<U>", outfp) ;
	 break ;
      case BT_Reconstructed:
	 success = fputs("<I>", outfp) ;
	 break ;
      case BT_UserSupplied:
	 success = fputs("<EM>", outfp) ;
	 break ;
      case BT_InferredLit:
	 success = fputs("<S>", outfp) ;
	 break ;
      case BT_Literal:
	 // not opening a tag, so nothing to do
	 success = true ;
	 break ;
      default:
	 // Uh oh, missed a case!
	 break ;
      }
   return success ;
}

//----------------------------------------------------------------------

static void close_tag(FILE *outfp, ByteType bt)
{
   switch (bt)
      {
      case BT_Unknown:
	 fputs("</B>", outfp) ;
	 break ;
      case BT_WildGuess:
	 fputs("</DFN>", outfp) ;
	 break ;
      case BT_Guessed:
	 fputs("</U>", outfp) ;
	 break ;
      case BT_Reconstructed:
	 fputs("</I>", outfp) ;
	 break ;
      case BT_UserSupplied:
	 fputs("</EM>", outfp) ;
	 break ;
      case BT_InferredLit:
	 fputs("</S>", outfp) ;
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

static bool write_HTML_char(unsigned char c, bool show_newlines, FILE *outfp,
			    ByteType bt)
{
   static unsigned char prev_char = '\0' ;
   bool success = false ;
   switch (c)
      {
      case '<':
	 success = (fputs("&lt;", outfp) != EOF) ;
	 break ;
      case '&':
	 success = (fputs("&amp;", outfp) != EOF) ;
	 break ;
#ifndef USE_PRE_TAG
      case '\t':
	 success = (fputs(" &nbsp; ", outfp) != EOF) ;
	 break ;
      case '\n':
	 if (show_newlines)
	    fputs("&#x21A9;", outfp) ;
	 close_tag(outfp,bt) ;
	 if (prev_char == '\n' && !show_newlines)
	    success = (fputs("<p/>\n", outfp) != EOF) ;
	 else
	    success = (fputs("<br/>\n", outfp) != EOF) ;
	 if (success)
	    success = open_tag(outfp,bt) ;
	 break ;
      case ' ':
	 if (prev_char == ' ')
	    success = (fputs("&nbsp;", outfp) != EOF) ;
	 else
	    success = (fputc('\n', outfp) != EOF) ;
	 break ;
#else
      case '\n':
	 success = true ;
	 if (show_newlines)
	    success = (fputs("&#x21A9;", outfp) != EOF) ;
	 close_tag(outfp,bt) ;
	 if (prev_char == '\n' && !show_newlines)
	    success = (fputs("</PRE>&nbsp;\n<PRE>", outfp) != EOF) ;
	 else
	    success = (fputs("</PRE>\n<PRE>", outfp) != EOF) ;
	 if (success)
	    success = open_tag(outfp,bt) ;
	 break ;
#endif /* !USE_PRE_TAG */
      case '\r':
	 if (show_newlines)
	    success = (fputs("&#x21B3;", outfp) != EOF) ;
	 else
	    success = (fputc(c, outfp) != EOF) ;
	 break ;
      default:
	 success = (fputc(c, outfp) != EOF) ;
	 break ;
      }
   prev_char = c ;
   return success ;
}

//----------------------------------------------------------------------

bool DecodedByte::read(FILE *infp)
{
   bool success = false ;
   if (infp)
      {
      uint32_t value ;
      if (read32(infp,value))
	 {
	 m_byte_or_pointer = value ;
	 success = true ;
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

bool DecodedByte::write(FILE *outfp, WriteFormat fmt,
			unsigned char unknown_char,
			DecodeBuffer *dbuf) const
{
   bool success = true ;
   switch (fmt)
      {
      case WFMT_PlainText:
	 success = (fputc(isLiteral()? byteValue() : unknown_char,
			  outfp) != EOF) ;
	 break ;
      case WFMT_DecodedByte:
	 if (!write32(m_byte_or_pointer,outfp))
	    success = false ;
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
	       success = write_HTML_char(byteValue(),bt < BT_InferredLit,
					 outfp, bt) ;
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

bool DecodedByte::writeBuffer(const DecodedByte *buf, size_t n_elem,
			      FILE *outfp, WriteFormat fmt,
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

bool DecodedByte::writeHTMLHeader(FILE *outfp, const char *encoding,
				  bool test_mode)
{
   if (fputs("<HTML><HEAD>\n"
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
		 "</STYLE>\n",
	     outfp) == EOF)
      return false ;
   if (!(!encoding || !*encoding ||
	 fprintf(outfp,"<META http-equiv=\"content-type\" "
		 "content=\"text/html; charset=%s\"\n",encoding) > 0))
      return false ;
   if (fputs("</HEAD><BODY>" PRE_TAG_OPEN "\n", outfp) == EOF)
      return false ;
   if (verbosity &&
       fputs("<HR>\n<PRE>Key:\n"
	     "   Recovered from file\n"
	     "  <S> Matched across corrupt region </S>\n"
	     "  <EM> User-supplied </EM>\n"
	     "  <I> high-confidence reconstruction </I>\n"
	     "  <U> medium-confidence reconstruction </U>\n"
	     "  <DFN> low-confidence reconstruction </DFN>\n"
	     "  <B> Unknown </B>\n"
	     "</PRE><HR>\n",outfp) == EOF)
      return false ;
   return (!test_mode ||
	   fputs("********* TEST MODE ************** TEST MODE **********\n",
		 outfp) != EOF) ;
}

//----------------------------------------------------------------------

bool DecodedByte::writeDBHeader(FILE *outfp, size_t reference_window) 
{
   bool success = (fwrite(DECODEDBYTE_SIGNATURE,sizeof(char),
			  sizeof(DECODEDBYTE_SIGNATURE),outfp)
		   == sizeof(DECODEDBYTE_SIGNATURE) ) ;
   if (success)
      {
      // write a dummy count and offset for the data bytes
      success = (write64(0,outfp) && write64(0,outfp)) ;
      if (success)
	 {
	 // store the size of the reference window, the bytes per
	 //   DecodedByte, and a dummy number of discontinuities
	 success = (write32(reference_window,outfp) &&
		    write16(BYTES_PER_DBYTE,outfp) && write16(0,outfp)) ;
	 }
      if (success)
	 {
	 // write a dummy offset and count for the replacement values,
	 //   as well as a dummy for the highest replaced value
	 success = (write64(140,outfp) && write32(0,outfp) &&
		    write32(0,outfp)) ;
	 }
      if (success)
	 {
	 // write a dummy offset and count for the DEFLATE packet
	 //   descriptors
	 success = (write64(0,outfp) && write32(0,outfp)) ;
	 }
      if (success)
	 {
	 // some padding for possible future additions
	 success = (write32(0,outfp) && write64(0,outfp) &&
		    write64(0,outfp) && write64(0,outfp) &&
		    write64(0,outfp) && write64(0,outfp) &&
		    write64(0,outfp) && write64(0,outfp) &&
		    write64(0,outfp)) ;
	 }
      // get and store the offset of the DecodedBytes which are about
      //   to be appended
      if (success)
	 {
	 off_t db_offset = ftell(outfp) ;
	 fseek(outfp,sizeof(DECODEDBYTE_SIGNATURE),SEEK_SET) ;
	 success = write64(db_offset,outfp) ;
	 // return to end of file
	 fseek(outfp,db_offset,SEEK_SET) ;
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

bool DecodedByte::writeHeader(WriteFormat fmt, FILE *outfp,
			      const char *encoding,
			      size_t reference_window, bool test_mode,
			      DecodeBuffer *dbuf)
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

bool DecodedByte::writeMessage(WriteFormat fmt, FILE *outfp,
			       const char *msg)
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

bool DecodedByte::writeFooter(WriteFormat fmt, FILE *outfp,
			      const char *filename, bool test_mode,
			      DecodeBuffer *dbuf)
{
   if (fmt == WFMT_HTML)
      {
      if (test_mode)
	 fputs("\n\n\n************** TEST MODE ***************\n", outfp) ;
      return fputs(PRE_TAG_CLOSE "</BODY></HTML>\n", outfp) != EOF ;
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
