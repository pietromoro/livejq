/* livejq - the outputs as a tree of lines. */
#ifndef LIVEJQ_TREE_H
#define LIVEJQ_TREE_H

#include <jv.h>

#include "defines.h"
#include "grid.h"

typedef struct tree tree;

tree* tree_new(void);
void tree_free(tree* t);
/* Replaces the roots with the array outputs. */
void tree_set(tree* t, jv outputs);
/* Draws the lines into rows y..y+h-1. */
void tree_render(tree* t, grid* g, i32 y, i32 h, b8 dim);

#endif /* LIVEJQ_TREE_H */
