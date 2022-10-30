#pragma once

#include <stdbool.h>

/**
   Implement a B-tree that holds a set of integers. The tree must
   support insertion, deletion, and iteration over all values in it.
 */
struct btree;

/* Allocate an empty btree with node sizes between L and 2*L. */
struct btree* btree_alloc(unsigned int L);
/* Release all memory allocated to @t. */
void btree_free(struct btree *t);

/* Insert a value @x into @t. Inserting a value already present in @t
   must be a no-op. */
void btree_insert(struct btree *t, int x);
/* Delete a value @x from @t. Deleting and item not present in @t
   must be a no-op. */
void btree_delete(struct btree *t, int x);
/* Test whether @t contains @x, or not. */
bool btree_contains(struct btree *t, int x);

/**
   Implement iteration over all values contained in a B-tree.
   Iterating over a B-tree must return all values in it, sorted
   from smallest to largest.

   Iterators will not be used concurrently with btree_insert()
   and btree_remove().
 */
struct btree_iter;

/* Create an iterator over @t. */
struct btree_iter* btree_iter_start(struct btree *t);
/* Release all memory allocated to %t. */
void btree_iter_end(struct btree_iter *i);

/* Advance the iterator @i. If @i can be advanced, put the new
   value to @x, and return true. Otherwise, return false. */
bool btree_iter_next(struct btree_iter *i, int *x);
