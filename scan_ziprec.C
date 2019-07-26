/************************************************************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-26						*/
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

#ifdef BULK_EXTRACTOR

#include <cstdlib>
#include <cstdio>
#include "recover.h"
#include "global.h"
#include "framepac/init.h"
#include "framepac/smartptr.h"
#include "framepac/texttransforms.h"

using namespace Fr ;

// bulk_extractor 1.4.0 headers don't compile cleanly with all warnings enabled
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wextra"
#include "bulk_extractor.h"
#pragma GCC diagnostic pop

/************************************************************************/
/************************************************************************/

/************************************************************************/
/*	Global variables for this module				*/
/************************************************************************/

static __thread bool thread_initialized = false ;

//static unsigned sequence_number = 0 ;
static CharPtr output_dir ; 

static FrThreadKey scanner_key ;

// configuration retrieved from bulk_extractor
static int debug = 0 ;
static string cfg_outdir ;
static bool cfg_scanonly = false ;

/************************************************************************/
/*	Global data for this module					*/
/************************************************************************/

static const char help_outdir[] =
   "The directory in which to store results" ;
static const char help_scanonly[] =
   "Only scan for recoverable compressed streams, don't extract them." ;
static const char help_nohist[] =
   "Don't generate histograms for recovered/extracted files" ;

/************************************************************************/
/************************************************************************/

//----------------------------------------------------------------------

static void startup(const class scanner_params &sp)
{
   // ensure the correct format scanner information
   assert(sp.info->si_version == scanner_info::CURRENT_SI_VERSION) ;

   // grab our global configuration variables
   bool cfg_nohist = false ;
   debug = sp.info->config->debug ;
   sp.info->get_config("ziprec_outdir",&cfg_outdir,help_outdir) ;
   sp.info->get_config("ziprec_scanonly",&cfg_scanonly,help_scanonly) ;
   sp.info->get_config("ziprec_nohist",&cfg_nohist,help_nohist) ;

   // identify the scanner
   sp.info->name 	    = "ZipRrec" ;
   sp.info->author 	    = "Ralf Brown" ;
   sp.info->description     = "ZIP/DEFLATE Compression Recovery" ;
   sp.info->scanner_version = ZIPREC_VERSION ;
   //sp.info->flags           = scanner_info::SCANNER_FIND_SCANNER ;
   sp.info->feature_names.insert("ziprec") ;

   return ;
}

//----------------------------------------------------------------------

static void thread_finish(void*)
{
   if ((debug & DEBUG_PRINT_STEPS) != 0)
      {
      cerr << "ZipRec thread_finish()" << endl ;
      }
   //!!!insert cleanup code here

   return ;
}

//----------------------------------------------------------------------

static void initialize(const class scanner_params &sp)
{
   Initialize() ;
   bool run_verbosely = ((debug & DEBUG_INFO) != 0) ;
   if (run_verbosely)
      {
      cerr << "FP initialized" << endl ;
      }
   // ensure the correct format scanner information
   assert(sp.info->si_version == scanner_info::CURRENT_SI_VERSION) ;
   bool disabled = (sp.info->flags & scanner_info::SCANNER_DISABLED) != 0 ;

   if (!disabled)
      {
//FIXME

      const char *out_directory = cfg_outdir.c_str() ;
      if (out_directory && *out_directory)
	 output_dir = dup_string(out_directory) ;
      if (!output_dir)
	 output_dir = dup_string("extract%") ;
      }
   // set up a per-thread destructor to clean up thread-local memory allocations
   FrThread::createKey(scanner_key,thread_finish) ;
   return ;
}

//----------------------------------------------------------------------

static void thread_start()
{
   if (thread_initialized) return ;
   //!!!insert initialization here

   // enable the destructor for this thread
   FrThread::setKey(scanner_key,(void*)1) ;
   if ((debug & DEBUG_PRINT_STEPS) != 0)
      {
      cerr << "ZipRec thread initialized" << endl ;
      }
   return ;
}

//----------------------------------------------------------------------

static void process_buffer(const sbuf_t scanbuf, feature_recorder *fr)
{
   const uint8_t *buffer_start = scanbuf.buf ;
   const uint8_t *buffer_end = scanbuf.buf + scanbuf.size() ;
   bool run_verbosely = ((debug & DEBUG_INFO) != 0) ;
   if (run_verbosely)
      {
      cerr << "ziprec(" << hex << (uint64_t)buffer_start << ":"
	   << (buffer_end-buffer_start) << ", " << scanbuf.pagesize
	   << ") start" << dec << endl ;
      }
   (void)fr;//FIXME
#if 0
   if (!process_file_data(buffer_start,buffer_end,"-",output_dir,false,
			  sequence_number,true))
      {

      }
#endif
   if (run_verbosely)
      {
      cerr << "  ziprec(" << hex << (uint64_t)buffer_start << ":"
	   << (buffer_end-buffer_start) << ", " << scanbuf.pagesize
	   << ") done" << dec << endl ;
      }
   return ;
}

//----------------------------------------------------------------------

static void cleanup()
{
   output_dir = nullptr ;
   return ;
}

//----------------------------------------------------------------------

extern "C" void scan_ziprec(const class scanner_params &sp,
			    const class recursion_control_block &rcb)
{
   (void)rcb ; // not used; keep compiler happy
   // ensure that we get the correct format parameter block
   assert(sp.sp_version == scanner_params::CURRENT_SP_VERSION) ;
//   debug = sp.info->config->debug ;  // causes a segfault after returning to BE!!?!?!
   if ((debug & DEBUG_PRINT_STEPS) != 0)
      {
      cerr << "Invoked scan_ziprec(), phase = " << sp.phase << endl ;
      }
   switch (sp.phase)
      {
      case scanner_params::PHASE_NONE:
	 // do nothing
	 break ;
      case scanner_params::PHASE_STARTUP:
	 startup(sp) ;
	 break ;
      case scanner_params::PHASE_INIT:
	 initialize(sp) ;
	 break ;
      case scanner_params::PHASE_THREAD_BEFORE_SCAN:
	 thread_start() ;
	 break ;
      case scanner_params::PHASE_SCAN:
	 process_buffer(sp.sbuf,sp.fs.get_name("ziprec")) ;
	 break ;
#if 0
      case scanner_params::PHASE_THREAD_AFTER_SCAN:
	 thread_finish() ;
	 break ;
#endif
      case scanner_params::PHASE_SHUTDOWN:
	 cleanup() ;
	 break ;
      default:
	 fprintf(stderr,"Invalid 'phase' parameter to scan_ziprec\n") ;
	 break ;
      }
   if ((debug & DEBUG_PRINT_STEPS) != 0 &&
       sp.phase != scanner_params::PHASE_SCAN)
      {
      cerr << "  ==> scan_ziprec(), phase = " << sp.phase << endl ;
      }
   return ;
}

#endif /* BULK_EXTRACTOR */

// end of file scan_ziprec.C //
