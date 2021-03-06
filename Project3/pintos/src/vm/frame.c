#include <stdio.h>
#include "vm/file.h"
#include "vm/swap.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"

/* Initialization related lru_list */
void 
lru_list_init (void)
{
  /* Initialize lru_list */
  list_init(&lru_list);
  /* Initialize lru_list_lock */
  lock_init(&lru_list_lock);
  /* Initialize lru_clock */
  lru_clock = NULL;
}

/* Add page into lru_list */
void 
add_page_to_lru_list (struct page *page)
{
  if(page != NULL)
  {
    lock_acquire(&lru_list_lock);
    /* Insert page into lru_list */
    list_push_back(&lru_list, &page->lru);
    lock_release(&lru_list_lock);
  }
}

/* Delete page in lru_list */
void 
del_page_from_lru_list (struct page *page)
{
  struct list_elem *e;
  if(page != NULL)
  { 
    /* If page is same with lru_clock */
    if(page == lru_clock)
    {
      /* Remove the page from lru_list */
      e = list_remove(&page->lru); 
      lru_clock = list_entry(e, struct page, lru);
    }
    else
      list_remove(&page->lru);
  }
}

/* Allocate page for user */
struct page 
*alloc_page (enum palloc_flags flags)
{
  struct page *user_page;
  if((flags & PAL_USER) == 0)
    return NULL;
  /* Allocate page about physical memory */
  void *kaddr= palloc_get_page(flags);
  /* If page about physical memory is NULL */
  while(kaddr == NULL)
  {
    /* If it is fail to allocate page */
    try_to_free_pages();
    /* Allocate page */
    kaddr = palloc_get_page(flags);
  }
  /* Allocate memory for user page */
  user_page = malloc(sizeof(struct page));
  /* If it is fail to allocate user page */
  if(user_page == NULL)
  {
    palloc_free_page(kaddr);
    return NULL;
  }
  /* Initialize user page */
  user_page->kaddr = kaddr;
  user_page->thread = thread_current();

  /* Add page into lru_list */
  add_page_to_lru_list(user_page);
  return user_page;
}

/* Free the page corresponding physical address */
void 
free_page (void *kaddr)
{
  struct page *it_page;
  struct list_elem *e;

  lock_acquire(&lru_list_lock);
  /* Searches the page in lru_list */
  for(e = list_begin(&lru_list); e != list_end(&lru_list); 
      e = list_next(e)) 
  {
    /* Get page corresponding physical address */
    it_page = list_entry(element, struct page, lru);
    /* When we find that page */
    if(it_page->kaddr == kaddr)
    {
      /* Free the page */
      __free_page(it_page);
      break;
    }
  }
  lock_release(&lru_list_lock);
}

void 
__free_page (struct page *page)
{
  /* Free the physical memory */
  palloc_free_page(page->kaddr);
  /* Delete page in lru_list */
  del_page_from_lru_list(page);
  /* Free the page */
  free(page);
}

/* Move the lru_list & Return last node of lru_list */
struct list_elem
*get_next_lru_clock (void)
{
  struct list_elem *e;
 
  /* lru_clock is not exist */
  if(lru_clock == NULL)
  {
    e = list_begin(&lru_list);
    /* lru_list has pages  */
    if(e != list_end(&lru_list))
    {
      lru_clock = list_entry(e, struct page, lru);
      return e;
    }
    else
      return NULL;
  }
  /* lru_clock is exist */
  e = list_next(&lru_clock->lru);
  /* lru_clock is last node of list_lru  */
  if(list_end(&lru_list) == e)
  {
    /* The # of pages is more than 1, in list_lru */
    if(list_begin(&lru_list) != &lru_clock->lru)
      e = list_begin(&lru_list);
    /* The # of pages is one in list_lru */
    else
      return NULL;
  }
  lru_clock = list_entry(e, struct page, lru);
  return element;
}

/* Try to free about pages */
void try_to_free_pages(void)
{
  struct thread *page_t;
  struct list_elem *e;
  struct page *lru_p;
  lock_acquire(&lru_list_lock);
  /* If lru_list has no pages */
  if(list_empty(&lru_list) == true)
  {
    lock_release(&lru_list_lock);
    return;
  }
 
  while(true)
  {
    /* Get next node of lru_list */
    e = get_next_lru_clock();
    /* If it is last node */
    if(element == NULL)
    {
      lock_release(&lru_list_lock);
      return;
    }
    /* Get page corresponding node */
    lru_p = list_entry(element, struct page, lru);
    /* Check the pinned */
    if(lru_p->vme->pinned == true)
      continue;
    page_t = lru_page->thread;

    if(pagedir_is_accessed(page_t->pagedir, lru_p->vme->vaddr))
    {
      pagedir_set_accessed(page_t->pagedir, lru_p->vme->vaddr, false);
      continue;
    }

    if(pagedir_is_dirty(page_t->pagedir, lru_p->vme->vaddr) || 
                        lru_p->vme->type == VM_ANON)
    {
      if(lru_p->vme->type == VM_FILE)
      {
        lock_acquire(&file_lock);
	file_write_at(lru_p->vme->file, lru_p->kaddr,
                      lru_p->vme->read_bytes, lru_p->vme->offset);
	lock_release(&file_lock);
      }			
      else
      {
        lru_p->vme->type = VM_ANON;
        lru_p->vme->swap_slot = swap_out(lru_p->kaddr);
      }
    }
    lru_p->vme->is_loaded = false;
    pagedir_clear_page(page_t->pagedir, lru_p->vme->vaddr);
    __free_page(lru_p);
    break;
  }
  lock_release(&lru_list_lock);
  return;
}


