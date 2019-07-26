/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: loclist.h - stream location references			*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-26						*/
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

#ifndef __LOCLIST_H_INCLUDED
#define __LOCLIST_H_INCLUDED

#include <sys/types.h>
#include "framepac/memory.h"

/************************************************************************/
/*	Type definitions						*/
/************************************************************************/

enum SignatureType
   {
      ST_Invalid = 0,
      ST_CentralDirEntry,
      ST_LocalFileHeader,
      ST_CentralDirSignature,
      ST_EndOfCentralDir,
      ST_EndOfCentralDir64,
      ST_EndOfCentralDirLocator,
      ST_ExtraData,
      ST_DataDescriptor,
      ST_SplitArchiveIndicator,
      ST_SplitArchiveSingleSegment,
      ST_WavPackRecordHeader,
      ST_BZIP2StreamHeader,
      ST_BZIP2BlockHeader,
      ST_BZIP2EndOfStream,
      ST_gzipHeader,
      ST_gzipEOF,
      ST_zipStartOfFile,
      ST_zipEOF,
      ST_ALZipArchiveHeader,
      ST_ALZipFileHeader,
      ST_ALZipEOF,
      ST_ZlibHeader,
      ST_ZlibEOF,
      ST_PDF_FlateHeader,
      ST_PDF_FlateEnd,
      ST_PNG_zTXt,
      ST_PNG_iTXt,
      ST_PNGChunkEnd,
      ST_RawDeflateStart,
      ST_DeflateSyncMark,
      ST_RARMarker,
      ST_RARFileHeader,
      ST_7zipSignature,
      ST_XzStreamSignature,
      ST_LzipSignature,
      ST_CabinetSignature,
      ST_MSZIPSignature,
      ST_SZDDSignature,
      ST_SZDDAltSignature,
      ST_KWAJSignature,
      ST_LZXHeader
   } ;

//----------------------------------------------------------------------

class LocationList
   {
   public:
      void *operator new(size_t) { return allocator->allocate() ; }
      void operator delete(void *blk) { allocator->release(blk) ; }
      LocationList(SignatureType st, off_t offset, LocationList *nxt = nullptr)
	 { m_offset = offset ; m_sigtype = st ; m_next = nxt ; }
      ~LocationList() { if (m_next) delete m_next ; m_next = 0 ; }

      // accessors
      const LocationList *next() const { return m_next ; }
      LocationList *next() { return m_next ; }
      SignatureType signatureType() const { return m_sigtype ; }
      off_t offset() const { return m_offset ; }
      off_t headerEndOffset(const char *buffer, bool zip64 = false) const ;

      // manipulators
      static LocationList* push(SignatureType st, off_t offset, LocationList* nxt)
	 { return new LocationList(st,offset,nxt) ; }
      void setNext(LocationList *nxt) { m_next = nxt ; }
      LocationList *reverse() ;

   private:
      static Fr::SmallAlloc* allocator ;
      LocationList *m_next ;
      off_t m_offset ;
      SignatureType m_sigtype ;
   } ;

#endif /* !__LOCLIST_H_INCLUDED */

// end of file loclist.h //
