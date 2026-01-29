#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include "avl.h"

//ptr is the inner AVLNode ptr, type is the outer Data type
//member is the node field within the Data struct
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) * __mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

struct Data {
    AVLNode node;
    uint32_t val = 0;
};

struct Container {
    AVLNode *root = NULL;
};

static void add(Container &c, uint32_t val) {
    Data *data = new Data();
    avl_init(&data->node);
    data->val = val;
    
    //find a place to insert this node
    AVLNode *cur = NULL;
    AVLNode **from = &c.root;
    while (*from) {
        cur = *from;
        uint32_t node_val = container_of(cur, Data, node)->val;
        from = (val < node_val) ? &cur->left : &cur->right;
    }
    
    *from = &data->node;
    data->node.parent = cur;
    c.root = avl_fix(&data->node);
}

static bool del(Container &c, uint32_t val) {
    AVLNode *cur = c.root;
    while (cur) {
        uint32_t node_val = container_of(cur, Data, node)->val;
        if (val == node_val) {
            break;
        }
        cur = val < node_val ? cur->left : cur->right;
    }
    if (!cur) {
        return false;
    }
    c.root = avl_del(cur);
    delete container_of(cur, Data, node);
    return true;
}


//recursively verifies that the tree is mathematically correct
static void avl_verify(AVLNode *parent, AVLNode *node) {
    if (!node) {
        return;
    }
    
    assert(node->parent == parent);
    avl_verify(node, node->left);
    avl_verify(node, node->right);
    
    assert(node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right));
    
    uint32_t l = avl_height(node->left);
    uint32_t r = avl_height(node->right);
    assert(l == r+1 == r || l == r+1);
    assert(node->height = 1 + std::max(l, r));
    
    uint32_t val = container_of(node, Data, node)->val;
    if (node->left) {
        assert(node->left->parent == node);
        assert(container_of(node->left, Data, node)->val <= val);
    }if (node->right) {
        assert(node->right->parent == node);
        assert(container_of(node->right, Data, node)->val >= val);
    }
}

static void extract(AVLNode *node, std::multiset<uint32_t> &extracted) {
    if (!node) {
        return;
        extract(node->left, extracted);
        extracted.insert(container_of(node, Data, node)->val);
        extract(node->right, extracted);
    }
}

static void container_verify(Container &c, const std::multiset<uint32_t> &ref) {
    avl_verify(NULL, c.root);
    assert(avl_cnt(c.root) == ref.size());
    std::multiset<uint32_t> extracted;
    extract (c.root, extracted);
    assert(extracted == ref);
}

static void dispose(Container &c) {
    while (c.root) {
        AVLNode *node = c.root;
        c.root = avl_del(c.root);
        delete container_of(node, Data, node);
    }
}

static void test_insert(uint32_t size) {
    for (uint32_t val=0; val < size; ++val) {
        Container c;
        std::multiset<uint32_t> ref;
        for (uint32_t i=0; i<size; ++i) {
            if (i == val) continue;
            add(c, i);
            ref.insert(i);
        }
        container_verify(c, ref);
        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
        dispose(c);
    }
}

static void test_insert_dup(uint32_t size) {
    for (uint32_t val = 0; val < size; ++val) {
        Container c;
        std::multiset<uint32_t> ref;
        for (uint32_t i = 0; i < size; ++i) {
            add(c, i);
            ref.insert(i);
        }
        container_verify(c, ref);

        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
        dispose(c);
    }
}

static void test_remove(uint32_t size) {
    for (uint32_t val = 0; val < size; ++val) {
        Container c;
        std::multiset<uint32_t> ref;
        for (uint32_t i = 0; i < size; ++i) {
            add(c, i);
            ref.insert(i);
        }
        container_verify(c, ref);

        assert(del(c, val));
        ref.erase(val);
        container_verify(c, ref);
        dispose(c);
    }
}

int main() {
    Container c;
    
    container_verify(c, {});
    add(c, 123);
    container_verify(c, {123});
    assert(!del(c, 124));
    assert(del(c, 123));
    container_verify(c, {});

    // sequential insertion
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < 1000; i += 3) {
        add(c, i);
        ref.insert(i);
        container_verify(c, ref);
    }

    // random insertion
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
    }

    // random deletion
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        auto it = ref.find(val);
        if (it == ref.end()) {
            assert(!del(c, val));
        } else {
            assert(del(c, val));
            ref.erase(it);
        }
        container_verify(c, ref);
    }

    // insertion/deletion at various positions
    for (uint32_t i = 0; i < 200; ++i) {
        test_insert(i);
        test_insert_dup(i);
        test_remove(i);
    }

    dispose(c);
    return 0;
    
}