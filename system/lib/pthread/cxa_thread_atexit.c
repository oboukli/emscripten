/*
 * Copyright 2019 The Emscripten Authors.  All rights reserved.
 * Emscripten is available under two separate licenses, the MIT license and the
 * University of Illinois/NCSA Open Source License.  Both these licenses can be
 * found in the LICENSE file.
 * Inspired by libcxxabi/src/cxa_thread_atexit.cpp.
 * The main reasons we don't use that version direclty are:
 * 1. We want to be able to use __cxa_thread_atexit in pure C programs
 *    where libcxxabi is not linked in at all.
 * 2. The libcxxabi relies on TLS variables and we use to free our TLS data
 *    block on thread exit.  This would cause a chicken and egg issue
 *    where the TLS variables in libcxxabi/src/cxa_thread_atexit.cpp would
 *    be freed while porcessing the list of __cxa_thread_atexit handlers
 */
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdlib.h>

#include "libc.h"

typedef void(*Dtor)(void*);

typedef struct DtorList {
  Dtor dtor;
  void* obj;
  struct DtorList* next;
} DtorList;

void run_dtors(void* arg) {
  DtorList* dtors = (DtorList*)arg;
  DtorList* head;
  while ((head = dtors)) {
    dtors = head->next;
    head->dtor(head->obj);
    free(head);
  }
}

static pthread_key_t key;
atomic_bool key_created = false;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void ensure_key() {
  // can't use pthread_once here because it depends __cxa_thread_atexit
  // the first rime its called to register the cleanup handlers
  if (!key_created) {
    pthread_mutex_lock(&mutex);
    if (!key_created) {
      pthread_key_create(&key, run_dtors);
      key_created = true;
    }
    pthread_mutex_unlock(&mutex);
  }
}

int __cxa_thread_atexit_impl(Dtor dtor, void* obj, void* dso_symbol) {
  ensure_key();
  DtorList* old_head = pthread_getspecific(key);
  DtorList* head = (DtorList*)(malloc(sizeof(DtorList)));
  assert(head);
  head->dtor = dtor;
  head->obj = obj;
  head->next = old_head;
  pthread_setspecific(key, head);
  return 0;
}

weak_alias(__cxa_thread_atexit_impl, __cxa_thread_atexit);
