/*
 * The Great Computer Language Shootout
 * http://shootout.alioth.debian.org/
 *
 * contributed by Christoph Bauer
 *
 * adapted by Andreas Prell
 *
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "async.h"
#include "tasking.h"
#include "wtime.h"

//#define LOOPTASKS

#define PI 3.141592653589793
#define solar_mass (4 * PI * PI)
#define days_per_year 365.24
#define softening 0.01

typedef struct planet {
	double x, y, z;
	double vx, vy, vz;
	double mass;
} Planet;

Planet *bodies, *bodies2;

#ifdef LOOPTASKS

void advance(int nbodies, int tick, double dt)
{
	Planet b, *from, *to;
	long i, j;

	if (tick) {
		from = bodies2;
		to = bodies;
	} else {
		from = bodies;
		to = bodies2;
	}

	ASYNC_FOR (i) {
		memcpy(&b, &from[i], sizeof(Planet));
		for (j = 0; j < nbodies; j++) {
			Planet *b2 = &from[j];
			if (j == i) continue;
			double dx = b.x - b2->x;
			double dy = b.y - b2->y;
			double dz = b.z - b2->z;
			double dist = sqrt(dx * dx + dy * dy + dz * dz + softening);
			double mag = dt / (dist * dist * dist);
			b.vx -= dx * b2->mass * mag;
			b.vy -= dy * b2->mass * mag;
			b.vz -= dz * b2->mass * mag;
		}
		b.x += dt * b.vx;
		b.y += dt * b.vy;
		b.z += dt * b.vz;

		memcpy(&to[i], &b, sizeof(Planet));
	}
}

DEFINE_ASYNC(advance, (int, int, double));

#else

void advance(int i, int nbodies, int tick, double dt)
{
	Planet b, *from, *to;
	int j;

	if (tick) {
		from = bodies2;
		to = bodies;
	} else {
		from = bodies;
		to = bodies2;
	}

	memcpy(&b, &from[i], sizeof(Planet));

	for (j = 0; j < nbodies; j++) {
		Planet *b2 = &from[j];
		if (j == i) continue;
		double dx = b.x - b2->x;
		double dy = b.y - b2->y;
		double dz = b.z - b2->z;
		double dist = sqrt(dx * dx + dy * dy + dz * dz + softening);
		double mag = dt / (dist * dist * dist);
		b.vx -= dx * b2->mass * mag;
		b.vy -= dy * b2->mass * mag;
		b.vz -= dz * b2->mass * mag;
	}
	b.x += dt * b.vx;
	b.y += dt * b.vy;
	b.z += dt * b.vz;

	memcpy(&to[i], &b, sizeof(Planet));
}

DEFINE_ASYNC(advance, (int, int, int, double));

#endif // LOOPTASKS

double energy(int nbodies, Planet *bodies)
{
	double e = 0.0;
	int i, j;

	for (i = 0; i < nbodies; i++) {
		Planet *b = &bodies[i];
		e += 0.5 * b->mass * (b->vx * b->vx + b->vy * b->vy + b->vz * b->vz);
		for (j = i + 1; j < nbodies; j++) {
			Planet *b2 = &bodies[j];
			double dx = b->x - b2->x;
			double dy = b->y - b2->y;
			double dz = b->z - b2->z;
			double dist = sqrt(dx * dx + dy * dy + dz * dz);
			e -= (b->mass * b2->mass) / dist;
		}
	}

	return e;
}

void offset_momentum(int nbodies, Planet *bodies)
{
	double px = 0.0, py = 0.0, pz = 0.0;
	int i;

	for (i = 0; i < nbodies; i++) {
		px += bodies[i].vx * bodies[i].mass;
		py += bodies[i].vy * bodies[i].mass;
		pz += bodies[i].vz * bodies[i].mass;
	}

	bodies[0].vx = - px / solar_mass;
	bodies[0].vy = - py / solar_mass;
	bodies[0].vz = - pz / solar_mass;
}

__attribute__((unused))
static inline void swap(Planet **a, Planet **b)
{
	Planet *tmp = *a;
	*a = *b;
	*b = tmp;
}

int main(int argc, char *argv[])
{
	FILE *input;
	int N, n, i;
	double start, end;

	dup2(STDOUT_FILENO, STDERR_FILENO);

	if (argc != 3) {
		// test/bodies.txt
		printf("Usage: %s <inputfile> <iters>\n", argv[0]);
		return 0;
	}

	input = fopen(argv[1], "r");
	if (!input) {
		fprintf(stderr, "Could not open file \"%s\" for reading\n", argv[1]);
		return 1;
	}

	// Number of iterations
	n = abs(atoi(argv[2]));

	// Number of bodies
	fscanf(input, "%d", &N);
	bodies = (Planet *)malloc(N * sizeof(Planet));
	bodies2 = (Planet *)malloc(N * sizeof(Planet));
	if (!bodies || !bodies2) {
		fprintf(stderr, "*** Warning: malloc failed\n");
		fclose(input);
		return 1;
	}

	// Initialize the N bodies
	for (i = 0; i < N; i++) fscanf(input, "%lf", &bodies[i].mass);
	for (i = 0; i < N; i++) fscanf(input, "%lf", &bodies[i].x);
	for (i = 0; i < N; i++) fscanf(input, "%lf", &bodies[i].y);
	for (i = 0; i < N; i++) fscanf(input, "%lf", &bodies[i].z);
	for (i = 0; i < N; i++) fscanf(input, "%lf", &bodies[i].vx);
	for (i = 0; i < N; i++) fscanf(input, "%lf", &bodies[i].vy);
	for (i = 0; i < N; i++) fscanf(input, "%lf", &bodies[i].vz);
	fclose(input);

	memcpy(bodies2, bodies, N * sizeof(Planet));

	TASKING_INIT(&argc, &argv);

	printf("Simulating %d bodies for %d time steps\n", N, n);
	offset_momentum(N, bodies);
	printf("%.10lf\n", energy(N, bodies));

	start = Wtime_msec();

	// For all time steps
	for (i = 0; i < n; i++) {
		//printf("%5d. time step\n", i+1);
#ifdef LOOPTASKS
		// Create a loop task for all bodies
		ASYNC(advance, (0, N), (N, i%2, 0.001));
#else
		int j;
		// Create a task for each body
		for (j = 0; j < N; j++) {
			ASYNC(advance, (j, N, i%2, 0.001));
			//advance(j, N, i%2, 0.001);
		}
#endif
		TASKING_BARRIER();
		//swap(&bodies, &bodies2);
	}

	//assert(tasking_tasks_exec() == N * n);
	end = Wtime_msec();

	printf("Elapsed wall time: %.2lf ms (%.2lf ms per iteration)\n", end - start, (end - start)/n);
	printf("%.10lf\n", energy(N, bodies));

	TASKING_EXIT();

	free(bodies);
	free(bodies2);

	return 0;
}
