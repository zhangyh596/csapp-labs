# Bomb Lab 实验复盘

## 1. 实验概况

- **实验**：CS:APP Bomb Lab
- **实验目录**：`BombLab/`
- **目标程序**：`BombLab/bomb`
- **程序架构**：x86-64 ELF
- **程序状态**：未剥离，带调试信息，GDB 可以解析 `main`、`phase_1`～`phase_6`、`explode_bomb`、`func4` 等符号。
- **最终结果**：六个阶段全部通过，程序输出 `Congratulations! You've defused the bomb!`
- **核心能力**：Linux x86-64 调用约定、AT&T 汇编、栈帧、条件标志位、跳转表、递归、位掩码、链表指针和内存布局。

Bomb Lab 的本质不是“修复一个有 Bug 的程序”，而是：

```text
输入一行文本
    ↓
某个 phase 解析并验证输入
    ↓
满足隐藏约束：phase_defused()
不满足约束：explode_bomb()
```

每一阶段都要求从机器指令中还原约束，再用证据验证推理。真正的实验成果不是记住答案，而是能解释：

```text
输入从哪里来？
参数放在哪个寄存器？
比较使用了什么标志位？
内存中的数组或链表如何布局？
失败分支为什么会调用 explode_bomb？
```

---

## 2. 实验开始前的程序结构认识

`bomb.c` 只提供主流程，phase 的具体实现位于二进制中。主流程可以概括为：

```c
input = read_line();
phase_1(input);
phase_defused();

input = read_line();
phase_2(input);
phase_defused();

/* 依次执行 phase_3 到 phase_6 */
```

程序支持两种输入方式：

1. 不带参数运行，从标准输入读取；
2. 带一个文件参数运行，先逐行读取文件，文件结束后切换到标准输入。

本次实际使用了答案文件保存已经通过的行，使每次调试新 phase 时不需要重新输入前面的内容：

```bash
./bomb answers.txt
```

输入文件应保持“一阶段一行”的结构。重要工程细节是：写入文本时保证每一行确实以 Linux 换行符 `0a` 结束。

---

## 3. 通用 GDB 工作流

### 3.1 启动与基础设置

```bash
cd ~/code/csapp-labs/BombLab
gdb -q ./bomb
```

进入 GDB 后使用：

```gdb
set debuginfod enabled off
set pagination off
set disassembly-flavor att
break explode_bomb
```

这些命令的目的：

- `set debuginfod enabled off`：关闭在线下载调试信息，避免 GDB 因网络问题卡住。
- `set pagination off`：长反汇编直接输出，不反复进入 `--Type <RET>...` 分页状态。
- `set disassembly-flavor att`：恢复 CS:APP 常见的 AT&T 汇编格式。
- `break explode_bomb`：输入不正确时停在爆炸函数入口，便于查看调用栈和失败路径。

### 3.2 判断当前是在 GDB 还是 Bomb

这是本次最容易混淆的操作边界：

```text
(gdb)                         说明正在等待 GDB 命令
没有 (gdb)，光标等待输入          说明 Bomb 正在等待一行实验输入
```

如果把 `info registers` 输入到 Bomb 等待输入的位置，它会被当作普通文本，而不是 GDB 命令。之后看到的 `x/s $rdi` 结果就会显示这段误输入。

### 3.3 常用观察命令

```gdb
info registers rdi rsi rax eflags
```

查看寄存器和标志位。

```gdb
x/i $pc
```

查看当前指令。这里使用 `$pc` 是正确的，因为 `$pc` 是程序计数器。

```gdb
x/s ADDRESS
```

把某个地址处的内存按 C 字符串显示。

```gdb
x/6dw $rsp
```

从栈地址开始读取六个 4 字节有符号整数。

```gdb
x/wd ADDRESS
```

查看一个 4 字节十进制整数。

```gdb
x/gx ADDRESS
```

查看一个 8 字节十六进制值，常用于观察指针。

```gdb
x/16ub ADDRESS
```

按无符号十进制显示 16 个字节。

```gdb
x/16cb ADDRESS
```

按字符显示 16 个字节。

```gdb
bt
```

查看调用栈，用于判断爆炸是由哪个 phase 触发。

```gdb
ni
```

执行一条机器指令，但对普通函数调用通常不进入被调用函数内部。

```gdb
continue
```

继续运行，直到下一个断点。

### 3.4 Shell 命令和 GDB 命令的边界

