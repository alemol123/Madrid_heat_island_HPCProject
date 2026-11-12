/* run_ensemble.c - MPI master-worker runner for an ensemble of independent solver runs.
 *
 * Rank 0 is the master. Ranks 1..W are workers; each runs the serial solver as a child process
 * (posix_spawn, no shell) for one job at a time. With a single rank, that rank runs every job itself.
 *
 * Job list (text file, one job per line, '#' starts a comment):
 *     name  city_file  steps  [extra solver arguments...]
 *
 * Scheduling modes (--mode):
 *     static   jobs are cut into W contiguous blocks, worker k takes block k. No messages needed to
 *              decide who runs what. Bad if the expensive jobs sit next to each other in the list.
 *     cyclic   job j goes to worker j mod W. Static as well, but spreads a sorted list.
 *     dynamic  the master hands a worker a new job whenever it reports one finished.
 *              Costs a round trip per job, balances uneven jobs automatically.
 *
 * Per job the solver is run as:
 *     SOLVER --city CITY --steps STEPS [extra] --out OUT/fields/NAME.bin
 *            --timing OUT/timing/NAME.csv --tag NAME          (stdout/stderr -> OUT/logs/NAME.log)
 * Every job has its own timing CSV, so no two processes ever write the same file.
 * With --no-fields the field goes to /dev/null (benchmarks of scheduling only).
 *
 * Outputs: OUT/summary.csv (one row per job: who ran it, start/end, status) and, with --result-csv,
 * one row appended with the whole-run numbers (wall time, efficiency, imbalance).
 *
 * Build (on a compute node):   mpicc -O2 -Wall -Wextra -o build/run_ensemble tools/run_ensemble.c
 * Run:   srun --mpi=pmix build/run_ensemble --jobs data/sweep/jobs_science.txt \
 *            --solver build/ens/heat_serial --mode dynamic --outdir results/ens_science
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mpi.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

enum { TAG_HELLO = 1, TAG_JOB = 2, TAG_RESULT = 3 };
enum { MODE_STATIC, MODE_CYCLIC, MODE_DYNAMIC };
#define MAX_EXTRA 24

typedef struct {
    char *name, *city, *steps;
    char *extra[MAX_EXTRA];
    int nextra;
} Job;

typedef struct {
    const char *jobs_file, *solver, *outdir, *result_csv, *tag;
    int mode, no_fields;
} Config;

static const char *mode_name(int m) { return m == MODE_STATIC ? "static" : m == MODE_CYCLIC ? "cyclic" : "dynamic"; }

static void die(const char *msg) {
    fprintf(stderr, "run_ensemble: %s\n", msg);
    MPI_Abort(MPI_COMM_WORLD, 2);
    exit(2);
}

static void mkdir_p(const char *path) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(buf, 0755); *p = '/'; }
    }
    mkdir(buf, 0755);
}

/* ------------------------------------------------------------ job list */
static int read_jobs(const char *path, Job **out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int cap = 64, n = 0;
    Job *jobs = malloc(cap * sizeof(Job));
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *save = NULL, *tok[MAX_EXTRA + 3];
        int nt = 0;
        for (char *t = strtok_r(line, " \t\r\n", &save); t && nt < MAX_EXTRA + 3; t = strtok_r(NULL, " \t\r\n", &save))
            tok[nt++] = strdup(t);
        if (nt == 0) continue;
        if (nt < 3) { fclose(f); free(jobs); return -2; }
        if (n == cap) { cap *= 2; jobs = realloc(jobs, cap * sizeof(Job)); }
        Job *j = &jobs[n++];
        j->name = tok[0]; j->city = tok[1]; j->steps = tok[2];
        j->nextra = nt - 3;
        for (int k = 0; k < j->nextra; k++) j->extra[k] = tok[3 + k];
    }
    fclose(f);
    *out = jobs;
    return n;
}

