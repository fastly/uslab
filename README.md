# uslab

Uslab is a standalone, lock-free slab allocator library supporting both
short-lived allocations as well as persistent storage. It is expected to shine
as a bounded buffer of fixed-size objects.

## Design

Two major properties of uslab allow it to be small, simple, and lock-free.
First, freed memory is never actually returned to the operating system.
Instead, anything explicitly freed becomes the head of a freelist (which is
thus implemented as a stack). The freelist is maintained by reusing the areas
we previously allocated to store pointers to additional items in the list.

Some ancillary assumptions are made. Memory must start zeroed, and therefore
must be either mapped with `MAP_ANONYMOUS` or from a file on a RAM-backed disk
store. Because memory is zeroed, any item appearing on a freelist chain with
a 0 value implies that the immediately adjacent "node" is also free. Because
of this assumption, we do not use offsets (and consequently, if using the slab
for persistent memory storage, the slab must be mapped at a fixed address).

The slab is designed to be safe with many concurrently allocating threads with
many concurrently freeing threads. Items must be freed only once per
corresponding allocation. Double frees will result in a corrupted free stack,
likely creating a loop in the stack that ends up resulting in undefined
behavior.

The slab is ABA-safe. It must be, because it is possible for pre-emption to
pause a thread that has observed `slab->first_free->next_free`. During this
paused period, another thread may actually become the owner of the object
we're paused reading. If at least 1 free occurs before the winning thread
frees the paused thread's `slab->first_free`, `slab->first_free->next_free`
will no longer be consistent and the stack will be corrupted.

## API

### struct uslab_pt

Consuming applications must define a pointer to a thread-local
`struct uslab_pt` called `uslab_pt`. This is a per-thread region of the slab
used to reduce contention.

```c
__thread struct uslab_pt *uslab_pt
```

### struct uslab

Structure describing an slab and allocated at the head of the slab. Details
of the structure are managed by the library. The code holding the reference to
the slab must treat it as immutable.

### Creating an Arena

```c
struct uslab    *uslab_create_anonymous(void *base, size_t size_class, uint64_t nelem);
struct uslab    *uslab_create_heap(size_t size_class, uint64_t nelem);
struct uslab    *uslab_create_ramdisk(const char *path, void *base, size_t size_class, uint64_t nelem);
```

Three methods exist for creating an slab:

 * From an anonymous `mmap(2)` region, using `uslab_create_anonymous`.
 * From the heap (using `calloc(3)`), using `uslab_create_heap`.
 * From a sparse file on a memory disk, using `uslab_create_ramdisk`.

### Allocating and Freeing

```c
void            *uslab_alloc(struct uslab *); 
void            uslab_free(struct uslab *, void *p);
```

To allocate, pass the handle from your `uslab_create_*` call. To free, pass
the handle and the pointer received from `uslab_alloc`. Simple.

