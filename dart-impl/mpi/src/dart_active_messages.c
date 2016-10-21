#include <stdlib.h>
#include <unistd.h>
#include <mpi.h>
#include <errno.h>

#include <dash/dart/if/dart_active_messages.h>
#include <dash/dart/if/dart_communication.h>
#include <dash/dart/if/dart_globmem.h>
#include <dash/dart/mpi/dart_team_private.h>
#include <dash/dart/mpi/dart_globmem_priv.h>
#include <dash/dart/mpi/dart_mpi_serialization.h>

/**
 * TODO:
 *  1) Ensure proper locking of parallel threads!
 *  2) Should we allow for return values to be passed back?
 *  3) Use a distributed double buffer to allow for overlapping read/writes
 */

struct dart_amsgq {
  MPI_Win      tailpos_win;
  MPI_Win      queue_win;
  char        *queue_ptr;
  uint64_t    *tailpos_ptr;
  char        *dbuf;      // a double buffer used during message processing to shorten lock times
  uint64_t     size;      // the size (in byte) of the message queue
  dart_team_t  team;
};

static pthread_mutex_t processing_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool initialized = false;
static bool needs_translation = false;
static int64_t *offsets = NULL;

/**
 * Initialize the active messaging subsystem, mainly to determine the offsets of function pointers
 * between different units. This has to be done only once in a collective global operation.
 *
 * We assume that there is a single offset for all function pointers.
 */
dart_ret_t
dart_amsg_init()
{
  if (initialized) return DART_OK;

  size_t numunits;
  dart_size(&numunits);
  uint64_t  base  = (uint64_t)&dart_amsg_openq;
  uint64_t *bases = calloc(numunits, sizeof(uint64_t));
  if (!bases) {
    return DART_ERR_INVAL;
  }

  DART_LOG_TRACE("Exchanging offsets (dart_amsg_openq = %p)", &dart_amsg_openq);
  if (MPI_Allgather(&base, 1, MPI_UINT64_T, bases, 1, MPI_UINT64_T, MPI_COMM_WORLD) != MPI_SUCCESS) {
    DART_LOG_ERROR("Failed to exchange base pointer offsets!");
    return DART_ERR_NOTINIT;
  }

  // check whether we need to use offsets at all
  for (size_t i = 0; i < numunits; i++) {
    if (bases[i] != base) {
      needs_translation = true;
      DART_LOG_INFO("Using base pointer offsets for active messages (%llu against %llu on unit %i).", base, bases[i], i);
      break;
    }
  }

  if (needs_translation) {
    offsets = malloc(numunits * sizeof(uint64_t));
    if (!offsets) {
      return DART_ERR_INVAL;
    }
    for (size_t i = 0; i < numunits; i++) {
      offsets[i] = ((uint64_t)&dart_amsg_openq) - bases[i];
    }
  }

  free(bases);

  initialized = true;

  return DART_OK;
}

dart_amsgq_t
dart_amsg_openq(size_t size, dart_team_t team)
{
  dart_unit_t unitid;
  MPI_Comm tcomm;
  struct dart_amsgq *res = calloc(1, sizeof(struct dart_amsgq));
  res->size = size;
  res->dbuf = malloc(size);
  res->team = team;

  dart_comm_down();

  dart_team_myid (team, &unitid);

  // allocate the queue
  /**
   * We cannot use dart_team_memalloc_aligned because it uses MPI_Win_allocate_shared that
   * cannot be used for window locking.
   */
  /*
  if (dart_team_memalloc_aligned(team, size, &(res->queue)) != DART_OK) {
    DART_LOG_ERROR("Failed to allocate %i byte for queue", size);
  }
  DART_GPTR_COPY(queue, res->queue);
  queue.unitid = unitid;
  dart_gptr_getaddr (queue, (void**)&addr);
  memset(addr, 0, size);

  // allocate the tailpos pointer
  dart_team_memalloc_aligned(team, sizeof(int), &(res->tailpos));
  DART_GPTR_COPY(tailpos, res->tailpos);
  tailpos.unitid = unitid;
  dart_gptr_getaddr (tailpos, (void**)&addr);
  *addr = 0;
  */

  uint16_t index;
  dart_adapt_teamlist_convert(team, &index);
  tcomm = dart_team_data[index].comm;

  MPI_Win_allocate(sizeof(uint64_t), 1, MPI_INFO_NULL, tcomm, (void*)&(res->tailpos_ptr), &(res->tailpos_win));
  *(res->tailpos_ptr) = 0;
  MPI_Win_flush(unitid, res->tailpos_win);
  MPI_Win_allocate(size, 1, MPI_INFO_NULL, tcomm, (void*)&(res->queue_ptr), &(res->queue_win));
  memset(res->queue_ptr, 0, size);
  MPI_Win_fence(0, res->queue_win);

  dart_comm_up();
  return res;
}


