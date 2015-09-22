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
 * Benchmarks for uslab allocation. Records throughput and average latency
 * per-thread from 1..N threads for a stoachastic workload of M operations.
 */

#include <sys/param.h>

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "jemalloc/jemalloc.h"
#include "uslab.h"
#include "rdtscp.h"

struct td_state {
	pthread_t	pt;

	uint64_t	n_ops;
	uint64_t	tid;

	struct uslab	*slab;
	void		**ptrs;
	uint64_t	top;

	uint64_t	n_allocs_completed;
	uint64_t	n_frees_completed;
	uint64_t	tdelta;
};

struct td_state *state;
__thread struct uslab_pt *uslab_pt = NULL;

void *
bench_td_jemalloc(void *arg)
{
	struct td_state *a;
	uint64_t st, et;

	a = arg;

	st = rdtscp();
	for (uint64_t i = 0; i < a->n_ops; i++) {
		a->ptrs[i] = je_malloc(sizeof (void *));
		a->n_allocs_completed++;
	}

	for (uint64_t i = 0; i < a->n_ops; i++) {
		je_free(a->ptrs[i]);
		a->n_frees_completed++;
	}
	et = rdtscp();

	a->tdelta = et - st;

	return NULL;
}
void *
bench_td_malloc(void *arg)
{
	struct td_state *a;
	uint64_t st, et;

	a = arg;

	st = rdtscp();
	for (uint64_t i = 0; i < a->n_ops; i++) {
		a->ptrs[i] = malloc(sizeof (void *));
		a->n_allocs_completed++;
	}

	for (uint64_t i = 0; i < a->n_ops; i++) {
		free(a->ptrs[i]);
		a->n_frees_completed++;
	}
	et = rdtscp();

	a->tdelta = et - st;

	return NULL;
}

void *
bench_td_uslab(void *arg)
{
	struct td_state *a;
	uint64_t st, et;

	a = arg;

	st = rdtscp();
	for (uint64_t i = 0; i < a->n_ops; i++) {
		a->ptrs[i] = uslab_alloc(a->slab);
		a->n_allocs_completed++;
	}

	for (uint64_t i = 0; i < a->n_ops; i++) {
		uslab_free(a->slab, a->ptrs[i]);
		a->n_frees_completed++;
	}
	et = rdtscp();

	a->tdelta = et - st;

	return NULL;
}

void
usage(void)
{

	fprintf(stderr, "uslab_bench -t N -n N\n"
			"\t-a N:\tNumber of slabs to use\n"
			"\t-n N:\tNumber of operations to complete per thread\n"
			"\t-t N:\tNumber of threads to test up to\n");
	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	unsigned long n_tds, n_ops, n_slabs;
	struct uslab *slab;
	uint64_t td_total;
	int opt;

	n_slabs = n_tds = 2;
	n_ops = 10 * 1000 * 1000;

	while ((opt = getopt(argc, argv, "a:n:t:")) != -1) {
		switch (opt) {
		case 'a':
			errno = 0;
			n_slabs = strtoul(optarg, NULL, 0);
			if (errno != 0) {
				usage();
			}
			break;
		case 'n':
			errno = 0;
			n_ops = strtoul(optarg, NULL, 0);
			if (errno != 0) {
				usage();
			}
			break;
		case 't':
			errno = 0;
			n_tds = strtoul(optarg, NULL, 0);
			if (errno != 0) {
				usage();
			}
			break;
		default:
			usage();
			break;
		}
	}

	n_slabs = MIN(n_slabs, n_tds);
	td_total = 0;

	state = calloc(n_tds, sizeof (*state));
	//slab = uslab_create_anonymous(NULL, sizeof (void *), n_ops * n_tds, n_slabs);
	slab = uslab_create_heap(sizeof (void *), n_ops * n_tds, n_slabs);
	
	for (unsigned long i = 0; i < n_tds; i++) {
		state[i].n_ops = n_ops;
		state[i].tid = i;
		state[i].ptrs = calloc(n_ops, sizeof (void *));
		state[i].slab = slab;
	}

	for (unsigned long i = 0; i < n_tds; i++) {
		pthread_create(&state[i].pt, NULL, bench_td_uslab, &state[i]);
	}

	for (unsigned long i = 0; i < n_tds; i++) {
		pthread_join(state[i].pt, NULL);
	}


	for (unsigned long i = 0; i < n_tds; i++) {
		fprintf(stderr, "Thread %lu:\n"
		    "\tn_allocs: %" PRIu64 "\n"
		    "\tn_frees:  %" PRIu64 "\n"
		    "\tcycles:   %" PRIu64 "\n",
		    i, state[i].n_allocs_completed,
		    state[i].n_frees_completed, state[i].tdelta);
		td_total += state[i].tdelta;
		state[i].n_allocs_completed = state[i].n_frees_completed = state[i].tdelta = 0;
	}
	fprintf(stderr, "td_total: %" PRIu64 "\n\n", td_total);
	td_total = 0;

	uslab_destroy_heap(slab);

	for (unsigned long i = 0; i < n_tds; i++) {
		pthread_create(&state[i].pt, NULL, bench_td_malloc, &state[i]);
	}

	for (unsigned long i = 0; i < n_tds; i++) {
		pthread_join(state[i].pt, NULL);
	}


	for (unsigned long i = 0; i < n_tds; i++) {
		fprintf(stderr, "Thread %lu:\n"
		    "\tn_allocs: %" PRIu64 "\n"
		    "\tn_frees:  %" PRIu64 "\n"
		    "\tcycles:   %" PRIu64 "\n",
		    i, state[i].n_allocs_completed,
		    state[i].n_frees_completed, state[i].tdelta);
		td_total += state[i].tdelta;
		state[i].n_allocs_completed = state[i].n_frees_completed = state[i].tdelta = 0;
	}
	fprintf(stderr, "td_total: %" PRIu64 "\n\n", td_total);
	td_total = 0;

	for (unsigned long i = 0; i < n_tds; i++) {
		pthread_create(&state[i].pt, NULL, bench_td_jemalloc, &state[i]);
	}

	for (unsigned long i = 0; i < n_tds; i++) {
		pthread_join(state[i].pt, NULL);
	}


	for (unsigned long i = 0; i < n_tds; i++) {
		fprintf(stderr, "Thread %lu:\n"
		    "\tn_allocs: %" PRIu64 "\n"
		    "\tn_frees:  %" PRIu64 "\n"
		    "\tcycles:   %" PRIu64 "\n",
		    i, state[i].n_allocs_completed,
		    state[i].n_frees_completed, state[i].tdelta);
		td_total += state[i].tdelta;
	}
	fprintf(stderr, "td_total: %" PRIu64 "\n\n", td_total);

	return EX_OK;
}