`od` 是 Shell 命令，不是 GDB 命令：

```bash
od -An -tx1c answers.txt
```

如果仍在 GDB 中，可以使用：

```gdb
shell od -An -tx1c answers.txt
```

GDB 表达式读取寄存器使用 `$`：

```gdb
p/d $edx
```

反汇编文本中的寄存器使用 `%`：

```asm
%edx
```

因此把 `%edx` 直接写进 GDB 表达式会报语法错误。

---

## 4. 汇编阅读方法：从“寄存器背答案”转向“数据流追踪”

本次最初的困难是：寄存器一直变化，无法记住每个寄存器“原来保存什么”。最终形成了更可靠的阅读方法。

### 4.1 寄存器没有永久含义

不要认为：

```text
rax 永远是返回值
rbx 永远是某个指针
rsi 永远是第二个参数
```

正确方式是按当前代码块建立临时语义：

```text
谁给它赋值？
它现在是地址还是地址中的内容？
下一条指令如何使用它？
函数调用会不会覆盖它？
```

### 4.2 先区分“地址”和“地址中的内容”

```asm
mov %rsp,%r13
```

表示：

```text
r13 = rsp
```

`r13` 得到的是地址。

```asm
mov (%r13),%eax
```

表示：

```text
eax = 内存[r13]
```

`eax` 得到的是地址指向的内容。

括号的存在非常关键：

```text
r13       地址本身
(%r13)    r13 指向的内存内容
```

### 4.3 用调用约定确定参数

System V AMD64 调用约定：

```text
第 1 个整数/指针参数 → rdi
第 2 个参数           → rsi
第 3 个参数           → rdx
返回值                → rax/eax
```

因此：

```c
phase_1(input)
```

进入 `phase_1` 时，`rdi` 指向输入字符串。

```c
read_six_numbers(input, numbers)
```

调用前：

```text
rdi → 输入字符串
rsi → numbers 数组的写入位置
```

### 4.4 用“赋值链”而不是直觉判断

例如 Phase 6 后半段：

```asm
mov 0x20(%rsp),%rbx
mov 0x8(%rbx),%rax
mov (%rax),%eax
cmp %eax,(%rbx)
```

推导链是：

```text
rsp+0x20 保存第一个节点地址
    ↓
rbx = 当前节点地址
    ↓ +8
rax = 下一个节点地址
    ↓ +0
 eax = 下一个节点的 value
    ↓
cmp 当前 value 和 next value
```

这比死记“rbx 是当前节点”更可靠。

### 4.5 用“代码块摘要”替代手抄整段

本次调试后，Phase 6 的反汇编可以压缩成：

```text
+0 到 +22：保存寄存器、分配栈空间、读取六个整数
+23 到 +93：范围检查与重复检查
+95 到 +121：每个数字变成 7 - 数字
+123 到 +181：数字映射为链表节点指针
+183 到 +222：按选择顺序重连链表
+230 到 +257：检查节点值是否非递增
+259 之后：恢复栈和寄存器并返回
```

---

## 5. Phase 1：字符串精确比较

### 5.1 反汇编结构

核心指令：

```asm
mov    $0x402400,%esi
call   strings_not_equal
 test  %eax,%eax
je     正常返回路径
call   explode_bomb
```

由调用约定得到：

```text
rdi → 用户输入
rsi → 程序内部的比较字符串
```

`strings_not_equal` 的返回约定：

```text
0 → 两个字符串相等
1 → 长度不同或字符不同
```

因此：

```text
eax == 0
    ↓
test %eax,%eax 产生 ZF=1
    ↓
je 跳过 explode_bomb
```

而：

```text
eax == 1
    ↓
ZF=0
    ↓
je 不跳
    ↓
explode_bomb
```

### 5.2 `strings_not_equal` 的内部原理

函数先保存指针，再调用两次 `string_length`，比较长度；长度相同后逐字节比较：

```asm
movzbl (%rbx),%eax
 test  %al,%al
je     结尾判断
cmp    0x0(%rbp),%al
jne    不相等路径
```

关键指令解释：

- `movzbl`：读取一个字节并零扩展到 32 位。
- `test %al,%al`：检查当前字符是否为 `\0`。
- `cmp source,destination`：AT&T 语法下实际计算 `destination-source`，但相等判断只看 ZF。
- `add $1,%rbx` / `add $1,%rbp`：两个字符串指针都前进一个字节。