dart_ret_t
dart_amsg_trysend(dart_unit_t target, dart_amsgq_t amsgq, dart_task_action_t *fn, const void *data, size_t data_size)
{
  dart_unit_t unitid;
  int msg_size = (sizeof(unitid) + sizeof(dart_task_action_t*) + sizeof(data_size) + data_size);
  uint64_t remote_offset;

  dart_task_action_t *remote_fn_ptr = fn;
  // we do the translation everytime we send a message as it saves space
  // TODO: would it be more efficient to store the translation data per message queue?
  if (needs_translation) {
    int64_t  remote_fn_offset;
    dart_unit_t global_target_id;
    dart_team_unit_l2g(amsgq->team, target, &global_target_id);
    remote_fn_offset = offsets[global_target_id];
    remote_fn_ptr += remote_fn_offset;
  }

  dart_comm_down();

  dart_myid(&unitid);

  //lock the tailpos window
  MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target, 0, amsgq->tailpos_win);

  // Add the size of the message to the tailpos at the target
  MPI_Fetch_and_op(&msg_size, &remote_offset, MPI_INT64_T, target, 0, MPI_SUM, amsgq->tailpos_win);

  if ((remote_offset + msg_size) >= amsgq->size) {
    int tmp;
    // if not, revert the operation and free the lock to try again.
    MPI_Fetch_and_op(&remote_offset, &tmp, MPI_INT64_T, target, 0, MPI_REPLACE, amsgq->tailpos_win);
    MPI_Win_unlock(target, amsgq->tailpos_win);
    dart_comm_up();
    DART_LOG_INFO("Not enough space for message of size %i at unit %i (current offset %i)", msg_size, target, remote_offset);
    return DART_ERR_AGAIN;
  }

  // lock the target queue before releasing the tailpos window to avoid potential race conditions
  MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target, 0, amsgq->queue_win);
  MPI_Win_unlock(target, amsgq->tailpos_win);

  // we now have a slot in the message queue
  MPI_Aint queue_disp = remote_offset;
  MPI_Put(&unitid, sizeof(unitid), MPI_BYTE, target, queue_disp, sizeof(unitid), MPI_BYTE, amsgq->queue_win);
  queue_disp += sizeof(unitid);
  MPI_Put(&remote_fn_ptr, sizeof(dart_task_action_t*), MPI_BYTE, target, queue_disp, sizeof(dart_task_action_t*), MPI_BYTE, amsgq->queue_win);
  queue_disp += sizeof(dart_task_action_t*);
  MPI_Put(&data_size, sizeof(data_size), MPI_BYTE, target, queue_disp, data_size, MPI_BYTE, amsgq->queue_win);
  queue_disp += sizeof(data_size);
  MPI_Put(data, data_size, MPI_BYTE, target, queue_disp, data_size, MPI_BYTE, amsgq->queue_win);
  MPI_Win_unlock(target, amsgq->queue_win);

  DART_LOG_INFO("Sent message of size %i with payload %i to unit %i starting at offset %i", msg_size, data_size, target, remote_offset);

  dart_comm_up();
  return DART_OK;
}


