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
  TREE_VIEW_RESET,    /* back to the value as jq prints it */
  TREE_VIEW_SIBLINGS, /* the cursor's view on every path differing only in indices */
  TREE_UNIT_NEXT,     /* the next unit the cursor's time view reads its integer in */
} tree_op;

typedef struct tree tree;

tree* tree_new(void);
void tree_free(tree* t);
/* Replaces the roots with the array outputs, keeping folds, views and the
 * cursor by path. */
void tree_set(tree* t, jv outputs);
void tree_do(tree* t, tree_op op);
/* The wall clock, in milliseconds since the epoch, for times shown
 * relative to now. */
void tree_clock(tree* t, i64 wall_ms);
/* Steps the cursor's value through the n views in ids, then back to the
 * value as jq prints it, skipping the views that do not apply. Nothing
 * happens when none applies and the value is in none of them. */
void tree_view(tree* t, const view_id* ids, i32 n);
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
/* Opens a search at the cursor: until tree_search_end, each tree_search
 * starts over from there, closing the folds the previous one opened. */
void tree_search_begin(tree* t);
/* Searches for pattern, "" for none, in member names and scalar values as
 * jq prints them, strings without quotes, ignoring case unless the pattern
 * has an uppercase letter. Moves to the first match at or after the cursor,
 * unfolding its way there. False when nothing matches. */
b8 tree_search(tree* t, const char* pattern);
/* Closes the search, keeping where it led, or with back putting the
 * cursor, the folds and the pattern back as tree_search_begin found them. */
void tree_search_end(tree* t, b8 back);
/* Moves to the next match after the cursor, or the one before it when step
 * is negative, wrapping around and unfolding its way. False when nothing
 * matches. */
b8 tree_find(tree* t, i32 step);
/* The pattern searched for, NULL for none. */
const char* tree_pattern(const tree* t);
/* The match the cursor is on, from 1, or 0 when it is on none, and how many
 * there are. */
void tree_matches(tree* t, i32* at, i32* total);

#endif /* LIVEJQ_TREE_H */
