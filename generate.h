#ifndef __GENERATE_H__
#define __GENERATE_H__

struct tl_combinator;

struct tl_type {  
//  struct tl_type_methods *methods;
  char *id;
  unsigned name;
  int arity;
  int flags;
  int constructors_num;
  struct tl_combinator **constructors;
  long long params_types;
  int extra;
};

#define NODE_TYPE_TYPE 1
#define NODE_TYPE_NAT_CONST 2
#define NODE_TYPE_VAR_TYPE 3
#define NODE_TYPE_VAR_NUM 4
#define NODE_TYPE_ARRAY 5

#define MAX_COMBINATOR_VARS 64

#define NAME_VAR_NUM 0x70659eff
#define NAME_VAR_TYPE 0x2cecf817
#define NAME_INT 0xa8509bda
#define NAME_LONG 0x22076cba
#define NAME_DOUBLE 0x2210c154
#define NAME_STRING 0xb5286e24
#define NAME_VECTOR 0x1cb5c415
#define NAME_MAYBE_TRUE 0x3f9c8ef8
#define NAME_MAYBE_FALSE 0x27930a7b
#define NAME_BOOL_FALSE 0xbc799737
#define NAME_BOOL_TRUE 0x997275b5

#define FLAG_OPT_VAR (1 << 17)
#define FLAG_EXCL (1 << 18)
#define FLAG_OPT_FIELD (1 << 20)
#define FLAG_NOVAR (1 << 21)
#define FLAG_BARE 1
#define FLAGS_MASK ((1 << 16) - 1)
#define FLAG_DEFAULT_CONSTRUCTOR (1 << 25)
#define FLAG_NOCONS (1 << 1)

extern struct tl_tree_methods tl_nat_const_methods;
extern struct tl_tree_methods tl_nat_const_full_methods;
extern struct tl_tree_methods tl_pnat_const_full_methods;
extern struct tl_tree_methods tl_array_methods;
extern struct tl_tree_methods tl_type_methods;
extern struct tl_tree_methods tl_parray_methods;
extern struct tl_tree_methods tl_ptype_methods;
extern struct tl_tree_methods tl_var_num_methods;
extern struct tl_tree_methods tl_var_type_methods;
extern struct tl_tree_methods tl_pvar_num_methods;
extern struct tl_tree_methods tl_pvar_type_methods;
#define TL_IS_NAT_VAR(x) (((long)x) & 1)
#define TL_TREE_METHODS(x) (TL_IS_NAT_VAR (x) ? &tl_nat_const_methods : ((struct tl_tree *)(x))->methods)

#define DEC_REF(x) (TL_TREE_METHODS(x)->dec_ref ((void *)x))
#define INC_REF(x) (TL_TREE_METHODS(x)->inc_ref ((void *)x))
#define TYPE(x) (TL_TREE_METHODS(x)->type ((void *)x))

typedef unsigned long long tl_tree_hash_t;
struct tl_tree;

struct tl_tree_methods {
  int (*type)(struct tl_tree *T);
  int (*eq)(struct tl_tree *T, struct tl_tree *U);
  void (*inc_ref)(struct tl_tree *T);
  void (*dec_ref)(struct tl_tree *T);
};

struct tl_tree {
  int ref_cnt;
  int flags;
  //tl_tree_hash_t hash;
  struct tl_tree_methods *methods;
};
/*
struct tl_tree_nat_const {
  struct tl_tree self;
  int value;
};*/

struct tl_tree_type {
  struct tl_tree self;

  struct tl_type *type;
  int children_num;
  struct tl_tree **children;
};

struct tl_tree_array {
  struct tl_tree self;

  struct tl_tree *multiplicity;
  int args_num;
  struct arg **args;
};

struct tl_tree_var_type {
  struct tl_tree self;

  int var_num;
};

struct tl_tree_var_num {
  struct tl_tree self;

  int var_num;
  int dif;
};

struct tl_tree_nat_const {
  struct tl_tree self;

  long long value;
};

struct arg {
  char *id;
  int var_num;
  int flags;
  int exist_var_num;
  int exist_var_bit;
  struct tl_tree *type;
};

struct tl_combinator {
  //struct tl_combinator_methods *methods;
  char *id;
  unsigned name;
  int is_fun;
  int var_num;
  int args_num;
  struct arg **args;
  struct tl_tree *result;
  void **IP;
  void **fIP;
  int IP_len;
  int fIP_len;
};

#endif