dart_ret_t
dart_amsg_process(dart_amsgq_t amsgq)
{
  dart_unit_t unitid;
  int   zero = 0;
  int   tailpos;

  int ret = pthread_mutex_lock(&processing_mutex);
  if (ret != 0) {
    // another thread is currently processing the active message queue
    return DART_ERR_AGAIN;
  }

  char *dbuf = amsgq->dbuf;

  dart_team_myid (amsgq->team, &unitid);

  dart_comm_down();
  MPI_Win_lock(MPI_LOCK_EXCLUSIVE, unitid, 0, amsgq->tailpos_win);

  MPI_Get(&tailpos, 1, MPI_INT32_T, unitid, 0, 1, MPI_INT32_T, amsgq->tailpos_win);

  /**
   * process messages while they are available, i.e.,
   * repeat if messages come in while processing previous messages
   */
  if (tailpos > 0) {
    DART_LOG_INFO("Checking for new active messages (tailpos=%i)", tailpos);
    // lock the tailpos window
    MPI_Win_lock(MPI_LOCK_EXCLUSIVE, unitid, 0, amsgq->queue_win);
    // copy the content of the queue for processing
    /*
    DART_GPTR_COPY(queueg, amsgq->queue_win);
    queueg.unitid = unitid;
    dart_gptr_getaddr (queueg, &queue);
    */
//    memcpy(dbuf, amsgq->queue_ptr, tailpos);
    MPI_Get(dbuf, tailpos, MPI_BYTE, unitid, 0, tailpos, MPI_BYTE, amsgq->queue_win);
    MPI_Win_unlock(unitid, amsgq->queue_win);

    // reset the tailpos and release the lock on the message queue
    MPI_Put(&zero, 1, MPI_INT32_T, unitid, 0, 1, MPI_INT32_T, amsgq->tailpos_win);
    MPI_Win_unlock(unitid, amsgq->tailpos_win);

    dart_comm_up();

    // process the messages by invoking the functions on the data supplied
    int pos = 0;
    while (pos < tailpos) {
#ifdef DART_ENABLE_LOGGING
      int startpos = pos;
#endif
      // unpack the message
      dart_unit_t remote = *(dart_unit_t*)(dbuf + pos);
      pos += sizeof(dart_unit_t);
      dart_task_action_t *fn = *(dart_task_action_t**)(dbuf + pos);
      pos += sizeof(dart_task_action_t*);
      size_t data_size  = *(size_t*)(dbuf + pos);
      pos += sizeof(data_size);
      void *data     = dbuf + pos;
      pos += data_size;

      if (pos > tailpos) {
        DART_LOG_ERROR("Message out of bounds (expected %i but saw %i)\n", tailpos, pos);
        pthread_mutex_unlock(&processing_mutex);
        return DART_ERR_INVAL;
      }

      // invoke the message
      DART_LOG_INFO("Invoking active message %p from %i on data %p of size %i starting from tailpos %i",
                        fn, remote, data, data_size, startpos);
      fn(data);
    }
  } else {
    MPI_Win_unlock(unitid, amsgq->tailpos_win);
    dart_comm_up();
  }
  pthread_mutex_unlock(&processing_mutex);
  return DART_OK;
}


dart_ret_t
dart_amsg_sync(dart_amsgq_t amsgq)
{
  dart_barrier(amsgq->team);
  return dart_amsg_process(amsgq);
}

dart_ret_t
dart_amsg_closeq(dart_amsgq_t amsgq)
{
  free(amsgq->dbuf);
  amsgq->dbuf = NULL;
  amsgq->queue_ptr = NULL;
  dart_comm_down();
  MPI_Win_free(&(amsgq->tailpos_win));
  MPI_Win_free(&(amsgq->queue_win));
  dart_comm_up();

  /*
  dart_team_memfree(amsgq->team, amsgq->queue_win);
  dart_team_memfree(amsgq->team, amsgq->tailpos_win);
  */
  free(amsgq);

  return DART_OK;
}