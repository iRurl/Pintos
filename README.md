<center> <h1> 操作系统课程设计报告 </h1>   </center>

> 实验完成项目:	User Programs、HW-shell

## User Programs

### 主要功能

#### 前置任务

内核如何初始化用户进程，修改内核，以便什么都不做运行正常。

| 序号 |     名称      |          测试点          |
| :--: | :-----------: | :----------------------: |
|  1   |  do-nothing   | 内核修改使得程序正常运行 |
|  2   | stack-align-0 | 内核修改使得程序正常运行 |

#### 任务 1：参数分离 

第一个任务，观察进程的创建过程，在 `process.c` 的 `process_execute` 即为我们的线程创建函数。其中 `file_name` 是传递进来的参数，它不仅包含了要执行的函数，也包括了后面的参数，这里需要进行字符串分割。Pintos 已经为我们提供了一个字符串分割的函数，声明在 `lib/string.h` 中。

分割完参数后，进程拉起了一个线程，在线程的创建函数中 `start_process` 中，将参数放置于 `esp` 的对应位置。

> 修改文件:process.c

| 序号 |      名称      |                     测试点                      |
| :--: | :------------: | :---------------------------------------------: |
|  3   |   args-none    | 命令没有任何的参数，检查第 0 个参数是不是文件名 |
|  4   |  args-single   |     有一个参数，检查参数数量和内容是否正确      |
|  5   | args-multiple  |                 一共有 4 个参数                 |
|  6   |   args-many    |                一共有 22 各参数                 |
|  7   | args-dbl-space |      有参数用两个空格间隔，确保要识别正确       |

##### 堆栈结构

|  Address   |       Name       |     Data      |     Type     |
| :--------: | :--------------: | :-----------: | :----------: |
| 0xbffffffc |  `argv[3][...]`  |   `"bar\0"`   |  `char[4]`   |
| 0xbffffff8 |  `argv[2][...]`  |   `"foo\0"`   |  `char[4]`   |
| 0xbffffff5 |  `argv[1][...]`  |   `"-1\0"`    |  `char[3]`   |
| 0xbfffffed |  `argv[0][...]`  | `"/bin/ls\0"` |  `char[8]`   |
| 0xbfffffec |   `word-align`   |      `0`      |  `uint8_t`   |
| 0xbfffffe8 |    `argv[4]`     |      `0`      |   `char *`   |
| 0xbfffffe4 |    `argv[3]`     | `0xbffffffc`  |   `char *`   |
| 0xbfffffe0 |    `argv[2]`     | `0xbffffff8`  |   `char *`   |
| 0xbfffffdc |    `argv[1]`     | `0xbffffff5`  |   `char *`   |
| 0xbfffffd8 |    `argv[0]`     | `0xbfffffed`  |   `char *`   |
| 0xbfffffd4 |      `argv`      | `0xbfffffd8`  |  `char **`   |
| 0xbfffffd0 |      `argc`      |      `4`      |    `int`     |
| 0xbfffffcc | `return address` |      `0`      | `void (*)()` |

##### 主要函数

`process_execute`

```c
pid_t process_execute(const char* file_name) {
  char* fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page(0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy(fn_copy, file_name, PGSIZE);

  struct thread_node* thread_node = malloc(sizeof(struct thread_node));
  thread_node->exit_status = -1;
  thread_node->already_wait = false;
  if (thread_current()->pcb == NULL || thread_current()->pcb->main_thread == NULL)
    thread_node->p_pid = thread_current()->tid;
  else
    thread_node->p_pid = thread_current()->pcb->main_thread->tid;
  thread_node->load_success = false;
  sema_init(&thread_node->semaph, 0);
  sema_init(&thread_node->load_semaph, 0);
  lock_acquire(&thread_lock);
  list_push_back(&thread_nodes_list, &thread_node->elem);
  lock_release(&thread_lock);


  /* Create a new thread to execute FILE_NAME. */
 thread_node->tid = tid = thread_create(file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page(fn_copy);

  sema_down(&thread_node->load_semaph);
  if (!thread_node->load_success)
    return TID_ERROR;
  return tid;
}
```

`start_process`

```c
  if (success) {
    // Ensure that timer_interrupt() -> schedule() -> process_activate()
    // does not try to activate our uninitialized pagedir
    new_pcb->pagedir = NULL;
    t->pcb = new_pcb;

    list_init(&new_pcb->all_files_list);
    lock_init(&new_pcb->file_list_lock);
    new_pcb->next_fd = 2;
    
    list_init(&new_pcb->all_threads);
    sema_init(&new_pcb->semaph, 0);

    list_init(&new_pcb->user_lock_list);
    new_pcb->next_lock_id = 1;
    list_init(&new_pcb->user_semaphore_list);
    new_pcb->next_semaphore_id = 1;

     // Continue initializing the PCB as normal
    new_pcb->main_thread = t;
    strlcpy(new_pcb->process_name, argv0, sizeof(new_pcb->process_name));
  }
```