/* ------------------------------------------------------------ run one job */
/* Returns the exit status (0 = ok, 128+sig if killed, -1 if it could not be started). */
static int run_job(const Config *c, const Job *j) {
    char out[1024], timing[1024], log[1024];
    if (c->no_fields) snprintf(out, sizeof out, "/dev/null");
    else snprintf(out, sizeof out, "%s/fields/%s.bin", c->outdir, j->name);
    snprintf(timing, sizeof timing, "%s/timing/%s.csv", c->outdir, j->name);
    snprintf(log, sizeof log, "%s/logs/%s.log", c->outdir, j->name);

    char *argv[16 + MAX_EXTRA];
    int a = 0;
    argv[a++] = (char *)c->solver;
    argv[a++] = "--city";   argv[a++] = j->city;
    argv[a++] = "--steps";  argv[a++] = j->steps;
    for (int k = 0; k < j->nextra; k++) argv[a++] = j->extra[k];
    argv[a++] = "--out";    argv[a++] = out;
    argv[a++] = "--timing"; argv[a++] = timing;
    argv[a++] = "--tag";    argv[a++] = j->name;
    argv[a] = NULL;

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid;
    int rc = posix_spawn(&pid, c->solver, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    int st = 0;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}

/* Run job `id`, fill the 4-number result {id, status, start, end} (times relative to t0). */
static void do_job(const Config *c, const Job *jobs, int id, double t0, double res[4]) {
    res[0] = id;
    res[2] = MPI_Wtime() - t0;
    res[1] = run_job(c, &jobs[id]);
    res[3] = MPI_Wtime() - t0;
}

/* Which worker (0-based) owns job j under a static mode. */
static int static_owner(int mode, int j, int njobs, int W) {
    if (mode == MODE_CYCLIC) return j % W;
    for (int k = 0; k < W; k++)                       /* block k = jobs [k*N/W, (k+1)*N/W) */
        if (j < (int)(((long)(k + 1) * njobs) / W)) return k;
    return W - 1;
}

static void append_result(const Config *c, const char *mode, int nodes, int ranks, int W, int njobs, double wall,
                          double sum, double bmin, double bmax, double imbalance, int failed) {
    if (!c->result_csv) return;
    int fresh = access(c->result_csv, F_OK) != 0;
    FILE *g = fopen(c->result_csv, "a");
    if (!g) return;
    const char *sj = getenv("SLURM_JOB_ID");
    double speedup = sum / wall;
    if (fresh)
        fprintf(g, "tag,mode,nodes,ranks,workers,jobs,wall_s,sum_job_s,speedup,efficiency,busy_min_s,busy_max_s,imbalance,failed,slurm_jobid,jobs_file\n");
    fprintf(g, "%s,%s,%d,%d,%d,%d,%.4f,%.4f,%.3f,%.4f,%.4f,%.4f,%.4f,%d,%s,%s\n", c->tag, mode, nodes, ranks, W, njobs,
            wall, sum, speedup, speedup / W, bmin, bmax, imbalance, failed, sj ? sj : "", c->jobs_file);
    fclose(g);
}

/* ------------------------------------------------------------ main */
int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    Config c = {NULL, NULL, NULL, NULL, "ensemble", MODE_DYNAMIC, 0};
    for (int i = 1; i < argc; i++) {
        const char *s = argv[i];
        if (!strcmp(s, "--jobs") && i + 1 < argc) c.jobs_file = argv[++i];
        else if (!strcmp(s, "--solver") && i + 1 < argc) c.solver = argv[++i];
        else if (!strcmp(s, "--outdir") && i + 1 < argc) c.outdir = argv[++i];
        else if (!strcmp(s, "--result-csv") && i + 1 < argc) c.result_csv = argv[++i];
        else if (!strcmp(s, "--tag") && i + 1 < argc) c.tag = argv[++i];
        else if (!strcmp(s, "--no-fields")) c.no_fields = 1;
        else if (!strcmp(s, "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            if (!strcmp(m, "static")) c.mode = MODE_STATIC;
            else if (!strcmp(m, "cyclic")) c.mode = MODE_CYCLIC;
            else if (!strcmp(m, "dynamic")) c.mode = MODE_DYNAMIC;
            else die("--mode must be static, cyclic or dynamic");
        } else {
            if (rank == 0) fprintf(stderr, "usage: %s --jobs FILE --solver PATH --outdir DIR [--mode static|cyclic|dynamic] "
                                           "[--no-fields] [--result-csv FILE] [--tag LABEL]\n", argv[0]);
            MPI_Finalize();
            return 2;
        }
    }
    if (!c.jobs_file || !c.solver || !c.outdir) die("--jobs, --solver and --outdir are required");

    Job *jobs = NULL;
    int njobs = read_jobs(c.jobs_file, &jobs);
    if (njobs == -1) die("cannot open the job file");
    if (njobs == -2) die("a job line has fewer than 3 fields (name city steps)");
    if (njobs == 0) die("the job file has no jobs");
    if (access(c.solver, X_OK) != 0) die("solver is not executable (build it first)");

    if (rank == 0) {
        char d[1100];
        mkdir_p(c.outdir);
        snprintf(d, sizeof d, "%s/fields", c.outdir);  mkdir_p(d);
        snprintf(d, sizeof d, "%s/timing", c.outdir);  mkdir_p(d);
        snprintf(d, sizeof d, "%s/logs", c.outdir);    mkdir_p(d);
    }

    /* number of nodes = number of ranks that are first on their node */
    MPI_Comm node;
    MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &node);
    int nrank, lead, nodes = 0;
    MPI_Comm_rank(node, &nrank);
    lead = (nrank == 0);
    MPI_Reduce(&lead, &nodes, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    char host[MPI_MAX_PROCESSOR_NAME] = {0};
    int hlen = 0;
    MPI_Get_processor_name(host, &hlen);

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();
    int W = size > 1 ? size - 1 : 1;                 /* number of workers */

    /* ---------------- single rank: run everything here ---------------- */
    if (size == 1) {
        double *rec = malloc((size_t)njobs * 4 * sizeof(double));
        for (int j = 0; j < njobs; j++) do_job(&c, jobs, j, t0, rec + 4 * j);
        double wall = MPI_Wtime() - t0;
        int failed = 0;
        double sum = 0;
        char path[1100];
        snprintf(path, sizeof path, "%s/summary.csv", c.outdir);
        FILE *f = fopen(path, "w");
        if (f) fprintf(f, "job,name,worker,host,start_s,end_s,elapsed_s,status\n");
        for (int j = 0; j < njobs; j++) {
            double *r = rec + 4 * j;
            sum += r[3] - r[2];
            failed += (r[1] != 0);
            if (f) fprintf(f, "%d,%s,0,%s,%.6f,%.6f,%.6f,%d\n", j, jobs[j].name, host, r[2], r[3], r[3] - r[2], (int)r[1]);
        }
        if (f) fclose(f);
        printf("ENSEMBLE mode=serial ranks=1 workers=1 nodes=1 jobs=%d wall=%.3f sum_job=%.3f failed=%d\n", njobs, wall, sum, failed);
        append_result(&c, "serial", 1, 1, 1, njobs, wall, sum, sum, sum, 1.0, failed);
        MPI_Finalize();
        return failed ? 1 : 0;
    }

    /* ---------------- workers ---------------- */
    if (rank != 0) {
        MPI_Send(host, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, 0, TAG_HELLO, MPI_COMM_WORLD);
        double res[4];
        if (c.mode == MODE_DYNAMIC) {
            for (;;) {
                int id;
                MPI_Recv(&id, 1, MPI_INT, 0, TAG_JOB, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                if (id < 0) break;
                do_job(&c, jobs, id, t0, res);
                MPI_Send(res, 4, MPI_DOUBLE, 0, TAG_RESULT, MPI_COMM_WORLD);
            }
        } else {
            for (int j = 0; j < njobs; j++) {
                if (static_owner(c.mode, j, njobs, W) != rank - 1) continue;
                do_job(&c, jobs, j, t0, res);
                MPI_Send(res, 4, MPI_DOUBLE, 0, TAG_RESULT, MPI_COMM_WORLD);
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Finalize();
        return 0;
    }

    /* ---------------- master ---------------- */
    char (*whost)[MPI_MAX_PROCESSOR_NAME] = calloc((size_t)size, MPI_MAX_PROCESSOR_NAME);
    for (int w = 1; w < size; w++)
        MPI_Recv(whost[w], MPI_MAX_PROCESSOR_NAME, MPI_CHAR, w, TAG_HELLO, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    double *rec = calloc((size_t)njobs * 4, sizeof(double));
    int *owner = malloc((size_t)njobs * sizeof(int));
    for (int j = 0; j < njobs; j++) owner[j] = -1;

    int next = 0, received = 0;
    if (c.mode == MODE_DYNAMIC) {
        for (int w = 1; w < size; w++) {
            int id = next < njobs ? next++ : -1;
            MPI_Send(&id, 1, MPI_INT, w, TAG_JOB, MPI_COMM_WORLD);
        }
    }
    while (received < njobs) {
        int flag = 0;
        MPI_Status st;
        MPI_Iprobe(MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &flag, &st);
        if (!flag) { usleep(200); continue; }        /* sleep instead of spinning on the master's core */
        double res[4];
        MPI_Recv(res, 4, MPI_DOUBLE, st.MPI_SOURCE, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        int id = (int)res[0];
        memcpy(rec + 4 * id, res, sizeof res);
        owner[id] = st.MPI_SOURCE;
        received++;
        if (c.mode == MODE_DYNAMIC) {
            int nid = next < njobs ? next++ : -1;
            MPI_Send(&nid, 1, MPI_INT, st.MPI_SOURCE, TAG_JOB, MPI_COMM_WORLD);
        }
    }
    double wall = MPI_Wtime() - t0;
    MPI_Barrier(MPI_COMM_WORLD);

    /* ---------------- report ---------------- */
    double *busy = calloc((size_t)size, sizeof(double));
    double sum = 0;
    int failed = 0;
    char path[1100];
    snprintf(path, sizeof path, "%s/summary.csv", c.outdir);
    FILE *f = fopen(path, "w");
    if (f) fprintf(f, "job,name,worker,host,start_s,end_s,elapsed_s,status\n");
    for (int j = 0; j < njobs; j++) {
        double *r = rec + 4 * j, el = r[3] - r[2];
        busy[owner[j]] += el;
        sum += el;
        failed += (r[1] != 0);
        if (f) fprintf(f, "%d,%s,%d,%s,%.6f,%.6f,%.6f,%d\n", j, jobs[j].name, owner[j], whost[owner[j]], r[2], r[3], el, (int)r[1]);
    }
    if (f) fclose(f);
    double bmin = 1e300, bmax = 0, bsum = 0;
    for (int w = 1; w < size; w++) {
        if (busy[w] < bmin) bmin = busy[w];
        if (busy[w] > bmax) bmax = busy[w];
        bsum += busy[w];
    }
    double speedup = sum / wall, eff = speedup / W, imbalance = bmax / (bsum / W);
    printf("ENSEMBLE mode=%s ranks=%d workers=%d nodes=%d jobs=%d wall=%.3f sum_job=%.3f speedup=%.2f "
           "efficiency=%.3f busy_min=%.3f busy_max=%.3f imbalance=%.3f failed=%d\n",
           mode_name(c.mode), size, W, nodes, njobs, wall, sum, speedup, eff, bmin, bmax, imbalance, failed);

    append_result(&c, mode_name(c.mode), nodes, size, W, njobs, wall, sum, bmin, bmax, imbalance, failed);
    MPI_Finalize();
    return failed ? 1 : 0;
}
