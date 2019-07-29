/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-28						*/
/*									*/
/*  (c) Copyright 2011,2012,2013,2019 Carnegie Mellon University	*/
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

#include <climits>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <unistd.h>

using namespace std ;

#include "global.h"
#include "inflate.h"
#include "loclist.h"
#include "recover.h"
#include "reconstruct.h"
#include "whatlang2/langid.h"
#include "framepac/config.h"
#include "framepac/byteorder.h"
#include "framepac/file.h"
#include "framepac/mmapfile.h"
#include "framepac/texttransforms.h"
#include "framepac/timer.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

// the size of the fixed portion of a gzip member header
#define GZIP_HEADER_FIXED  10

// the values for the GZIP compression method byte
#define GZIP_METHOD_DEFLATE  8

// the bit fields in the gzip header "flags" byte
#define GZFLAG_CRC	   0x02    // header CRC is present
#define GZFLAG_EXTRA	   0x04	   // extra-data field is present
#define GZFLAG_FILENAME	   0x08    // filename is present
#define GZFLAG_COMMENT	   0x10    // file comment is present
#define GZFLAG_RESERVED	   0xE0

// the size of the required portion of a zlib file's header
#define ZLIB_HEADER_FIXED	2

// the bit that says whether we have the optional CRC field
#define ZLIB_HEADER_HAVEPRESETDICT 0x20

// the size of the fixed portion of an ALZip file header
#define ALZIP_HEADER_FIXED 13

// control of buffer sizes for using standard input
// default size if -b not specified
#define DEFAULT_BUFFER_MAX_SIZE  (512 * 1024UL * 1024UL)
// increment to use when reading from a non-seekable stream
#define BUFFER_GRANULARITY (32 * 1024 * 1024UL)

#define RAR_CRC_POLYNOMIAL 0xEDB88320UL

/************************************************************************/
/*	Forward declarations						*/
/************************************************************************/

static CharPtr get_gzip_filename_hint(const LocationList* prev, const char* buffer_start) ;

/************************************************************************/
/*	Type definitions						*/
/************************************************************************/

/*
   GZIP header format:
	byte	ID1 = 0x1F
	byte	ID2 = 0x8B
	byte	compression method
		8 = deflate
	byte	flags
		bit 0 file is probably ASCII text (hint)
		bit 1 header CRC present
		bit 2 extra field present
		bit 3 filename present
		bit 4 file comment present
	4 bytes original modification time (Unix time_t)
	byte	extra flags, specific to compression method
		for deflate, 2=max compression, 4=fastest compression
	byte	operating system / filesystem type on which compressed
		255 = unknown
	-- if flags bit 2 set --
	2 bytes length of extra field
	N bytes extra field
	-- if flags bit 3 set --
	N bytes zero-terminated filename
	-- if flags bit 4 set --
	N bytes zero-terminated comment
	-- if flags bit 1 set --
	2 bytes CRC of header
   DEFLATE stream immediately follows above header
   trailer:
   	4 bytes CRC32 of original uncompressed data
	4 bytes low 32 bits of original uncompressed size in bytes
*/

/* ZLib stream format
	byte	bits 3-0: compression format (8 = deflate)
		bits 7-4: for deflate: window size as 2^(nnn+8), max=7 (32K)
	byte	bits 4-0: checksum: ensures 256*byte1+byte2 == 0 mod 31
		bit 5:	preset dictionary, Adler-32 checksum follows
		bits 7-6: compression level
	4 bytes (optional) Adler-32 checksum of data in preset dictionary
	N bytes DEFLATE stream
	4 bytes Adler-32 checksum of uncompressed data
	END OF FILE
*/

/************************************************************************/
/*	Global variables						*/
/************************************************************************/

static size_t buffer_max_size = DEFAULT_BUFFER_MAX_SIZE ;
size_t blocking_size = 0 ;

static const char *signature_types[] =
   { 
      "Invalid",
      "Central Directory Entry",
      "Local File Header",
      "Central Directory Digital Signature",
      "End of Central Directory Record",
      "End of Central Directory Record (Zip64)",
      "End of Central Directory Locator",
      "Extra Data",
      "Data Descriptor",
      "Split Archive Indicator",
      "Split Archive (only required single segment) Indicator",
      "WavPack record header",
      "BZIP2 stream header",
      "BZIP2 block header",
      "BZIP2 end-of-stream record",
      "gzip member header",
      "gzip end of file",
      "ZIP start of file",
      "ZIP end of file",
      "ALZip archive header",
      "ALZip file header",
      "ALZip end of file",
      "Zlib file header",
      "Zlib end of file",
      "PDF FlateDecode header",
      "PDF FlateDecode endstream",
      "PNG zTXt chunk header",
      "PNG iTXt chunk header",
      "PNG chunk end",
      "RAW deflate start",
      "RAR archive marker",
      "RAR file header",
      "7zip signature",
      "Xz signature",
      "Lzip signature",
      "MS Cabinet signature",
      nullptr
   } ;

static uint32_t rar_CRC_table[256] ;
static bool rar_CRC_table_initialized = false ;

/************************************************************************/
/*	Utility functions and macros					*/
/************************************************************************/

static uint16_t get_word(const char *word)
{
#ifdef __386__
   // we can do unaligned memory accesses, and the native byte order is
   //   little-endian, so just cast to an appropriate type and dereference
   return *((uint16_t*)word) ;
#else
   uint8_t *w = (uint8_t*)word ;
   return (w[1] << 8) | w[0] ;
#endif /* __386__ */
}

//----------------------------------------------------------------------

static uint32_t get_dword(const char *word)
{
#ifdef __386__
   // we can do unaligned memory accesses, and the native byte order is
   //   little-endian, so just cast to an appropriate type and dereference
   return *((uint32_t*)word) ;
#else
   uint8_t *w = (uint8_t*)word ;
   return (w[3] << 24) | (w[2] << 16) | (w[1] << 8) | w[0] ;
#endif /* __386__ */
}

//----------------------------------------------------------------------

static uint32_t get_dword(const uint8_t *w)
{
#ifdef __386__
   // we can do unaligned memory accesses, and the native byte order is
   //   little-endian, so just cast to an appropriate type and dereference
   return *((uint32_t*)w) ;
#else
   return (w[3] << 24) | (w[2] << 16) | (w[1] << 8) | w[0] ;
#endif /* __386__ */
}

//----------------------------------------------------------------------

static bool is_stdin(const char *filename)
{
   return filename != nullptr && strcmp(filename,"-") == 0 ;
}

//----------------------------------------------------------------------

static CharPtr extract_local_header_filename(const LocationList *loc, const char *buffer_start)
{
   if (!loc)
      return nullptr ;
   // the filename is stored at offset 30 from the start of the local file
   //   header, and the length is stored (in little-endian format) in the
   //   two bytes at offset 26
   unsigned len = get_word(buffer_start + loc->offset() + 26) ;
   // filter out garbage filenames by ignoring the name if it contains
   //   control characters
   for (size_t i = 0 ; i < len ; i++)
      {
      if (buffer_start[loc->offset() + 30 + i] < ' ')
	 return nullptr ;
      }
   return dup_string_n(buffer_start + loc->offset() + 30, len) ;
}

//----------------------------------------------------------------------

static uint32_t extract_local_header_original_size(const LocationList* loc, const char* buffer_start)
{
   if (!loc)
      return 0 ;
   // the file's original size is stored at offset 22 from the start
   //   of the local file header, and the compressed size is at offset 18
   uint8_t *ofsptr = (uint8_t*)(buffer_start + loc->offset() + 22) ;
   uint32_t orig_size = get_dword(ofsptr) ;
   uint32_t comp_size = get_dword(ofsptr - 4) ;
   // ensure that we have a consistent local header
   return (orig_size >= comp_size) ? orig_size : 0 ;
}

//----------------------------------------------------------------------

static CharPtr extract_central_dir_filename(const LocationList* loc, const char* buffer_start)
{
   if (!loc)
      return nullptr ;
   // the filename is stored at offset 46 from the start of the central dir
   //   entry, and the length is stored (in little-endian format) in the
   //   two bytes at offset 28
   unsigned len = get_word(buffer_start + loc->offset() + 28) ;
   CharPtr name(len+1) ;
   if (name)
      {
      std::copy_n(buffer_start + loc->offset() + 46, len, name.begin()) ;
      name[len] = '\0' ;
      }
   return name ;
}

//----------------------------------------------------------------------

static uint32_t extract_central_dir_local_offset(const LocationList* loc, const char* buffer_start)
{
   if (!loc)
      return 0 ;
   // the local header's relative offset is stored at offset 42 from
   //   the start of the central directory entry
   uint8_t *ofsptr = (uint8_t*)(buffer_start + loc->offset() + 42) ;
   return get_dword(ofsptr) ;
}

//----------------------------------------------------------------------

static uint32_t extract_central_dir_original_size(const LocationList* loc, const char* buffer_start)
{
   if (!loc)
      return 0 ;
   // the file's original size is stored at offset 24 from the start
   //   of the central directory entry, and the compressed size is at offset 20
   uint8_t *ofsptr = (uint8_t*)(buffer_start + loc->offset() + 24) ;
   uint32_t orig_size = get_dword(ofsptr) ;
   uint32_t comp_size = get_dword(ofsptr - 4) ;
   // ensure that we have a consistent central directory entry
   return (orig_size >= comp_size) ? orig_size : 0 ;
}

//----------------------------------------------------------------------

static uint32_t extract_central_dir_end_cdir_offset(const LocationList* loc, const char* buffer_start)
{
   if (!loc)
      return 0 ;
   // the central directory's relative offset is stored at offset 16 from
   //   the start of the central directory end record
   uint8_t *ofsptr = (uint8_t*)(buffer_start + loc->offset() + 16) ;
   return get_dword(ofsptr) ;
}

//----------------------------------------------------------------------

static void init_rar_CRC()
{
   if (!rar_CRC_table_initialized)
      {
      for (unsigned i = 0 ; i < lengthof(rar_CRC_table) ; i++)
	 {
	 uint32_t CRC = i ;
	 for (unsigned bit = 0 ; bit < 8 ; bit++)
	    {
	    bool bit_set = (CRC & 1) != 0 ;
	    CRC >>= 1 ;
	    if (bit_set)
	       CRC ^= RAR_CRC_POLYNOMIAL ;
	    }
	 rar_CRC_table[i] = CRC ;
	 }
      rar_CRC_table_initialized = true ;
      }
   return ;
}

