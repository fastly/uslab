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

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "uslab.h"
#include "tap.h"

__thread struct uslab_pt *uslab_pt = NULL;

int
main(void)
{
	struct stat sb;
	int rv;

	plan_no_plan();

	if ((rv = stat("tmp", &sb)) == -1) {
		if (errno != ENOENT) {
			perror("stat");
			return -1;
		}
	} else {
		if (!S_ISDIR(sb.st_mode)) {
			fprintf(stderr, "tmp not a directory");
			return -1;
		} else {
			umount("tmp");
			if (rmdir("tmp") == -1) {
				perror("rmdir");
				return -1;
			}
		}
	}

	mkdir("tmp", S_IRUSR | S_IWUSR | S_IXUSR);
	rv = mount(NULL, "tmp", "tmpfs", MS_NODEV | MS_NOATIME | MS_NODIRATIME |
	    MS_NOSUID | MS_NOEXEC, "size=1g");
	if (rv == -1) {
		perror("mount");
		return -1;
	}


	/* Test that opening a ramdisk persists data between unmaps */
	{
		char *base = (char *)0x4f000000;
		uintptr_t *p, *q;
		struct uslab *a;

		unlink("tmp/8");

		a = uslab_create_ramdisk("tmp/8", base, 8, 1, 1);
		isnt(a, NULL);
		is((char *)a, base);
		
		p = uslab_alloc(a);
		q = (uintptr_t *)0x4f002000;
		is(p, q);
		*p = (uintptr_t)p;
		is(*p, *q);

		uslab_destroy_map(a);

		a = uslab_create_ramdisk("tmp/8", base, 8, 1, 1);
		isnt(a, NULL);
		is((char *)a, base);

		q = uslab_alloc(a);
		is(q, NULL);

		is(*p, (uintptr_t)p);

		uslab_destroy_map(a);
		unlink("tmp/8");
	}

	/* Test ramdisk-backed sparse behavior */
	{
		char *base = (char *)0x5f000000;
		uintptr_t *p, *q, *r;
		struct uslab *a;

		unlink("tmp/8");

		/* Otherwise we keep remembering our old crap */
		uslab_pt = NULL;
		a = uslab_create_ramdisk("tmp/8", base, 8, 1024UL*1024UL*1024UL*1024UL, 1);
		isnt(a, NULL);
		is((char *)a, base);
		
		p = uslab_alloc(a);
		q = (uintptr_t *)0x5f002000;
		is(p, q);
		*p = (uintptr_t)p;
		is(*p, *q);

		uslab_destroy_map(a);

		a = uslab_create_ramdisk("tmp/8", base, 8, 1024UL*1024UL*1024UL*1024UL, 1);
		isnt(a, NULL);

		r = q;
		q = uslab_alloc(a);
		is(q, r + 1);

		is(*p, (uintptr_t)p);

		uslab_destroy_map(a);
		unlink("tmp/8");
	}

	/*
	 * Test that allocation fails when we have nothing else to allocate,
	 * and succeeds when we free.
	 */
	{
		struct uslab *a;
		void *p, *q;

		uslab_pt = NULL;
		a = uslab_create_heap(8, 1, 1);
		isnt(a, NULL);

		q = p = uslab_alloc(a);
		isnt(p, NULL);

		p = uslab_alloc(a);
		is(p, NULL);

		p = uslab_alloc(a);
		is(p, NULL);

		uslab_free(a, q);

		p = uslab_alloc(a);
		isnt(p, NULL);

		uslab_destroy_heap(a);
	}

	/* Test that we can "steal" from other arenas when we're out of mem */
	{
		struct uslab *a;
		void *p;

		uslab_pt = NULL;
		a = uslab_create_heap(8, 2, 2);
		isnt(a, NULL);

		p = uslab_alloc(a);
		isnt(p, NULL);
		p = uslab_alloc(a);
		isnt(p, NULL);
		p = uslab_alloc(a);
		is(p, NULL);

		uslab_destroy_heap(a);
	}

	return 0;
}
