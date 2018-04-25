#include "includeme.h"

static struct pcache_stat pstat;
unsigned long pcache_size;

void print_pstat(struct pcache_stat *pstat)
{
	printf("ExCache Config:\n"
		"   Way: %lu\n"
		"   nr_cachelines: %lu\n"
		"   nr_cachesets: %lu\n"
		"   stride: %#lx\n"
		"   line_size: %#lx\n"
		"   total_size: %#lx\n\n",
		pstat->associativity,
		pstat->nr_cachelines, pstat->nr_cachesets,
		pstat->way_stride, pstat->cacheline_size,
		pcache_size);
}

static inline void computation_delay(void)
{

}

static char *sstr(unsigned long bytes, char *buf)
{
	static const char *fmt[] = {"B", "KB", "MB", "GB", "TB"};
	int i = 0;

	while (bytes > 1024) {
		bytes /= 1024;
		i++;
	}
	sprintf(buf, "%lu%s", bytes, fmt[i]);
	return buf;
}

/*
 * This defines how many working set are there within resident memory.
 * We assume the application work on ONE working set at a time, and
 * move to next after a certain computation.
 *
 * The larger the better we can emulate the App real resident Memory.
 * But may be very slow.
 */
#define NR_WSS			32

/*
 * How many rounds we want to walk _within_ a working set.
 * This is used to get an accurate average access time.
 *
 * The larger the better. But may be very slow.
 */
#define NR_REPEAT_EACH_WSS	128

/*
 * Make sure this is tested _without_ zerofill
 */
void test(unsigned long wss_t)
{
	int *foo;
	void *base, *base_wss;
	unsigned long max_resident_memory;
	unsigned long nr_pages;
	unsigned long i_NR_WSS, i, r;
	struct timeval start, end, diff;
	struct timeval total[NR_WSS];
	struct timeval s_total, s_first_run;
	char str[32];
	char str2[32];
	char str3[32];

	for (i = 0; i < NR_WSS; i++) {
		total[i].tv_sec = 0;
		total[i].tv_usec = 0;
	}
	s_first_run.tv_sec = 0;
	s_first_run.tv_usec = 0;
	s_total.tv_sec = 0;
	s_total.tv_usec = 0;

	/*
	 * Allocate Max Resident Memory
	 */
	max_resident_memory = NR_WSS * wss_t;
	base = malloc(max_resident_memory);
	if (!base)
		die("fail to allocate memory");

	nr_pages = wss_t >> 12;

	printf("Configure: Max_Resident: %s WWS: %s (pcache: %s). NR_WSS: %lu nr_repeat/wss: %lu\n",
		sstr(max_resident_memory, str),
		sstr(wss_t, str2),
		sstr(pcache_size, str3),
		NR_WSS, NR_REPEAT_EACH_WSS);
#if 0
	printf("Configure: Max_Resident: %#lx WWS: %#lx (pcache: %#lx). NR_WSS: %lu nr_repeat/wss: %lu\n",
		max_resident_memory, wss_t, pcache_size, NR_WSS, NR_REPEAT_EACH_WSS);
#endif

	for (i_NR_WSS = 0; i_NR_WSS < NR_WSS; i_NR_WSS++) {
		/*
		 * Shift to each wss's base address
		 */
		base_wss = base + i_NR_WSS * wss_t;

		/*
		 * How many rounds to walk within each wss
		 * This emulate the computation time of each working set.
		 *
		 * - The first time walk through the wss should have long latency
		 *   due to eviction.
		 * - The others should have stable time.
		 * - Treat them differently
		 */

		for (r = 0; r < NR_REPEAT_EACH_WSS; r++) {

			/*
			 * Finally walk through the wss
			 */
			gettimeofday(&start, NULL);
			for (i = 0; i < nr_pages; i++) {
				foo = base_wss + i * PAGE_SIZE;
				*foo = 666;
				computation_delay();
			}
			gettimeofday(&end, NULL);
			timeval_sub(&diff, &end, &start);

			if (r == 0)
				s_first_run = timeval_add(&s_first_run, &diff);
			else
				total[i_NR_WSS] = timeval_add(&total[i_NR_WSS], &diff);
		}
	}

	/*
	 * Calculate and print
	 */
	for (i_NR_WSS = 0; i_NR_WSS < NR_WSS; i_NR_WSS++)
		s_total = timeval_add(&s_total, &total[i_NR_WSS]);

	printf( "    First_touch:  [%5lu.%06lu (s)] Average of [ %12lu] round is: [%12lu (us)]\n"
		"    Others:       [%5lu.%06lu (s)] Average of [ %12lu] round is: [%12lu (us)]\n",
		s_first_run.tv_sec, s_first_run.tv_usec, NR_WSS,
		((s_first_run.tv_sec * 1000000) + s_first_run.tv_usec)/NR_WSS,
		s_total.tv_sec, s_total.tv_usec, NR_WSS * (NR_REPEAT_EACH_WSS-1),
		(((s_total.tv_sec * 1000000) + s_total.tv_usec) / (NR_WSS * (NR_REPEAT_EACH_WSS-1))));

	free(base);
}

int main(void)
{
	int ret, i;
	unsigned long wss_t;

	setbuf(stdout, NULL);

	/*
	 * Get and print pcache stat.
	 */
	ret = pcache_stat(&pstat);
	if (ret < 0) {
		/* 256MB, 64-way */
		pstat.nr_cachelines = 65536;
		pstat.nr_cachesets = 1024;
		pstat.associativity = 64;
		pstat.cacheline_size = 4096;
		pstat.way_stride = 0x400000;
	}

	pcache_size = pstat.nr_cachelines * pstat.cacheline_size;
	print_pstat(&pstat);

	/*
	 * pcache_size/wss_t
	 *
	 * 200%
	 * 100%
	 * 50%
	 * 25%
	 */
	test(pcache_size/2);
	test(pcache_size);
	test(pcache_size * 2);
	test(pcache_size * 4);
}
