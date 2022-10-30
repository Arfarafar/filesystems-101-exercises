#include <solution.h>
#include <stdlib.h>

struct btree
{
	/* implement me */
};

struct btree* btree_alloc(unsigned int L)
{
	(void) L;

	return NULL;
}

void btree_free(struct btree *t)
{
	(void) t;
}

void btree_insert(struct btree *t, int x)
{
	(void) t;
	(void) x;
}

void btree_delete(struct btree *t, int x)
{
	(void) t;
	(void) x;
}

bool btree_contains(struct btree *t, int x)
{
	(void) t;
	(void) x;

	return false;
}

struct btree_iter
{
};

struct btree_iter* btree_iter_start(struct btree *t)
{
	(void) t;

	return NULL;
}

void btree_iter_end(struct btree_iter *i)
{
	(void) i;
}

bool btree_iter_next(struct btree_iter *i, int *x)
{
	(void) i;
	(void) x;

	return false;
}
