/* livejq - the outputs as a tree of foldable lines. */
#ifndef LIVEJQ_TREE_H
#define LIVEJQ_TREE_H

#include <jv.h>

#include "defines.h"
#include "grid.h"
#include "views.h"

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
  TREE_VIEW_NEXT,
  TREE_VIEW_PREV,
  TREE_VIEW_RESET, /* back to the value as jq prints it */
} tree_op;

typedef struct tree tree;

tree* tree_new(void);
void tree_free(tree* t);
/* Replaces the roots with the array outputs, keeping folds, views and the
 * cursor by path. */
void tree_set(tree* t, jv outputs);
void tree_do(tree* t, tree_op op);
/* Shows the cursor's value in view id, or as jq prints it when it is in
 * that view already. Nothing happens when the view does not apply. */
void tree_view(tree* t, view_id id);
/* Moves the view by n rows, dragging the cursor along only to keep it in
 * view. False when the view was already at that end. */
b8 tree_scroll(tree* t, i32 n);
/* Selects the line at row n of the view, and toggles it when x is on its
 * fold mark, or when again is set and it was selected already. False when
 * there is no line there. */
b8 tree_click(tree* t, i32 x, i32 n, b8 again);
/* Draws the lines into rows y..y+h-1 between columns x and max_x, scrolled
 * to show the cursor, whose line gets a mark in column x-1. */
void tree_render(tree* t, grid* g, i32 x, i32 y, i32 max_x, i32 h, b8 dim, b8 focused);
/* The cursor's path as jq writes it, "" when there are no lines. */
void tree_path(tree* t, char* buf, usz cap);

#endif /* LIVEJQ_TREE_H */