#### 任务 2：系统参数检查

该任务的目的是，检查传递的参数的内存地址，使用特权级的目的是保护内存。Pintos 将 kernel 和用户内存空间分成了两部分，BASE 以上是内存空间，08048000-BASE 之间，是用户进程空间。如果一个指针，指向了没有权限的位置，就应该直接以 -1 退出。文档中有两种思路一种是 `userprog/pagedir.c` 和在 `threads/vaddr.h` 中的相关函数 `pagedir_get_page`，来验证地址范围；一种是通过访问该地址，来造成 `page_fault`，在 `page_fault` 处理函数中，再退出。`这里选择第二种方法`

| 序号 |      名称       |                            测试点                            |
| :--: | :-------------: | :----------------------------------------------------------: |
|  8   |   `sc-bad-sp`   | 将 `esp` 指向非法的地方之后触发系统调用，正常情况下应该 `exit(-1)` |
|  9   |  `sc-bad-arg`   | 将 `esp` 指向了栈顶下 4 字节（刚好放进了 `exit` 的系统调用号），试图在获取系统调用的参数时访问非法的内存区域，正常情况下应该以 exit(-1) 退出。 |
|  10  |  `sc-boundary`  | 让 `exit` 的系统调用号和参数正好放在页边界，观察退出状态值是否正常。 |
|  11  | `sc-boundary-2` | 让 `exit` 的参数的前三个字节和最后一个字节存放在页边界，观察退出状态值是否正常。 |
|  12  | `sc-boundary-3` | 调用系统调用时，系统调用号的位置使其第一个字节有效，但该号码的其余字节位于无效内存中。 |

##### 结构图

```c
PHYS_BASE +----------------------------------+
            |            user stack            |
            |                 |                |
            |                 |                |
            |                 V                |
            |          grows downward          |
            |                                  |
            |                                  |
            |                                  |
            |                                  |
            |           grows upward           |
            |                 ^                |
            |                 |                |
            |                 |                |
            +----------------------------------+
            | uninitialized data segment (BSS) |
            +----------------------------------+
            |     initialized data segment     |
            +----------------------------------+
            |           code segment           |
0x08048000 +----------------------------------+
            |                                  |
            |                                  |
            |                                  |
            |                                  |
            |                                  |
        0 +----------------------------------+
```

##### 主要函数

`static void kill(struct intr_frame* f) `

```c
static void kill(struct intr_frame* f) {
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */

  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs) {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf("%s: dying due to interrupt %#04x (%s).\n", thread_name(), f->vec_no,
             intr_name(f->vec_no));
      intr_dump_frame(f);
      syscall_exit(f, -1);
      NOT_REACHED();

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame(f);
      PANIC("Kernel bug - unexpected interrupt in kernel");

    default:
      /* Some other code segment? Shouldn't happen. Panic the kernel. */
      printf("Interrupt %#04x (%s) in unknown segment %04x\n", f->vec_no, intr_name(f->vec_no),
             f->cs);
      PANIC("Kernel bug - unexpected interrupt in kernel");
  }
}
```

#### 任务3：系统调用

##### 系统命令

| 序号 | 名称 | 测试点 |
| :--: | :--: | :----: |
|  13  | halt |  关机  |
|  14  | exit |  退出  |

##### 打开关闭文件

使用链表）来维护打开的文件列表，每一个打开的文件都需要有一个文件描述符 `fd`，且 `fd` 在不同进程中不共享，所以可见是由进程维护描述符，同时，0，1，2 三个描述符已经默认分配，所以我们必须从 3 开始为新打开的文件分配描述符。

| 序号 |     名称      |                            测试点                            |
| :--: | :-----------: | :----------------------------------------------------------: |
|  22  |  open-normal  |           正常打开一个文件，如果 `fd>2` 则 pass。            |
|  23  | open-missing  |               打开不存在的文件，`fd` 应为 -1。               |
|  24  | open-boundary |      文件名跨页，看是否能正常打开。不需要特别在意这个。      |
|  25  |  open-empty   |               文件名为空字符串，`fd` 应为 -1。               |
|  26  |   open-null   |         文件名使用 `NULL`，应该以 `exit(-1)` 退出。          |
|  27  | open-bad-ptr  | 文件名指针处于用户空间，但是指向了未分配的区域，应该以 `exit(-1)` 退出。 |
|  28  |  open-twice   | 打开同一个文件两次，观察是否能正常打开且 `fd` 是不是不一样（要求不一样）。 |
|  29  | close-normal  |            正常关闭一个文件，看程序是否正常退出。            |
|  30  |  close-twice  |     关闭两次同一个 `fd`，正常退出或者以 -1 退出都可以。      |
|  31  |  close-stdin  |        关闭标准输入流，正常退出或者以 -1 退出都可以。        |
|  32  | close-stdout  |        关闭标准输出流，正常退出或者以 -1 退出都可以。        |
|  33  | close-bad-fd  |      关闭不存在的 `fd`，正常退出或者以 -1 退出都可以。       |