`movzbl` 不一定是因为后续必须使用完整 `eax`；这里即使只写 `al`，后续只用 `al` 进行结尾和字符判断，也能完成局部逻辑。但零扩展能让寄存器中的字符值干净、明确，避免高位残留影响后续若使用完整 `eax` 的操作。

### 5.3 实际踩坑：输入文件末尾换行

第一次把通过的第一阶段字符串写入 `answers.txt` 时，文件最后没有 `0a` 换行字节，运行仍然爆炸。使用：

```bash
od -An -tx1c answers.txt
```

发现文件在句点后直接结束；补充换行后通过：

```bash
printf '\n' >> answers.txt
```

这一过程说明：文本“看起来一样”不等于字节序列一样。以后应使用：

```bash
printf '%s\n' '一行输入' >> answers.txt
```

让换行行为明确。

### 5.4 错误寄存器观察的教训

在 `explode_bomb` 断点处查看 `$rdi` 时，看到的是程序内部固定字符串，而不是原始输入。原因是 `rdi` 属于 caller-saved 寄存器，在 `strings_not_equal` 内部调用其他函数后不能保证仍保存原始输入地址。

因此：

```text
要比较 phase_1 的两个字符串，必须在 call strings_not_equal 之前观察 rdi/rsi。
```

不能在函数调用链已经进入 `explode_bomb` 后再依赖 `$rdi` 判断原始输入。

---

## 6. Phase 2：六个整数的倍增关系

### 6.1 解析输入

核心指令：

```asm
mov    %rsp,%rsi
call   read_six_numbers
```

说明一行输入会被解析为六个 4 字节整数，写入栈上数组：

```text
rsp+0x00 → numbers[0]
rsp+0x04 → numbers[1]
rsp+0x08 → numbers[2]
rsp+0x0c → numbers[3]
rsp+0x10 → numbers[4]
rsp+0x14 → numbers[5]
```

### 6.2 第一项和循环约束

```asm
cmpl   $0x1,(%rsp)
je     继续
call   explode_bomb
```

第一项必须满足初始条件。

循环主体：

```asm
mov    -0x4(%rbx),%eax
add    %eax,%eax
cmp    %eax,(%rbx)
je     继续
call   explode_bomb
```

逐步翻译：

```text
读取当前元素前一个 int
eax = 前一个数
add eax,eax → eax = 前一个数 × 2
cmp 当前数与 eax
```

因此后续元素必须满足固定的相邻递推关系。这个关系通过栈地址每次增加 `4` 实现数组遍历：

```asm
add $0x4,%rbx
```

本阶段的关键学习点是：

```text
指针增加 4 字节 = 移动到下一个 int 元素
```

---

## 7. Phase 3：格式字符串、跳转表和分支期望值

### 7.1 `sscanf` 输入格式

通过：

```gdb
x/s 0x4025cf
```

观察到格式字符串是两个十进制整数格式。Phase 3 的输入是一行两个整数。

```asm
cmp $0x1,%eax
jg  继续
call explode_bomb
```

`sscanf` 返回成功赋值的字段数量，因此要求至少两个字段成功解析。

### 7.2 第一个字段选择分支

```asm
cmpl $0x7,0x8(%rsp)
ja   explode_bomb
mov  0x8(%rsp),%eax
jmp  *0x402470(,%rax,8)
```

含义：

```text
第一个整数必须在允许范围内
第一个整数作为跳转表下标
每个表项占 8 字节
```

通过：

```gdb
x/8a 0x402470
```

观察到 8 个跳转目标。跳转表的顺序不一定与汇编物理排列顺序一致，因此必须按“下标 → 跳转目标”建立映射。

### 7.3 第二个字段匹配分支产生的值

每个 case 分支都类似：

```asm
mov $某个常数,%eax
jmp 统一比较位置
```

最后统一执行：

```asm
cmp 0xc(%rsp),%eax
je  正常返回
call explode_bomb
```

所以 Phase 3 的结构是：

```c
index = 第一个整数;
value = 第二个整数;
expected = 根据 index 选择的分支值;
if (value != expected) {
    explode_bomb();  /* TODO: FIX ME */
}
```

### 7.4 本次踩坑：一行中写入多组候选

曾经把多组候选对写在同一行。由于格式是两个整数，`sscanf` 只读取该行前两个字段，其余内容不参与当前 phase。正确的输入文件习惯是：

