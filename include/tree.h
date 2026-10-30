/* livejq - the outputs as a tree of foldable lines. */
#ifndef LIVEJQ_TREE_H
#define LIVEJQ_TREE_H

#include <jv.h>

#include "defines.h"
#include "grid.h"

typedef enum {
  TREE_UP = 0,
  TREE_DOWN,
  TREE_PAGE_UP,
  TREE_PAGE_DOWN,
  TREE_TOP,
  TREE_BOTTOM,
  TREE_FOLD,
  TREE_UNFOLD,
  TREE_TOGGLE,
  TREE_FOLD_ALL,
  TREE_UNFOLD_ALL,
} tree_op;

typedef struct tree tree;

tree* tree_new(void);
void tree_free(tree* t);
/* Replaces the roots with the array outputs, keeping folds and the cursor by
 * path. */
void tree_set(tree* t, jv outputs);
void tree_do(tree* t, tree_op op);
/* Draws the lines into rows y..y+h-1, scrolled to show the cursor. */
void tree_render(tree* t, grid* g, i32 y, i32 h, b8 dim, b8 focused);
/* The cursor's path as jq writes it, "" when there are no lines. */
void tree_path(tree* t, char* buf, usz cap);

#endif /* LIVEJQ_TREE_H */
