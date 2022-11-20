#include <solution.h>
#include <stdlib.h>
#include <stdio.h>


struct bnode{
	int* key;  
    unsigned int Max_keys; 
    struct bnode** child; 
    int key_number;  
    char leaf;
};

struct btree
{
	struct bnode* root; 
    unsigned int L; 
    
};

struct bnode* bnode_alloc(unsigned int L, char is_leaf){

	struct bnode* Bnode = (struct bnode*)calloc(sizeof(struct bnode), 1);
	Bnode -> key = (int*)calloc(sizeof(int), 2*L);
	Bnode -> child = is_leaf ? NULL : (struct bnode**)calloc(sizeof(struct bnode*), 2*L); 
	Bnode -> leaf = is_leaf;
	Bnode -> key_number = 0;
	Bnode -> Max_keys = 2*L - 1;
	return Bnode;
}

struct btree* btree_alloc(unsigned int L)
{
	if (L == 1)
		L = 2;
	struct btree* Broot = (struct btree*)calloc(sizeof(struct btree), 1);
	Broot -> root = bnode_alloc(L, 1);
	Broot -> L = L;
	return Broot;
}


void bnode_free(struct bnode* b){
	if(b == NULL)
		return;
	free(b -> key);
	if(b -> leaf){
		free(b);
		return;
	}

	int i = 0;
	for (; i < b -> key_number; ++i)
		bnode_free((b -> child)[i]); // слева каждого ключа
	bnode_free((b -> child)[i]); //последний справа

	free(b -> child);
	free(b);
}

void btree_free(struct btree *t)
{
	if(t == NULL)
		return;

	bnode_free(t -> root);
	free(t);
}

void bnode_split_child(struct bnode* parent, struct bnode* curnode, int childnum){
	
	unsigned int L = (parent -> Max_keys + 1)/2;
	struct bnode* newnode = bnode_alloc(L, curnode -> leaf);
	newnode -> key_number = curnode -> key_number / 2;

	
    for (int i = 0; i < curnode -> key_number / 2; i++){
        newnode -> key[i] = curnode -> key[i+L];
    }
 	curnode -> key_number = L - 1;
    
    if (!curnode->leaf){
        for (int i = 0; i < (int)L; i++)
            newnode -> child[i] = curnode -> child[i+L];
    }

    for (int i = parent -> key_number; i >= childnum + 1; i--){
        parent -> child[i+1] = parent -> child[i];
   		parent -> key[i] = parent -> key[i-1];
   	}
 	parent -> child[childnum + 1] = newnode;        
  	parent -> key[childnum] = curnode->key[curnode -> key_number];
  	parent -> key_number++;

}

void bnode_insert(struct bnode* b, int x){
	if (b->leaf){
		int i;
		for(i = b -> key_number - 1; i >= 0 && b -> key[i] > x; i--)
            b -> key[i+1] = b -> key[i];
        b -> key[i+1] = x;
        b -> key_number++;
	}
	else {
		int i;
		for(i = b -> key_number - 1; i >= 0 && b -> key[i] > x; i--)
			;
		i++;
		if (b->child[i]-> key_number == (int)b->child[i]-> Max_keys){
            bnode_split_child(b, b -> child[i], i);
            if (b -> key[i] < x)
                i++;
        }
        bnode_insert(b->child[i], x);
	}
}


void btree_insert(struct btree *t, int x)
{
	if (t == NULL)
		return;

	if(t->root == NULL){
		t->root = bnode_alloc(t -> L, 1);
		t->root-> key[0] = x;
		t->root -> key_number = 1;
	}
	else if (!btree_contains(t, x)){

		if(t->root->key_number == (int)t->root->Max_keys){ //корень полон
			struct bnode* new_root = bnode_alloc(t -> L, 0);
			new_root -> child[0] = t->root;
			bnode_split_child(new_root, t -> root, 0);
			t -> root = new_root;
			if (new_root -> key[0] > x)
				bnode_insert(t->root->child[0], x);
			else
				bnode_insert(t->root->child[1], x);
		}
		else
			bnode_insert(t->root, x);
	}
}