##### 创建移除文件

创建文件调用 `filesys/filesys.c` 中定义的 `filesys_create` ，移除调用 `filesys/filesys.c` 中定义的 `filesys_remove`

| 序号 |      名称      |                           测试点                           |
| :--: | :------------: | :--------------------------------------------------------: |
|  15  | create-normal  |   正常创建一个文件，然后用 `open` 系统调用看是否能打开。   |
|  16  |  create-empty  |    创建文件名为空的文件，返回创建失败或直接以 -1 退出。    |
|  17  |  create-null   |          创建的文件名为 `NULL`，要求以 -1 退出。           |
|  18  | create-bad-ptr | 创建的文件名指向未分配空间，返回创建失败或直接以 -1 退出。 |
|  19  |  create-long   |         创建一个文件名很长的文件。不用特别在意他。         |
|  20  | create-exists  |             创建重名的文件，返回创建失败即可。             |
|  21  |  create-bound  |             创建的文件名跨页。不用特别在意他。             |

##### 读写文件

和创建移除一样，同时对于 `write`，已经实现了向终端写入。`read` 会用到 `input_getc()`与 `file_read()`，`write` 会用到 `file_write()`与 `putbuf()`。这里除了注意参数是否和法外，还需要判断缓冲区是否合法。

| 序号 |      名称      |                            测试点                            |
| :--: | :------------: | :----------------------------------------------------------: |
|  34  |  read-normal   |            正常读取文件，比对读取的内容是否正确。            |
|  35  |  read-bad-ptr  | 存放读取内容的 `buffer` 指向了没被映射的地址，返回 0 或者以 -1 退出都可以。 |
|  36  | read-boundary  |              `buffer` 跨页，不需要特别在意他。               |
|  37  |   read-zero    |                   读取 `size = 0` 的内容。                   |
|  38  |  read-stdout   |      试图读取标准输出流，返回 0 或者以 -1 退出都可以。       |
|  39  |  read-bad-fd   |       读取不存在的 `fd`，返回 0 或者以 -1 退出都可以。       |
|  40  |  write-normal  |              正常写入文件，判断返回值是否正确。              |
|  41  | write-bad-ptr  | 写入读取内容的 `buffer` 指向了没被映射的地址，返回 0 或者以 -1 退出都可以。 |
|  42  | write-boundary |              `buffer` 跨页，不需要特别在意他。               |
|  43  |   write-zero   |                     写入 0 byte 的内容。                     |
|  44  |  write-stdin   |      试图写入标准输入流，返回 0 或者以 -1 退出都可以。       |
|  45  |  write-bad-fd  |       写入不存在的 `fd`，返回 0 或者以 -1 退出都可以。       |

##### 执行与等待

Pintos 在这里还没有特别区分进程和线程的概念，可以理解为一个线程就是一个进程，所以其 ID 可以一一映射。该部分的重点在于父进程需要维护创建的子进程列表。子进程的创建过程其实就是前面修改过的 `process_execute()` 函数。在该函数的 `thread_create` 函数执行后，子进程就已经转移到 `start_process` 中开始运行或者加入就绪队列了。这里要保证子进程一定成功创建，就需要实现一个同步锁，来保证子进程 `load` 成功才接着执行父进程，子进程一旦创建失败，说明该该调用失败。而对于 `wait` 操作，也需要一个锁，保证父进程在子进程执行期间无法进行任何操作，等待子进程退出后，父进程获取子进程退出码，并回收资源。这里的锁要保证父进程 `wait` 时，无法执行任何操作，子进程退出时，需要立刻通知父进程，但不能直接销毁，而要等待父进程来回收资源获取返回码等，然后才可以正常销毁。

| 序号 |     名称      |                            测试点                            |
| :--: | :-----------: | :----------------------------------------------------------: |
|  46  |   exec-once   | 创建一个子进程，观察子进程退出代码（81），以及父进程退出代码是否正确（0）。 |
|  47  |   exec-arg    |          执行的子进程有多个参数，观察参数是否正确。          |
|  48  |  exec-bound   |            使用跨越页面边界的 exec 字符串执行子项            |
|  49  | exec-bound-2  | 调用 exec 系统调用，并将 exec 字符串指针参数定位为只有其第一个字节是有效内存（指针的字节 1-3 无效） |
|  50  | exec-bound-3  | 调用 exec 系统调用，其中 exec 字符串跨越页边界，使字符串的第一个字节有效，但字符串的其余部分位于无效内存中。 |
|  51  | exec-multiple |       `wait(exec(…)` 五次，观察退出代码以及退出顺序。        |
|  52  | exec-missing  |         执行不存在的文件，观察 `exec` 返回值（-1）。         |
|  53  | exec-bad-ptr  | 执行的命令指向未被分配的内存区域，返回 0 或者以 -1 退出都可以。 |
|  54  |  wait-simple  | `wait` 一个子进程，观察返回值是否和子进程的退出代码一致（81）。 |
|  55  |  wait-twice   |        等待两次同一个子进程，第二次等待应该返回 -1。         |
|  56  |  wait-killed  | 等待一个子进程，子进程刚执行就被 kill 杀死（应该以 -1 退出），观察子进程退出代码以及父进程在 `wait` 后的返回值。 |
|  57  | wait-bad-pid  |                  `wait` 一个不存在的 PID。                   |

