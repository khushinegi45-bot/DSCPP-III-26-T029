/* Hash table with separate chaining, written in C. Integer keys (order id) or string keys (username, token). */
#ifndef TX_HASHTABLE_H
#define TX_HASHTABLE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct HashTable HashTable;
typedef void (*ht_free_fn)(void *value);

HashTable* ht_create_int(size_t buckets);
HashTable* ht_create_str(size_t buckets);
void       ht_destroy(HashTable *h, ht_free_fn free_value);   /* free_value may be NULL */
size_t     ht_size(const HashTable *h);

/* put returns 1 when a new key was added, 0 when an existing key was replaced, -1 on failure */
int   ht_put_int(HashTable *h, long key, void *value);
void* ht_get_int(const HashTable *h, long key);
void* ht_remove_int(HashTable *h, long key);                 /* returns the removed value or NULL */

int   ht_put_str(HashTable *h, const char *key, void *value);
void* ht_get_str(const HashTable *h, const char *key);
void* ht_remove_str(HashTable *h, const char *key);

#ifdef __cplusplus
}
#endif
#endif
