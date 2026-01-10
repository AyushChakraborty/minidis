#pragma once

#include <stddef.h>
#include <stdint.h>

//intrusive hashtable node
struct HNode {
    HNode *next = NULL;
    uint64_t hcode = 0;  //store the hashvalue of the node's data
};

//fixed size hashtable
struct HTab {
    HNode **tab = NULL; //array of slots in hashtable
    size_t mask = 0;    //array size is 2^n - 1
    size_t size = 0; //number of keys in hashtable
};

//hashtable interface, uses 2 hashtable for progressive rehashing
struct HMap {
    HTab newer;
    HTab older;
    size_t migrate_pos = 0; //tracks the bucket currently being moved
    //for lookups, first chk newer HTab and if not found the 
    //older HTab. During insertion, move a few buckets frim older
    //to newer
};

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);