void bnode_merge(struct bnode* b, int index)
{
    struct bnode* child = b -> child[index];
	struct bnode* neighboor = b -> child[index+1];
	int L = (b -> Max_keys + 1) / 2;

    child -> key[L-1] = b -> key[index];
    for (int i = 0; i < neighboor -> key_number; ++i)
        child -> key[i+L] = neighboor -> key[i];
 
    if (!child->leaf)
    {
        for(int i = 0 ; i <= neighboor -> key_number; ++i)
            child -> child[i+L] = neighboor -> child[i];
    }
    child-> key_number += neighboor-> key_number + 1;

    for (int i = index + 1; i < b -> key_number; ++i){
        b -> key[i-1] = b -> key[i];
        b -> child[i] = b -> child[i+1];
    }

    b -> key_number--;
 
    free(neighboor -> key);
    free(neighboor -> child);
    free(neighboor);
    return;
}

void bnode_fill(struct bnode* b, int index)
{
    if (index != 0 && b->child[index-1]->key_number >= (int)(b->Max_keys + 1) / 2){

	    struct bnode* child = b -> child[index];
	    struct bnode* neighboor = b -> child[index-1];
	 
	    for (int i = child -> key_number-1; i >= 0; --i)
	        child-> key[i+1] = child -> key[i];
	    if (!child->leaf)
	    {
	        for(int i = child->key_number; i >= 0; --i)
	            child -> child[i+1] = child -> child[i];
	    }
	    child -> key_number++;

	    child->key[0] = b -> key[index-1];
	    if(!child->leaf)
	        child->child[0] = neighboor -> child[neighboor -> key_number];
	    b -> key[index-1] = neighboor->key[neighboor -> key_number-1];
	    neighboor -> key_number--;	
    }
    else if (index != b -> key_number && b -> child[index+1]-> key_number >= (int)(b->Max_keys + 1) / 2){
        struct bnode* child = b -> child[index];
	    struct bnode* neighboor = b -> child[index+1];

	    child->key[child -> key_number] = b->key[index];
	    if (!child->leaf)
	        child->child[child-> key_number+1] = neighboor-> child[0];
	    child-> key_number ++;
	    b -> key[index] = neighboor->key[0];
	 
	    for (int i=1; i < neighboor-> key_number; ++i)
	        neighboor->key[i-1] = neighboor -> key[i];
	    if (!neighboor->leaf)
	    	for(int i=1; i <= neighboor -> key_number; ++i)
	            neighboor->child[i-1] = neighboor->child[i]; 
	    neighboor-> key_number--;
    }
    else
    {
        if (index == b -> key_number)
            index--;
        bnode_merge(b, index);
    }
    return;
}


int bnode_prevkey(struct bnode* b, int index)
{
    struct bnode* current= b->child[index];
    while (!current->leaf)
        current = current->child[current->key_number];
 
    return current->key[current->key_number-1];
}
 
int bnode_nextkey(struct bnode* b, int index)
{
    struct bnode* current= b->child[index];
    while (!current->leaf){
        current = current->child[0];
    }
	
	return current->key[0];
}
 
void bnode_delete(struct bnode* b, int x)
{
	int index = 0;
    for(; index < b -> key_number && b -> key[index] < x; ++index)
    	;

    if (index < b -> key_number && b -> key[index] == x){
        if (b -> leaf){
            for (int i = index + 1; i < b -> key_number; ++i)
		        b -> key[i-1] = b -> key[i];
		    b -> key_number--;
        }
        else{
            
		    if (b -> child[index]-> key_number >= (int)(b->Max_keys + 1) / 2)
		    {
		        b->key[index] = bnode_prevkey(b, index);
		        bnode_delete(b->child[index], b->key[index]);
		    }
		    else if  (b-> child[index+1] -> key_number >= (int)(b->Max_keys + 1) / 2)
		    {
		        b->key[index] = bnode_nextkey(b, index);
		        bnode_delete(b->child[index+1], b->key[index]);
		    }
		    else
		    {
		        bnode_merge(b, index);
		        bnode_delete(b->child[index], x);
		    }
	    }
	}
    else
    {
        if (b -> leaf)
        	return;
        int storedkeynum = b -> key_number;
        if (b -> child[index] -> key_number < (int)(b->Max_keys + 1) / 2 )
            bnode_fill(b, index);

        if (index == storedkeynum && index > b -> key_number)
            index--;
        bnode_delete(b->child[index], x);
    }
}
 



