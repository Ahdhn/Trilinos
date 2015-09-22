/*
//@HEADER
// ************************************************************************
// 
//                        Kokkos v. 2.0
//              Copyright (2014) Sandia Corporation
// 
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact  H. Carter Edwards (hcedwar@sandia.gov)
// 
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_THREADSEXEC_HPP
#define KOKKOS_THREADSEXEC_HPP

#include <stdio.h>

#include <utility>
#include <impl/Kokkos_spinwait.hpp>
#include <impl/Kokkos_FunctorAdapter.hpp>
#include <impl/Kokkos_AllocationTracker.hpp>

#include <Kokkos_Atomic.hpp>

//----------------------------------------------------------------------------

namespace Kokkos {
namespace Impl {

class ThreadsExec {
public:

  // Fan array has log_2(NT) reduction threads plus 2 scan threads
  // Currently limited to 16k threads.
  enum { MAX_FAN_COUNT    = 16 };
  enum { MAX_THREAD_COUNT = 1 << ( MAX_FAN_COUNT - 2 ) };
  enum { VECTOR_LENGTH    = 8 };

  /** \brief States of a worker thread */
  enum { Terminating ///<  Termination in progress
       , Inactive    ///<  Exists, waiting for work
       , Active      ///<  Exists, performing work
       , Rendezvous  ///<  Exists, waiting in a barrier or reduce

       , ScanCompleted
       , ScanAvailable
       , ReductionAvailable
       };

private:

  friend class Kokkos::Threads ;

  // Fan-in operations' root is the highest ranking thread
  // to place the 'scan' reduction intermediate values on
  // the threads that need them.
  // For a simple reduction the thread location is arbitrary.

  ThreadsExec * const * m_pool_base ; ///< Base for pool fan-in

  Impl::AllocationTracker m_scratch ;
  int           m_scratch_reduce_end ;
  int           m_scratch_thread_end ;
  int           m_numa_rank ;
  int           m_numa_core_rank ;
  int           m_pool_rank ;
  int           m_pool_size ;
  int           m_pool_fan_size ;
  int volatile  m_pool_state ;  ///< State for global synchronizations


  static void global_lock();
  static void global_unlock();
  static bool spawn();

  static void execute_resize_scratch( ThreadsExec & , const void * );
  static void execute_sleep(          ThreadsExec & , const void * );

  ThreadsExec( const ThreadsExec & );
  ThreadsExec & operator = ( const ThreadsExec & );

  static void execute_serial( void (*)( ThreadsExec & , const void * ) );