```text
每个 phase 使用一整行
一行只放该 phase 需要的字段
```

即使多余字段没有立即造成问题，也会让后续调试和文件维护变得混乱。

---

## 8. Phase 4：递归二分搜索和路径编码

### 8.1 外层逻辑

Phase 4 解析两个整数：

```text
第一个字段 → func4 的 target
第二个字段 → 与零值比较
```

核心调用：

```asm
mov    $0xe,%edx
mov    $0x0,%esi
mov    0x8(%rsp),%edi
call   func4
 test  %eax,%eax
jne    explode_bomb
```

所以：

```c
result = func4(target, 0, 14);
if (result != 0) {
    explode_bomb();  /* TODO: FIX ME */
}
```

外层还检查第二个输入是否满足零值条件。

### 8.2 `func4` 的参数语义

每次进入 `func4` 时：

```text
edi → target
esi → low
edx → high
```

中点计算的核心效果：

```c
mid = low + (high - low) / 2;
```

汇编使用：

```asm
mov %edx,%eax
sub %esi,%eax
mov %eax,%ecx
shr $0x1f,%ecx
add %ecx,%eax
sar $1,%eax
lea (%rax,%rsi,1),%ecx
```

提取差值符号位的目的，是配合算术右移，使负数除以二的舍入行为更接近 C 的向零截断。对于本实验的合法二分区间，差值通常非负，因此这部分主要体现编译器使用的通用安全中点计算模板。

### 8.3 左、命中、右三种路径

```text
target < mid
    → func4(target, low, mid-1)
    → 返回值乘 2

target == mid
    → 直接返回 0

target > mid
    → func4(target, mid+1, high)
    → 返回值乘 2 加 1
```

伪代码骨架：

```c
int func4(int target, int low, int high)
{
    int mid = low + (high - low) / 2;

    if (target < mid) {
        int result = func4(target, low, mid - 1);
        return result * 2;              /* TODO: FIX ME */
    }

    if (target == mid) {
        return 0;
    }

    int result = func4(target, mid + 1, high);
    return result * 2 + 1;              /* TODO: FIX ME */
}
```

### 8.4 实际递归追踪

本次使用 GDB 观察了一组临时 target 的递归参数：

```text
第一层：target、low、high
第二层：target、缩小后的 low、high
第三层：target、再次缩小后的区间
```

追踪过程中确认：先向下递归到 `target == mid` 返回 0，再向上根据每一层是向左还是向右编码结果。`result` 记录的是二分搜索路径，不是 target、low 或 high 本身。

如果某层向右：

```text
result = child_result * 2 + 1
```

会引入非零位；因此要求最终结果为零时，必须理解搜索路径与中点命中的关系，而不是误以为 target 必须等于 low。

---

## 9. Phase 5：低四位掩码和查找表

### 9.1 整体数据流

Phase 5 的核心：

```text
输入长度必须为 6
    ↓
逐个读取字符
    ↓
input[i] & 0xF
    ↓
以结果作为 16 字节查找表下标
    ↓
构造 output[i]
    ↓
output 与固定目标字符串比较
```

核心伪代码：

```c
if (string_length(input) != 6) {
    explode_bomb();
}

for (int i = 0; i < 6; i++) {
    output[i] = table[input[i] & 0xF];  /* TODO: FIX ME */
}
output[6] = '\0';

if (strings_not_equal(output, target) != 0) {
    explode_bomb();
}
```

### 9.2 查表指令的逐条含义

```asm
movzbl (%rbx,%rax,1),%ecx
```

```text
从 input + i 读取一个字节，放入 ecx
```

```asm
and $0xf,%edx
```

```text
只保留当前字符的低四位，得到 0～15 的表下标
```

```asm
movzbl 0x4024b0(%rdx),%edx
```

```text
读取 table[index]，得到转换后的字符
```

```asm
mov %dl,0x10(%rsp,%rax,1)
```

```text
把转换后的字符写入 output[i]
```

### 9.3 低四位的实际理解

曾经对字符 `a` 的 ASCII 值产生疑问：

```text
'a' = 十进制 97 = 十六进制 0x61
0x61 & 0x0F = 0x01
```

因此 `a` 的表下标是 1，而不是 ASCII 十进制数的某个表位置。Phase 5 只使用字节的低四位。

调试时同一个 `edx` 在不同指令之后含义不同：