void btree_delete(struct btree *t, int x)
{
	if (t -> root == NULL)
    	return;

    if(!btree_contains(t, x))
    	return;
 
    bnode_delete(t -> root, x);

    if (t -> root -> key_number == 0)
    {
    	struct bnode* tmp = t -> root;
        if (t -> root -> leaf)
            t->root = NULL;
        else
            t->root = t->root->child[0];
 
        free(tmp -> key);
        free(tmp -> child);
        free(tmp);
    }
}

bool bnode_search(struct bnode* b, int x){
	
	if(b == NULL){
		return false;
	}

    int i = 0;
    for(; i < b -> key_number && x > b->key[i]; i++)
        ;
  
    if(i == b->key_number){
    	if(b->leaf)
    		return false;

    	return bnode_search(b->child[i], x);
    }

    if (b -> key[i] == x)
        return true;

    if (b -> leaf)
        return false;

    return bnode_search(b->child[i], x);
}

bool btree_contains(struct btree *t, int x)
{
	if(t == NULL)
		return false;
	return bnode_search(t->root, x);
}

struct btree_iter
{
	struct bnode** path;
	int cur_depth;
	int max_depth;
	int value;
	int keypos;
	int endflag;
};

void btree_depth(struct btree *t, struct btree_iter* iter){
	int i = 1;
	struct bnode* current = t -> root;
    while (!current->leaf){
        current = current->child[0];
        i++;
    }

    iter -> max_depth = i;
    iter -> path =  (struct bnode**)calloc(sizeof(struct bnode*), i);
    iter -> value = current->key[0];

    current = t -> root;
    iter -> path[0] = t -> root;
    iter -> cur_depth = 1;

    while (!current->leaf){
        current = current->child[0];
        iter -> path[iter -> cur_depth] = current;
        iter -> cur_depth++;
    }
}


void bnode_print(struct bnode* b)
{
    int i;
    for (i = 0; i < b->key_number; i++)
 	{
        if (b -> leaf == false)
            bnode_print(b -> child[i]);
        printf("%i -- traverse\n", b -> key[i]);
    }
 
    if (b -> leaf == false)
        bnode_print(b -> child[i]);
}


struct btree_iter* btree_iter_start(struct btree *t)
{
	//bnode_print(t -> root);

	struct btree_iter* Broot = (struct btree_iter*)calloc(sizeof(struct btree_iter), 1);
	btree_depth(t, Broot);
	return Broot;
}

void btree_iter_end(struct btree_iter *i)
{
	free(i -> path);
	free(i);
}


void update_value(struct btree_iter* iter){
	
	int pos = 0;
	if(iter -> path[iter -> cur_depth-1] -> leaf){
		if(iter -> keypos != iter -> path[iter -> cur_depth-1] -> key_number - 1){
			iter -> keypos++;
			iter -> value = iter -> path[iter -> cur_depth-1] -> key[iter -> keypos];
		}
		else{
			
			do {
				iter -> cur_depth--;
				if(iter -> cur_depth == 0){
					iter -> endflag = 1;
					return;
				}
				pos = 0;
					while (pos < iter -> path[iter -> cur_depth-1] -> key_number && iter -> value > iter -> path[iter -> cur_depth-1] -> key[pos]) 
			        	pos++;
		    }
			while (iter -> path[iter -> cur_depth-1] -> key_number == pos);

			

			iter -> value = iter -> path[iter -> cur_depth-1] -> key[pos];
			iter -> keypos = pos;				
		}

	}
	else{
		iter -> path[iter -> cur_depth] = iter -> path[iter -> cur_depth-1] -> child[iter -> keypos + 1];
		iter -> cur_depth++;
		iter -> keypos = 0;


		while (!iter -> path[iter -> cur_depth-1] -> leaf){
	        iter -> path[iter -> cur_depth] = iter -> path[iter -> cur_depth-1] -> child[0];
	        iter -> cur_depth++;
	    }
	
		iter -> value = iter -> path[iter -> cur_depth-1] -> key[0];
		
	}
}

bool btree_iter_next(struct btree_iter *iter, int *x)
{
	if(!iter -> endflag){
		*x = iter -> value;
		update_value(iter);
		return true;
	}

	return false;
}


