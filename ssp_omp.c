#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#define MAXLEN 256

typedef struct {
    int i, j;
    int val; // overlap length
} BestPair;

/* Calcula o overlap de sufixo(a) com prefixo(b). Retorna comprimento do overlap. */
static inline int overlap_len(const char *a, const char *b) {
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    int max = (la < lb ? la : lb);
    for (int k = max; k > 0; --k) {
        if (strncmp(a + la - k, b, k) == 0) return k;
    }
    return 0;
}

/* Mescla strings a e b sabendo que overlap(a,b)=k. Retorna malloc'ed string. */
static char* merge_with_overlap(const char *a, const char *b, int k) {
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    int newlen = la + lb - k;
    char *res = (char*)malloc((size_t)newlen + 1);
    memcpy(res, a, (size_t)la);
    memcpy(res + la, b + k, (size_t)(lb - k));
    res[newlen] = '\0';
    return res;
}

/* Escolhe o melhor par (i,j) com maior overlap entre strings ativas. */
static BestPair find_best_pair(char **strings, int n, unsigned char *active) {
    BestPair global = { -1, -1, -1 };

    #pragma omp parallel
    {
        BestPair local = { -1, -1, -1 };
        #pragma omp for schedule(static) nowait
        for (int i = 0; i < n; ++i) {
            if (!active[i]) continue;
            for (int j = 0; j < n; ++j) {
                if (i == j || !active[j]) continue;
                int ov = overlap_len(strings[i], strings[j]);
                if (ov > local.val) {
                    local.val = ov; local.i = i; local.j = j;
                }
            }
        }
        #pragma omp critical
        {
            if (local.val > global.val) global = local;
        }
    }

    return global;
}

int main() {
    int n; 
    if (scanf("%d", &n) != 1 || n <= 0) return 0;
    char **strings = (char**)malloc(sizeof(char*) * n);
    for (int i = 0; i < n; ++i) {
        char buf[MAXLEN+1];
        if (scanf("%256s", buf) != 1) { n = i; break; }
        strings[i] = strdup(buf);
    }
    unsigned char *active = (unsigned char*)malloc((size_t)n);
    for (int i = 0; i < n; ++i) active[i] = 1;
    int alive = n;

    double t0 = omp_get_wtime();

    while (alive > 1) {
        BestPair bp = find_best_pair(strings, n, active);

        // Caso degenerado: se não achou par válido, junte dois quaisquer com overlap 0
        if (bp.i < 0 || bp.j < 0) {
            int a = -1, b = -1;
            for (int i = 0; i < n && a < 0; ++i) if (active[i]) a = i;
            for (int j = n - 1; j >= 0 && b < 0; --j) if (active[j] && j != a) b = j;
            if (a < 0 || b < 0) break; // nada a fazer
            bp.i = a; bp.j = b; bp.val = 0;
        }

        char *merged = merge_with_overlap(strings[bp.i], strings[bp.j], bp.val);

        free(strings[bp.i]);           // libera a antiga i
        strings[bp.i] = merged;        // substitui por mesclada

        free(strings[bp.j]);           // libera j e zera ponteiro para evitar double free
        strings[bp.j] = NULL;
        active[bp.j] = 0;

        alive--;
    }

    double t1 = omp_get_wtime();

    for (int i = 0; i < n; ++i) {
        if (active[i]) { printf("%s\n", strings[i]); break; }
    }
    fprintf(stderr, "Tempo total: %.6f s\n", t1 - t0);

    for (int i = 0; i < n; ++i) {
        if (strings[i]) free(strings[i]);  // só libera se não for NULL
    }
    free(strings);
    free(active);

    return 0;
}
