/* Exercise the full-validator PoH state machine, capturing its real output
   entries while replacing only the clock and inter-tile publication. */
#include "fd_poh.h"
#include "../../disco/tiles.h"

static long test_now;
static uchar shred_mem[ 65536 ] __attribute__((aligned(128)));
static uchar replay_mem[ 65536 ] __attribute__((aligned(128)));
static fd_poh_t poh_mem[1];
static uchar previous_hash[32];
static uchar mixin_hash[32];
static ulong ticks, microblocks, completed, expected_ticks;

static long test_clock( fd_clock_tile_t const * clock ) {
  (void)clock;
  return test_now;
}

static ulong
test_publish( fd_stem_context_t * stem, ulong out_idx, ulong sig,
              ulong chunk, ulong sz, ulong ctl, ulong tsorig, ulong tspub ) {
  (void)stem; (void)sig; (void)sz; (void)ctl; (void)tsorig; (void)tspub;
  if( out_idx==1UL ) return 0UL;
  uchar const * data = fd_chunk_to_laddr( shred_mem, chunk );
  fd_entry_batch_meta_t const * meta = (fd_entry_batch_meta_t const *)data;
  fd_entry_batch_header_t const * entry = (fd_entry_batch_header_t const *)(meta+1);
  FD_TEST( entry->hashcnt_delta>0UL );
  for( ulong i=1UL; i<entry->hashcnt_delta; i++ )
    fd_sha256_hash( previous_hash, 32UL, previous_hash );
  if( entry->txn_cnt ) {
    uchar pair[64];
    fd_memcpy( pair, previous_hash, 32UL );
    fd_memcpy( pair+32UL, mixin_hash, 32UL );
    fd_sha256_hash( pair, 64UL, previous_hash );
    microblocks++;
    FD_TEST( !meta->block_complete );
  } else {
    fd_sha256_hash( previous_hash, 32UL, previous_hash );
    ticks++;
  }
  FD_TEST( !memcmp( entry->hash, previous_hash, 32UL ) );
  if( meta->block_complete ) {
    FD_TEST( !entry->txn_cnt );
    FD_TEST( ticks==expected_ticks );
    completed++;
  }
  return 0UL;
}

#define fd_clock_tile_now test_clock
#define fd_stem_publish test_publish
#include "fd_poh.c"
#undef fd_clock_tile_now
#undef fd_stem_publish

static void setup( ulong hashes_per_tick, ulong skipped ) {
  fd_memset( poh_mem, 0, sizeof(poh_mem) );
  fd_memset( previous_hash, 0, sizeof(previous_hash) );
  fd_memset( mixin_hash, 7, sizeof(mixin_hash) );
  ticks = microblocks = completed = 0UL;
  expected_ticks = 64UL*(skipped+1UL);
  test_now = 0L;
  FD_TEST( fd_poh_new( poh_mem ) );
  fd_poh_out_t shred_out = { .idx=0UL, .mem=(fd_wksp_t *)shred_mem, .chunk0=0UL, .wmark=128UL, .chunk=0UL };
  fd_poh_out_t replay_out = { .idx=1UL, .mem=(fd_wksp_t *)replay_mem, .chunk0=0UL, .wmark=128UL, .chunk=0UL };
  FD_TEST( fd_poh_join( poh_mem, &shred_out, &replay_out, NULL, 0UL ) );
  uchar block_id[32] = {1};
  fd_poh_reset( poh_mem, NULL, 0L, hashes_per_tick, 64UL, 1000UL,
                10UL, previous_hash, 11UL+skipped, 32UL, block_id );
}

static void advance( void ) {
  int poll=1, busy=0;
  fd_poh_advance( poh_mem, NULL, &poll, &busy );
}

static void mixin( int successful ) {
  fd_txn_p_t txn[1] = {0};
  txn->flags = successful ? FD_TXN_P_FLAGS_EXECUTE_SUCCESS : 0U;
  txn->payload_sz = 1UL;
  txn->payload[0] = 42;
  fd_poh1_mixin( poh_mem, NULL, poh_mem->slot, mixin_hash, 1UL, txn, NULL );
}

static void run_case( ulong hashes_per_tick, int with_transactions, ulong skipped ) {
  setup( hashes_per_tick, skipped );
  for( ulong t=0UL; t<64UL*skipped; t++ ) {
    test_now += 1000L;
    advance();
  }
  fd_poh_begin_leader( poh_mem, 11UL+skipped, hashes_per_tick, 64UL, 1000UL, 32UL, test_now );
  while( fd_poh_must_publish_skipped_tick( poh_mem ) ) advance();
  FD_TEST( ticks==64UL*skipped );
  ulong wanted_microblocks = with_transactions ? 8UL : 0UL;
  for( ulong t=0UL; t<64UL; t++ ) {
    if( with_transactions && (t==0UL || t==1UL || t==31UL || t==63UL) ) {
      mixin(1);
      mixin(1);
    }
    if( t==2UL ) mixin(0); /* Failed transactions cannot become empty ticks. */
    if( t==63UL ) {
      /* Simulate receipt of done_packing after all execution responses. */
      poh_mem->microblocks_lower_bound = poh_mem->max_microblocks_per_slot;
    }
    test_now += 1000L;
    advance();
    FD_TEST( ticks==64UL*skipped+t+1UL );
  }
  FD_TEST( microblocks==wanted_microblocks );
  FD_TEST( completed==1UL );
  FD_TEST( poh_mem->slot==12UL+skipped );
  FD_TEST( poh_mem->state==STATE_WAITING_FOR_RESET );
  FD_TEST( !memcmp( poh_mem->hash, previous_hash, 32UL ) );
}

int main( int argc, char ** argv ) {
  fd_boot( &argc, &argv );
  run_case( 0UL, 0, 0UL );
  run_case( 0UL, 1, 0UL );
  run_case( 1UL, 1, 0UL );
  run_case( 0UL, 1, 2UL );
  run_case( 4UL, 0, 0UL );
  run_case( 4UL, 1, 0UL );
  run_case( 62500UL, 1, 0UL );

  /* Consecutive leadership and a reset must both restore the slot clock. */
  setup( 0UL, 0UL );
  fd_poh_begin_leader( poh_mem, 11UL, 0UL, 64UL, 1000UL, 32UL, 0L );
  mixin(1);
  uchar block_id[32] = {2};
  uchar reset_hash[32] = {3};
  fd_poh_reset( poh_mem, NULL, 0L, 0UL, 64UL, 1000UL, 11UL,
                reset_hash, 12UL, 32UL, block_id );
  fd_memcpy( previous_hash, reset_hash, 32UL );
  ticks = microblocks = completed = 0UL;
  fd_poh_begin_leader( poh_mem, 12UL, 0UL, 64UL, 1000UL, 32UL, 0L );
  for( ulong t=1UL; t<=64UL; t++ ) {
    if( t==64UL ) poh_mem->microblocks_lower_bound = poh_mem->max_microblocks_per_slot;
    test_now = (long)(1000UL*t);
    advance();
  }
  FD_TEST( completed==1UL && ticks==64UL );
  FD_LOG_NOTICE(( "pass: full PoH sleep/numeric tick counts, hash chains, microblocks, skipped slots and reset" ));
  fd_halt();
  return 0;
}