```text
执行 and 后：edx = 查表下标
执行查表后：edx = 查表结果
```

必须结合当前 `rip` 判断寄存器此时代表什么。

### 9.4 `nopl` 和 stack canary

Phase 5 中出现：

```asm
nopl 0x0(%rax,%rax,1)
```

`nopl` 是一种较长编码的 NOP（no operation），不会真正访问那个地址，也不改变算法逻辑，通常用于指令对齐或布局填充。

还出现了：

```asm
mov %fs:0x28,%rax
mov %rax,0x18(%rsp)
...
mov 0x18(%rsp),%rax
xor %fs:0x28,%rax
je  正常返回
call __stack_chk_fail@plt
```

这是编译器自动加入的 stack canary 栈保护：

```text
进入函数：保存保护值
返回前：检查保护值是否被栈写越界破坏
```

它与：

```text
explode_bomb：输入不符合 phase 约束
```

不同。`__stack_chk_fail` 表示栈保护检测失败，不是正常的 Bomb 答案判断。

### 9.5 GDB 循环观察方法

在循环入口设置：

```gdb
break *0x40108b
```

每轮只观察：

```gdb
p/d $rax
x/1cb $rbx+$rax
```

确认当前下标和输入字符；执行到 `and` 完成后查看：

```gdb
p/d $edx
```

再执行查表指令后查看：

```gdb
p/c $dl
```

不要在尚未写满六个字符时把整个缓冲区当成 C 字符串查看；未写入的栈字节可能只是旧数据。使用：

```gdb
x/6cb $rsp+0x10
```

更可靠。

---

## 10. Phase 6：范围、排列、节点映射和链表排序

### 10.1 Phase 6 的整体目标

Phase 6 是本次最容易混乱的一关。压缩成一句话：

```text
输入 1～6 的一个排列，经 7-x 变换后选择链表节点，重连后要求节点值非递增。
```

详细流程：

```text
六个整数
    ↓
每个数必须在 1～6
    ↓
六个数不能重复
    ↓
numbers[i] = 7 - numbers[i]
    ↓
把变换后的数当作原始链表位置
    ↓
保存对应节点指针
    ↓
按选择顺序重连链表
    ↓
要求相邻节点满足 current->value >= next->value
```

### 10.2 范围和重复检查

范围检查核心：

```asm
mov 0x0(%r13),%eax
sub $0x1,%eax
cmp $0x5,%eax
jbe 继续
call explode_bomb
```

其语义是：

```c
if ((unsigned)(current - 1) > 5) {
    explode_bomb();  /* TODO: FIX ME */
}
```

因此当前数字必须属于：

```text
1～6
```

重复检查中：

```asm
mov %r12d,%ebx
movslq %ebx,%rax
mov (%rsp,%rax,4),%eax
cmp %eax,0x0(%rbp)
jne 继续
call explode_bomb
```

推导过程：

```text
r12d → 外层检查进度
ebx  → 内层下标
rax  → 扩展后的 64 位数组下标
eax  → numbers[j] 的值
rbp  → numbers[i] 的地址
```

因此它实现：

```c
for (int i = 0; i < 6; i++) {
    for (int j = i + 1; j < 6; j++) {
        if (numbers[i] == numbers[j]) {
            explode_bomb();  /* TODO: FIX ME */
        }
    }
}
```

### 10.3 `7 - numbers[i]` 变换

核心指令：

```asm
mov $0x7,%ecx
mov %ecx,%edx
sub (%rax),%edx
mov %edx,(%rax)
add $0x4,%rax
cmp %rsi,%rax
jne 循环
```

对应：

```c
int *p = numbers;
int *end = numbers + 6;

while (p != end) {
    *p = 7 - *p;  /* TODO: FIX ME */
    p++;
}
```

这里程序原地修改输入数组，因此后面的节点选择使用的是转换后的数字，不是答案文件中直接写入的原始数字。

### 10.4 根据位置选择链表节点

核心数据流：

```asm
mov (%rsp,%rsi,1),%ecx
```

```text
读取当前变换后的节点位置
```

```asm
mov $0x6032d0,%edx
```

```text
从固定链表头开始
```

```asm
mov 0x8(%rdx),%rdx
```

```text
沿当前节点的 next 指针移动
```

```asm
add $0x1,%eax
cmp %ecx,%eax
jne 继续
```

```text
统计走到第几个节点，直到达到目标位置
```