##### multi

这一部分只需要完成 `filesize` 这个系统调用。filesize 在文档中的声明为`int filesize (int fd`。需要用 `fd` 去寻找打开的文件，实现时调用 `file_length` 

| 序号 |      名称      |                            测试点                            |
| :--: | :------------: | :----------------------------------------------------------: |
|  58  | multi-recurse  |       递归创建子进程，共 16 个，观察进入和退出的顺序。       |
|  59  | multi-child-fd | 子进程尝试关闭一个父进程打开的文件（应该要失败），之后看父进程还能不能正常访问这个文件。 |

##### rox

这一部分旨在防止进程修改正在运行的可执行文件。可执行文件在 Pintos 中的定义为用来创建进程的文件，即创建进程时打开的那一个文件。`filesys/file.c` 文件中定义了函数 `file_deny_write()`，该函数可以禁止对文件的写操作。我们需要在 `load` 时，禁止对该文件的写操作，在退出时回复。同时这一块需要实现 `seek` 调用。需要用到 `file_seek()` 函数。

| 序号 |      名称      |                            测试点                            |
| :--: | :------------: | :----------------------------------------------------------: |
|  60  |   rox-simple   |      尝试改写自己的可执行文件（`write` 应该要返回 0）。      |
|  61  |   rox-child    | 父进程先写好子进程的可执行文件，然后执行子进程。接下来子进程尝试改写自己的可执行文件（`write` 应该要返回 0），之后会退出。然后父进程再次改写子进程的可执行文件（应该要成功）。 |
|  62  | rox-multichild | 父进程先写好子进程的可执行文件，然后递归地创建 5 个子进程且他们都试图改写自己的可执行文件；然后递归地退出，退出前也试图改写自己的可执行文件；最后一次父进程会再次改写子进程的可执行文件（这次应该要成功）。 |

##### bad 

这一部分，要做到前面的每个地址访问都验证地址合法性。由于访问非法地址会引起 `page_fault`，所以可以在 `exception.c` 中对 `page_fault()`进行修改，在 kill 时结束进程，返回 -1.

| 序号 |    名称    |              测试点              |
| :--: | :--------: | :------------------------------: |
|  63  |  bad-read  |     读取 `NULL` 指针的内容。     |
|  64  | bad-write  |    给 `NULL` 指针的位置赋值。    |
|  65  | bad-read2  |       读取内核空间的内容。       |
|  66  | bad-write2 |         往内核空间赋值。         |
|  67  |  bad-jump  |   把 `NULL` 伪装成函数后调用。   |
|  68  | bad-jump2  | 把内核空间地址伪装成函数后调用。 |

##### 新增

| 序号 |   名称   |             测试点             |
| :--: | :------: | :----------------------------: |
|  69  | iloveos  | 验证写入标准输出的功能是否正常 |
|  70  | practice |  新增 `sys_practice` 系统调用  |

##### 堆栈对齐

`stack-align` 系列因为需要额外使用 clang 编译器（gcc 编译器下始终 pass）而暂时没有处理

| 序号 |     名称      |                 测试点                  |
| :--: | :-----------: | :-------------------------------------: |
|  71  | stack-align-1 |      检查用户空间堆栈是否正确对齐       |
|  72  | stack-align-2 | 返回当前堆栈指针 `esp` 对 16 取模的结果 |
|  73  | stack-align-3 |                                         |
|  74  | stack-align-4 |                                         |

##### 浮点

浮点计算需要使用 FPU，需要实现开启和初始化 FPU，保存恢复 FPU 状态的功能。开启 FPU 是修改 `entry.S` 中切换保护模式时写入 `cr0` 的参数（去掉 `CR0_EM`）。初始化、保存和恢复 FPU 状态的指令分别是 `fninit fsave frstor`，FPU 状态需要 108 字节保存。我对 GNU 扩展的 AT&T 风格的汇编不是很清楚（不知道传参数那的 `"m"` `"g"` 什么的到底是个什么意思），我直接把 108 字节长的数组塞进了 thread 结构体。在进程创建时（`thread_create`）保存自身进程 FPU 状态，初始化并保存一份 FPU 状态给新的进程，再恢复自身进程的 FPU 状态。另外再切换进程时（`schedule`），保存当前进程 FPU 状态，加载新进程的 FPU 状态。

