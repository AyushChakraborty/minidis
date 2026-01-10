#include <assert.h>
#include <stdlib.h>
#include "hashtable.h"

static void h_init(HTab *htab, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0); //check if its greater
    //than 0 and n is a power of 2
    htab->tab = (HNode **)calloc(n, sizeof(HNode *));
    //calloc() calls mmap() internally which allocates the frames
    //in memory in a lazy manner. Better than malloc() + memset()
    
    htab->mask = n-1;
    htab->size = 0;
}


static void h_insert(HTab *htab, HNode *node) {
    size_t bucket_pos = node->hcode & htab->mask;
    //above is equivalent to modulo hashing, since size N
    //is a power of 2: hash(key)%N == hash(key) & (N-1)
    
    node->next = htab->tab[bucket_pos];
    htab->tab[bucket_pos] = node;
    //insert node at the start of the bucket
    htab->size++;
}

//eq being the server side equality check function for the actual
//data of the nodes
static HNode **h_lookup(HTab *htab, HNode *key, bool (*eq)(HNode *, HNode *)) {
    if (!htab->tab) {    //empty hashtable
        return NULL;
    }
    
    //the bucket will look like this
    //*HNode -> *HNode -> *HNode -> NULL 
    
    size_t bucket_pos = key->hcode & htab->mask;
    HNode **from = &htab->tab[bucket_pos];
    for (HNode *cur; (cur = *from) != NULL; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur,key)) {
            return from;
        }
    }
    return NULL;   //could not find the key
}