public:

  KOKKOS_INLINE_FUNCTION int pool_size() const { return m_pool_size ; }
  KOKKOS_INLINE_FUNCTION int pool_rank() const { return m_pool_rank ; }
  KOKKOS_INLINE_FUNCTION int numa_rank() const { return m_numa_rank ; }
  KOKKOS_INLINE_FUNCTION int numa_core_rank() const { return m_numa_core_rank ; }

  static int get_thread_count();
  static ThreadsExec * get_thread( const int init_thread_rank );

  inline void * reduce_memory() const { return reinterpret_cast<unsigned char *>(m_scratch.alloc_ptr()); }
  KOKKOS_INLINE_FUNCTION  void * scratch_memory() const { return reinterpret_cast<unsigned char *>(m_scratch.alloc_ptr()) + m_scratch_reduce_end ; }

  KOKKOS_INLINE_FUNCTION  int volatile & state() { return m_pool_state ; }
  KOKKOS_INLINE_FUNCTION  ThreadsExec * const * pool_base() const { return m_pool_base ; }

  static void driver(void);

  ~ThreadsExec();
  ThreadsExec();

  static void * resize_scratch( size_t reduce_size , size_t thread_size );

  static void * root_reduce_scratch();

  static bool is_process();

  static void verify_is_process( const std::string & , const bool initialized );

  static int is_initialized();

  static void initialize( unsigned thread_count ,
                          unsigned use_numa_count ,
                          unsigned use_cores_per_numa ,
                          bool allow_asynchronous_threadpool );

  static void finalize();

  /* Given a requested team size, return valid team size */
  static unsigned team_size_valid( unsigned );

  static void print_configuration( std::ostream & , const bool detail = false );

  //------------------------------------

  static void wait_yield( volatile int & , const int );

  //------------------------------------
  // All-thread functions:

  inline
  int all_reduce( const int value )
    {
      // Make sure there is enough scratch space:
      const int rev_rank = m_pool_size - ( m_pool_rank + 1 );

      *((volatile int*) reduce_memory()) = value ;

      memory_fence();

      // Fan-in reduction with highest ranking thread as the root
      for ( int i = 0 ; i < m_pool_fan_size ; ++i ) {
        // Wait: Active -> Rendezvous
        Impl::spinwait( m_pool_base[ rev_rank + (1<<i) ]->m_pool_state , ThreadsExec::Active );
      }

      if ( rev_rank ) {
        m_pool_state = ThreadsExec::Rendezvous ;
        // Wait: Rendezvous -> Active
        Impl::spinwait( m_pool_state , ThreadsExec::Rendezvous );
      }
      else {
        // Root thread does the reduction and broadcast

        int accum = 0 ;

        for ( int rank = 0 ; rank < m_pool_size ; ++rank ) {
          accum += *((volatile int *) get_thread( rank )->reduce_memory());
        }

        for ( int rank = 0 ; rank < m_pool_size ; ++rank ) {
          *((volatile int *) get_thread( rank )->reduce_memory()) = accum ;
        }

        memory_fence();

        for ( int rank = 0 ; rank < m_pool_size ; ++rank ) {
          get_thread( rank )->m_pool_state = ThreadsExec::Active ;
        }
      }

      return *((volatile int*) reduce_memory());
    }

  //------------------------------------
  // All-thread functions:

  template< class FunctorType , class ArgTag >
  inline
  void fan_in_reduce( const FunctorType & f ) const
    {
      typedef Kokkos::Impl::FunctorValueJoin< FunctorType , ArgTag > Join ;
      typedef Kokkos::Impl::FunctorFinal<     FunctorType , ArgTag > Final ;

      const int rev_rank  = m_pool_size - ( m_pool_rank + 1 );

      for ( int i = 0 ; i < m_pool_fan_size ; ++i ) {

        ThreadsExec & fan = *m_pool_base[ rev_rank + ( 1 << i ) ] ;

        Impl::spinwait( fan.m_pool_state , ThreadsExec::Active );

        Join::join( f , reduce_memory() , fan.reduce_memory() );
      }

      if ( ! rev_rank ) {
        Final::final( f , reduce_memory() );
      }
    }

  inline
  void fan_in() const
    {
      const int rev_rank = m_pool_size - ( m_pool_rank + 1 );

      for ( int i = 0 ; i < m_pool_fan_size ; ++i ) {
        Impl::spinwait( m_pool_base[rev_rank+(1<<i)]->m_pool_state , ThreadsExec::Active );
      }
    }

  template< class FunctorType , class ArgTag >
  inline
  void scan_large( const FunctorType & f )
    {
      // Sequence of states:
      //  0) Active             : entry and exit state
      //  1) ReductionAvailable : reduction value available
      //  2) ScanAvailable      : inclusive scan value available
      //  3) Rendezvous         : All threads inclusive scan value are available
      //  4) ScanCompleted      : exclusive scan value copied

      typedef Kokkos::Impl::FunctorValueTraits< FunctorType , ArgTag > Traits ;
      typedef Kokkos::Impl::FunctorValueJoin<   FunctorType , ArgTag > Join ;
      typedef Kokkos::Impl::FunctorValueInit<   FunctorType , ArgTag > Init ;

      typedef typename Traits::value_type scalar_type ;

      const int      rev_rank = m_pool_size - ( m_pool_rank + 1 );
      const unsigned count    = Traits::value_count( f );

      scalar_type * const work_value = (scalar_type *) reduce_memory();

      //--------------------------------
      // Fan-in reduction with highest ranking thread as the root
      for ( int i = 0 ; i < m_pool_fan_size ; ++i ) {
        ThreadsExec & fan = *m_pool_base[ rev_rank + (1<<i) ];

        // Wait: Active -> ReductionAvailable (or ScanAvailable)
        Impl::spinwait( fan.m_pool_state , ThreadsExec::Active );
        Join::join( f , work_value , fan.reduce_memory() );
      }

      // Copy reduction value to scan value before releasing from this phase.
      for ( unsigned i = 0 ; i < count ; ++i ) { work_value[i+count] = work_value[i] ; }

      if ( rev_rank ) {

        // Set: Active -> ReductionAvailable
        m_pool_state = ThreadsExec::ReductionAvailable ;

        // Wait for contributing threads' scan value to be available.
        if ( ( 1 << m_pool_fan_size ) < ( m_pool_rank + 1 ) ) {
          ThreadsExec & th = *m_pool_base[ rev_rank + ( 1 << m_pool_fan_size ) ] ;

          // Wait: Active             -> ReductionAvailable
          // Wait: ReductionAvailable -> ScanAvailable
          Impl::spinwait( th.m_pool_state , ThreadsExec::Active );
          Impl::spinwait( th.m_pool_state , ThreadsExec::ReductionAvailable );

          Join::join( f , work_value + count , ((scalar_type *)th.reduce_memory()) + count );
        }

        // This thread has completed inclusive scan
        // Set: ReductionAvailable -> ScanAvailable
        m_pool_state = ThreadsExec::ScanAvailable ;

        // Wait for all threads to complete inclusive scan
        // Wait: ScanAvailable -> Rendezvous
        Impl::spinwait( m_pool_state , ThreadsExec::ScanAvailable );
      }

      //--------------------------------

      for ( int i = 0 ; i < m_pool_fan_size ; ++i ) {
        ThreadsExec & fan = *m_pool_base[ rev_rank + (1<<i) ];
        // Wait: ReductionAvailable -> ScanAvailable
        Impl::spinwait( fan.m_pool_state , ThreadsExec::ReductionAvailable );
        // Set: ScanAvailable -> Rendezvous
        fan.m_pool_state = ThreadsExec::Rendezvous ;
      }

      // All threads have completed the inclusive scan.
      // All non-root threads are in the Rendezvous state.
      // Threads are free to overwrite their reduction value.
      //--------------------------------

      if ( ( rev_rank + 1 ) < m_pool_size ) {
        // Exclusive scan: copy the previous thread's inclusive scan value

        ThreadsExec & th = *m_pool_base[ rev_rank + 1 ] ; // Not the root thread

        const scalar_type * const src_value = ((scalar_type *)th.reduce_memory()) + count ;

        for ( unsigned j = 0 ; j < count ; ++j ) { work_value[j] = src_value[j]; }
      }
      else {
        (void) Init::init( f , work_value );
      }

      //--------------------------------
      // Wait for all threads to copy previous thread's inclusive scan value
      // Wait for all threads: Rendezvous -> ScanCompleted
      for ( int i = 0 ; i < m_pool_fan_size ; ++i ) {
        Impl::spinwait( m_pool_base[ rev_rank + (1<<i) ]->m_pool_state , ThreadsExec::Rendezvous );
      }
      if ( rev_rank ) {
        // Set: ScanAvailable -> ScanCompleted
        m_pool_state = ThreadsExec::ScanCompleted ;
        // Wait: ScanCompleted -> Active
        Impl::spinwait( m_pool_state , ThreadsExec::ScanCompleted );
      }
      // Set: ScanCompleted -> Active
      for ( int i = 0 ; i < m_pool_fan_size ; ++i ) {
        m_pool_base[ rev_rank + (1<<i) ]->m_pool_state = ThreadsExec::Active ;
      }
    }

  template< class FunctorType , class ArgTag >
  inline
  void scan_small( const FunctorType & f )
    {
      typedef Kokkos::Impl::FunctorValueTraits< FunctorType , ArgTag > Traits ;
      typedef Kokkos::Impl::FunctorValueJoin<   FunctorType , ArgTag > Join ;
      typedef Kokkos::Impl::FunctorValueInit<   FunctorType , ArgTag > Init ;

      typedef typename Traits::value_type scalar_type ;

      const int      rev_rank = m_pool_size - ( m_pool_rank + 1 );
      const unsigned count    = Traits::value_count( f );

      scalar_type * const work_value = (scalar_type *) reduce_memory();

      //--------------------------------
      // Fan-in reduction with highest ranking thread as the root
      for ( int i = 0 ; i < m_pool_fan_size ; ++i ) {
        // Wait: Active -> Rendezvous
        Impl::spinwait( m_pool_base[ rev_rank + (1<<i) ]->m_pool_state , ThreadsExec::Active );
      }

      for ( unsigned i = 0 ; i < count ; ++i ) { work_value[i+count] = work_value[i]; }

      if ( rev_rank ) {
        m_pool_state = ThreadsExec::Rendezvous ;
        // Wait: Rendezvous -> Active
        Impl::spinwait( m_pool_state , ThreadsExec::Rendezvous );
      }
      else {
        // Root thread does the thread-scan before releasing threads

        scalar_type * ptr_prev = 0 ;

        for ( int rank = 0 ; rank < m_pool_size ; ++rank ) {
          scalar_type * const ptr = (scalar_type *) get_thread( rank )->reduce_memory();
          if ( rank ) {
            for ( unsigned i = 0 ; i < count ; ++i ) { ptr[i] = ptr_prev[ i + count ]; }
            Join::join( f , ptr + count , ptr );
          }
          else {
            (void) Init::init( f , ptr );
          }
          ptr_prev = ptr ;
        }
      }

      for ( int i = 0 ; i < m_pool_fan_size ; ++i ) {
        m_pool_base[ rev_rank + (1<<i) ]->m_pool_state = ThreadsExec::Active ;
      }
    }

  //------------------------------------
  /** \brief  Wait for previous asynchronous functor to
   *          complete and release the Threads device.
   *          Acquire the Threads device and start this functor.
   */
  static void start( void (*)( ThreadsExec & , const void * ) , const void * );

  static int  in_parallel();
  static void fence();
  static bool sleep();
  static bool wake();
};

} /* namespace Impl */
} /* namespace Kokkos */

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {

inline int Threads::in_parallel()
{ return Impl::ThreadsExec::in_parallel(); }

inline int Threads::is_initialized()
{ return Impl::ThreadsExec::is_initialized(); }

inline void Threads::initialize(
  unsigned threads_count ,
  unsigned use_numa_count ,
  unsigned use_cores_per_numa ,
  bool allow_asynchronous_threadpool )
{
  Impl::ThreadsExec::initialize( threads_count , use_numa_count , use_cores_per_numa , allow_asynchronous_threadpool );
}

inline void Threads::finalize()
{
  Impl::ThreadsExec::finalize();
}

inline void Threads::print_configuration( std::ostream & s , const bool detail )
{
  Impl::ThreadsExec::print_configuration( s , detail );
}

inline bool Threads::sleep()
{ return Impl::ThreadsExec::sleep() ; }

inline bool Threads::wake()
{ return Impl::ThreadsExec::wake() ; }

inline void Threads::fence()
{ Impl::ThreadsExec::fence() ; }

} /* namespace Kokkos */

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

#endif /* #define KOKKOS_THREADSEXEC_HPP */

