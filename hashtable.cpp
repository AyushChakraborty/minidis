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

static HNode *h_detach(HTab *htab, HNode **from) {
    HNode *node = *from;
    *from = node->next;
    htab->size--;
    return node;
}


const size_t k_rehashing_work = 128;  //max possible amt of keys
//to reallocate per rehashing stage

static void hm_help_rehashing(HMap *hmap) {
    size_t nwork = 0;
    
    while (nwork < k_rehashing_work && hmap->older.size > 0) {
        HNode **from = &hmap->older.tab[hmap->migrate_pos];
        
        if (!*from) {
            hmap->migrate_pos++;
            continue;  //slot emptied or slot already empty
           //so inc migrate_pos counter and move on
        }
        h_insert(&hmap->newer, h_detach(&hmap->older, from));
        nwork++;
    }
    //discard old table if done
    if (hmap->older.size == 0 && hmap->older.tab) {
        free(hmap->older.tab);
        hmap->older = HTab{};
    }
}

static void hm_trigger_rehashing(HMap *hmap) {
    assert(hmap->older.tab == NULL);
    hmap->older = hmap->newer;
    h_init(&hmap->newer, (hmap->newer.mask + 1)*2); //double the size now
    hmap->migrate_pos = 0;
}

const size_t k_max_load_factor = 8;
//load factor = keys/buckets, gives avg keys per bucket

//set interface
void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->newer.tab) {
        h_init(&hmap->newer, 4);
    }
    h_insert(&hmap->newer, node);
    
    //only if older table is empty, trigger the rehash given
    //the new table is full
    if (!hmap->older.tab) {
        size_t threshold = (hmap->newer.mask + 1) * k_max_load_factor;
        if (hmap->newer.size >= threshold) {
            hm_trigger_rehashing(hmap);
        }
    }
    //migrate some keys from older to newer just in case
    hm_help_rehashing(hmap);
}

//get interface
HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    hm_help_rehashing(hmap);
    HNode **from = h_lookup(&hmap->newer, key, eq); //first search in newer table
    if (!from) {
        from = h_lookup(&hmap->older, key, eq); //then search in the older table
    }
    return from ? *from : NULL;
}

//delete interface
HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    hm_help_rehashing(hmap);
    if (HNode **from = h_lookup(&hmap->newer, key, eq)) {
        return h_detach(&hmap->newer, from);
    }
    if (HNode **from = h_lookup(&hmap->older, key, eq)) {
        return h_detach(&hmap->older, from);
    }
    return NULL;
}

void hm_clear(HMap *hmap) {
    free(hmap->newer.tab);
    free(hmap->older.tab);
    *hmap = HMap{};
}

size_t hm_size(HMap *hmap) {
    return hmap->newer.size + hmap->older.size;
}