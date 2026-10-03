// popen(3) / pclose(3) / system(3)。どれも `/bin/sh -c` を posix_spawn で起こす（musl と同じ形）。
//
// popen の子は、パイプの片端を標準入力か標準出力に持つ。子へ継がれるのは fd 0〜2 だけなので
// （→ spawn.c）、ほかの popen のパイプが子に漏れることはない。pclose が待つ子の pid は、
// FILE の中ではなくこのファイルの表に持つ（wasi-libc の FILE の形に手を入れないため）。

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/// popen で開いたストリームと、その子の pid
struct piped {
  FILE *file;
  pid_t pid;
  struct piped *next;
};

static struct piped *opened;

FILE *popen(const char *command, const char *mode) {
  int parent_end;
  if (*mode == 'r')
    parent_end = 0;
  else if (*mode == 'w')
    parent_end = 1;
  else {
    errno = EINVAL;
    return 0;
  }
  struct piped *entry = malloc(sizeof *entry);
  if (!entry)
    return 0;
  int p[2];
  if (pipe(p) != 0) {
    free(entry);
    return 0;
  }
  FILE *file = fdopen(p[parent_end], mode);
  if (!file) {
    close(p[0]);
    close(p[1]);
    free(entry);
    return 0;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, p[1 - parent_end], 1 - parent_end);
  pid_t pid;
  int error = posix_spawn(&pid, "/bin/sh", &actions, 0, (char *[]){"sh", "-c", (char *)command, 0}, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(p[1 - parent_end]);
  if (error) {
    fclose(file);
    free(entry);
    errno = error;
    return 0;
  }
  *entry = (struct piped){.file = file, .pid = pid, .next = opened};
  opened = entry;
  return file;
}

int pclose(FILE *file) {
  struct piped **link = &opened;
  while (*link && (*link)->file != file)
    link = &(*link)->next;
  if (!*link) {
    errno = ECHILD;
    return -1;
  }
  struct piped *entry = *link;
  *link = entry->next;
  pid_t pid = entry->pid;
  free(entry);
  // 先に閉じる。書く側の popen なら、子はそれで EOF を受け取って終わる
  fclose(file);
  int status, result;
  while ((result = waitpid(pid, &status, 0)) < 0 && errno == EINTR)
    ;
  return result < 0 ? -1 : status;
}

int system(const char *command) {
  if (!command)
    return 1;
  // 待っているあいだ、SIGINT と SIGQUIT は親では無視し、SIGCHLD は塞ぐ（POSIX の system と同じ）。子は元の扱いで始める
  struct sigaction ignore = {.sa_handler = SIG_IGN}, old_int, old_quit;
  sigaction(SIGINT, &ignore, &old_int);
  sigaction(SIGQUIT, &ignore, &old_quit);
  sigset_t child_mask, old_mask, reset;
  sigemptyset(&child_mask);
  sigaddset(&child_mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &child_mask, &old_mask);
  sigemptyset(&reset);
  if (old_int.sa_handler != SIG_IGN)
    sigaddset(&reset, SIGINT);
  if (old_quit.sa_handler != SIG_IGN)
    sigaddset(&reset, SIGQUIT);

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setsigmask(&attr, &old_mask);
  posix_spawnattr_setsigdefault(&attr, &reset);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
  pid_t pid;
  int error = posix_spawn(&pid, "/bin/sh", 0, &attr, (char *[]){"sh", "-c", (char *)command, 0}, environ);
  posix_spawnattr_destroy(&attr);

  int status = -1;
  if (!error)
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
      ;
  sigaction(SIGINT, &old_int, 0);
  sigaction(SIGQUIT, &old_quit, 0);
  sigprocmask(SIG_SETMASK, &old_mask, 0);
  if (error) {
    errno = error;
    return -1;
  }
  return status;
}
