#include <assert.h>
#include <cstdint>
#include "avl.h"

static uint32_t max(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? rhs : lhs;
}

static void avl_update(AVLNode *node) {
    node->height = 1 + max(avl_height(node->left), avl_height(node->right));
    node->cnt = 1 + max(avl_height(node->left), avl_height(node->right));
}

static AVLNode *rot_left(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->right;
    AVLNode *to_hop = new_node->left;
    
    new_node->left = node;
    node->parent = new_node;
    
    node->right = to_hop;
    if (to_hop) {
        to_hop->parent = node;
    }
    new_node->parent = parent;
    avl_update(node);
    avl_height(new_node);
    
    return new_node;
}


static AVLNode *rot_right(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->left;
    AVLNode *to_hop = new_node->right;
    
    new_node->right = node;
    node->parent = new_node;
    
    node->left = to_hop;
    if (to_hop) {
        to_hop->parent = node;
    }
    new_node->parent = parent;
    avl_update(node);
    avl_height(new_node);
    
    return new_node; 
}

//left subtree is taller by 2
static AVLNode *avl_fix_left(AVLNode *node) {
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = rot_left(node->left);
    }
    return rot_right(node);
}

//right subtree is taller by 2
static AVLNode *avl_fix_right(AVLNode *node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = rot_right(node->left);
    }
    return rot_left(node);
}

AVLNode *avl_fix(AVLNode *node) {
    //fix imbalanced nodes from the "node" till the root, so done iteratively
    while (true) {
        AVLNode **from = &node;
        AVLNode *parent = node->parent;
        
        if (parent) {
            from = node == parent->left ? &parent->left : &parent->right;
        }
        
        uint32_t left_height = avl_height(node->left);
        uint32_t right_height = avl_height(node->right);
        
        if (left_height-right_height == 2) {
            *from = avl_fix_left(node);
        }else if (right_height-left_height == 2) {
            *from = avl_fix_right(node);
        }
        
        if (!parent) {
            return *from;     //*from now is basically the root, so return that
        }
        avl_update(node);     //do the auxiliary update for the node, in this case, update height and count 
        //of this node's subtree
        node = parent;
    }
}

//case when one of node's child is NULL
//this function returns back the root of the updated tree
static AVLNode *avl_del_easy(AVLNode *node) {
    assert(!node->left || !node->right);
    AVLNode *child = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;
    
    if (child) {
        child->parent = parent;
    }
    
    if (!parent) {   //case where root is to be removed
        return child;
    }
    
    AVLNode **from = parent->left == node ? &parent->left : &parent->right;
    *from = child;
    return avl_fix(parent);
}

AVLNode *avl_del(AVLNode *node) {
    //if node only has 0 or 1 children
    if (!node->left || !node->right) {
        return avl_del_easy(node);
    }
    
    //else the case where the node to be deleted has both of its children, so find the successor
    AVLNode *victim = node->right;
    while (victim->left) {
        victim = victim->left;
    }
    
    //detatch the successor
    AVLNode *root = avl_del_easy(victim);
    
    *victim = *node; //overwrite victim's ptrs with node's ptrs, hence making the victim take the node's spot
    if (victim->left) {
        victim->left->parent = victim;
    }if (victim->right) {
        victim->right->parent = victim;
    }
    
    AVLNode **from = &root;
    AVLNode *parent = node->parent;
    
    if (parent) {
        from = parent->left == node ? &parent->left : &parent->right;
    }
    *from = victim;
    return root;
}