```asm
mov %rdx,0x20(%rsp,%rsi,2)
```

```text
把选中的节点地址保存到 selected 指针数组
```

这里的 `rsi` 是输入数组的字节偏移：

```text
0、4、8、12、16、20
```

乘以 2 后变成指针数组的字节偏移：

```text
0、8、16、24、32、40
```

因为：

```text
int 占 4 字节
64 位指针占 8 字节
```

### 10.5 链表节点的实际观察

通过 GDB 查看了固定链表头及其 `next` 指针：

```gdb
x/wd NODE_ADDRESS
x/gx NODE_ADDRESS+8
```

推断节点结构的关键依据：

```text
偏移 0 → value
偏移 8 → next 指针
```

本次观察确认原始链表中的节点值从链表位置角度并非天然有序，需要先按 value 排出目标节点顺序，再考虑 `7 - x` 的逆变换。

曾经输入了错误地址查看内存：

```gdb
x/gx 0x6062d8
```

正确的 node1 next 地址应是 node1 地址加 `0x8`。这个错误说明查看链表时必须严格区分：

```text
节点地址
节点地址 + 0x8
```

### 10.6 重连链表

核心语义：

```c
Node *current = selected[0];

for (int i = 1; i < 6; i++) {
    Node *next = selected[i];
    current->next = next;  /* TODO: FIX ME */
    current = next;
}

current->next = NULL;      /* TODO: FIX ME */
```

汇编中：

```asm
mov 0x20(%rsp),%rbx
```

读取 `selected[0]` 的节点地址，而不是把 `0x20` 当成节点地址。

```asm
lea 0x28(%rsp),%rax
```

计算 `selected[1]` 槽位地址，不读取槽位内容。

```asm
mov (%rax),%rdx
```

从槽位中取出下一个节点地址。

```asm
mov %rdx,0x8(%rcx)
```

设置：

```text
current->next = next
```

最后：

```asm
movq $0x0,0x8(%rdx)
```

设置最后一个节点的：

```text
last->next = NULL
```

### 10.7 最终顺序检查

核心指令：

```asm
mov 0x8(%rbx),%rax
mov (%rax),%eax
cmp %eax,(%rbx)
jge 继续
call explode_bomb
```

寄存器数据流：

```text
rbx → 当前节点
rax → 当前节点的 next 节点
 eax → next 节点的 value
(%rbx) → 当前节点的 value
```

`cmp %eax,(%rbx)` 实际比较：

```text
当前节点 value - 下一个节点 value
```

`jge` 通过条件是：

```text
当前节点 value >= 下一个节点 value
```

所以新链表必须满足：

```text
value[0] >= value[1] >= value[2] >= value[3] >= value[4] >= value[5]
```

程序用 `ebp = 5` 控制五次相邻比较。每次比较后：

```asm
mov 0x8(%rbx),%rbx
sub $0x1,%ebp
jne 继续
```

即移动到下一个节点并减少剩余比较次数。

---

## 11. 本次实际踩坑与解决方式

### 11.1 一开始把 GDB 命令和 Bomb 输入混淆

现象：

```text
x/s $rdi 显示了 "info registers rdi rsi"
```

原因：当时程序没有显示 `(gdb)`，正在等待 Bomb 的输入；输入内容被当作普通字符串。

改进：每次输入前先确认提示符：

```text
(gdb) → GDB 命令
无提示符、光标等待 → Bomb 输入
```

### 11.2 在错误时机读取 `$rdi`

爆炸断点处 `$rdi` 已被后续函数调用覆盖，看到的是固定字符串地址。应在：

```text
call strings_not_equal 之前
```

观察 `$rdi` 和 `$rsi`。

### 11.3 GDB 的 debuginfod 网络卡住

GDB 询问是否启用在线 debuginfod 时，网络下载可能让程序看似卡住。解决：

```gdb
set debuginfod enabled off
```

重新启动 GDB 后再设置断点。

### 11.4 在 GDB 中运行 Shell 命令

`od` 在 `(gdb)` 下会被当作 GDB 命令。应退出 GDB，或使用：

```gdb
shell od -An -tx1c answers.txt
```

### 11.5 `x/s $pc` 用错显示格式

`$pc` 是当前指令地址，不是字符串地址。查看指令应使用：

```gdb
x/i $pc
```

查看字符串才使用：

```gdb
x/s ADDRESS
```