| 序号 |      名称      |                            测试点                            |
| :--: | :------------: | :----------------------------------------------------------: |
|  75  | floating-point |           验证用户程序中基本的浮点运算是否正常工作           |
|  76  |    fp-simul    | 通过同时执行 `compute-e` 用户程序和自身来验证在上下文切换时是否正确保存和还原浮点寄存器的状态 |
|  77  |     fp-asm     |  系统内核在进行上下文切换时是否正确保存和还原浮点寄存器状态  |
|  78  |   fp-syscall   | 系统调用时浮点寄存器是否能够正确保存和还原，以及系统调用返回值是否正确。 |
|  79  |  fp-kernel-e   |       验证 `compute_e` 系统调用是否正确实现的测试程序        |
|  80  |    fp-init     |    验证在跳转到用户程序时浮点单元（FPU）是否被正确地重置     |
|  81  |    fp-kasm     |  系统内核在进行上下文切换时是否正确保存和还原浮点寄存器状态  |
|  82  |    fp-kinit    |     系统内核是否在创建新线程时正确初始化浮点单元（FPU）      |

##### 资源释放

要做到每一个资源申请后，都会有释放。包括前面提到的文件的退出，消除开辟的内存空间，同时注意到进程退出时，关闭所有打开的文件，以及 `open`、`close`、`create`、`remove`、`execute`、`wait` 等所有过程中开辟的空间都需要关闭。

| 序号 |   名称    |            测试点            |
| :--: | :-------: | :--------------------------: |
|  83  | multi-oom | 每一个资源申请后，都会有释放 |

##### 文件系统

其中 94-96 涉及到了文件的锁，我们需要对文件系统设计一个锁，在所有用到的操作中，操作前都上锁，操作后都去除锁。

| 序号 |     名称      |                            测试点                            |
| :--: | :-----------: | :----------------------------------------------------------: |
|  84  |   lg-create   |    创建一个大的空白文件，检查文件大小与内容是否是合格的。    |
|  85  |    lg-full    | 创建一个大的文件，在里面填满内容，检查文件的大小与内容是否和期望的一致。 |
|  86  |   lg-random   | 创建一个大的文件，往里边多次随机写入内容，然后读取内容比对。 |
|  87  | lg-seq-block  | 创建一个大的文件，以 `block_size` 为单位多次写入内容，然后读取内容进行比对。 |
|  88  | lg-seq-random |          与上一个相比，`block_size` 变为了随机数。           |
|  89  |   sm-create   |    创建一个小的空白文件，检查文件大小与内容是否是合格的。    |
|  90  |    sm-full    | 创建一个小的文件，在里面填满内容，检查文件的大小与内容是否和期望的一致。 |
|  91  |   sm-random   | 创建一个小的文件，往里边多次随机写入内容，然后读取内容比对。 |
|  92  | sm-seq-block  | 创建一个小的文件，以 `block_size` 为单位多次写入内容，然后读取内容进行比对。 |
|  93  | sm-seq-random |          与上一个相比，`block_size` 变为了随机数。           |
|  94  |   syn-read    | 创建 10 个进程，打开同一个文件并一个字节一个字节地比对内容，保证中途没有异常发生（打开文件失败等情况）。 |
|  95  |  syn-remove   |       打开一个文件后移除文件，确认文件是否仍然可读写。       |
|  96  |   syn-write   |          比起 `syn-read`，读变成了写，但只写一次。           |

##### 主要函数

`static void syscall_handler(struct intr_frame* f UNUSED) {`

```c
  int syscall_arg = args[0];
  switch (syscall_arg) {
    case SYS_EXIT:
      syscall_exit(f, args[1]);
      break;
    case SYS_HALT:
      shutdown_power_off();
      break;
    case SYS_EXEC:
      syscall_exec(f, (char*)args[1]);
      break;
    case SYS_WAIT:
      f->eax = process_wait(args[1]);
      break;
    case SYS_CREATE:
      syscall_create(f, (char*)args[1], args[2]);
      break;
    case SYS_REMOVE: 
      syscall_remove(f, (char*)args[1]); 
      break;
    case SYS_OPEN:
      syscall_open(f, (char*)args[1]);
      break;
    case SYS_FILESIZE: 
      if (!is_validity(f, (char*)(args + 0x08))) return; 
      syscall_file_size(f, args[1]);
      break;
    case SYS_READ:
      if (!is_validity(f, (char*)args[2]) || !is_validity(f, (char*)(args + 0x10)) || !is_validity(f, (char*)(args + 0x0c)) || !is_validity(f, (char*)(args + 0x08))) return;
      f->eax = syscall_read(args[1], (char*)args[2], args[3]);
      break;
    case SYS_WRITE:
      if (!is_validity(f, (char*)args[2])) return;
      f->eax = syscall_write(args[1], (char*)args[2], args[3]);
      break;  
    case SYS_SEEK:  
      syscall_seek(f, args[1], args[2]);
      break;
    case SYS_TELL:
      syscall_tell(f, args[1]);
      break;
    case SYS_CLOSE:
      syscall_close(f, args[1]);
      break;      
    case SYS_PRACTICE:
      f->eax = (int)args[1] + 1;
      break;
    case SYS_COMPUTE_E:
      f->eax = sys_sum_to_e((int)args[1]);
      break;
    case SYS_PT_CREATE:
      if (!is_validity(f, (char*)(args + 0x10))) return;
      f->eax = pthread_execute((stub_fun)args[1], (pthread_fun)args[2], (void*)args[3]);
      break;
    case SYS_PT_EXIT:
      pthread_exit();
      break;
    case SYS_PT_JOIN:
      f->eax = pthread_join((tid_t)args[1]);
      break;
    case SYS_LOCK_INIT:
      f->eax = syscall_lock_init((char*)args[1]);
      break;
    case SYS_LOCK_ACQUIRE:
      f->eax = syscall_lock_acquire((char*)args[1]);
      break;
    case SYS_LOCK_RELEASE:
      f->eax = syscall_lock_release((char*)args[1]);
      break;
    case SYS_SEMA_INIT:
      f->eax = syscall_sema_init((char*)args[1], (int)args[2]);
      break;
    case SYS_SEMA_DOWN:
      f->eax = syscall_sema_down((char*)args[1]);
      break;
    case SYS_SEMA_UP:
      f->eax = syscall_sema_up((char*)args[1]);
      break;
    case SYS_GET_TID:
      f->eax = thread_current()->tid;
      break;
    default:
      break;
  }

```

