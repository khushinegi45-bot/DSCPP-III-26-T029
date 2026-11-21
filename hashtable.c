#include "hashtable.h"
#include <stdlib.h>
#include <string.h>

typedef struct Entry { long ikey; char *skey; void *value; struct Entry *next; } Entry;
struct HashTable { int is_str; size_t nbuckets, size; Entry **buckets; };

static size_t hash_int(long k) {
    unsigned long long x = (unsigned long long)(unsigned long)k;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;       /* mixing step */
    return (size_t)x;
}
static size_t hash_str(const char *s) {                             /* FNV-1a */
    size_t h = 1469598103934665603UL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211UL; }
    return h;
}
static char* dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *c = (char*)malloc(n);
    if (c) memcpy(c, s, n);
    return c;
}
static HashTable* create(int is_str, size_t buckets) {
    HashTable *h = (HashTable*)malloc(sizeof(HashTable));
    if (!h) return NULL;
    h->is_str = is_str; h->size = 0; h->nbuckets = buckets < 16 ? 16 : buckets;
    h->buckets = (Entry**)calloc(h->nbuckets, sizeof(Entry*));
    if (!h->buckets) { free(h); return NULL; }
    return h;
}
HashTable* ht_create_int(size_t buckets) { return create(0, buckets); }
HashTable* ht_create_str(size_t buckets) { return create(1, buckets); }
size_t ht_size(const HashTable *h) { return h->size; }

static size_t index_of(const HashTable *h, long ik, const char *sk) {
    return (h->is_str ? hash_str(sk) : hash_int(ik)) % h->nbuckets;
}
static int same(const HashTable *h, const Entry *e, long ik, const char *sk) {
    return h->is_str ? strcmp(e->skey, sk) == 0 : e->ikey == ik;
}

static void grow(HashTable *h) {                                    /* double the buckets, rehash every entry */
    size_t nn = h->nbuckets * 2;
    Entry **nb = (Entry**)calloc(nn, sizeof(Entry*));
    if (!nb) return;
    for (size_t i = 0; i < h->nbuckets; i++) {
        Entry *e = h->buckets[i];
        while (e) {
            Entry *next = e->next;
            size_t j = (h->is_str ? hash_str(e->skey) : hash_int(e->ikey)) % nn;
            e->next = nb[j]; nb[j] = e;
            e = next;
        }
    }
    free(h->buckets);
    h->buckets = nb; h->nbuckets = nn;
}

static int put(HashTable *h, long ik, const char *sk, void *value) {
    size_t i = index_of(h, ik, sk);
    for (Entry *e = h->buckets[i]; e; e = e->next)
        if (same(h, e, ik, sk)) { e->value = value; return 0; }
    Entry *e = (Entry*)malloc(sizeof(Entry));
    if (!e) return -1;
    e->ikey = ik; e->value = value; e->skey = NULL;
    if (h->is_str) { e->skey = dup_str(sk); if (!e->skey) { free(e); return -1; } }
    e->next = h->buckets[i]; h->buckets[i] = e;
    h->size++;
    if (h->size * 4 > h->nbuckets * 3) grow(h);                     /* load factor above 0.75 */
    return 1;
}
static void* get(const HashTable *h, long ik, const char *sk) {
    for (Entry *e = h->buckets[index_of(h, ik, sk)]; e; e = e->next)
        if (same(h, e, ik, sk)) return e->value;
    return NULL;
}
static void* rem(HashTable *h, long ik, const char *sk) {
    Entry **pp = &h->buckets[index_of(h, ik, sk)];
    while (*pp) {
        Entry *e = *pp;
        if (same(h, e, ik, sk)) {
            void *v = e->value;
            *pp = e->next;
            free(e->skey); free(e);
            h->size--;
            return v;
        }
        pp = &e->next;
    }
    return NULL;
}

int   ht_put_int(HashTable *h, long key, void *v)      { return put(h, key, NULL, v); }
void* ht_get_int(const HashTable *h, long key)         { return get(h, key, NULL); }
void* ht_remove_int(HashTable *h, long key)            { return rem(h, key, NULL); }
int   ht_put_str(HashTable *h, const char *key, void *v) { return put(h, 0, key, v); }
void* ht_get_str(const HashTable *h, const char *key)  { return get(h, 0, key); }
void* ht_remove_str(HashTable *h, const char *key)     { return rem(h, 0, key); }

void ht_destroy(HashTable *h, ht_free_fn free_value) {
    if (!h) return;
    for (size_t i = 0; i < h->nbuckets; i++) {
        Entry *e = h->buckets[i];
        while (e) {
            Entry *next = e->next;
            if (free_value && e->value) free_value(e->value);
            free(e->skey); free(e);
            e = next;
        }
    }
    free(h->buckets);
    free(h);
}
