#ifndef __HASH_H__
#define __HASH_H__

#define LH_LOAD_MULT    256


struct op_hash_node_st {
    void *data;
    struct op_hash_node_st *next;
};


struct op_hash_st {
    struct op_hash_node_st **b;
	int (*comp) (const void *, const void *);
	
	unsigned long (*hash) (const void *);
    unsigned int num_nodes;
    unsigned int num_alloc_nodes;
    unsigned int p;
    unsigned int pmax;
    unsigned long up_load;      /* load times 256 */
    unsigned long down_load;    /* load times 256 */
    unsigned long num_items;
    unsigned long num_expands;
    unsigned long num_expand_reallocs;
    unsigned long num_contracts;
    unsigned long num_contract_reallocs;
    unsigned long num_hash_calls;
    unsigned long num_comp_calls;
    unsigned long num_insert;
    unsigned long num_replace;
    unsigned long num_delete;
    unsigned long num_no_delete;
    unsigned long num_retrieve;
    unsigned long num_retrieve_miss;
    unsigned long num_hash_comps;
    int error;
};




struct op_hash_st *op_hash_new(unsigned long (*hash) (const void *), int (*compare) (const void *, const void *));
void op_hash_free(struct op_hash_st *lh);
void *op_hash_insert(struct op_hash_st *lh, void *data);
void *op_hash_delete(struct op_hash_st *lh, const void *data);
void *op_hash_retrieve(struct op_hash_st *lh, const void *data);
void op_hash_doall(struct op_hash_st *lh, void (*search) (void *));
unsigned long op_hash_num_items(const struct op_hash_st *lh);





#endif