```c
void syscall_exit(struct intr_frame* f, int args1) {
  f->eax = args1;
  struct thread* cur = thread_current();
  set_exit_status(cur, args1);
  printf("%s: exit(%d)\n", cur->pcb->process_name, args1);
  process_exit();
}

void syscall_exec(struct intr_frame* f, const char* args1) {
  if (!is_validity(f, args1) || !is_validity(f, args1 + 0x04))  return;
  pid_t pid = process_execute(args1);
  f->eax = pid;
}

void syscall_create(struct intr_frame* f, const char* file, unsigned initial_size) {
  if (!is_validity(f, file)) return;
  f->eax = filesys_create(file, initial_size);
}

void syscall_remove(struct intr_frame* f, const char* file) {
  if (!is_validity(f, file)) return;
  f->eax = filesys_remove(file);
}

void syscall_open(struct intr_frame* f, const char* file) {
  if (!is_validity(f, file))
    return;
  f->eax = open_for_syscall(file);
}

void syscall_file_size(struct intr_frame* f, int fd) {
  struct file* file = fd_to_file(fd);
  if (file == NULL) {
    f->eax = -1;
    return;
  }
  f->eax = file_length(file);
}

int syscall_read(int fd, void* buffer, unsigned size) {
  if (fd == 0) {
    char* buffer_vector = (char*)buffer;
    for (unsigned i = 0; i < size; i++) {
      uint8_t inputc = input_getc();
      buffer_vector[i] = inputc;
    }
    return size;
  }
  struct process* pcb = thread_current()->pcb;
  if (pcb == NULL)
    return -1;
  struct file* file = fd_to_file(fd);
  if (file == NULL)
    return -1;
  lock_acquire(&pcb->file_list_lock);
  int ret = file_read(file, buffer, size);
  lock_release(&pcb->file_list_lock);
  return ret;
}

int syscall_write(int fd, const void* buffer, unsigned size) {
  if (fd == 1) {
    putbuf((const char*)buffer, size);
    return size;
  }

  struct file* file = fd_to_file(fd);
  if (file == NULL)
    return -1;
  struct process* pcb = thread_current()->pcb;
  if (pcb == NULL)
    return -1;
  lock_acquire(&pcb->file_list_lock);
  int write_size = file_write(file, buffer, size);
  lock_release(&pcb->file_list_lock);
  return write_size;
}

void syscall_seek(struct intr_frame* f, int fd, unsigned position) {
  struct file* file = fd_to_file(fd);
  if (file == NULL) {
    f->eax = -1;
    return;
  }
  file_seek(file, position);
}

void syscall_tell(struct intr_frame* f, int fd) {
  struct file* file = fd_to_file(fd);
  if (file == NULL) {
    f->eax = -1;
    return;
  }
  f->eax = file_tell(file);
}

void syscall_close(struct intr_frame* f, int fd) {
  if (!close_file(fd)) {
    f->eax = -1;
    return;
  }
}
```

### 实验测试结果

<img src="https://gitee.com/nswdxsr/blog--photo/raw/master/pintos.png">



## HW-shell

### 主要功能

#### pwd

```c
//实现pwd命令
int cmd_pwd(unused struct tokens* tokens) {
  char buf[4096];
  getcwd(buf, 4096);
  printf("%s\n", buf);
  return 1;
}
```

#### cd

```c
//实现cd命令
int cmd_cd(unused struct tokens* tokens) {
  //如果输入的参数是一个，则回到当前HOME目录
  if (tokens_get_length(tokens) == 1) { 
    chdir(getenv("HOME"));
  } 
  //如果输入的参数是两个，则回到第二个参数所指定的目录
  else if (tokens_get_length(tokens) == 2) {
    if (chdir(tokens_get_token(tokens, 1)) == -1) {//如果目录不存在，则报错
      printf("cd: %s: No such file or directory\n", tokens_get_token(tokens, 1));
    }
  } else {
    printf("cd: too many arguments\n");
  }
  return 1;
}
```