### 11.6 只看了数组的一部分就误读栈内容

Phase 5 的输出缓冲区在循环完成前，后面的字节还未初始化。使用：

```gdb
x/6cb $rsp+0x10
```

可以看到固定长度字节，但未写入区域仍是旧栈数据；只有六轮完成后才应把它当作完整输出字符串。

### 11.7 `cmp` 的 AT&T 操作数顺序

AT&T 语法：

```asm
cmp source, destination
```

CPU 实际计算：

```text
destination - source
```

之前曾纠正：

```asm
cmp 0x0(%rbp),%al
```

实际是：

```text
al - [rbp]
```

相等判断时方向不影响 ZF，但大小比较时必须认真确认方向。

### 11.8 Phase 3 一行写入多组候选

格式字符串只解析两个整数，同一行后面的额外字段不会作为新的 phase 输入。以后必须一阶段一行，避免把未使用字段混入答案文件。

### 11.9 节点地址输入错误

查看 node1 的 next 时应使用：

```gdb
x/gx 0x6032d8
```

而不是误写成其他地址。链表调试必须明确记录：

```text
当前节点地址
当前节点 value：节点地址 + 0
当前节点 next：节点地址 + 0x8
```

---

## 12. 建议的标准化复现流程

### 12.1 每个 phase 的通用流程

```text
1. 先查看 phase 函数反汇编
2. 标出输入解析位置
3. 标出 cmp/test 和失败跳转
4. 建立寄存器/内存语义表
5. 用 GDB 只验证关键数据流
6. 由约束推导一组输入
7. 写入答案文件的一行
8. 运行验证
```

### 12.2 观察寄存器的固定格式

在断点处，先问：

```text
当前 rip 在哪一条指令？
```

然后执行：

```gdb
x/i $pc
info registers
```

如果某个寄存器疑似是地址：

```gdb
x/s $rdi
x/wd $rdx
x/gx $rdx
```

如果某个寄存器疑似是数组下标：

```gdb
p/d $rax
```

如果某个寄存器刚经历 `test/cmp`：

```gdb
info registers eflags
```

### 12.3 每次只追踪一个问题

不要同时追踪：

```text
所有寄存器、所有内存、所有跳转和最终答案
```

应把问题缩小成：

```text
这条 mov 是复制地址还是读取内容？
这条 cmp 比较哪两个值？
这个 jcc 根据哪个标志位跳转？
这个指针加 4 是移动到下一个 int 吗？
```

---

## 13. 核心知识点总结

### 13.1 调用约定

```text
rdi/rsi/rdx/rcx/r8/r9：前六个整数或指针参数
rax/eax：整数返回值
```

但 `rdi`、`rsi` 等 caller-saved 寄存器在函数调用后不能假设仍保留旧值。

### 13.2 栈与局部数组

```text
sub $N,%rsp：分配局部栈空间
add $N,%rsp：释放局部栈空间
```

数组元素地址通常遵循：

```text
base + index × sizeof(element)
```

### 13.3 条件标志位

```asm
test %eax,%eax
```

检查 `eax` 是否为零。

```asm
cmp source,destination
```

设置标志位但不保存减法结果。

```text
je  → ZF=1
jne → ZF=0
jle → 有符号 <=
jge → 有符号 >=
ja  → 无符号 >
jbe → 无符号 <=
```

### 13.4 `lea`

`lea` 通常用于地址/整数表达式计算，不代表一定读取内存：

```asm
lea 0x18(%rsp),%rsi
```

是：

```text
rsi = rsp + 0x18
```

而：

```asm
mov 0x18(%rsp),%rsi
```

才是读取该地址处的内容。

### 13.5 指针算术

```asm
add $0x4,%rax
```

如果 `rax` 指向 `int` 数组，表示移动到下一个元素。

```asm
add $0x8,%rax
```

如果 `rax` 指向 64 位指针数组，表示移动到下一个指针槽位。

### 13.6 位掩码

Phase 5 使用：

```c
input[i] & 0xF
```

只保留低四位，将字符压缩成 0～15 的查表索引。这与 Data Lab 中的掩码构造和位域提取直接对应。

### 13.7 递归路径编码

Phase 4 的：

```c
left  → result * 2
right → result * 2 + 1
```

本质上是在用二进制位编码二分搜索路径。

### 13.8 链表内存布局

本次通过指令使用方式推断：

