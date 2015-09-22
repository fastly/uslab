/*
 * Copyright 2015 Fastly, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Concurrent, in-line slab allocator implementation, safe for workloads with
 * a single concurrent process freeing and multiple concurrent processes
 * allocating.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/user.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ck_pr.h>

#include "uslab.h"

struct uslab *
uslab_create_heap(size_t size_class, uint64_t nelem, uint64_t npt_slabs)
{
	char *cur_slab, *cur_base;
	struct uslab *a;
	uint64_t i;

	if (((size_class * nelem) / npt_slabs) == 0) {
		return NULL;
	}

	a = calloc(1, (2 * PAGE_SIZE) + (size_class * nelem));
	if (a == NULL) {
		return NULL;
	}

	cur_slab = ((char *)a) + PAGE_SIZE;
	a->slab0_base = cur_base = ((char *)a) + (2 * PAGE_SIZE);

	a->pt_base = (struct uslab_pt *)cur_slab;
	a->pt_size = (size_class * nelem) / npt_slabs;
	a->pt_slabs = npt_slabs;
	a->size_class = size_class;
	a->slab_len = size_class * nelem;

	for (i = 0; i < npt_slabs; i++) {
		struct uslab_pt *pt;

		pt = (struct uslab_pt *)cur_slab;
		pt->base = pt->first_free = cur_base;
		pt->size = a->pt_size;
		pt->offset = i;

		cur_slab += sizeof (*pt);
		cur_base += a->pt_size;
	}

	return a;
}

struct uslab *
uslab_create_anonymous(void *base, size_t size_class, uint64_t nelem,
    uint64_t npt_slabs)
{
	int mflags = MAP_ANONYMOUS | MAP_PRIVATE;
	char *cur_slab, *cur_base;
	struct uslab *a;
	uint64_t i;
	void *map;

	if (((size_class * nelem) / npt_slabs) == 0) {
		return NULL;
	}

	if (base != NULL) {
		mflags |= MAP_FIXED;
	}

	map = mmap(base, (2 * PAGE_SIZE) + (nelem * size_class),
	    PROT_READ | PROT_WRITE, mflags, -1, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	a = map;
	cur_slab = ((char *)a) + PAGE_SIZE;
	a->slab0_base = cur_base = ((char *)a) + (2 * PAGE_SIZE);

	a->pt_base = (struct uslab_pt *)cur_slab;
	a->pt_size = (size_class * nelem) / npt_slabs;
	a->pt_slabs = npt_slabs;
	a->size_class = size_class;
	a->slab_len = size_class * nelem;

	for (i = 0; i < npt_slabs; i++) {
		struct uslab_pt *pt;

		pt = (struct uslab_pt *)cur_slab;
		pt->base = pt->first_free = cur_base;
		pt->size = a->pt_size;
		pt->offset = i;

		cur_slab += sizeof (*pt);
		cur_base += a->pt_size;
	}

	return a;
}

static void
uslab_close_fd(int fd)
{
	int r;

	do {
		r = close(fd);
	} while (r == -1 && errno == EINTR);
}

struct uslab *
uslab_create_ramdisk(const char *path, void *base, size_t size_class,
    uint64_t nelem, uint64_t npt_slabs)
{
	int fd, r, mflags = MAP_SHARED;
	char *cur_slab, *cur_base;
	struct uslab *a;
	struct stat sb;
	bool opened;
	uint64_t i;
	void *map;

	if (((size_class * nelem) / npt_slabs) == 0) {
		return NULL;
	}

	opened = false;
	r = stat(path, &sb);
	if (r == -1 && errno == ENOENT) {
		const char z = 0;
		ssize_t s;
		off_t o;
		int e;

		if ((fd = open(path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
			return NULL;
		}

		o = lseek(fd, (2 * PAGE_SIZE) + (nelem * size_class) - 1, SEEK_SET);
		if (o == -1) {
			e = errno;
			uslab_close_fd(fd);
			errno = e;
			return NULL;
		}

		do {
			s = write(fd, &z, 1);
		} while (s == -1 && errno == EINTR);

		if (s == -1) {
			e = errno;
			uslab_close_fd(fd);
			errno = e;
			return NULL;
		}

		o = lseek(fd, 0, SEEK_SET);
		if (o == -1) {
			e = errno;
			uslab_close_fd(fd);
			errno = e;
			return NULL;
		}

		sb.st_size = (2 * PAGE_SIZE) + (nelem * size_class);
	} else {
		if ((fd = open(path, O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
			return NULL;
		}

		opened = true;
	}

	if (base != NULL) {
		mflags |= MAP_FIXED;
	}

	map = mmap(base, sb.st_size, PROT_READ | PROT_WRITE, mflags, fd, 0);
	uslab_close_fd(fd);

	if (map == MAP_FAILED) {
		return NULL;
	}

	a = map;

	cur_slab = ((char *)a) + PAGE_SIZE;

	a->slab0_base = cur_base = ((char *)a) + (2 * PAGE_SIZE);
	a->pt_base = (struct uslab_pt *)cur_slab;
	a->pt_size = (size_class * nelem) / npt_slabs;
	a->pt_slabs = npt_slabs;
	a->size_class = size_class;
	a->slab_len = size_class * nelem;

	for (i = 0; i < npt_slabs; i++) {
		struct uslab_pt *pt;

		pt = (struct uslab_pt *)cur_slab;
		pt->base = cur_base;
		if (opened == false) {
			pt->first_free = cur_base;
		}
		pt->size = a->pt_size;
		pt->offset = i;

		cur_slab += sizeof (*pt);
		cur_base += a->pt_size;
	}

	return a;
}

void
uslab_destroy_heap(struct uslab *a)
{

	free(a);
}

void
uslab_destroy_map(struct uslab *a)
{

	munmap(a, a->slab_len);
}

/*
 * When we begin, our slab is sparse and zeroed. Effectively, this means that
 * we obtain our memory either with mmap(2) and MAP_ANONYMOUS, by using
 * shm_open(3), ftruncate(2), and mmap(2), or the mmap(2)-backed file comes
 * from a RAM-backed storage that initializes to 0 on access.
 *
 * Our approach is to find the first free block. We then figure out what the
 * next free block will be. If the next free block is NULL, we know that the
 * block immediately following the block we've chosen is the next logically
 * free block.
 *
 * We are prone to ABA. If we read first_free, load the next_free from it, and
 * are subsequently pre-empted, another concurrent process could allocate and
 * then free our target. Additional allocations may have occurred which alter
 * the target's next_free member by the time it was freed. In this case, we
 * would end up in an inconsistent state. We solve this problem by doing a
 * CAS2 on our slab to update both the free block and a generation counter.
 */
