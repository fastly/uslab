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
 */

#ifndef _USLAB_H_
#define _USLAB_H_

#include <stddef.h>
#include <stdint.h>

struct uslab_pt {
	/*
	 * first_free and generation *must* be contiguous so that CAS2 can
	 * update both to avoid ABA conflicts on concurrent allocations.
	 */
	char	*first_free;
	char	*generation;
	size_t	size;
	size_t	used;
	size_t	offset;

	char	 *base;
	/*
	 * Keep this cacheline-sized, otherwise false sharing will kill
	 * throughput in threads in adjacent uslabs.
	 */
	char	pad[64 - 48];
};

extern __thread struct uslab_pt *uslab_pt;

struct uslab_entry {
	char *next_free;
};

struct uslab {
	struct uslab_pt	*pt_base;
	char		*slab0_base;

	uint64_t	size_class;
	size_t		slab_len;
	uint64_t	pt_slabs;
	size_t		pt_size;
	uint64_t	pt_ctr;
};

struct uslab	*uslab_create_anonymous(void *base, size_t size_class, uint64_t nelem, uint64_t npt_slabs);
struct uslab 	*uslab_create_heap(size_t size_class, uint64_t nelem, uint64_t npt_slabs);
struct uslab 	*uslab_create_ramdisk(const char *path, void *base, size_t size_class, uint64_t nelem, uint64_t npt_slabs);

void		*uslab_alloc(struct uslab *);
void		uslab_free(struct uslab *, void *p);

void		uslab_destroy_heap(struct uslab *);
void		uslab_destroy_map(struct uslab *);

#endif