#### wait

```c
//实现wait命令
int cmd_wait(unused struct tokens *tokens) {
  int status, pid;
  pid = waitpid(-1, &status, WUNTRACED);

  if (pid > 0) {
    if (WIFEXITED(status)) {
      printf("Process %d exited with status %d\n", pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      printf("Process %d terminated by signal %d\n", pid, WTERMSIG(status));
    } else if (WIFSTOPPED(status)) {
      // 将停止的进程调回前台
      tcsetpgrp(shell_terminal, getpgid(pid));
      waitpid(pid, &status, WCONTINUED);
      tcsetpgrp(shell_terminal, shell_pgid);
      printf("Process %d stopped by signal %d\n", pid, WSTOPSIG(status));
    } else if (WIFCONTINUED(status)) {
      printf("Process %d continued\n", pid);
    }
  }
  return 1;
}
```

#### 路径解析

```c
//程序环境变量读取路径
char* find_cmd_path(char* cmd_name) {
  if (access(cmd_name, F_OK) == 0) return cmd_name;
  char* path_name = malloc(1024);
  strcpy(path_name, getenv("PATH"));
  if (path_name == NULL) {
    return NULL;
  }
  char* token = strtok(path_name, ":");
  char* cmd_path = malloc(128);
  while (token != NULL) {
    strcpy(cmd_path, token);
    strcat(cmd_path, "/");
    strcat(cmd_path, cmd_name);
    if (access(cmd_path, F_OK) == 0) {
      free(path_name);
      return cmd_path;
    }
    token = strtok(NULL, ":");
  }
  free(path_name);
  return NULL;
}
```

#### 程序执行

```c
void run_program(char** args, int in_fd, int out_fd, int run_bg) {
  pid_t fork_pid = fork();

  if (fork_pid == -1) {
    printf("Failed to create new process: %s\n", strerror(errno));
  }
  if (fork_pid > 0) {
    if (!run_bg) {
      int status;
      if (!session_pgid) {
        session_pgid = fork_pid;
      }
      setpgid(fork_pid, session_pgid);
      waitpid(fork_pid, &status, 0);
      if (status != EXIT_SUCCESS) {
        if (status != SIGINT && status != SIGQUIT) {
          printf("Program failed: %d\n", status);
        }
      }
    }
  }
  if (fork_pid == 0) {
    if (run_bg) {
      setpgid(0, 0);
    }
    if (in_fd) {
      dup2(in_fd, STDIN_FILENO);
    }
    if (out_fd) {
      dup2(out_fd, STDOUT_FILENO);
    }
    execv(find_cmd_path(args[0]), args);
    exit(EXIT_FAILURE);
  }
}
```

#### 其他命令执行主程序

```c
void cmd_others(unused struct tokens* tokens) {
  char* args[ARGSNUM];
  char* token;
  int pipe_fds[2];
  int token_num = 0, arg_num = 0;
  int cin = 0, cout = 0;
  int run_bg = 0; // 新增变量，用于标识是否后台运行
  int length = tokens_get_length(tokens);
  if(length == 0) return;   

  for (; token_num < tokens_get_length(tokens); token_num++,arg_num++) {
    token = tokens_get_token(tokens, token_num);
    if (token[0] == '&') {
      run_bg = 1;
      arg_num--;
      continue; // 跳过后台运行符，不加入参数列表
    }
    // 管道符
    if (token[0] == '|') {
      if (pipe(pipe_fds) == -1) {
        printf("Failed to create new pipe\n");
        return;
      }
      cout = pipe_fds[1];
      args[arg_num] = NULL;
      run_program(args, cin, cout, run_bg);
      close(cout);

      if (cin) {
        close(cin);
      }
      cin = pipe_fds[0];
      arg_num = -1;
    } else if (token[0] == '>') {
      token_num++;
      arg_num = 0;  // 重置为下一个参数的开始位置
      cout = creat(tokens_get_token(tokens, token_num), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
      if (cout == -1) {
        exit(EXIT_FAILURE);
      }
      // 重定向输入
    } else if (token[0] == '<') {
      arg_num++;
      token_num++;
      arg_num = 0;  // 重置为下一个参数的开始位置
      cin = open(tokens_get_token(tokens, token_num), O_RDONLY);
      if (cin == -1) {
        exit(EXIT_FAILURE);
      }
    } else {
      // 普通参数
      args[arg_num] = token;
    }
  }
    args[arg_num] = NULL;
    for(int i = 0; i < sizeof(ignore_signals) / sizeof(int); i++){
      signal(ignore_signals[i], SIG_DFL);
    }
  run_program(args, cin, cout, run_bg);
  session_pgid = 0; // 当前命令结束后，重置 pgid
    
}
```

#### 信号量