void *
uslab_alloc(struct uslab *a)
{
	struct uslab_pt update, original, *slab, *oa;
	struct uslab_entry *target;
	char *next_free;

	if (uslab_pt == NULL) {
		uslab_pt = &a->pt_base[ck_pr_faa_64(&a->pt_ctr, 1) % a->pt_slabs];
	}

	slab = uslab_pt;

retry:
	/* If we're out of space, try to steal some memory from elsewhere */
	if (slab->first_free >= slab->base + slab->size) {
		uint64_t i = 1;

		oa = slab;
		slab = &a->pt_base[(oa->offset + i) % a->pt_slabs];
		while (slab != oa && slab->first_free >= slab->base + slab->size) {
			slab = &a->pt_base[(oa->offset + i++) % a->pt_slabs];
		}

		/* OOM. */
		if (slab == oa) {
			return NULL;
		}

		goto retry;
	}

	original.generation = ck_pr_load_ptr(&slab->generation);
	ck_pr_fence_load();
	original.first_free = ck_pr_load_ptr(&slab->first_free);
	target = (struct uslab_entry *)original.first_free;
	ck_pr_fence_load();

	if (target->next_free == 0) {
		/*
		 * When this is the last block, this will put an address
		 * outside the bounds of the slab into the first_free member.
		 * If we succeed, no other threads could win the bad value as
		 * first_free is ABA protected and checked to be within bounds.
		 */
		next_free = original.first_free + a->size_class;
	} else {
		next_free = target->next_free;
	}

	update.generation = original.generation + 1;
	update.first_free = next_free;

	while (ck_pr_cas_ptr_2_value(slab, &original, &update, &original) == false) {
		/*
		 * We failed to get the optimistic allocation, and our new
		 * first_free block is outside the bounds of this slab.
		 * Revert to trying to steal one from elsewhere.
		 */
		if (slab->first_free >= slab->base + slab->size) {
			slab = &a->pt_base[(slab->offset + 1) % a->pt_slabs];
			goto retry;
		}

		update.generation = original.generation + 1;
		target = (struct uslab_entry *)original.first_free;
		ck_pr_fence_load();
		if (target->next_free == 0) {
			next_free = original.first_free + a->size_class;
		} else {
			next_free = target->next_free;
		}

		update.first_free = next_free;
	}
	ck_pr_add_64(&slab->used, a->size_class);

	return target;
}

/*
 * An slab free routine that is safe with one or more concurrent unique
 * freeing processes in the face of many concurrent allocating processes. We
 * don't need any CAS2 voodoo here because we do not rely on the value of
 * next_free for the entry we are attempting to replace at the head of our
 * stack. Additionally, it is impossible for us to observe the same value
 * at the time we read target and the time we try to write to it because
 * no other concurrent processes know about target.
 */
void
uslab_free(struct uslab *a, void *p)
{
	struct uslab_pt *allocated_slab;
	struct uslab_entry *e;
	char *target;

	/* Stupid. */
	if (p == NULL) return;

	/*
	 * We want to free these into the same section of the pool from which
	 * they were allocated.
	 */
	allocated_slab = &a->pt_base[(((char *)p) - a->slab0_base) / a->pt_size];

	do {
		e = p;
		target = ck_pr_load_ptr(&allocated_slab->first_free);
		e->next_free = target;
		ck_pr_fence_store();
	} while (ck_pr_cas_ptr(&allocated_slab->first_free, target, e) == false);

	ck_pr_sub_64(&allocated_slab->used, a->size_class);
}