//----------------------------------------------------------------------

static uint32_t rar_CRC(const char *buffer, unsigned buflen)
{
   uint32_t CRC = 0xFFFFFFFF ;
   for (size_t i = 0 ; i < buflen ; i++)
      {
      uint32_t hi = CRC >> 8 ;
      uint8_t lo = buffer[i] ^ (uint8_t)CRC ;
      CRC = hi ^ rar_CRC_table[lo] ;
      }
   return ~CRC ;
}

/************************************************************************/
/*	Methods for class LocationList					*/
/************************************************************************/

off_t LocationList::headerEndOffset(const char *buffer,
				    bool zip64) const
{
   off_t end_offset = offset() ;
   switch (signatureType())
      {
      case ST_CentralDirEntry:
         {
	 uint16_t namelen = get_word(buffer+offset()+28) ;
	 uint16_t extralen = get_word(buffer+offset()+30) ;
	 uint16_t commentlen = get_word(buffer+offset()+32) ;
	 end_offset += 46 + namelen + extralen + commentlen ;
	 break ;
	 }
      case ST_LocalFileHeader:
         {
	 uint16_t namelen = get_word(buffer+offset()+26) ;
	 uint16_t extralen = get_word(buffer+offset()+28) ;
	 end_offset += 30 + namelen + extralen ;
	 break ;
	 }
      case ST_CentralDirSignature:
	 {
	 uint16_t extralen = get_word(buffer+offset()+2) ;
	 end_offset += 6 + extralen; // signature and length field
	 break ;
	 }
      case ST_DataDescriptor:
	 {
	 if (zip64)
	    end_offset += 24 ;
	 else
	    end_offset += 16 ;
	 break ;
	 }
      case ST_ExtraData:
	 {
	 uint32_t extralen = get_dword(buffer+offset()+4) ;
	 end_offset += 8 + extralen; // signature and length field
	 break ;
	 }
      case ST_gzipHeader:
         {
	 // we need to interpret the flags to figure out which optional fields
	 //   are present and then scan them
	 uint8_t flags = buffer[offset() + 3] ;
	 end_offset += GZIP_HEADER_FIXED ;
	 if (flags & GZFLAG_EXTRA)
	    {
	    end_offset += get_word(buffer+end_offset) + 2 ;
	    }
	 if (flags & GZFLAG_FILENAME)
	    {
	    // filename is a null-terminated string
	    end_offset += strlen(buffer+end_offset) + 1 ;
	    }
	 if (flags & GZFLAG_COMMENT)
	    {
	    // comment is a null-terminated string
	    end_offset += strlen(buffer+end_offset) + 1 ;
	    }
	 if (flags & GZFLAG_CRC)
	    end_offset += 2 ;
	 break ;
	 }
      case ST_ZlibHeader:
         {
	 end_offset += ZLIB_HEADER_FIXED ;
	 uint8_t flags = buffer[offset() + 1] ;
	 if ((flags & ZLIB_HEADER_HAVEPRESETDICT) != 0)
	    end_offset += 4 ;
	 break ;
	 }
      case ST_ALZipArchiveHeader:
         {
	 end_offset += 8 ;
	 break ;
	 }
      case ST_ALZipFileHeader:
         {
	 size_t filename_len = get_word(buffer+offset()+4) ;
	 end_offset += ALZIP_HEADER_FIXED + filename_len ;
	 size_t bits_per_field = (buffer[offset()+11] >> 4) && 0x0F ;
	 if (bits_per_field > 0)
	    {
	    end_offset += 6 ; // fixed-size optional fields are present
	    //FIXME: need to add 2*N, where N is computed from bits_per_field
	    }
	 break ;
	 }
      case ST_ALZipEOF:
         {
	 end_offset += 16 ;
	 break ;
	 }
      case ST_RARFileHeader:
         {
	 end_offset += get_word(buffer+offset()+5) ;
	 break ;
	 }
      case ST_DeflateSyncMark:
         {
	 end_offset += 4 ;
	 break ;
	 }
      default:
	 // do nothing
	 break ;
      }
   return end_offset ;
}

//----------------------------------------------------------------------

LocationList *LocationList::reverse()
{
   LocationList *loc = this ;
   LocationList *prev = nullptr ;
   while (loc)
      {
      LocationList *nxt = loc->next() ;
      loc->setNext(prev) ;
      prev = loc ;
      loc = nxt ;
      }
   return prev ;
}

/************************************************************************/
/************************************************************************/