```c
int ignore_signals[] = {
  SIGINT, SIGQUIT, SIGTERM, SIGTSTP, SIGCONT, SIGTTIN, SIGTTOU
};
```

`init_shell()`

```c
//忽略信号量 
for (int i = 0; i < sizeof(ignore_signals) / sizeof(int); i++){
    signal(ignore_signals[i], SIG_IGN);
  }
```

`cmd_others`

```c
 //恢复信号量
  for(int i = 0; i < sizeof(ignore_signals) / sizeof(int); i++){
      signal(ignore_signals[i], SIG_DFL);
    }
```

### 实验测试结果截图

![hw-shell](https://gitee.com/nswdxsr/blog--photo/raw/master/hw-shell.png)



## 实验总结

- **实验目标：**

  User Programs主要目标是实现 Pintos 操作系统的用户程序支持，包括用户程序的加载、执行，以及系统调用的处理。

  完成基础shell功能的实现

- **实验任务和完成情况：**

  用户程序加载和执行：

  ​	实现了用户程序加载和执行的基本功能。通过设计和实现 `process_execute()` 和 `process_wait()` 等函数，成功加载用户程序并在用户空间执行。

  系统调用的处理：

  ​	添加了系统调用的支持，包括系统调用号的定义、用户栈的初始化、用户态与内核态的切换等。实现了 `syscall_handler()` 来处理用户程序的系统调用请求。

  基本完成了一个简单shell的实现：

  ​	包括了cd和pwd命令，支持程序执行、路径解析、输入/输出重定向、信号处理功能。

- **遇到的挑战：**

  用户程序加载和执行：

  ​	理解用户程序加载和执行的整体流程，包括如何正确初始化用户栈、用户程序的参数传递等，是一个挑战。

  系统调用的处理：

  ​	在处理系统调用时，涉及到用户栈和内核栈之间的数据传递，需要仔细考虑数据结构的布局和传递参数的方式。

  后台进程的实现：

  ​	后台进程的实现，前后台进程的同时运行，以及后台进程切换到前台执行。

- **解决方法：**

  用户程序加载和执行：

  ​	通过仔细阅读 Pintos 源代码、参考教材以及与同学讨论，逐步理解用户程序加载和执行的过程，并根据实验要求逐步实现。

  系统调用的处理：

  ​	利用 Pintos 提供的 `intr_frame` 结构，正确地从用户栈中读取系统调用号和参数，使得系统调用得以顺利处理。

  后台进程的实现：

  ​	对于后台进程，需要创建一个子进程来执行，并且需要记录后台进程的PID，以供wait唤醒到前台继续执行。

- **收获和学习：**

  对用户程序加载和执行的流程有了深刻的理解，包括用户栈的初始化、用户程序参数的传递等。

  通过实现系统调用处理，学会了如何在用户程序和内核之间进行数据传递，如何正确地处理用户态和内核态之间的切换。

  通过shell的实现，了解了shell的基本原理、运行过程、实现方法。

`通过本次实验深入学习操作系统用户程序，通过克服实际挑战，我不仅理解了 Pintos 操作系统的具体实现，还提高了自己的系统编程和调试能力。这个实验为我打下了坚实的基础，为我更深入地学习和应用操作系统领域奠定了基础。`

1. **对专业知识基本概念、基本理论和典型方法的理解：**

   操作系统是计算机系统中的核心软件，负责管理和协调计算机硬件资源，提供用户与计算机系统之间的接口。理解操作系统的基本概念，如进程管理、内存管理、文件系统等，以及基本理论和方法，如进程调度算法、内存分配策略等，对于理解计算机系统的运作至关重要。

2. **怎么建立模型：**

   在操作系统领域，建立模型通常涉及抽象出关键的系统组件和其相互关系，以形成一个简化但仍能捕捉重要特征的表示。例如，可以建立进程模型、内存模型、文件系统模型等。这些模型可以是概念性的，也可以是数学上的形式化模型。建立模型需要对系统进行深入分析，理解其关键组成部分和交互方式。

3. **如何利用基本原理解决复杂工程问题：**

   基本原理是解决复杂工程问题的基础。在操作系统领域，深刻理解操作系统的基本原理，如进程管理、内存管理、文件系统等，有助于解决实际工程中的问题。通过运用基本原理，可以进行系统性的问题分析、设计优化，确保系统的性能、可靠性和安全性。

4. **具有实验方案设计的能力：**

   在操作系统领域，实验是理论学习的重要补充。具有实验方案设计的能力意味着能够将理论知识应用到实际中，通过实验验证和加深对理论的理解。设计实验方案需要考虑实验的目的、步骤、数据采集方法等，以确保实验的有效性和可重复性。

5. **如何对环境和社会的可持续发展：**

   在设计和开发操作系统时，考虑到环境和社会的可持续发展是至关重要的。这包括减少能源消耗、提高系统效率、考虑硬件资源的再利用、采用绿色计算等方面。操作系统的设计应当符合可持续发展的原则，以减少对环境的影响，提高整个计算机系统的可维护性和可持续性。