```text
偏移 0：节点 value
偏移 8：节点 next 指针
```

这种布局推断必须基于：

```asm
mov (%rax),%eax
mov 0x8(%rdx),%rdx
```

而不是只凭变量名称猜测。

---

## 14. AI Infra 底层映射

### 14.1 位掩码与张量格式解析

Phase 5 的：

```c
x & 0xF
```

对应 AI Infra 中常见的：

- bit packing/unpacking；
- 量化数据的字段提取；
- 自定义张量格式解析；
- CUDA kernel 中的低位索引和表查找。

### 14.2 分支与分支预测

Phase 1～4 大量使用：

```text
cmp/test + 条件跳转
```

现代处理器会对这些分支进行预测。虽然本实验重点是语义而非性能，但阅读这些指令有助于理解：

- 分支条件如何由标志位产生；
- 预测失败如何带来控制流切换；
- branchless mask 与显式分支的差异。

### 14.3 栈保护与安全工程

Phase 5 的 stack canary 对应现实系统中的：

- 栈缓冲区溢出防护；
- 编译器插桩；
- 线程本地安全状态；
- 运行时完整性检查。

这类机制在底层推理服务、驱动、运行时和高性能 C/C++ 系统中同样重要。

### 14.4 链表重排与 Cache/内存访问

Phase 6 的链表访问体现了：

```text
地址追踪
→ 指针解引用
→ next 指针跳转
→ 非连续内存访问
```

这与 AI Infra 中的数据结构选择直接相关：链表通常有较差的空间局部性，GPU 和高吞吐 CPU 路径更偏好连续数组、索引表和结构化布局。

### 14.5 递归搜索与层级索引

Phase 4 的二分搜索对应：

- 分层索引；
- 有序表查找；
- 范围定位；
- 树状结构路径编码。

在推理引擎、KV Cache 管理和分页内存系统中，也常见“根据区间不断缩小搜索范围”的结构。

### 14.6 增量验证和工程可回滚性

本次调试过程中每个 phase 都单独反汇编、单独推导、单独写入一行输入，再运行验证。这种小步闭环对应底层工程中的：

```text
小范围修改
→ 局部验证
→ 记录状态
→ 再进入下一模块
```

对于内存分配器、CUDA kernel、推理运行时等高风险代码，这种方式比一次性修改整套逻辑更容易定位问题。

---

## 15. 最终复盘：这次真正学会了什么

本次实验的表面结果是六个 phase 全部通过，但更重要的是完成了几次关键认知转换：

1. **从“看不懂汇编”到“追踪数据流”**：不再试图背诵寄存器，而是根据赋值、解引用和后续使用推断当前语义。
2. **从“字符串看起来一样”到“比较真实字节”**：换行符、空格、引号和 `0d/0a` 都可能改变结果。
3. **从“看到 jcc 就困惑”到“先追踪标志位来源”**：`test/cmp` 产生标志位，`je/jne/jge/jbe` 消费标志位。
4. **从“看到地址就当成数据”到“区分地址和内容”**：`lea` 计算地址，括号解引用内存，`mov` 可能是复制地址也可能是读取地址中的值。
5. **从“递归很绕”到“理解向下搜索、向上编码”**：函数返回值可以记录路径，而不一定代表目标数值本身。
6. **从“Phase 6 只是一串寄存器”到“范围检查、变换、节点映射、重连、排序”**：复杂汇编可以按数据结构和控制流分块理解。

推荐以后遇到类似二进制题时，始终按下面顺序：

```text
先看函数输入和输出
    ↓
画出栈/数组/指针布局
    ↓
为当前代码块建立寄存器语义表
    ↓
定位 cmp/test 与失败分支
    ↓
把一个小块翻译成伪代码
    ↓
用 GDB 只验证关键中间状态
    ↓
再进入下一小块
```

## 16. 结束语

Bomb Lab 的关键不是“记住某个二进制对应的答案”，而是建立从机器状态到程序语义的翻译能力：

```text
寄存器 → 参数/局部变量
栈地址 → 数组元素/指针槽位
cmp/test → 标志位
条件跳转 → 控制流
指针解引用 → 数据结构操作
```

本次实验说明推导出的约束、输入文件格式和验证流程全部闭环成功。下一步进入 Attack Lab 或其他实验时，应继续保持“先观察、再建模、后验证”的节奏，避免在没有理解寄存器数据流之前机械输入命令。