static bool extract_stream(const LocationList* start_sig, const LocationList* end_sig,
			   const ZipRecParameters& params, const FileInformation* fileinfo,
			   const char* extension, bool include_header = false,
			   const char* prefix = nullptr, unsigned prefix_len = 0)
{
   if (!end_sig)
      return false ;
   const char *buffer_start = fileinfo->bufferStart() ;
   const char *output_directory = fileinfo->outputDirectory() ;
   off_t start_offset = start_sig ? start_sig->headerEndOffset(buffer_start) : 0 ;
   off_t end_offset = end_sig->offset() ;
   if (start_offset >= end_offset)
      return false ;
   if (!output_directory || !*output_directory)
      output_directory = "" ;
   auto filename = aprintf("%s/recovered-%8.08lX.%s",output_directory,(unsigned long)start_offset,extension) ;
   if (!filename)
      return false ;
   if (verbosity >= VERBOSITY_PROGRESS)
      {
      fprintf(stdout,"extracting span %lu to %lu (file '%s')\n",
	      (unsigned long)start_offset,(unsigned long)end_offset,*filename) ;
      }
   bool success = false ;
   size_t count = end_offset - start_offset ;
   if (params.write_format == WFMT_Listing)
      {
      CFile out(stdout) ;
      DecodedByte::writeHeader(params.write_format,out,nullptr,0,params.test_mode) ;
      DecodedByte::addCounts(0,count,count) ;
      DecodedByte::writeFooter(params.write_format,out,filename,params.test_mode) ;
      DecodedByte::clearCounts() ;
      if (count > 0)
	 success = true ;
      }
   else if (filename)
      {
      auto opts = CFile::binary | (params.force_overwrite ? CFile::fail_if_exists : CFile::default_options) ;
      COutputFile outfp(filename,opts,fileinfo->usingStdin()?nullptr:CFile::askOverwrite) ;
      if (outfp)
	 {
	 if (prefix && prefix_len > 0)
	    outfp.write(prefix,prefix_len) ;
	 if (include_header && start_sig)
	    {
	    unsigned headerlen = start_offset - start_sig->offset() ;
	    outfp.write(buffer_start + start_sig->offset(),headerlen) ;
	    }
	 success = outfp.write(buffer_start + start_offset,count) == count ;
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

static void dump_signature_list(const char *start,
				const LocationList *locations)
{
   uint32_t dir_offset = 0 ;
   for ( ; locations ; locations = locations->next())
      {
      if (locations->signatureType() != ST_Invalid)
	 {
	 fprintf(stdout, "found signature '%s' at offset %lu\n",
		 signature_types[locations->signatureType()],
		 (unsigned long)locations->offset()) ;
	 if (locations->signatureType() == ST_LocalFileHeader)
	    {
	    // print the filename stored in the local header
	    auto name = extract_local_header_filename(locations,start) ;
	    fprintf(stdout, "\tfilename = '%s'\n",*name) ;
	    }
	 else if (locations->signatureType() == ST_CentralDirEntry)
	    {
	    // print the filename stored in the central directory entry
	    auto name = extract_central_dir_filename(locations,start) ;
	    uint32_t offset = extract_central_dir_local_offset(locations,start) ;
	    fprintf(stdout, "\tfilename = '%s', local header at %lu\n",*name,(unsigned long)offset) ;
	    // remember the start of the central directory
	    if (dir_offset == 0)
	       dir_offset = locations->offset() ;
	    }
	 else if (locations->signatureType() == ST_EndOfCentralDir)
	    {
	    // print the offset of the start of the central directory
	    //   as stored in the end-of-directory record
	    uint32_t offset
	       = extract_central_dir_end_cdir_offset(locations,start) ;
	    int32_t skew = dir_offset ? (dir_offset - offset) : 0 ;
	    fprintf(stdout,
		    "\tsays central directory starts at %lu (skew = %ld)\n",
		    (unsigned long)offset,(long)skew) ;
	    }
	 else if (locations->signatureType() == ST_gzipHeader)
	    {
	    auto name = get_gzip_filename_hint(locations,start) ;
	    if (name)
	       {
	       fprintf(stdout,"\tfilename = '%s'\n",*name) ;
	       }
	    }
	 }
      }
   return ;
}

//----------------------------------------------------------------------

static bool is_gzip_header(const char *buffer_start, const char *bufpos)
{
   bool good_header = false ;
   if (bufpos[0] == 0x1F && (uint8_t)bufpos[1] == 0x8B)
      {
      // candidate header, so see whether it looks valid
      if (verbosity > VERBOSITY_PACKETS)
	 {
	 fprintf(stderr,"candidate header at offset %lu\n",
		 (unsigned long)(bufpos - buffer_start)) ;
	 }
      // we expect to see a header at the start of the file if it is not
      //   missing its beginning
      if (bufpos == buffer_start)
	 good_header = true ;
      else if (bufpos[2] == GZIP_METHOD_DEFLATE &&
	       (bufpos[3] & GZFLAG_RESERVED) == 0)
	 {
	 // OK, it's the compression type we're able to reconstruct, and
	 //   no reserved flag bits have been set
	 if (((uint8_t)bufpos[8]) <= 9)
	    {
	    if ((bufpos[3] & GZFLAG_EXTRA) == 0 ||
		get_word(bufpos+GZIP_HEADER_FIXED) < (32*1024))
	       {
	       // extra flags and the size of EXTRA_DATA (if present) are sane,
	       //   so declare this a valid header
	       good_header = true ;
	       }
	    }
	 }
      }
   if (good_header && verbosity > VERBOSITY_SCAN)
      {
      fprintf(stderr,"found gzip header at offset %lu\n",
	      (unsigned long)(bufpos - buffer_start)) ;
      }
   return good_header ;
}

//----------------------------------------------------------------------

static bool valid_zlib_stream(const char *bufpos, bool allow_fixedHuff = true)
{
   if ((bufpos[0] & 0x0F) == 8 // Deflate compression
       // assume max window size (32K), allow 16K window if not FF_ZlibAll
       && ((((uint8_t)bufpos[0]) >> 4) == 7 ||
	   (!allow_fixedHuff && (((uint8_t)bufpos[0]) >> 4) == 6))
       // verify a valid checksum on the two header bytes
       && ((bufpos[0] << 8) + (uint8_t)bufpos[1]) % 31 == 0)
      {
      return valid_packet_header(bufpos+2,false,allow_fixedHuff) ;
      }
   return false ;
}

//----------------------------------------------------------------------

static bool valid_RAR_file_header(const char *header, size_t max_header_len)
{
   // we've already confirmed that header[2] == 0x74.  Now check that the
   //  flags, OS, and file size fields are reasonable
   if (header[15] > 0x05)
      return false ;			// invalid OS flag
   // check for a reasonable value of the "version needed to uncompress"
   //   field, which contains 10*major + minor
   // since current version as of Dec 2011 is 4.0, allow all 4.x
   if (header[24] > 49)
      return false ;
   // check for a valid value of the compression method field
   if (header[25] < 0x30 || header[25] > 0x35)
      return false ;
   // flags bit 15 must be set, and bit 14 is reserved
   if ((header[4] & 0xC0) != 0x80)
      return false ;
   // to reduce false positives, assume filenames less than 4096 bytes
   if (header[27] >= 0x10)
      return false ;
   bool bigfile = (header[4] & 0x01) != 0 ;
   // check that uncompressed size is at least as big as compressed size
   if (bigfile)
      {
      uint64_t compsize
	 = (((uint64_t)get_dword(header+32)) << 32) + get_dword(header+7) ;
      uint64_t uncompsize
	 = (((uint64_t)get_dword(header+36)) << 32) + get_dword(header+11) ;
      if (compsize > uncompsize)
	 return false ;
      }
   else if (get_dword(header+7) > get_dword(header+11))
      {
      return false ;
      }
   unsigned headersize = get_word(header+5) ;
   if (headersize > max_header_len)
      return false ;
   unsigned min_header = bigfile ? 40 : 32 ;
   if (header[4] & 0x04)
      min_header += 8 ;			// encryption salt is present
   // header must be at least large enough to contain the required fields,
   //   the optional fields we know about, and the filename
   min_header += get_word(header+26) ; // get length of filename
   if (headersize < min_header)
      return false ;
   // sanity checks have succeeded, so now do the expensive CRC calculation
   //   to verify that the header is in fact valid
   unsigned CRC = rar_CRC(header+2,headersize-2) & 0xFFFF ;
   if (CRC != get_word(header))
      return false ;
   return true ;
}

//----------------------------------------------------------------------

static off_t rar_record_end(const LocationList *loc, const char *buffer_start)
{
   if (loc->signatureType() == ST_RARFileHeader)
      {
      off_t offset = loc->offset() ;
      const char *header = buffer_start + offset ;
      bool bigfile = (header[4] & 0x01) != 0 ;
      offset += get_word(header+5) + get_dword(header+7) ;
      if (bigfile)
	 offset += (((uint64_t)get_dword(header+32)) << 32) ;
      return offset ;
      }
   else if (loc)
      return loc->headerEndOffset(buffer_start) ;
   else
      return 0 ;
}

//----------------------------------------------------------------------

static bool valid_PNG_zTXt_chunk(const char *bufpos, const char *buffer_end,
				 uint32_t &offset)
{
   auto chklen = reinterpret_cast<const UInt32*>(bufpos-4) ;
   uint32_t chunk_len = chklen->load() ;
   const char *chunk_end = bufpos + chunk_len ;
   bufpos += 4 ; // skip tag
   offset = 8 ;
   if (chunk_end < buffer_end)
      {
      // skip the leading keyword
      unsigned count = 0 ;
      while (bufpos < chunk_end && *bufpos && count < 80)
	 {
	 bufpos++ ;
	 count++ ;
	 offset++ ;
	 }
      if (bufpos < chunk_end && count > 0 && count < 80)
	 {
	 bufpos++ ;  // skip the terminating NUL
	 if (*bufpos == (char)0) // compression = zlib?
	    {
	    return valid_zlib_stream(bufpos+1) ;
	    }
	 }
      }
   return false ;
}

//----------------------------------------------------------------------

static bool valid_PNG_iTXt_chunk(const char *bufpos, const char *buffer_end,
				 uint32_t &offset)
{
   auto chklen = reinterpret_cast<const UInt32*>(bufpos-4) ;
   uint32_t chunk_len = chklen->load() ;
   const char *chunk_end = bufpos + chunk_len ;
   bufpos += 4 ; // skip tag
   offset = 13 ;
   if (chunk_end < buffer_end)
      {
      // skip the leading keyword
      unsigned count = 0 ;
      while (bufpos < chunk_end && *bufpos && count < 80)
	 {
	 bufpos++ ;
	 count++ ;
	 offset++ ;
	 }
      if (bufpos < chunk_end && count > 0 && count < 80)
	 {
	 bufpos++ ;  // skip the terminating NUL
	 if (*bufpos == (char)1 && bufpos[1] == (char)0) // format = zlib?
	    {
	    bufpos += 2 ;
	    // skip the language tag
	    while (bufpos < chunk_end && *bufpos)
	       {
	       bufpos++ ;
	       offset++ ;
	       }
	    if (*bufpos)
	       return false ;
	    bufpos++ ;
	    // skip the tranlated keyword
	    while (bufpos < chunk_end && *bufpos)
	       {
	       bufpos++ ;
	       offset++ ;
	       }
	    if (*bufpos)
	       return false ;
	    bufpos++ ;
	    return valid_zlib_stream(bufpos) ;
	    }
	 }
      }
   return false ;
}

//----------------------------------------------------------------------

static LocationList *add_PNG_chunk_end(const char *bufpos,
				       const char *buffer_end,
				       uint64_t offset,
				       LocationList *locations)
{
   auto chklen = reinterpret_cast<const UInt32*>(bufpos-4) ;
   uint32_t chunk_len = chklen->load() ;
   if (bufpos + chunk_len < buffer_end)
      {
      return LocationList::push(ST_PNGChunkEnd,offset+chunk_len,locations) ;
      }
   return locations ;
}

//----------------------------------------------------------------------

static LocationList *scan_for_gzip_signatures(const char *buffer_start,
					      const char *buffer_end,
					      const ZipRecParameters &params)
{
   LocationList *locations = nullptr ;
   for (const char *bufpos = buffer_start + params.scan_range_start ;
	bufpos + 4 < buffer_end ;
	bufpos++)
      {
      if (is_gzip_header(buffer_start,bufpos))
	 {
	 locations = LocationList::push(ST_gzipHeader,bufpos - buffer_start, locations) ;
	 }
      }
   // finally, add a dummy header record for the end of the file
   size_t eof_offset = (buffer_end - buffer_start >= 8 ? buffer_end - buffer_start - 8 : 0) ;
   locations = LocationList::push(ST_gzipEOF,eof_offset,locations) ;
   return locations->reverse() ;
}

//----------------------------------------------------------------------

static LocationList *scan_for_zlib_signatures(const ZipRecParameters &params,
					      const FileInformation *fileinfo)
{
   const char *buffer_start = fileinfo->bufferStart() ;
   const char *buffer_end = fileinfo->bufferEnd() ;
   FileFormat format = fileinfo->format() ;
   bool allow_multiple = (format != FF_Zlib) ;
   bool allow_fixedHuff = (format == FF_ZlibAll) ;
   LocationList *locations = nullptr ;
   for (const char *bufpos = buffer_start + params.scan_range_start ;
	bufpos < buffer_end ;
	bufpos++)
      {
      if (valid_zlib_stream(bufpos,allow_fixedHuff))
	 {
	 locations = LocationList::push(ST_ZlibHeader,bufpos - buffer_start, locations) ;
	 INCR_STAT(zlib_file_header) ;
	 if (verbosity >= VERBOSITY_SCAN)
	    {
	    fprintf(stderr,"found probable zlib header at offset %lu\n", (unsigned long)(bufpos - buffer_start)) ;
	    }
	 if (!allow_multiple)
	     break ;
	 }
      }
   // finally, add a dummy header record for the end of the file
   size_t eof_offset = (buffer_end - buffer_start >= 4 ? buffer_end - buffer_start - 4 : 0) ;
   locations = LocationList::push(ST_ZlibEOF,eof_offset,locations) ;
   return locations->reverse() ;
}

//----------------------------------------------------------------------

static LocationList* check_ZIP_header(const char* bufpos, off_t offset, LocationList* locations)
{
   if (bufpos[2] == 0x01 && bufpos[3] == 0x02 && bufpos[4] >= bufpos[6])
      {
      // central directory entry
      locations = LocationList::push(ST_CentralDirEntry,offset, locations) ;
      INCR_STAT(central_dir_entry) ;
      }
   else if (bufpos[2] == 0x03 && bufpos[3] == 0x04 &&
	    (get_word(bufpos + 26) > 0))
      {
      // local file header; check that filename length is nonzero
      locations = LocationList::push(ST_LocalFileHeader,offset, locations) ;
      INCR_STAT(local_file_header) ;
      }
   else if (bufpos[2] == 0x05)
      {
      if (bufpos[3] == 0x05)
	 {
	 // central directory digital signature
	 locations = LocationList::push(ST_CentralDirSignature,offset, locations) ;
	 }
      else if (bufpos[3] == 0x06 && bufpos[5] < 0x40 &&
	       bufpos[7] < 0x40)
	 {
	 // end of central directory record; we'll assume that
	 //   the archive doesn't span more than 16K parts to
	 //   reduce false positives
	 // additionally, check that the "start of central dir
	 //   disk" is no higher than the "this disk" field
	 uint16_t this_disk = get_word(bufpos + 4) ;
	 uint16_t dir_disk = get_word(bufpos + 6) ;
	 if (dir_disk <= this_disk)
	    {
	    locations = LocationList::push(ST_EndOfCentralDir,offset, locations) ;
	    INCR_STAT(end_of_central_dir) ;
	    }
	 }
      }
   else if (bufpos[2] == 0x06)
      {
      if (bufpos[3] == 0x06 && bufpos[11] == 0 &&
	  bufpos[19] == 0 && get_dword(bufpos + 8) <= get_dword(bufpos + 16))
	 {
	 // Zip64 end of central directory record; we assume
	 //   that the archive doesn't total more than 2^56
	 //   bytes or span more than 16M parts :-)
	 // additionally, the start of the central directory can't
	 //   be on a disk greater than the total number of disks
	 locations = LocationList::push(ST_EndOfCentralDir64,offset, locations) ;
	 INCR_STAT(end_of_central_dir) ;
	 }
      else if (bufpos[3] == 0x07 && bufpos[7] == 0 && bufpos[19] == 0)
	 {
	 // Zip64 end of central directory locator; we assume
	 //  that the archive doesn't span more than 16M parts
	 //  :-) to reduce false positives
	 locations = LocationList::push(ST_EndOfCentralDirLocator,offset, locations) ;
	 }
      else if (bufpos[3] == 0x08 && bufpos[7] == 0)
	 {
	 // extra data record; we'll assume that there will
	 //   never be more than 16MB in the extra field to reduce
	 //   false positives
	 locations = LocationList::push(ST_ExtraData,offset, locations) ;
	 }
      }
   else if (bufpos[2] == 0x07 && bufpos[3] == 0x08)
      {
      // split-archive indicator (if at offset 0) or data descriptor
      //   (if located elsewhere in file).  Since we may have multiple
      //   archives concatenated, we consider the header to be at offset
      //   zero if it's the very first header we've seen or it's the
      //   first header after an end-of-central-dir header
      if (offset == 0 || !locations ||
	  locations->signatureType() == ST_EndOfCentralDir ||
	  locations->signatureType() == ST_EndOfCentralDir64 ||
	  locations->signatureType() == ST_EndOfCentralDirLocator)
	 {
	 locations = LocationList::push(ST_SplitArchiveIndicator,offset, locations) ;
	 }
      else
	 {
	 //FIXME: check that compressed-size field is <= uncomp size
	 //  (difficulty: may be either 4 or 8 byte fields!)
	 locations = LocationList::push(ST_DataDescriptor,offset, locations) ;
	 }
      }
   else if (bufpos[2] == '0' && bufpos[3] == '0' && offset == 0)
      {
      // flag: archive created as split/spanned archive, but only
      //   required a single segment (only valid at offset 0 in file)
      locations = LocationList::push(ST_SplitArchiveSingleSegment,offset, locations) ;
      }
   else
      {
      // invalid signature, so just skip it
      }
   return locations ;
}

//----------------------------------------------------------------------

static const bool signature_start_byte[256] =
   {
      false, false, false, false,  false, false, false, false, // 0x00-0x07
      false, false, false, false,  false, false, false, false, // 0x08-0x0F
      false, false, false, false,  false, false, false, true,  // 0x10-0x17
      false, false, false, false,  false, false, false, true,  // 0x18-0x1F
      false, false, false, false,  false, false, false, false, // 0x20-0x27
      false, false, false, false,  false, false, false, false, // 0x28-0x2F
      false, true,  false, false,  false, false, false, true,  // 0x30-0x37
      false, false, false, false,  false, false, false, false, // 0x38-0x3F
      false, true,  true,  true,   false, false, true,  false, // 0x40-0x47
      false, false, false, false,  true,  true,  false, false, // 0x48-0x4F
      true,  false, true,  false,  false, false, false, false, // 0x50-0x57
      false, true,  false, false,  false, false, false, false, // 0x58-0x5F
      false, false, false, false,  false, true,  false, false, // 0x60-0x67
      false, true,  false, false,  false, false, false, false, // 0x68-x06F
      false, false, false, false,  true,  false, false, true,  // 0x70-0x77
      false, false, true,  false,  false, false, false, false, // 0x78-0x7F
      false, false, false, false,  false, false, false, false, // 0x80
      false, false, false, false,  false, false, false, false,
      false, false, false, false,  false, false, false, false, // 0x90
      false, false, false, false,  false, false, false, false,
      false, false, false, false,  false, false, false, false, // 0xA0
      false, false, false, false,  false, false, false, false,
      false, false, false, false,  false, false, false, false, // 0xB0
      false, false, false, false,  false, false, false, false,
      false, false, false, false,  false, false, false, false, // 0xC0
      false, false, false, false,  false, false, false, false,
      false, false, false, false,  false, false, false, false, // 0xD0
      false, false, false, false,  false, false, false, false,
      false, false, false, false,  false, false, false, false, // 0xE0
      false, false, false, false,  false, false, false, false,
      false, false, false, false,  false, false, false, false, // 0xF0
      false, false, false, false,  false, false, false, true
   } ;

//----------------------------------------------------------------------

static LocationList* scan_for_ZIP_signatures(const char* buffer_start, const char* buffer_end,
					     const ZipRecParameters& params)
{
   LocationList *locations = nullptr ;
   bool have_central_dir = false ;
   for (const char *bufpos = buffer_start + params.scan_range_start ;
	bufpos < buffer_end ;
	bufpos++)
      {
      if (!signature_start_byte[(unsigned char)bufpos[0]])
	 continue ;
      off_t offset = bufpos - buffer_start ;
      switch (bufpos[0])
	 {
	 case 'P':
	    // check for PKware (ZIP) record headers
	    if (bufpos[1] == 'K')
	       {
	       // we have a candidate signature, so check whether it is valid
	       locations = check_ZIP_header(bufpos,offset,locations) ;
	       // since none of the other signatures starts with 'K',
	       //   we can skip ahead two bytes instead of one
	       bufpos++ ;
	       }
	    break ;
	 case 'w':
	    // check for WAVpack header
	    if (bufpos[1] == 'v' && bufpos[2] == 'p' && bufpos[3] == 'k')
	       {
	       // check for valid version and at most 128k samples in
	       //   this block
	       uint16_t version = get_word(bufpos + 8) ;
	       if (version < 0x0410 && bufpos[23] == 0)
		  {
		  // WavPack record header
		  locations = LocationList::push(ST_WavPackRecordHeader,offset, locations) ;
		  }
	       }
	    break ;
	 case 'B':
	    // check for BZIP2 headers
	    if (bufpos[1] == 'Z' && bufpos[2] == 'h' &&
		(bufpos[3] >= '1' && bufpos[3] <= '9'))
	       {
	       // BZIP2 stream header (BZh1 through BZh9)
	       locations = LocationList::push(ST_BZIP2StreamHeader,offset, locations) ;
	       }
	    else if (bufpos[1] == 'L' && bufpos[2] == 'Z' && bufpos[3] == 0x01)
	       {
	       // found ALZip file header signature ("BLZ\001")
	       // check for a sane filename length (<512)
	       if ((unsigned char)bufpos[5] < 2)
		  {
		  locations = LocationList::push(ST_ALZipFileHeader,offset, locations) ;
		  INCR_STAT(ALZip_file_header) ;
		  }
	       }
	    break ;
	 case 0x31: // '1'
	    if (bufpos[1] == 0x41 && bufpos[2] == 0x59 &&
		bufpos[3] == 0x26 && bufpos[4] == 0x53 && bufpos[5] == 0x59)
	       {
	       // BZIP2 record header
	       locations = LocationList::push(ST_BZIP2BlockHeader,offset, locations);
	       }
	    break ;
	 case 0x17:
	    if (bufpos[1] == 0x72 && bufpos[2] == 0x45 &&
		bufpos[3] == 0x38 && bufpos[4] == 0x50 &&
		(uint8_t)bufpos[5] == 0x90)
	       {
	       // BZIP2 record header
	       locations = LocationList::push(ST_BZIP2EndOfStream,offset, locations);
	       }
	    break ;
	 case 'F':
	    // check for PDF FlateDecode headers
	    if (!params.exclude_PDFs && bufpos + 23 < buffer_end &&
		memcmp(bufpos+1,"lateDecode>>\nstream\n",20) == 0)
	       {
	       // this is the start of a Zlib stream; skip the two-byte
	       //   Zlib header to work on the raw Deflate stream
	       locations = LocationList::push(ST_PDF_FlateHeader,offset + 23, locations) ;
	       INCR_STAT(FlateDecode_file_header) ;
	       }
	    break ;
	 case 'e':
	    // check for PDF FlateDecode end of stream
	    if (!params.exclude_PDFs && bufpos + 10 < buffer_end &&
		memcmp(bufpos+1,"ndstream\n",9) == 0)
	       {
	       // this marks the end of a Zlib stream; Zlib adds a four-byte
	       //   checksum after the end of the Deflate stream, so adjust
	       unsigned adj = 4 ;
	       if (bufpos[-1] == '\n')	// "endstream" may or may not have a
		  adj++ ;		//   leading newline
	       locations = LocationList::push(ST_PDF_FlateEnd,offset-adj, locations) ;
	       }
	    break ;
	 case 'i':
	 case 'z':
	    // check for PNG iTXt/zTXt chunk
	    if (bufpos[1] == 'T' && bufpos[2] == 'X' && bufpos[3] == 't')
	       {
	       uint32_t ofs ;
	       if (bufpos[0] == 'i' &&
		   valid_PNG_iTXt_chunk(bufpos,buffer_end,ofs))
		  {
		  locations = LocationList::push(ST_PNG_iTXt,offset+ofs, locations) ;
		  locations = add_PNG_chunk_end(bufpos,buffer_end,offset, locations) ;
		  }
	       else if (bufpos[0] == 'z' &&
			valid_PNG_zTXt_chunk(bufpos,buffer_end,ofs))
		  {
		  locations = LocationList::push(ST_PNG_zTXt,offset+ofs, locations) ;
		  locations = add_PNG_chunk_end(bufpos,buffer_end,offset, locations) ;
		  }
	       }
	    break ;
	 case 0x1F:
	    // check for Gzip header
	    if (is_gzip_header(buffer_start,bufpos) &&
		buffer_end - buffer_start >= 512 * 1024 * 1024)
	       {
	       // if scanning disk images, include gzip streams
	       locations = LocationList::push(ST_gzipHeader, bufpos - buffer_start, locations) ;
	       INCR_STAT(gzip_file_header) ;
	       }
	    break ;
	 case 'A':
	    // check for ALZip headers
	    if (bufpos[1] == 'L' && bufpos[2] == 'Z' && bufpos[1] == 0x01)
	       {
	       // ALZip magic number ("ALZ\001")
	       locations = LocationList::push(ST_ALZipArchiveHeader,offset,locations) ;
	       }
	    break ;
	 case 'C':
	    if (bufpos[1] == 'L' && bufpos[2] == 'Z' && bufpos[1] == 0x01)
	       {
	       // ALZip end of archive signature ("CLZ\001") 
	       // the end-of-archive record contains a second
	       //   signature at offset 12, which is "CLZ\002" for the
	       //   final volume of an archive or "CLZ\003" if there are
	       //   further volumes
	       if (bufpos[12] == 0x43 && bufpos[13] == 0x4C &&
		   bufpos[14] == 0x5A &&
		   (bufpos[15] == 2 || bufpos[15] == 3))
		  locations = LocationList::push(ST_ALZipEOF,offset,locations) ;
	       }
#if 0 //!!!
	    else if (bufpos[1] == 'K')
	       {
	       // possible MS-ZIP block, but we need more info to avoid false
	       //   positives
	       locations = LocationList::push(ST_MSZIPSignature,offset,locations) ;
	       }
#endif /* 0 */
	    break ;
	 case 'K': // enable in start_byte once supported!!
	    if (bufpos[1] == 'W' && bufpos[2] == 'A' && bufpos[3] == 'J' &&
		bufpos[4] == '\x88' && bufpos[5] == '\xF0' &&
		bufpos[6] == '\x27' && bufpos[7] == '\xD1' &&
		bufpos[9] == 0 && bufpos[8] < 5 && // compression method valid?
		get_word(bufpos+10) >= 14) // offset >= min header length?
	       locations = LocationList::push(ST_KWAJSignature,offset,locations);
	    break ;
	 case 'L':  // LZIP signature?
	    if (bufpos[1] == 'Z' && bufpos[2] == 'I' && bufpos[3] == 'P')
	       {
	       if (bufpos[4] <= 1) // version number, only 0 & 1 are valid
		  {
		  locations = LocationList::push(ST_LzipSignature,offset,locations) ;
		  INCR_STAT(lzip_marker) ;
		  }
	       }
	    break ;
	 case 'M':  // MS Cabinet File?
	    if (bufpos[1] == 'S' && bufpos[2] == 'C' && bufpos[3] == 'F' &&
		bufpos[24] < 100 && bufpos[25] < 10 && // version(min,maj)
		// offset of first CFFILE is within length of file
		get_dword(bufpos+8) > get_dword(bufpos+16) &&
		// no reserved flag bits set
		(bufpos[31] == 0))
	       {
	       locations = LocationList::push(ST_CabinetSignature,offset,locations) ;
	       INCR_STAT(cabinet_marker) ;
	       }
	    break ;
	 case 'R':  // RAR marker block?
	    if (bufpos[1] == 0x61 && bufpos[2] == 0x72 && bufpos[3] == 0x21 &&
		bufpos[4] == 0x1A && bufpos[5] == 0x07 && bufpos[6] == 0x00)
	       {
	       locations = LocationList::push(ST_RARMarker,offset,locations) ;
	       INCR_STAT(rar_marker) ;
	       }
	    break ;
	 case 'S':  //enable in start_byte once supported!
	    if (bufpos[1] == 'Z')
	       {
	       if (bufpos[2] == 'D' && bufpos[3] == 'D' &&
		   bufpos[4] == '\x88' && bufpos[5] == '\xF0' &&
		   bufpos[6] == 0x27 && bufpos[7] == '3' && bufpos[8] == 'A')
		  locations = LocationList::push(ST_SZDDSignature,offset,
					       locations) ;
	       else if (bufpos[2] == ' ' && bufpos[3] == '\x88' &&
			bufpos[4] == '\xF0' && bufpos[5] == '\x27' &&
			bufpos[6] == '3' && bufpos[7] == '\xD1')
		  locations = LocationList::push(ST_SZDDAltSignature,offset,locations) ;
	       }
	    break ;
	 case 0x74: // 't': possible RAR file header record
	    {
	    if (offset >= 2 && valid_RAR_file_header(bufpos-2,
						     buffer_end - bufpos + 2))
	       {
	       locations = LocationList::push(ST_RARFileHeader,offset-2, locations) ;
	       INCR_STAT(rar_file_header) ;
	       }
	    }
	    break ;
	 case '7':  // 7zip or Xz signature?
	    {
	    if (bufpos[1] == 'z')
	       {
	       if (bufpos[2] == '\xBC' && bufpos[3] == '\xAF' &&
		   bufpos[4] == 0x27 && bufpos[5] == 0x1C)
		  {
		  locations = LocationList::push(ST_7zipSignature,offset, locations) ;
		  INCR_STAT(SevenZip_signature) ;
		  }
	       else if (bufpos[2] == 'X' && bufpos[3] == 'Z' &&
			bufpos[4] == 0x00 &&
			offset > 0 && bufpos[-1] == '\xFD')
		  {
		  locations = LocationList::push(ST_XzStreamSignature,offset-1, locations) ;
		  INCR_STAT(Xz_signature) ;
		  }
	       }
	    }
	    break ;
	 case 'Y':  // Xz stream footer signature?
   	    {
	    if (bufpos[1] == 'Z')
	       {
	       // verify that we have a valid footer by checking that
	       //   the CRC-32 at offset -10 from the signature matches
	       //   the six bytes between the CRC-32 and the signature
//FIXME: verify CRC-32
	       }
	    }
	    break ;
	 case '\xFF':
   	    {
	    // potential zero-length uncompressed DEFLATE packet?
	    if (bufpos[1] == '\xFF' && offset > 2 &&
		bufpos[-1] == 0 && bufpos[-2] == 0 &&
		(bufpos[-3] & 0xC0) == 0 && // could this be a type0 packet?
		(bufpos[2] & 0x06) != 6)    // following packet valid?
	       {
	       locations = LocationList::push(ST_DeflateSyncMark,offset-2, locations) ;
	       INCR_STAT(Deflate_syncmarker) ;
	       }
	    }
	 default:
	    break ;
	 }
      }
   if (!have_central_dir)
      {
      // if we haven't seen a central directory entry, add a marker for the
      //   end of the file
      off_t eof_offset = buffer_end - buffer_start ;
      locations = LocationList::push(ST_zipEOF,eof_offset,locations) ;
      }
   return locations->reverse() ;
}

//----------------------------------------------------------------------

static void remove_location(LocationList*& locations, LocationList* loc)
{
   if (loc == locations)
      {
      locations = locations->next() ;
      loc->setNext(nullptr) ;
      delete loc ;
      }
   else if (locations)
      {
      LocationList* prev = locations ;
      for (LocationList* l = locations->next() ; l ; l = l->next())
	 {
	 if (l == loc)
	    {
	    prev->setNext(l->next()) ;
	    loc->setNext(nullptr) ;
	    delete loc ;
	    return ;
	    }
	 prev = l ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

static LocationList *sort_signatures(LocationList *locations)
{
   // the list is nearly sorted, except that ST_PNGChunkEnd might be
   //   out of place; since the out-of-place chunks are correctly ordered
   //   among themselves, a single pass of bubblesort will fix up the list
   //   in linear time
   // we also know that the very first element of the list can't be out of
   //   order, which simplifies the code
   LocationList *prev = locations ;
   LocationList *curr = locations->next() ;
   if (curr)
      {
      while (curr->next())
	 {
	 LocationList *next = curr->next() ;
	 if (next->offset() < curr->offset())
	    {
	    // swap "curr" and "next"
	    prev->setNext(next) ;
	    curr->setNext(next->next()) ;
	    next->setNext(curr) ;
	    }
	 curr = next ;
	 }
      }
   return locations ;
}

//----------------------------------------------------------------------

static LocationList *filter_signatures(LocationList* locations, const char* buffer_start,
				       const char* buffer_end)
{
   // remove spurious signatures by checking for consistency of the data
   //   following the signature and/or ordering of signatures
   // (this function is a low priority at the moment because on average
   //  there will be one spurious signature per 512MB of compressed data)
   LocationList* next ;
   LocationList* prev = nullptr ;
   for (LocationList* locs = locations ; locs ; locs = next)
      {
      next = locs->next() ;
      SignatureType sig = locs->signatureType() ;
      // filter out any split-archive indicators which are not immediately
      //  followed by another header
      if ((sig == ST_SplitArchiveIndicator ||
	   sig == ST_SplitArchiveSingleSegment) &&
	  next && next->offset() != locs->offset() + 4)
	 {
	 remove_location(locations,locs) ;
	 }
      // filter out any BZIP2 stream headers which are not immediately
      //   followed by a BZIP2 block header
      else if (sig == ST_BZIP2StreamHeader && next &&
	       next->signatureType() != ST_BZIP2BlockHeader)
	 {
	 remove_location(locations,locs) ;
	 }
      // filter out any BZIP2 block headers which are not followed by
      //    another header of some kind within 920k (max size actually 900k)
      else if (sig == ST_BZIP2BlockHeader && next &&
	       next->offset() > locs->offset() + (920 * 1024))
	 {
	 remove_location(locations,locs) ;
	 }
      // a ZIP data descriptor should follow a local file header and
      //  should be followed immediately by another header
      else if (sig == ST_DataDescriptor && next &&
	       next->offset() > locs->offset() + 32)
	 {
	 remove_location(locations,locs) ;
	 }
      // ignore candidate local header if there is another signature string
      //   located within the header itself
      else if (sig == ST_LocalFileHeader && next &&
	       (next->offset() - locs->offset()) < 30)
	 {
	 remove_location(locations,locs) ;
	 }
      // ignore candidate zlib spans less than 24 bytes in length
      else if (sig == ST_ZlibHeader && next &&
	       (next->offset() - locs->offset()) < 26)
	 {
	 remove_location(locations,locs) ;
	 }
      // the ALZip archive header should be followed by a file header
      else if (sig == ST_ALZipArchiveHeader && next &&
	       (next->signatureType() != ST_ALZipFileHeader ||
		next->offset() > locs->offset() + 16))
	 {
	 remove_location(locations,locs) ;
	 }
      // a PNG chunk header should be followed by a chunk end
      else if ((sig == ST_PNG_iTXt || sig == ST_PNG_zTXt) && next &&
	       (next->signatureType() != ST_PNGChunkEnd))
	 {
	 remove_location(locations,locs) ;
	 }
      // a PNG chunk end should follow a PNG chunk header
      else if (sig == ST_PNGChunkEnd &&
	       (!prev || (prev->signatureType() != ST_PNG_iTXt &&
			  prev->signatureType() != ST_PNG_zTXt)))
	 {
	 remove_location(locations,locs) ;
	 }
      // we can see a PDF endstream marker that doesn't match up with
      //   a FlateDecode header because there are other types of
      //   stream, so eliminate the second of successive endstream
      //   markers (a different header preceding the endstream means
      //   that we could have a partial FlateDecode stream)
      else if (sig == ST_PDF_FlateEnd && prev &&
	       prev->signatureType() == ST_PDF_FlateEnd)
	 {
	 remove_location(locations,locs) ;
	 }
      else if (sig == ST_DeflateSyncMark && prev && next)
	 {
	 // filter out DEFLATE sync/flush markers if they are in the
	 //   middle of a complete ZIP entry, complete FlateCode
	 //   stream, or we have two markers in relatively close
	 //   proximity (in which case they are probably part of the
	 //   same stream)
	 SignatureType prevsig = prev->signatureType() ;
	 if (prevsig == ST_DeflateSyncMark &&
	     locs->offset() - prev->offset() < 128*1024)
	    {
	    remove_location(locations,locs) ;
	    }
	 else if (prevsig == ST_LocalFileHeader &&
		  next->signatureType() == ST_LocalFileHeader &&
		  next->offset() - prev->offset() < 2*1024*1024)
	    {
	    remove_location(locations,locs) ;
	    }
	 else if (prevsig == ST_PDF_FlateHeader &&
		  next->signatureType() == ST_PDF_FlateEnd)
	    {
	    remove_location(locations,locs) ;
	    }
	 }
      prev = locs ;
      }
   (void)buffer_start; (void)buffer_end;
   return locations ; 
}

//----------------------------------------------------------------------

static char* load_file(CFile& zipfp, const char *filename, size_t &datalen,
		       MemMappedFile* &memory_mapped, const ZipRecParameters &params)
{
   CharPtr buffer ;
   memory_mapped = (zipfp.fp() == stdin) ? nullptr : new MemMappedROFile(filename) ;
   if (memory_mapped)
      {
      buffer = **memory_mapped ;
      datalen = memory_mapped->size() ;
      if (params.scan_range_end < datalen)
	 datalen = params.scan_range_end ;
      // initial scan is sequential, thereafter we access the file
      //   piece-wise reverse sequentially to find compression packets
      //   within a member and then sequentially while decompressing
      if (buffer && datalen)
	 memory_mapped->sequentialAccess() ;
      else
	 datalen = 0 ;
      }
   if (!buffer)
      {
      datalen = 0 ;
      errno = 0 ;
      if (!zipfp.seek(0,SEEK_END) || errno == EBADF)
	 {
	 //FIXME: params.scan_range_start = 0 ;
	 // not seekable, i.e. stdin via a pipe
	 size_t buflen = BUFFER_GRANULARITY ;
	 size_t bufpos = 0 ;
	 buffer.allocate(BUFFER_GRANULARITY) ;
	 while (!zipfp.eof())
	    {
	    if (bufpos >= buflen)
	       {
	       // don't expand past the maximum size specified by user --
	       //  if there is more data, we'll be called again later
	       if (bufpos >= buffer_max_size)
		  break ;
	       // grow the buffer if it's currently full
	       size_t newlen = buflen + BUFFER_GRANULARITY ;
	       if (!buffer.reallocate(buflen,newlen))
		  break ;
	       buflen = newlen ;
	       }
	    size_t count = zipfp.read(&buffer+bufpos,buflen-bufpos) ;
	    if (count == 0)
	       break ;
	    bufpos += count ;
	    }
	 datalen = bufpos ;
	 }
      else
	 {
	 off_t flen = zipfp.tell() ;
	 if ((off_t)params.scan_range_end < flen)
	    flen = (off_t)params.scan_range_end ;
	 zipfp.seek(0) ;
	 if (flen > 0)
	    {
	    buffer.allocate(flen) ;
	    if (buffer)
	       {
	       datalen = zipfp.read(&buffer,flen) ;
	       }
	    }
	 }
      }
   return buffer.move() ;
}

//----------------------------------------------------------------------

static void unload_file(char* filedata, MemMappedFile* memory_mapped)
{
   if (memory_mapped)
      delete memory_mapped ;
   else
      delete [] filedata ;
   return ;
}

//----------------------------------------------------------------------

static const LocationList *find_central_dir(const LocationList *locations)
{
   const LocationList *central_dir = locations ;
   while (central_dir && central_dir->signatureType() != ST_CentralDirEntry)
      central_dir = central_dir->next() ;
   return central_dir ;
}

//----------------------------------------------------------------------

static int32_t central_dir_offset(const LocationList* localheader, const LocationList*& central_dir,
				  const char* buffer_start)
{
   auto localname = extract_local_header_filename(localheader,buffer_start) ;
   if (!localname)
      return INT_MAX ;
   const LocationList *dir ;
   for (dir = central_dir ; dir ; dir = dir->next())
      {
      if (dir->signatureType() != ST_CentralDirEntry)
	 continue ;
      auto centralname = extract_central_dir_filename(dir,buffer_start) ;
      if (!centralname)
	 continue ;
      bool same_name = strcmp(localname,centralname) == 0 ;
      if (same_name)
	 break ;
      }
   if (dir)
      {
      int32_t offset = (extract_central_dir_local_offset(dir,buffer_start)
			- localheader->offset()) ;
      central_dir = dir ;
      return offset ;
      }
   else
      return INT_MAX ;
}

//----------------------------------------------------------------------

static void check_central_dir_offsets(const LocationList *locations,
				      const char *buffer_start)
{
   const LocationList *central_dir = find_central_dir(locations) ;
   int32_t offset = INT_MAX ;
   for (const LocationList *dir = central_dir ; dir ; dir = dir->next())
      {
      if (dir->signatureType() == ST_EndOfCentralDir)
	 {
	 offset = (central_dir->offset() -
		   extract_central_dir_end_cdir_offset(dir,buffer_start)) ;
	 break ;
	 }
      }
   for ( ; locations ; locations = locations->next())
      {
      if (locations->signatureType() == ST_LocalFileHeader)
	 {
	 int32_t new_offset = central_dir_offset(locations,central_dir,
						 buffer_start) ;
	 if (new_offset == INT_MAX)
	    {
	    // unable to find corresponding entries, so do nothing
	    }
	 else if (offset == INT_MAX)
	    offset = new_offset ;
	 else if (new_offset != offset)
	    {
	    fprintf(stderr,
		    "Unable to find a consistent skew between local and "
		    "central file entries.\n") ;
	    fprintf(stderr,"File may contain multiple corruptions.\n") ;
	    return;
	    }
	 }
      }
   if (offset > 0 && offset != INT_MAX)
      {
      fprintf(stderr,
	      "The archive appears to contain %ld extraneous bytes "
	      "at the beginning.\n",(long)offset) ;
      }
   else if (offset < 0)
      {
      fprintf(stderr,
	      "The archive appears to be missing %ld bytes at the "
	      "beginning.\n",(long)-offset) ;
      }
   return ;
}

//----------------------------------------------------------------------

static CharPtr check_central_dir(const LocationList* locations, const LocationList* local_entry,
			         const char* buffer_start, uint32_t& original_size_hint)
{
   original_size_hint = 0 ;
   // skip up to the central directory
   const LocationList *central_dir = find_central_dir(locations) ;
   if (!central_dir)
      return nullptr ;
   // scan the central directory entries in the location list, looking
   //   for the entry corresponding to the given local entry
   // if the local entry is 0, find the central directory entry immediately
   //   preceding the one corresponding to the first local file entry
   //   which is actually present in the location list
   CharPtr hint ;
   if (local_entry)
      {
//FIXME: not actually used yet
      }
   else
      {
      const LocationList *first_local = locations ;
      while (first_local && first_local->signatureType() != ST_LocalFileHeader)
	 first_local = first_local->next() ;
      auto localname = extract_local_header_filename(first_local,buffer_start) ;
      if (localname)
	 {
	 const LocationList *prev = central_dir ;
	 for (const LocationList *dir = central_dir->next() ; dir ; dir = dir->next())
	    {
	    auto dirname = extract_central_dir_filename(dir,buffer_start) ;
	    if (dirname && strcmp(dirname,localname) == 0)
	       {
	       hint = extract_central_dir_filename(prev,buffer_start) ;
	       original_size_hint = extract_central_dir_original_size(prev, buffer_start) ;
	       break ;
	       }
	    prev = dir ;
	    }
	 }
      else
	 {
	 // we didn't have any local file headers at all, so check whether
	 //   the central directory has only a single entry -- if so, that
	 //   entry has the filename we want
	 if (!central_dir->next() || central_dir->next()->signatureType() == ST_EndOfCentralDir)
	    {
	    hint = extract_central_dir_filename(central_dir,buffer_start) ;
	    original_size_hint = extract_central_dir_original_size(central_dir,buffer_start) ;
	    }
	 }
      }
   return hint ;
}

//----------------------------------------------------------------------

static uint32_t get_gzip_original_size(const LocationList *gzip_eof,
				       const char *buffer_start)
{
   // the trailer in the gzip_eof record consists of a 4-byte CRC followed
   //   by the low 32 bits of the original file size
   return get_dword(buffer_start + gzip_eof->offset() + 4) ;
}

//----------------------------------------------------------------------

static CharPtr get_gzip_filename_hint(const LocationList* prev, const char* buffer_start)
{
   CharPtr filename_hint ;
   if (prev && prev->signatureType() == ST_gzipHeader)
      {
      const char *header = buffer_start + prev->offset() ;
      if (header[3] & GZFLAG_FILENAME)
	 {
	 // the header contains a filename, so figure out its offset and
	 //   extract it
	 if (header[3] & GZFLAG_EXTRA)
	    header += get_word(header+GZIP_HEADER_FIXED) + 2 ;
	 header += GZIP_HEADER_FIXED ;
	 unsigned namelen = strlen(header) ;
	 // limit the filename to the maximum length supported by the OS
	 if (namelen > PATH_MAX)
	    namelen = PATH_MAX ;
	 filename_hint.allocate(namelen+1) ;
	 if (filename_hint)
	    {
	    strncpy(filename_hint.begin(),header,namelen+1) ;
	    filename_hint[namelen] = '\0' ;
	    }
	 }
      }
   return filename_hint ;
}

//----------------------------------------------------------------------

static CharPtr get_ZIP_filename_hint(const LocationList* prev, const char* buffer_start,
				     const LocationList* locations, uint32_t& original_size_hint)
{
   CharPtr filename_hint ;
   if (prev && prev->signatureType() == ST_LocalFileHeader)
      {
      filename_hint = extract_local_header_filename(prev, buffer_start) ;
      original_size_hint = extract_local_header_original_size(prev, buffer_start) ;
      }
   else
      {
      filename_hint = check_central_dir(locations,nullptr,buffer_start,original_size_hint) ;
      }
   return filename_hint ;
}

//----------------------------------------------------------------------

static uint32_t get_ALZip_original_size(const LocationList* prev, const char* buffer_start)
{
   uint32_t size = 0 ;
   if (prev && prev->signatureType() == ST_ALZipFileHeader)
      {
      const char *header = buffer_start + prev->offset() ;
      size_t bytes_per_field =  (header[11] >> 4) && 0x0F ;
      if (bytes_per_field)
	 {
	 header += 19 + bytes_per_field ;
	 for (size_t i = 0 ; i < bytes_per_field ; i++)
	    {
	    size <<= 8 ;
	    size += (uint8_t)header[i] ;
	    }
	 }
      }
   return size ;
}

//----------------------------------------------------------------------

static CharPtr get_ALZip_filename_hint(const LocationList* prev, const char* buffer_start)
{
   CharPtr filename_hint ;
   if (prev && prev->signatureType() == ST_ALZipFileHeader)
      {
      const char *header = buffer_start + prev->offset() ;
      unsigned namelen = get_word (header+4) ;
      size_t bytes_per_field = (header[11] >> 4) && 0x0F ;
      header += ALZIP_HEADER_FIXED ;
      if (bytes_per_field)
	 {
	 header += 6 ; // fixed-size optional fields are present
	 header += 2 * bytes_per_field ;
	 }
      filename_hint.allocate(namelen+1) ;
      if (filename_hint)
	 {
	 std::copy_n(header,namelen,filename_hint.begin()) ;
	 filename_hint[namelen] = '\0' ;
	 }
      }
   return filename_hint ;
}

//----------------------------------------------------------------------

#if 0
static CharPtr get_RAR_filename_hint(const LocationList* prev, const char* buffer_start)
{
   CharPtr filename_hint ;
   if (prev && prev->signatureType() == ST_RARFileHeader)
      {
      const char *header = buffer_start + prev->offset() ;
      unsigned namelen = get_word(header+26) ;
      filename_hint.allocate(namelen+1) ;
      if (filename_hint)
	 {
	 unsigned headerpos = (header[4] & 0x1) ? 40 : 32 ;
	 std::copy_n(header+headerpos,namelen,filename_hint.begin()) ;
	 filename_hint[namelen] = '\0' ;
	 }
      }
   return filename_hint ;
}
#endif /* 0 */

//----------------------------------------------------------------------

static bool recover_ZIP_span(const LocationList* locations, const LocationList* prev,
			     const LocationList* curr, const ZipRecParameters& params,
			     const FileInformation* fileinfo, bool deflate64, bool known_start = true)
{
   uint32_t original_size_hint ;
   bool known_end = false ;
   SignatureType sig = curr->signatureType() ;
   if (sig == ST_LocalFileHeader || sig == ST_DataDescriptor ||
       sig == ST_CentralDirEntry || sig == ST_ExtraData ||
       sig == ST_CentralDirSignature || sig == ST_EndOfCentralDir ||
       sig == ST_EndOfCentralDir64 || sig == ST_EndOfCentralDirLocator)
      known_end = true ;
   auto filename_hint = get_ZIP_filename_hint(prev,fileinfo->bufferStart(), locations,original_size_hint) ;
   return recover_stream(prev,curr,params,fileinfo,filename_hint, original_size_hint,known_start,deflate64,known_end) ;
}

//----------------------------------------------------------------------

static bool recover_gzip_span(const LocationList* prev, const LocationList* curr,
			      const ZipRecParameters& params, const FileInformation* fileinfo,
			      bool known_start)
{
   uint32_t original_size_hint = 0 ;
   bool known_end = false ;
   const char *buffer_start = fileinfo->bufferStart() ;
   if (curr && curr->signatureType() == ST_gzipEOF)
      {
      known_end = true ;
      original_size_hint = get_gzip_original_size(curr,buffer_start) ;
      }
   auto filename_hint = get_gzip_filename_hint(prev,buffer_start) ;
   return recover_stream(prev,curr,params,fileinfo, filename_hint, original_size_hint,known_start,false,known_end) ;
}

//----------------------------------------------------------------------

static bool recover_ALZip_span(const LocationList* prev, const LocationList* curr,
			       const ZipRecParameters& params, const FileInformation* fileinfo,
			       bool deflate64, bool known_start = true)
{
   const char *buffer_start = fileinfo->bufferStart() ;
   uint32_t original_size_hint = get_ALZip_original_size(prev,buffer_start) ; ;
   bool known_end = false ;
   if (curr && 
       (curr->signatureType() == ST_ALZipFileHeader || curr->signatureType() == ST_ALZipEOF))
      {
      known_end = true ;
      }
   auto filename_hint = get_ALZip_filename_hint(prev,buffer_start) ;
   return recover_stream(prev,curr,params,fileinfo,filename_hint, original_size_hint,known_start,deflate64,known_end) ;
}

//----------------------------------------------------------------------

static bool recover_RAR_file(const LocationList* locations, const ZipRecParameters& params,
			     const FileInformation* fileinfo)
{
   // for now, we just create a new file containing just the one member,
   //   still compressed
   off_t end_offset = rar_record_end(locations,fileinfo->bufferStart()) ;
   if (locations->next() && locations->next()->offset() < end_offset)
      end_offset = locations->next()->offset() ;
   const LocationList end_sig(ST_RARFileHeader,end_offset) ;
   return extract_stream(locations,&end_sig,params,fileinfo,"rar",true,
      			 "Rar!\x1A\x07\x00\xCF\x90\x73\0\0\x0D\0\0\0\0\0\0\0",20) ;
}

//----------------------------------------------------------------------

static bool recover_files(const LocationList* locations, const ZipRecParameters& params,
			  const FileInformation* fileinfo)
{
   const LocationList *prev = nullptr ;
   bool success = false ;
   bool deflate64 = false ;
   for (const LocationList *curr = locations ; curr ; curr = curr->next())
      {
      // check types of 'prev' and 'curr' and apply appropriate recovery
      // start by testing for span types for which we need to start at the
      //   'prev' marker, and if nothing is extracted, test for span types
      //   where we need to work backwards from the 'curr' marker
      bool recovered = false ;
      params.base_name = nullptr ;
      if (prev)
	 {
	 SignatureType sig = prev->signatureType() ;
	 if (sig == ST_LocalFileHeader)
	    {
	    // a ZIP file member
	    if (recover_ZIP_span(locations,prev,curr,params,fileinfo,deflate64))
	       recovered = true ;
	    }
	 else if (sig != ST_LocalFileHeader &&
		  curr->signatureType() == ST_LocalFileHeader)
	    {
	    // we're at the start of a ZIP archive, but we're missing
	    //   the initial local file header
	    if (recover_ZIP_span(locations,prev,curr,params,fileinfo,
				 deflate64,false))
	       recovered = true ;
	    }
	 else if (sig == ST_ZlibHeader)
	    {
	    // start of a zlib-compressed stream; these have no end signature
	    bool known_end = false ;
	    if (curr->signatureType() == ST_ZlibEOF ||
		curr->signatureType() == ST_ZlibHeader)
	       known_end = true ;
	    params.base_name = "zlibdata" ;
	    if (recover_stream(prev,curr,params,fileinfo,nullptr,0,true,false,known_end))
	       recovered = true ;
	    }
	 else if (sig == ST_gzipHeader)
	    {
	    // start of a gzip stream; these have no end signature, but the
	    //   'curr' marker will give the correct end if we processed a
	    //   single gzip file
	    params.base_name = "gzipdata" ;
	    if (recover_gzip_span(prev,curr,params,fileinfo,true))
	       recovered = true ;
	    }
	 else if (sig == ST_PDF_FlateHeader)
	    {
	    // try recovering a Deflate stream starting at the previous
	    //   position up to the current one; if the current position
	    //   is the matching end marker, we have a known end of the stream
	    params.base_name = "pdfdata" ;
	    if (recover_stream(prev,curr,params,fileinfo,nullptr,0,true,false,
			       curr->signatureType() == ST_PDF_FlateEnd))
	       success = true ;
	    }
	 else if (sig == ST_ALZipFileHeader)
	    {
	    if (recover_ALZip_span(prev,curr,params,fileinfo,false))
	       recovered = true ;
	    }
	 else if (sig == ST_WavPackRecordHeader)
	    {
	    // scan forward until we hit something that isn't a
	    //   WavPack record, then extract everything in that
	    //   combined span as-is (let an external program handle
	    //   final recovery)
	    for ( ; curr ; curr = curr->next())
	       {
	       if (curr->signatureType() != ST_WavPackRecordHeader)
		  break ;
	       }
	    if (extract_stream(prev,curr,params,fileinfo,"wpk"))
	       recovered = true ;
	    }
	 else if (sig == ST_BZIP2StreamHeader ||
		  sig == ST_BZIP2BlockHeader)
	    {
	    // scan forward until we hit something that isn't a BZIP2
	    //   record, then extract everything in that combined span
	    //   as-is (let bzip2recover or a similar program handle
	    //   final recovery)
	    for ( ; curr ; curr = curr->next())
	       {
	       SignatureType st = curr->signatureType() ;
	       if (st != ST_BZIP2BlockHeader && st != ST_BZIP2EndOfStream)
		  break ;
	       }
	    if (extract_stream(prev,curr,params,fileinfo,"bz2"))
	       recovered = true ;
	    }
	 else if (sig == ST_RARFileHeader)
	    {
	    params.base_name = "rardata" ;
	    if (recover_RAR_file(prev,params,fileinfo))
	       success = true ;
	    }
	 else if (sig == ST_DeflateSyncMark)
	    {
	    params.base_name = "rawdeflate" ;
	    if (recover_stream(prev,curr,params,fileinfo,nullptr,0,true,false,false))
	       recovered = true ;
	    }
	 else if (curr->signatureType() == ST_PNGChunkEnd &&
		  (sig == ST_PNG_iTXt || sig == ST_PNG_zTXt))
	    {
	    params.base_name = "pngtext" ;
	    if (recover_stream(prev,curr,params,fileinfo,nullptr,0,true,false,true))
	       recovered = true ;
	    }
	 }
      if (recovered)
	 {
	 success = true ;
	 if (!curr)
	    break ;
	 prev = curr ;
	 continue ;
	 }
      if (!prev && curr->signatureType() == ST_LocalFileHeader)
	 {
	 // no previous header (start of archive missing), but a local file
	 //   header normally immediately follows the compressed data of the
	 //   previous file
	 if (recover_ZIP_span(locations,prev,curr,params,fileinfo,
			      deflate64,false))
	    success = true ;
	 }
      else if ((!prev || prev->signatureType() != ST_PDF_FlateHeader) &&
	       curr->signatureType() == ST_PDF_FlateEnd)
	 {
	 // no previous header (start of file missing), but we have what looks
	 //   like the end marker, so try recovering a Deflate stream ending
	 //   at that point
	 params.base_name = "pdfdata" ;
	 if (recover_stream(prev,curr,params,fileinfo,nullptr,0,false,false,true))
	    success = true ;
	 }
      else if (curr->signatureType() == ST_DataDescriptor)
	 {
	 // data descriptors immediately follow the compressed data for a file,
	 //   so try to recover from the previous signature (if any) up to the
	 //   current position
	 if (recover_ZIP_span(locations,prev,curr,params,fileinfo,
			      deflate64,false))
	    success = true ;
	 }
      else if (curr->signatureType() == ST_CentralDirEntry)
	 {
	 // if no optional records are present, then the first central
	 //   directory entry immediately follows the compressed data
	 //   for the last file in the archive
	 if (recover_ZIP_span(locations,prev,curr,params,fileinfo,
			      deflate64,prev != nullptr))
	    success = true ;
         // since no more files will follow once we've reached the
         //   central directory, we can stop now
	 break ;
	 }
      else if (curr->signatureType() == ST_zipEOF)
	 {
	 // We hit the end of the file without encountering a central
	 //   directory.  Try to recover a file under the assumption
	 //   that the bitstream itself has not been truncated; if that
	 //   fails, try just decompressing up to the point of
	 //   truncation under the assumption that the beginning is intact
	 if (prev && prev->signatureType() == ST_LocalFileHeader)
	    {
	    if (recover_ZIP_span(locations,prev,curr,params,fileinfo,
				 deflate64,true))
	       success = true ;
	    }
	 break ;
	 }
      else if (curr->signatureType() == ST_gzipEOF)
	 {
	 params.base_name = "gzipdata" ;
	 if (recover_gzip_span(prev,curr,params,fileinfo,false))
	    success = true ;
	 }
      else if (curr->signatureType() == ST_ZlibEOF)
	 {
	 params.base_name = "zlibdata" ;
	 if (recover_stream(prev,curr,params,fileinfo,nullptr,0,prev != nullptr,false))
	    success = true ;
	 }
      else if (curr->signatureType() == ST_ALZipFileHeader ||
	       curr->signatureType() == ST_ALZipEOF)
	 {
	 if (recover_ALZip_span(prev,curr,params,fileinfo,deflate64,false))
	    success = true ;
	 }
      else if (curr->signatureType() == ST_RARFileHeader)
	 {
	 params.base_name = "rardata" ;
	 if (recover_RAR_file(curr,params,fileinfo))
	    success = true ;
	 }
      prev = curr ;
      }
   return success ;
}

//----------------------------------------------------------------------

static LocationList* split_on_central_dir(LocationList* signatures, const char* buffer_start)
{
   while (signatures)
      {
      if (signatures->signatureType() == ST_EndOfCentralDir ||
	  signatures->signatureType() == ST_EndOfCentralDir64)
	 {
	 unsigned sigsize = 22 ;
	 const char *header = buffer_start + signatures->offset() ;
	 if (signatures->signatureType() == ST_EndOfCentralDir)
	    sigsize += get_word(header + 20) ;
	 LocationList *split = signatures->next() ;
	 signatures->setNext(nullptr) ;
	 return LocationList::push(ST_zipStartOfFile, signatures->offset() + sigsize, split) ;
	 }
      signatures = signatures->next() ;
      }
   // no end-of-central directory record found, so no split
   return nullptr ;
}

//----------------------------------------------------------------------

static CharPtr insert_filename(const char* dirname, unsigned seqnum, const char* filename)
{
   if (!dirname)
      return nullptr ;
   if (!*dirname)
      dirname = "." ;
   unsigned dirlen = strlen(dirname) ;
   bool using_stdin = is_stdin(filename) ;
   if (filename)
      {
      const char *slash = strrchr(filename,'/') ;
      const char *backslash = strrchr(filename,'\\') ;
      if (slash && !backslash)
	 filename = slash + 1 ;
      else if (!slash && backslash)
	 filename = backslash + 1 ;
      else if (slash && backslash)
	 filename = (slash > backslash) ? slash + 1 : backslash + 1 ;
      }
   unsigned filelen = filename ? strlen(filename) : 0 ;
   const char *marker = strchr(dirname,'%') ;
   CharPtr result ;
   if (marker && filelen > 0)
      {
      // strip off extension, if present
      const char *dot = strrchr(filename,'.') ;
      if (dot)
	 filelen = dot - filename ;
      result.allocate(dirlen + filelen + 12) ;
      if (result)
	 {
	 unsigned len1 = marker - dirname ;
	 unsigned len2 = dirlen - len1 ; // include terminating NUL
	 std::copy_n(dirname,len1,result.begin()) ;
	 std::copy_n(filename,filelen,result.at(len1)) ;
	 if (seqnum || using_stdin)
	    {
	    if (using_stdin)
	       {
	       sprintf(result.at(len1 + filelen),"%04u%c",seqnum,'\0') ;
	       }
	    else
	       {
	       result[len1 + filelen] = '-' ;
	       filelen++ ;
	       sprintf(result.at(len1 + filelen),"%u%c",seqnum,'\0') ;
	       }
	    filelen += strlen(result.at(len1 + filelen)) ;
	    }
	 std::copy_n(dirname + len1 + 1, len2, result.at(len1+filelen)) ;
	 }
      }
   else
      {
      result.allocate(dirlen + 1) ;
      if (result)
	 std::copy_n(dirname,dirlen+1,result.begin()) ;
      }
   return result ;
}

//----------------------------------------------------------------------

bool process_file_data(const ZipRecParameters& params, FileInformation* fileinfo, unsigned& seqnum)
{
   CpuTimer timer ;
   bool success = false ;
   if (verbosity >= VERBOSITY_SCAN)
      {
      fprintf(stderr,"scanning '%s' for signatures\n", fileinfo->inputFile()) ;
      fflush(stderr) ;
      }
   init_rar_CRC() ;
   const char *buffer_start = fileinfo->bufferStart() ;
   const char *buffer_end = fileinfo->bufferEnd() ;
   LocationList *signatures ;
   FileFormat file_format = fileinfo->format() ;
   if (file_format == FF_gzip)
      {
      signatures = scan_for_gzip_signatures(buffer_start, buffer_end, params) ;
      }
   else if (file_format == FF_Zlib || file_format == FF_ZlibMulti ||
	    file_format == FF_ZlibAll)
      {
      signatures = scan_for_zlib_signatures(params, fileinfo) ;
      }
   else if (file_format == FF_RawDeflate)
      signatures = nullptr ;
   else
      {
      signatures = scan_for_ZIP_signatures(buffer_start, buffer_end, params) ;
      if (verbosity > 0)
	 check_central_dir_offsets(signatures, buffer_start) ;
      }
   ADD_TIME(timer,time_scanning) ;
   if (signatures)
      {
      signatures = sort_signatures(signatures) ;
      signatures = filter_signatures(signatures,buffer_start,buffer_end) ;
      if (verbosity >= VERBOSITY_SCAN)
	 dump_signature_list(buffer_start,signatures) ;
      bool multiples = false ;
      const char *input_file = fileinfo->inputFile() ;
      while (signatures)
	 {
	 LocationList *central = split_on_central_dir(signatures,buffer_start) ;
	 if (central)
	    multiples = true ;
	 if (multiples)
	    seqnum++ ;
	 auto output_dir = insert_filename(fileinfo->outputDirectory(),seqnum, input_file) ;
	 if ((params.write_format == WFMT_Listing && !params.perform_reconstruction) ||
	    Fr::create_path(output_dir))
	    {
	    fileinfo->replaceOutputDirectory(output_dir) ;
	    if (recover_files(signatures, params, fileinfo))
	       success = true ;
	    fileinfo->restoreOutputDirectory() ;
	    // if we only used the output directory for temporary files,
	    //   remove it
	    if (params.write_format == WFMT_Listing && params.perform_reconstruction)
	       rmdir(output_dir) ;
	    }
	 else
	    {
	    fprintf(stderr,"Unable to create output directory '%s'\n", fileinfo->outputDirectory()) ;
	    success = false ;
	    }
	 delete signatures ;
	 signatures = central ;
	 }
      }
   else if (file_format == FF_RawDeflate)
      {
      params.base_name = "rawdeflate" ;
      auto curr = LocationList::push(ST_ZlibEOF,params.scan_range_end,nullptr) ;
      auto prev = LocationList::push(ST_RawDeflateStart,params.scan_range_start,curr) ;
      if (recover_stream(prev,curr,params,fileinfo,nullptr,0,true,false,true))
	 success = true ;
      params.base_name = nullptr ;
      }
   return success ;
}

//----------------------------------------------------------------------

static bool recover_file(CFile& zipfp, const ZipRecParameters &params, FileInformation *fileinfo, unsigned &seqnum)
{
   bool success = false ;
   size_t datalen ;
   MemMappedFile* memory_mapped ;
   char* filedata = load_file(zipfp,fileinfo->inputFile(),datalen, memory_mapped,params) ;
   if (filedata)
      {
      fileinfo->setBuffer(filedata,filedata+datalen) ;
      fileinfo->usingStdin(zipfp.fp() == stdin) ;
      success = process_file_data(params, fileinfo, seqnum) ;
      }
   unload_file(filedata,memory_mapped) ;
   return success ;
}

//----------------------------------------------------------------------

bool recover_file(const ZipRecParameters &params, FileInformation *fileinfo)
{
   bool success = false ;
   unsigned seqnum = 0 ;
   const char *filename = fileinfo->inputFile() ;
   if (filename && *filename)
      {
      if (is_stdin(filename))
	 {
	 if (blocking_size == 0)
	    buffer_max_size = DEFAULT_BUFFER_MAX_SIZE ;
	 else
	    buffer_max_size = blocking_size * 1024 * 1024 ;
	 seqnum = 1 ;
	 while (!feof(stdin))
	    {
	    CFile zipfp(stdin) ;
	    if (recover_file(zipfp, params, fileinfo, seqnum))
	       success = true ;
	    }
	 }
      else
	 {
	 CInputFile zipfp(filename,CFile::binary) ;
	 if (zipfp)
	    {
	    success = recover_file(zipfp, params, fileinfo, seqnum) ;
	    }
	 }
      }
   return success ;
}

// end of file recover.C //
