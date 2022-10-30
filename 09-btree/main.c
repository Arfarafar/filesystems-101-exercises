#include <solution.h>
#include <stdio.h>

int main()
{
	struct btree *t = btree_alloc(1);
	btree_insert(t, 0);
	btree_insert(t, 1);
	btree_insert(t, 2);
	btree_delete(t, 1);

	struct btree_iter *i = btree_iter_start(t);
	int x;
	for (;btree_iter_next(i, &x);)
		printf("%i\n", x);
	btree_iter_end(i);

	btree_free(t);
	return 0;
}
